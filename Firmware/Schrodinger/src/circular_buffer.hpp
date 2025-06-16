#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class CircularBuffer {
private:
    uint32_t* buffer;
    size_t capacity;
    volatile size_t write_pos;
    volatile size_t read_pos;
    volatile size_t available_samples;
    SemaphoreHandle_t mutex;
    
public:
    CircularBuffer(size_t size);
    ~CircularBuffer();
    
    // Write stereo 16-bit samples, convert to mono 32-bit
    bool write_bt_samples(const uint8_t* data, size_t byte_length);
    
    // Read mono 32-bit samples for FFT
    bool read_fft_samples(uint32_t* dest, size_t sample_count);
    
    // Get available sample count
    size_t available() const { return available_samples; }
    
    // Check if buffer has enough samples for FFT
    bool has_fft_frame(size_t fft_size) const { return available_samples >= fft_size; }
    
    // Clear buffer
    void clear();
};

#endif
