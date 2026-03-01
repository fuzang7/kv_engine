#include "utils.hpp"

void* aligned_allocate(size_t alignment, size_t size) {
    void *ptr = aligned_alloc(alignment, size);
    return ptr;
}

void aligned_free(void* ptr) noexcept {
    free(ptr);
}

void fill_data(void* ptr, size_t size) {
    memset(ptr, 0xAA, size);
}

bool clear_page_cache() {
    FILE* fp = std::fopen("/proc/sys/vm/drop_caches", "w");
    if (!fp) {
        return false;
    }

    if (std::fputs("3", fp) == EOF) {
        std::fclose(fp);
        return false;
    }

    std::fflush(fp);
    std::fclose(fp);
    return true;
}