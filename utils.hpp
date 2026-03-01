#pragma once

#include <cstdlib>
#include <cstring>
#include <cstdio>

void* aligned_allocate(size_t size, size_t alignment);

void aligned_free(void* ptr) noexcept;

void fill_data(void* ptr, size_t size);

bool clear_page_cache();