#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include "Arduino.h"
#include "esp_heap_caps.h"

// Memory management functions
void force_garbage_collection();
bool check_memory_health();
void print_memory_info();
size_t get_largest_free_block();
void* malloc_psram_fallback(size_t size);
void free_psram_fallback(void* ptr);

// Memory thresholds
#define CRITICAL_HEAP_THRESHOLD 15000
#define LOW_HEAP_THRESHOLD 30000
#define RESERVE_HEAP_SIZE 8192

#endif
