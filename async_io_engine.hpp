#pragma once

#include <liburing.h>
#include <stdexcept>
#include <system_error>
#include <cstring>
#include <stack>
#include <vector>

class AsyncIOEngine {
private:
    enum class IOOperation {
        Read,
        Write
    };
    struct IOContext {
        IOOperation op;
        int fd;
        void* buffer;
        size_t size; 
        off_t offset;
        size_t pool_index;
        size_t bytes_completed;
    };

    struct io_uring ring_{};
    unsigned int queue_depth_ = 0;
    unsigned int flags_ = 0;
    bool is_initialized_ = false;

    IOContext* context_pool_ = nullptr;
    std::stack<size_t, std::vector<size_t>> free_indices_;

public:
    AsyncIOEngine(unsigned int queue_depth, unsigned int flags)
        : queue_depth_(queue_depth), flags_(flags) {
        int ret = io_uring_queue_init(queue_depth_, &ring_, flags_);
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
        }
        context_pool_ = new (std::nothrow) IOContext[queue_depth_];
        if (!context_pool_) {
            io_uring_queue_exit(&ring_);
            throw std::runtime_error("Failed to pre-allocate IOContext pool");
        }

        for (size_t i = 0; i < queue_depth_; ++i) {
            context_pool_[i].pool_index = i;
            context_pool_[i].bytes_completed = 0;
            free_indices_.push(i);
        }
        is_initialized_ = true;
    }

    ~AsyncIOEngine() {
        if (is_initialized_) {
            io_uring_queue_exit(&ring_);
            delete[] context_pool_;
        }
    }

    AsyncIOEngine(const AsyncIOEngine&) = delete;
    AsyncIOEngine& operator=(const AsyncIOEngine&) = delete;

    AsyncIOEngine(AsyncIOEngine&& other) noexcept
        : ring_(other.ring_),
          queue_depth_(other.queue_depth_),
          flags_(other.flags_),
          is_initialized_(other.is_initialized_),
          context_pool_(other.context_pool_),
          free_indices_(std::move(other.free_indices_)) {
        std::memset(&other.ring_, 0, sizeof(io_uring));
        other.is_initialized_ = false;
        other.queue_depth_ = 0;
        other.flags_ = 0;
        other.context_pool_ = nullptr;
    }

    AsyncIOEngine& operator=(AsyncIOEngine&& other) noexcept {
        if (this != &other) {
            if (is_initialized_) {
                io_uring_queue_exit(&ring_);
                delete[] context_pool_;
            }
            ring_ = other.ring_;
            queue_depth_ = other.queue_depth_;
            flags_ = other.flags_;
            is_initialized_ = other.is_initialized_;
            context_pool_ = other.context_pool_;
            free_indices_ = std::move(other.free_indices_);

            std::memset(&other.ring_, 0, sizeof(io_uring));
            other.is_initialized_ = false;
            other.queue_depth_ = 0;
            other.flags_ = 0;
            other.context_pool_ = nullptr;
        }
        return *this;
    }

    bool submit_read(void* ptr, int fd, size_t size, off_t offset);

    bool submit_write(void* ptr, int fd, size_t size, off_t offset) {
        if (free_indices_.empty()) {
            return false;
        }
        
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }

        size_t idx = free_indices_.top();
        free_indices_.pop();
        IOContext* ctx = &context_pool_[idx];
        ctx->op = IOOperation::Write;
        ctx->fd = fd;
        ctx->buffer = ptr;
        ctx->size = size;
        ctx->offset = offset;
        ctx->bytes_completed = 0;
        ctx->pool_index = idx;

        io_uring_prep_write(sqe, fd, ptr, size, offset);
        io_uring_sqe_set_data(sqe, ctx);

        return true;
    }

    bool resubmit_context(IOContext* ctx) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            io_uring_submit(&ring_);
            sqe = io_uring_get_sqe(&ring_);
            if (!sqe) {
                return false;
            }
        }
        io_uring_prep_write(sqe, ctx->fd, ctx->buffer, ctx->size, ctx->offset);
        io_uring_sqe_set_data(sqe, ctx);
        return true;
    }
    
    int submit_sqes() {
        int ret = io_uring_submit(&ring_);
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "sqe submit failed");
        }
        return ret;
    }

    int reap_completions() {
        int cnt = 0;
        struct io_uring_cqe* cqe;
        
        while (!io_uring_peek_cqe(&ring_, &cqe)) {
            IOContext* user_data = (IOContext*) io_uring_cqe_get_data(cqe);
            int res = cqe->res;
            if (res > 0) {
                if (res < user_data->size) {
                    user_data->buffer = static_cast<char*>(user_data->buffer) + res;
                    user_data->size -= res;
                    user_data->offset += res;
                    if (!resubmit_context(user_data)) {
                        throw std::runtime_error("Failed to submit context");
                    }
                } else {
                    free_indices_.push(user_data->pool_index);
                }
            } else {
                switch (-res) {
                    case 0:
                        fprintf(stdout, "Write 0 bytes (fd=%d)\n", user_data->fd);
                        free_indices_.push(user_data->pool_index);
                        break;
                    case EINVAL:
                        fprintf(stderr, "EINVAL: Invalid param - fd=%d, offset=%lld, size=%zu\n",
                                user_data->fd, (long long)user_data->offset, user_data->size);
                        break;
                    case EAGAIN:
                        fprintf(stderr, "EAGAIN: Resource temp unavailable (fd=%d)\n", user_data->fd);
                        if (!resubmit_context(user_data)) {
                            throw std::runtime_error("Failed to submit context");
                        } 
                        break;
                    default:
                        break;
                }
            }
            cqe_seen(cqe);
            cnt++;
        }
        return cnt;
    }

    void cqe_seen(struct io_uring_cqe* cqe) {
        io_uring_cqe_seen(&this->ring_, cqe);
    }
};