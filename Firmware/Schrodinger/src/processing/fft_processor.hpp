#ifndef FFT_PROCESSOR_HPP
#define FFT_PROCESSOR_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_dsp.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"

// FFT processor states
typedef enum {
    FFT_STATE_UNINITIALIZED = 0,
    FFT_STATE_INITIALIZING,
    FFT_STATE_RUNNING,
    FFT_STATE_ERROR,
    FFT_STATE_SHUTDOWN
} fft_state_t;

// Peak structure for FFT analysis
typedef struct {
    int index;
    float magnitude;
} fft_peak_t;

// FFT processor class
class FFTProcessor {
public:
    static FFTProcessor& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // State management
    fft_state_t getState() const { return fft_state_; }
    
    // Configuration
    bool setNumHighest(int value);
    bool setMinWidth(int value);
    int getNumHighest() const;
    int getMinWidth() const;
    
    // Processing control
    bool startProcessing();
    bool stopProcessing();
    bool isProcessing() const;
    
    // Statistics
    uint32_t getProcessedFrames() const { return processed_frames_; }
    uint32_t getProcessingErrors() const { return error_count_; }
    float getProcessingRate() const;
    
    // Memory management
    size_t getMemoryUsage() const;
    bool isMemoryHealthy() const;
    
    // Error handling
    void handleFFTError(const char* error, const char* context);
    
private:
    FFTProcessor() = default;
    ~FFTProcessor() = default;
    FFTProcessor(const FFTProcessor&) = delete;
    FFTProcessor& operator=(const FFTProcessor&) = delete;
    
    // Internal state
    fft_state_t fft_state_ = FFT_STATE_UNINITIALIZED;
    TaskHandle_t fft_task_handle_ = nullptr;
    SemaphoreHandle_t config_mutex_ = nullptr;
    
    // Configuration
    int num_highest_ = 6;
    int min_width_ = 10;
    
    // Processing buffers (PSRAM allocated)
    uint32_t* samples_buffer_ = nullptr;
    float* fft_input_ = nullptr;
    float* fft_output_ = nullptr;
    float* temp_buffer_ = nullptr;
    
    // Statistics
    uint32_t processed_frames_ = 0;
    uint32_t error_count_ = 0;
    uint32_t last_stats_update_ = 0;
    uint32_t processing_start_time_ = 0;
    
    // Memory tracking
    size_t total_memory_allocated_ = 0;
    
    // Smoothing parameters
    float smoothed_max_magnitude_ = 1e-6f;
    
    // Configuration change callback
    static void onConfigChange(config_category_t category, void* user_data);
    
    // Internal methods
    void setState(fft_state_t state);
    bool allocateBuffers();
    void deallocateBuffers();
    bool initializeESPDSP();
    void deinitializeESPDSP();
    
    // Processing methods
    static void fftTaskWrapper(void* parameter);
    void fftTask();
    void convertToMono();
    void performFFT();
    void extractPeaks(fft_peak_t* peaks, int& peak_count);
    void normalizeMagnitudes(fft_peak_t* peaks, int count);
    void sortPeaksByIndex(fft_peak_t* peaks, int count);
    
    // Utility methods
    bool performHealthCheck();
    void logProcessingStats();
    void updateStatistics();
};

// Global FFT processor access
#define FFT_PROCESSOR FFTProcessor::getInstance()

#endif // FFT_PROCESSOR_HPP
