#pragma once

#include <iostream>
#include <liburing.h>
#include <stdexcept>
#include <system_error>
#include <cstring>
#include <stack>
#include <vector>
#include <iomanip>

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
        void* expected_buffer;
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

    bool submit_read(void* ptr, void* expected_ptr, int fd, size_t size, off_t offset) {
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
        ctx->op = IOOperation::Read;
        ctx->fd = fd;
        ctx->buffer = ptr;
        ctx->size = size;
        ctx->offset = offset;
        ctx->bytes_completed = 0;
        ctx->pool_index = idx;
        ctx->expected_buffer = expected_ptr;

        io_uring_prep_read(sqe, fd, ptr, size, offset);
        io_uring_sqe_set_data(sqe, ctx);

        return true;
    }

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

    void* current_buffer = static_cast<char*>(ctx->buffer) + ctx->bytes_completed;
    size_t remaining_size = ctx->size - ctx->bytes_completed;
    off_t current_offset = ctx->offset + ctx->bytes_completed;

    if (ctx->op == IOOperation::Write) {
        io_uring_prep_write(sqe, ctx->fd, current_buffer, remaining_size, current_offset);
    } else if (ctx->op == IOOperation::Read) {
        io_uring_prep_read(sqe, ctx->fd, current_buffer, remaining_size, current_offset);
    } else {
        return false;
    }

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
        bool need_submit = false;
        struct io_uring_cqe* cqe;
        
        while (!io_uring_peek_cqe(&ring_, &cqe)) {
            IOContext* user_data = (IOContext*) io_uring_cqe_get_data(cqe);
            int res = cqe->res;
            #if 0
            if (res > 4 * 1024 * 1024) {
                res = 4 * 1024 * 1024;
            }
            #endif
            if (res > 0) {
                user_data->bytes_completed += res;
                if (user_data->bytes_completed < user_data->size) {
                    if (user_data->bytes_completed % 4096 != 0) {
                        throw std::runtime_error("Fatal: Unaligned short read/write under O_DIRECT");
                    }
                    if (!resubmit_context(user_data)) {
                        throw std::runtime_error("Failed to submit context");
                    }
                    need_submit = true;
                } else {
                    if (user_data->op == IOOperation::Read) {
                        do_data_validation(user_data); 
                    }
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
        if (need_submit) {
            io_uring_submit(&ring_);
        }
        return cnt;
    }

    void cqe_seen(struct io_uring_cqe* cqe) {
        io_uring_cqe_seen(&this->ring_, cqe);
    }

    void do_data_validation(IOContext* ctx) {
        if (!memcmp(ctx->expected_buffer, ctx->buffer, ctx->size)) {
            return;
        } else {
           uint8_t* origin_data = static_cast<uint8_t*> (ctx->expected_buffer);
           uint8_t* read_data = static_cast<uint8_t*> (ctx->buffer);
           size_t i = 0;
           while (i < ctx->size && origin_data[i] == read_data[i]) {
                i++;
           }
           std::cerr << "偏移: " << i 
                     << "期望字节: 0x" << std::hex << std::setw(2) << std::setfill('0') 
                     << static_cast<int>(origin_data[i]) << " | "
                     << "实际字节: 0x" << std::setw(2) << std::setfill('0')
                     << static_cast<int>(read_data[i]) 
                     << " | 4K余数: " << i % 4096 
                     << " | 512余数: " << i % 512 << std::dec << std::endl;
           throw std::runtime_error("Data corruption detected");
        }
    }

    size_t get_pending_count() const {
        return queue_depth_ - free_indices_.size();
    }
};