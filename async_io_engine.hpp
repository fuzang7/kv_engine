#pragma once

#include <liburing.h>
#include <stdexcept>
#include <system_error>
#include <cstring>

class AsyncIOEngine {
private:
    struct io_uring ring_{};
    unsigned int queue_depth_ = 0;
    unsigned int flags_ = 0;
    bool is_initialized_ = false;

public:
    AsyncIOEngine(unsigned int queue_depth, unsigned int flags)
        : queue_depth_(queue_depth), flags_(flags) {
        int ret = io_uring_queue_init(queue_depth_, &ring_, flags_);
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
        }
        is_initialized_ = true;
    }

    ~AsyncIOEngine() {
        if (is_initialized_) {
            io_uring_queue_exit(&ring_);
        }
    }

    AsyncIOEngine(const AsyncIOEngine&) = delete;
    AsyncIOEngine& operator=(const AsyncIOEngine&) = delete;

    AsyncIOEngine(AsyncIOEngine&& other) noexcept
        : ring_(other.ring_),
          queue_depth_(other.queue_depth_),
          flags_(other.flags_),
          is_initialized_(other.is_initialized_) {
        std::memset(&other.ring_, 0, sizeof(io_uring));
        other.is_initialized_ = false;
        other.queue_depth_ = 0;
        other.flags_ = 0;
    }

    AsyncIOEngine& operator=(AsyncIOEngine&& other) noexcept {
        if (this != &other) {
            if (is_initialized_) {
                io_uring_queue_exit(&ring_);
            }
            ring_ = other.ring_;
            queue_depth_ = other.queue_depth_;
            flags_ = other.flags_;
            is_initialized_ = other.is_initialized_;

            std::memset(&other.ring_, 0, sizeof(io_uring));
            other.is_initialized_ = false;
            other.queue_depth_ = 0;
            other.flags_ = 0;
        }
        return *this;
    }

    bool submit_read(int fd, void* buf, size_t count, off_t offset);
    bool submit_write(int fd, const void* buf, size_t count, off_t offset);
    
    int submit_sqes();

    int wait_cqe(struct io_uring_cqe** cqe);
    void cqe_seen(struct io_uring_cqe* cqe);
};