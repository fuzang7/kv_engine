#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "utils.hpp"
#include "io_backends.hpp"
#include "perf_monitor.hpp"

#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#define FILE_PATH "test.bin"
#define FILE_SIZE (10 * 1024ULL * 1024 * 1024)  // 10G
#define BUFFER_SIZE (512 * 1024 * 1024)  // 512MB
#define ALIGN_SIZE (4 * 1024)  // 4K


int main () {
    std::cout << std::left << std::setw(12) << "I/O Mode" 
          << std::right << std::setw(18) << "Throughput" 
          << std::setw(16) << "Wall-clock" 
          << std::setw(16) << "Sys Time" 
          << std::setw(16) << "User Time" << "\n";
    std::cout << std::string(78, '-') << "\n";


    int fd_init = open(FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_init < 0) {
        perror("Initial open failed");
        return -1;
    }
    if (fallocate(fd_init, 0, 0, FILE_SIZE) == -1) {
        perror("Initial fallocate failed");
        close(fd_init);
        return -1;
    }
    close(fd_init);

    void *ptr = aligned_allocate(ALIGN_SIZE, BUFFER_SIZE);
    if (ptr == nullptr) {
        perror("aligned_allocate failed"); 
        aligned_free(ptr);
        return -1;
    }

    fill_data(ptr, BUFFER_SIZE);

    if (!clear_page_cache()) {
        perror("clear_page_cache failed"); 
        aligned_free(ptr);
        return -1;
    }

    int fd_direct = open(FILE_PATH, O_WRONLY | O_DIRECT);

    if (fd_direct < 0) {
        perror("open failed");
        aligned_free(ptr);
        return -1;
    }

    {
        PerfMonitor PM1;

        run_pwrite_benchmark(ptr, fd_direct, BUFFER_SIZE, FILE_SIZE);
        PM1.stop();
        
        double throughput1 = (static_cast<double>(FILE_SIZE) / (1024.0 * 1024.0 * 1024.0)) / PM1.elapsed();
    
        std::cout << std::left << std::setw(12) << "O_DIRECT" 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) << throughput1 << " GiB/s"
              << std::setw(14) << PM1.elapsed() << " s"
              << std::setw(14) << PM1.kernel() << " s"
              << std::setw(14) << PM1.user() << " s\n";
        close(fd_direct);
    }

    if (system("sync; echo 3 > /proc/sys/vm/drop_caches") == -1) {
        perror("system failed");
    }

    int fd_buffer = open(FILE_PATH, O_WRONLY);

    if (fd_buffer < 0) {
        perror("open failed");
        aligned_free(ptr);
        return -1;
    }

    {
        PerfMonitor PM2;

        run_pwrite_benchmark(ptr, fd_buffer, BUFFER_SIZE, FILE_SIZE);
        PM2.stop();
        double throughput2 = (static_cast<double>(FILE_SIZE) / (1024.0 * 1024.0 * 1024.0)) / PM2.elapsed();
        std::cout << std::left << std::setw(12) << "buffered" 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) << throughput2 << " GiB/s"
              << std::setw(14) << PM2.elapsed() << " s"
              << std::setw(14) << PM2.kernel() << " s"
              << std::setw(14) << PM2.user() << " s\n";
        close(fd_buffer);
    }
    
    if (system("sync; echo 3 > /proc/sys/vm/drop_caches") == -1) {
        perror("system failed");
    }

    {
        PerfMonitor PM3;

        run_fstream_benchmark(ptr, FILE_PATH, BUFFER_SIZE, FILE_SIZE);
        PM3.stop();
        double throughput3 = (static_cast<double>(FILE_SIZE) / (1024.0 * 1024.0 * 1024.0)) / PM3.elapsed();
        std::cout << std::left << std::setw(12) << "fstream" 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) << throughput3 << " GiB/s"
              << std::setw(14) << PM3.elapsed() << " s"
              << std::setw(14) << PM3.kernel() << " s"
              << std::setw(14) << PM3.user() << " s\n";
    }

    aligned_free(ptr);
    return 0;
}