#include "utils.hpp"

void run_fstream_benchmark(void* ptr, const char* filepath, size_t block_size, size_t size);

void run_pwrite_benchmark(void* ptr, int fd, size_t block_size, size_t size);
