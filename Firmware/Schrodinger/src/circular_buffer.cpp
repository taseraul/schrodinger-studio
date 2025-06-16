#include "circular_buffer.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "config.hpp"
#include <cstring>

static const char* TAG = "circular_buffer";

// Global instance
CircularBuffer bt_circular_buffer;

CircularBuffer::CircularBuffer() 
    : buffer(nullptr), buffer_size(0), write_index(0), read_index(0), 
      available_samples(0), mutex(nullptr), use_psram(false),
      overflow_count(0), total_writes(0), total_reads(0) {
}

CircularBuffer::~CircularBuffer() {
    if (buffer) {
        if (use_psram) {
            heap_caps_free(buffer);
        } else {
            free(buffer);
        }
        buffer = nullptr;
    }
    
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
}

bool CircularBuffer::init(size_t preferred_size) {
    // Create mutex first
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Check PSRAM availability
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Available PSRAM: %d bytes", psram_size);
    
    size_t buffer_bytes = preferred_size * sizeof(uint32_t);
    
    // Try to allocate in PSRAM first
    if (psram_size > buffer_bytes + 1024) { // Leave some PSRAM headroom
        buffer = (uint32_t*)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM);
        if (buffer) {
            use_psram = true;
            buffer_size = preferred_size;
            ESP_LOGI(TAG, "Allocated %d samples (%d bytes) in PSRAM", buffer_size, buffer_bytes);
        }
    }
    
    // Fallback to regular heap with smaller size
    if (!buffer) {
        size_t fallback_size = preferred_size / 4; // Use 1/4 size in regular heap
        buffer_bytes = fallback_size * sizeof(uint32_t);
        
        buffer = (uint32_t*)malloc(buffer_bytes);
        if (buffer) {
            use_psram = false;
            buffer_size = fallback_size;
            ESP_LOGW(TAG, "PSRAM allocation failed, using heap: %d samples (%d bytes)", 
                     buffer_size, buffer_bytes);
        }
    }
    
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer memory");
        vSemaphoreDelete(mutex);
        mutex = nullptr;
        return false;
    }
    
    // Initialize buffer state
    clear();
    
    ESP_LOGI(TAG, "Circular buffer initialized successfully");
    ESP_LOGI(TAG, "Buffer size: %d samples, Using %s", 
             buffer_size, use_psram ? "PSRAM" : "Heap");
    
    return true;
}

size_t CircularBuffer::write(const uint32_t* samples, size_t count) {
    if (!buffer || !samples || count == 0 || !mutex) {
        ESP_LOGW(TAG, "Invalid write parameters: buffer=%p, samples=%p, count=%d, mutex=%p", 
                 buffer, samples, count, mutex);
        return 0;
    }
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Write mutex timeout");
        return 0; // Timeout
    }
    
    size_t written = 0;
    size_t space_available = buffer_size - available_samples;
    
    if (count > space_available) {
        // Buffer overflow - we'll overwrite old data
        overflow_count++;
        ESP_LOGW(TAG, "Buffer overflow! Requested: %d, Available: %d, Total overflows: %d", 
                 count, space_available, overflow_count);
        
        // Advance read pointer to make space
        size_t samples_to_skip = count - space_available;
        read_index = (read_index + samples_to_skip) % buffer_size;
        available_samples -= samples_to_skip;
        space_available = count;
    }
    
    // Write samples in two parts if wrapping around
    size_t first_part = buffer_size - write_index;
    if (first_part > count) {
        first_part = count;
    }
    
    // Copy first part
    memcpy(&buffer[write_index], samples, first_part * sizeof(uint32_t));
    written += first_part;
    write_index = (write_index + first_part) % buffer_size;
    
    // Copy second part if needed
    if (written < count) {
        size_t second_part = count - written;
        memcpy(&buffer[write_index], &samples[written], second_part * sizeof(uint32_t));
        written += second_part;
        write_index = (write_index + second_part) % buffer_size;
    }
    
    available_samples += written;
    total_writes += written;
    
    xSemaphoreGive(mutex);
    return written;
}

size_t CircularBuffer::read(uint32_t* dest, size_t count) {
    if (!buffer || !dest || count == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0; // Timeout
    }
    
    size_t to_read = (count > available_samples) ? available_samples : count;
    size_t read_count = 0;
    
    if (to_read > 0) {
        // Read samples in two parts if wrapping around
        size_t first_part = buffer_size - read_index;
        if (first_part > to_read) {
            first_part = to_read;
        }
        
        // Copy first part
        memcpy(dest, &buffer[read_index], first_part * sizeof(uint32_t));
        read_count += first_part;
        read_index = (read_index + first_part) % buffer_size;
        
        // Copy second part if needed
        if (read_count < to_read) {
            size_t second_part = to_read - read_count;
            memcpy(&dest[read_count], &buffer[read_index], second_part * sizeof(uint32_t));
            read_count += second_part;
            read_index = (read_index + second_part) % buffer_size;
        }
        
        available_samples -= read_count;
        total_reads += read_count;
    }
    
    xSemaphoreGive(mutex);
    return read_count;
}

void CircularBuffer::reset_stats() {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        overflow_count = 0;
        total_writes = 0;
        total_reads = 0;
        xSemaphoreGive(mutex);
    }
}

void CircularBuffer::clear() {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        write_index = 0;
        read_index = 0;
        available_samples = 0;
        xSemaphoreGive(mutex);
    }
}

// Helper function to convert 16-bit samples to 32-bit
void convert_16bit_to_32bit(const int16_t* src, uint32_t* dest, size_t count) {
    for (size_t i = 0; i < count; i++) {
        // Convert 16-bit signed to 32-bit signed, then to uint32_t
        int32_t sample_32 = (int32_t)src[i] * 65536; // Scale up by 2^16
        dest[i] = (uint32_t)sample_32;
    }
}

// Helper function to convert 8-bit samples to 32-bit (if needed)
void convert_8bit_to_32bit(const uint8_t* src, uint32_t* dest, size_t count) {
    for (size_t i = 0; i < count; i++) {
        // Convert 8-bit unsigned to 32-bit signed, then to uint32_t
        int32_t sample_32 = ((int32_t)src[i] - 128) * 16777216; // Scale and center
        dest[i] = (uint32_t)sample_32;
    }
}
