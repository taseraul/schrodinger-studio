#include "circular_buffer.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstring>

static const char* TAG = "circular_buffer";

CircularBuffer::CircularBuffer(size_t size_bytes) {
    buffer = nullptr;
    buffer_size = size_bytes / sizeof(uint32_t);  // Convert bytes to 32-bit elements
    write_index = 0;
    read_index = 0;
    sample_count = 0;
    mutex = nullptr;
}

CircularBuffer::~CircularBuffer() {
    deinit();
}

bool CircularBuffer::init() {
    // Create mutex first
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Allocate buffer in PSRAM
    buffer = (uint32_t*)heap_caps_malloc(buffer_size * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes in PSRAM", buffer_size * sizeof(uint32_t));
        vSemaphoreDelete(mutex);
        mutex = nullptr;
        return false;
    }
    
    // Clear buffer
    memset(buffer, 0, buffer_size * sizeof(uint32_t));
    
    ESP_LOGI(TAG, "Circular buffer initialized: %d bytes (%d samples) in PSRAM", 
             buffer_size * sizeof(uint32_t), buffer_size / 2);
    
    return true;
}

void CircularBuffer::deinit() {
    if (buffer) {
        heap_caps_free(buffer);
        buffer = nullptr;
    }
    
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    
    write_index = 0;
    read_index = 0;
    sample_count = 0;
}

bool CircularBuffer::write_samples_16bit(const uint8_t* data, size_t byte_length) {
    if (!buffer || !mutex || !data) {
        return false;
    }
    
    // Convert byte length to number of 16-bit sample pairs
    size_t sample_pairs = byte_length / 4;  // 4 bytes = 1 LR pair of 16-bit samples
    
    if (sample_pairs == 0) {
        return true;  // Nothing to write
    }
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Write mutex timeout");
        return false;
    }
    
    const int16_t* src = (const int16_t*)data;
    size_t written = 0;
    
    for (size_t i = 0; i < sample_pairs; i++) {
        // Check if buffer is full
        if (sample_count >= buffer_size) {
            // Buffer overflow - drop oldest samples by advancing read index
            read_index = (read_index + 2) % buffer_size;
            sample_count -= 2;
            ESP_LOGW(TAG, "Buffer overflow, dropping samples");
        }
        
        // Convert 16-bit LR pair to 32-bit LR pair
        int16_t left_16 = src[i * 2];
        int16_t right_16 = src[i * 2 + 1];
        
        // Convert to 32-bit by left-shifting 16 bits (sign extension)
        int32_t left_32 = ((int32_t)left_16) << 16;
        int32_t right_32 = ((int32_t)right_16) << 16;
        
        // Store in buffer
        buffer[write_index] = (uint32_t)left_32;
        buffer[write_index + 1] = (uint32_t)right_32;
        
        write_index = (write_index + 2) % buffer_size;
        sample_count += 2;
        written++;
    }
    
    xSemaphoreGive(mutex);
    
    return written == sample_pairs;
}

bool CircularBuffer::read_samples_32bit(uint32_t* dest, size_t sample_pairs_needed) {
    if (!buffer || !mutex || !dest) {
        return false;
    }
    
    size_t elements_needed = sample_pairs_needed * 2;  // LR pairs = 2 elements each
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Read mutex timeout");
        return false;
    }
    
    // Check if we have enough samples
    if (sample_count < elements_needed) {
        xSemaphoreGive(mutex);
        return false;
    }
    
    // Copy samples from circular buffer
    for (size_t i = 0; i < elements_needed; i++) {
        dest[i] = buffer[read_index];
        read_index = (read_index + 1) % buffer_size;
    }
    
    sample_count -= elements_needed;
    
    xSemaphoreGive(mutex);
    
    return true;
}

size_t CircularBuffer::get_sample_count() {
    if (!mutex) return 0;
    
    size_t count = 0;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        count = sample_count / 2;  // Return number of sample pairs
        xSemaphoreGive(mutex);
    }
    
    return count;
}

float CircularBuffer::get_utilization() {
    if (!mutex || buffer_size == 0) return 0.0f;
    
    float utilization = 0.0f;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        utilization = (float)sample_count / (float)buffer_size * 100.0f;
        xSemaphoreGive(mutex);
    }
    
    return utilization;
}
