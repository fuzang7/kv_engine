#include "async_io_engine.hpp"
#include "utils.hpp"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

#define FILE_PATH "test.bin"
#define BUFFER_SIZE (512 * 1024 * 1024)  // 512MB
#define ALIGN_SIZE (4 * 1024)  // 4K

int main() {
    void *ptr = aligned_allocate(ALIGN_SIZE, BUFFER_SIZE);
    if (ptr == nullptr) {
        perror("aligned_allocate failed"); 
        aligned_free(ptr);
        return -1;
    }
    fill_data(ptr, BUFFER_SIZE);
    int fd = open(FILE_PATH, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fd < 0) {
        perror("Failed to open file");
        aligned_free(ptr);
        return 1;
    }

    try {
        AsyncIOEngine engine(16, 0);

        if (!engine.submit_write(ptr, fd, BUFFER_SIZE, 0)) {
            std::cerr << "Failed to submit write request (queue full?)" << std::endl;
            close(fd);
            unlink(FILE_PATH);
            aligned_free(ptr);
            return 1;
        }
        std::cout << "Write request submitted." << std::endl;

        int submitted = engine.submit_sqes();
        std::cout << "Submitted " << submitted << " SQE(s) to kernel." << std::endl;

        int total_completed = 0;
        while (total_completed == 0) {
            total_completed = engine.reap_completions();
        }
        std::cout << "IO completed. Reaped " << total_completed << " CQE(s)." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Engine exception: " << e.what() << std::endl;
        close(fd);
        unlink(FILE_PATH);
        aligned_free(ptr);
        return 1;
    }

    aligned_free(ptr);
    close(fd);
    return 0;
}