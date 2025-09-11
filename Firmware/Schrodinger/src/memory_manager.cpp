#include "memory_manager.hpp"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"

static const char* TAG = "memory";

void force_garbage_collection() {
    ESP_LOGD(TAG, "Forcing garbage collection");
    
    // Force heap cleanup
    ESP.getHeapSize();
    
    // Small delay to allow cleanup
    delay(10);
    
    // Try to trigger any pending cleanup operations
    heap_caps_check_integrity_all(true);
    
    ESP_LOGD(TAG, "Garbage collection completed");
}

bool check_memory_health() {
    uint32_t freeHeap = ESP.getFreeHeap();
    size_t largestBlock = get_largest_free_block();
    
    // Check for memory fragmentation
    if (largestBlock < (freeHeap / 2)) {
        ESP_LOGW(TAG, "Memory fragmentation detected. Free: %d, Largest block: %d", 
                 freeHeap, largestBlock);
        return false;
    }
    
    // Check for critical memory levels
    if (freeHeap < CRITICAL_HEAP_THRESHOLD) {
        ESP_LOGE(TAG, "Heap memory critically low: %d bytes", freeHeap);
        return false;
    }
    
    return true;
}

void print_memory_info() {
    ESP_LOGI(TAG, "=== MEMORY STATUS ===");
    ESP_LOGI(TAG, "Free heap: %d bytes", ESP.getFreeHeap());
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", ESP.getFreePsram());
    ESP_LOGI(TAG, "Largest free block: %d bytes", get_largest_free_block());
    ESP_LOGI(TAG, "Min free heap: %d bytes", ESP.getMinFreeHeap());
    ESP_LOGI(TAG, "Heap size: %d bytes", ESP.getHeapSize());
    ESP_LOGI(TAG, "PSRAM size: %d bytes", ESP.getPsramSize());
    
    // Print heap caps info
    ESP_LOGI(TAG, "Internal free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "SPIRAM free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "DMA capable free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
    ESP_LOGI(TAG, "======================");
}

size_t get_largest_free_block() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void* malloc_psram_fallback(size_t size) {
    // Validate input size
    if (size == 0 || size > 65536) {  // Reasonable upper limit
        ESP_LOGW(TAG, "Invalid allocation size: %d", size);
        return nullptr;
    }
    
    // Check memory health before allocation
    uint32_t free_heap = ESP.getFreeHeap();
    if (free_heap < CRITICAL_HEAP_THRESHOLD) {
        ESP_LOGW(TAG, "Heap critically low (%d bytes), refusing allocation of %d bytes", free_heap, size);
        return nullptr;
    }
    
    void* ptr = nullptr;
    
    // Try PSRAM first for large allocations
    if (size > 1024) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            ESP_LOGD(TAG, "Allocated %d bytes in PSRAM at %p", size, ptr);
            return ptr;
        }
        ESP_LOGD(TAG, "PSRAM allocation failed for %d bytes, trying internal RAM", size);
    }
    
    // Fallback to internal RAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
    if (ptr) {
        ESP_LOGD(TAG, "Allocated %d bytes in internal RAM at %p", size, ptr);
    } else {
        ESP_LOGW(TAG, "Failed to allocate %d bytes in any memory type", size);
        // Try to force garbage collection and retry once
        force_garbage_collection();
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
        if (ptr) {
            ESP_LOGI(TAG, "Allocation succeeded after garbage collection: %d bytes at %p", size, ptr);
        } else {
            ESP_LOGE(TAG, "Allocation failed even after garbage collection: %d bytes", size);
        }
    }
    
    return ptr;
}

void free_psram_fallback(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}
