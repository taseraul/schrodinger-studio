#include "circular_buffer.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstring>

static const char* TAG = "circular_buffer";

CircularBuffer::CircularBuffer(size_t size) : capacity(size), write_pos(0), read_pos(0), available_samples(0) {
    // Allocate buffer in PSRAM for better memory management
    buffer = (uint32_t*)heap_caps_malloc(capacity * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate circular buffer in PSRAM, trying regular heap");
        buffer = (uint32_t*)malloc(capacity * sizeof(uint32_t));
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate circular buffer in regular heap");
            capacity = 0;
            return;
        }
    }
    
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        if (heap_caps_get_allocated_size(buffer) > 0) {
            heap_caps_free(buffer);
        } else {
            free(buffer);
        }
        buffer = nullptr;
        capacity = 0;
        return;
    }
    
    ESP_LOGI(TAG, "Circular buffer created with capacity: %d samples (%d bytes)", 
             capacity, capacity * sizeof(uint32_t));
}

CircularBuffer::~CircularBuffer() {
    if (buffer) {
        free(buffer);
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
}

bool CircularBuffer::write_bt_samples(const uint8_t* data, size_t byte_length) {
    if (!buffer || !mutex) return false;
    
    // Convert byte length to stereo sample count (4 bytes per stereo sample: 2x16-bit)
    size_t stereo_samples = byte_length / 4;
    if (stereo_samples == 0) return false;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    
    // Check if we have space (leave one slot empty to distinguish full from empty)
    if (available_samples + stereo_samples >= capacity) {
        // Buffer overflow - drop oldest samples
        size_t samples_to_drop = (available_samples + stereo_samples) - capacity + 1;
        read_pos = (read_pos + samples_to_drop) % capacity;
        available_samples -= samples_to_drop;
        ESP_LOGW(TAG, "Buffer overflow, dropped %d samples", samples_to_drop);
    }
    
    // Convert and write samples
    const int16_t* stereo_data = (const int16_t*)data;
    for (size_t i = 0; i < stereo_samples; i++) {
        // Convert stereo 16-bit to mono 32-bit
        int16_t left = stereo_data[i * 2];
        int16_t right = stereo_data[i * 2 + 1];
        
        // Mix to mono and scale to 32-bit
        int32_t mono = ((int32_t)left + (int32_t)right) / 2;
        uint32_t sample = (uint32_t)(mono << 16); // Scale to 32-bit range
        
        buffer[write_pos] = sample;
        write_pos = (write_pos + 1) % capacity;
        available_samples++;
    }
    
    xSemaphoreGive(mutex);
    return true;
}

bool CircularBuffer::read_fft_samples(uint32_t* dest, size_t sample_count) {
    if (!buffer || !mutex || !dest) return false;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    
    if (available_samples < sample_count) {
        xSemaphoreGive(mutex);
        return false;
    }
    
    // Copy samples
    for (size_t i = 0; i < sample_count; i++) {
        dest[i] = buffer[read_pos];
        read_pos = (read_pos + 1) % capacity;
    }
    
    available_samples -= sample_count;
    
    xSemaphoreGive(mutex);
    return true;
}

void CircularBuffer::clear() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        write_pos = 0;
        read_pos = 0;
        available_samples = 0;
        xSemaphoreGive(mutex);
    }
}
