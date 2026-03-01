#pragma once

#include <chrono>
#include <sys/resource.h>
#include <stdexcept>
#include <utility>

class PerfMonitor {
public:
    PerfMonitor() {
        getrusage(RUSAGE_SELF, &start_rusage_);
        start_time_ = std::chrono::steady_clock::now();
        stopped_ = false;
    }

    ~PerfMonitor() {
        stop();
    }

    void stop() {
        if (stopped_) {
            return;
        }

        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time_;
        elapsed_time_ = elapsed.count();

        struct rusage end_rusage;
        getrusage(RUSAGE_SELF, &end_rusage);
        kernel_time_ = timevalToSec(end_rusage.ru_stime) - timevalToSec(start_rusage_.ru_stime);
        user_time_ = timevalToSec(end_rusage.ru_utime) - timevalToSec(start_rusage_.ru_utime);

        stopped_ = true;
    }

    double elapsed() const {
        return elapsed_time_;
    }

    double kernel() const {
        return kernel_time_;
    }

    double user() const {
        return user_time_;
    }

private:
    std::chrono::steady_clock::time_point start_time_;
    struct rusage start_rusage_;
    double elapsed_time_ = 0.0;
    double kernel_time_ = 0.0;                         
    double user_time_ = 0.0;                           
    bool stopped_ = false;

    static double timevalToSec(const struct timeval& tv) {
        return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1e6;
    }
};