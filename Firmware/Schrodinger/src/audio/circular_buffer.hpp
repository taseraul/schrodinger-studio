#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class CircularBuffer {
private:
    uint32_t* buffer;           // PSRAM buffer for 32-bit LR pairs
    size_t buffer_size;         // Size in number of 32-bit elements
    volatile size_t write_index;
    volatile size_t read_index;
    volatile size_t sample_count;
    SemaphoreHandle_t mutex;
    
public:
    CircularBuffer(size_t size_bytes);
    ~CircularBuffer();
    
    bool init();
    void deinit();
    
    // Write 16-bit LR pairs, converts to 32-bit internally
    bool write_samples_16bit(const uint8_t* data, size_t byte_length);
    
    // Read 32-bit LR pairs
    bool read_samples_32bit(uint32_t* dest, size_t sample_pairs_needed);
    
    // Get current sample count
    size_t get_sample_count();
    
    // Get buffer utilization percentage
    float get_utilization();
};

#endif // CIRCULAR_BUFFER_HPP
