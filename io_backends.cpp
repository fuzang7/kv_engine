#include "io_backends.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <iosfwd>


void run_fstream_benchmark(void* ptr, const char* filepath, size_t block_size, size_t size) {
    std::ofstream file(filepath, std::ios::in | std::ios::out | std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Failed to open file!" << std::endl;
        return;
    }

    size_t remaining_total = size;

    while(remaining_total > 0) {
        size_t curr_block = (remaining_total < block_size) ? remaining_total : block_size;
        file.write(static_cast<const char*>(ptr), curr_block);

        if (file.fail()) {
            std::cerr << "Failed to write!" << std::endl;
            return;
        }

        remaining_total -= curr_block;
    }

    file.close();
    int fd = open(filepath, O_WRONLY);
    if (fd < 0) {
        perror("fstream open fail failed");
        return;
    }
    fdatasync(fd);
    close(fd);

    return;
}

void run_pwrite_benchmark(void* ptr, int fd, size_t block_size, size_t size) {
    off_t file_off = 0;
    size_t remaining_total = size;

    while(remaining_total > 0) {
        size_t curr_block = (remaining_total < block_size) ? remaining_total : block_size;
        size_t remaining_block = curr_block;
        off_t block_file_off = file_off;
        size_t block_written = 0;

        while (remaining_block > 0) {
            char* curr_ptr = (char*)ptr + block_written;
            ssize_t ret = pwrite(fd, curr_ptr, remaining_block, block_file_off);

            if (ret == -1) {
                if (errno == EINTR) continue;
                perror("pwrite failed");
                return;
            }

            if (ret == 0) {
                fprintf(stderr, "pwrite returned 0 abruptly, device full or error\n");
                return;
            }

            block_file_off += ret;
            block_written += ret;
            remaining_block -= ret; 
        }
        
        file_off += curr_block;
        remaining_total -= curr_block;

    }

    fdatasync(fd);

    return;
}