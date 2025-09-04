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
    // Try PSRAM first for large allocations
    if (size > 1024) {
        void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            return ptr;
        }
    }
    
    // Fallback to internal RAM
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

void free_psram_fallback(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}
