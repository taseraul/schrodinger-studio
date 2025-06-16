#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class CircularBuffer {
private:
    uint32_t* buffer;
    size_t buffer_size;
    volatile size_t write_index;
    volatile size_t read_index;
    volatile size_t available_samples;
    SemaphoreHandle_t mutex;
    bool use_psram;
    
    // Statistics
    volatile uint32_t overflow_count;
    volatile uint32_t total_writes;
    volatile uint32_t total_reads;

public:
    CircularBuffer();
    ~CircularBuffer();
    
    // Initialize buffer with preferred size, will use PSRAM if available
    bool init(size_t preferred_size);
    
    // Write samples to buffer (thread-safe)
    // Returns number of samples actually written
    size_t write(const uint32_t* samples, size_t count);
    
    // Read samples from buffer (thread-safe)
    // Returns number of samples actually read
    size_t read(uint32_t* dest, size_t count);
    
    // Get current fill level
    size_t available() const { return available_samples; }
    
    // Get buffer capacity
    size_t capacity() const { return buffer_size; }
    
    // Get statistics
    uint32_t get_overflow_count() const { return overflow_count; }
    uint32_t get_total_writes() const { return total_writes; }
    uint32_t get_total_reads() const { return total_reads; }
    bool is_using_psram() const { return use_psram; }
    
    // Reset statistics
    void reset_stats();
    
    // Clear buffer
    void clear();
};

// Global circular buffer instance
extern CircularBuffer bt_circular_buffer;

// Helper functions for 16-bit to 32-bit conversion
void convert_16bit_to_32bit(const int16_t* src, uint32_t* dest, size_t count);
void convert_8bit_to_32bit(const uint8_t* src, uint32_t* dest, size_t count);

#endif // CIRCULAR_BUFFER_HPP
