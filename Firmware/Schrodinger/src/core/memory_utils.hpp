#ifndef MEMORY_UTILS_HPP
#define MEMORY_UTILS_HPP

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "logger.hpp"

// Memory allocation with PSRAM fallback
inline void* malloc_psram_fallback(size_t size) {
    void* ptr = nullptr;
    
    // Try PSRAM first if available
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= size) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            LOG_MEMORY_D("Allocated %u bytes in PSRAM at %p", size, ptr);
            return ptr;
        }
    }
    
    // Fallback to regular heap
    ptr = malloc(size);
    if (ptr) {
        LOG_MEMORY_D("Allocated %u bytes in heap at %p", size, ptr);
    } else {
        LOG_MEMORY_E("Failed to allocate %u bytes", size);
    }
    
    return ptr;
}

// Free memory allocated with malloc_psram_fallback
inline void free_psram_fallback(void* ptr) {
    if (ptr) {
        LOG_MEMORY_D("Freeing memory at %p", ptr);
        free(ptr);  // free() works for both heap and PSRAM
    }
}

// Memory status logging
inline void log_memory_status(const char* context) {
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    
    LOG_MEMORY_I("%s - Heap: %u, PSRAM: %u, Largest: %u", 
                 context, free_heap, free_psram, largest_block);
}

#endif // MEMORY_UTILS_HPP
