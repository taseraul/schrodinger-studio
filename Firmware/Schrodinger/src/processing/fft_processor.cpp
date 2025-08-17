#include "fft_processor.hpp"
#include "../core/config_manager.hpp"
#include "../core/memory_utils.hpp"
#include "../audio/bt_audio.hpp"
#include "../network/network_manager.hpp"
#include <cmath>
#include <algorithm>

// FFT processing constants
#define MAX_FREQ 10000
#define MAX_BIN ((MAX_FREQ * SAMPLES) / SAMPLE_RATE)
#define SMOOTH_TIME_SEC 10.0f
#define FFT_FPS 100.0f
#define ALPHA (1.0f - expf(-1.0f / (SMOOTH_TIME_SEC * FFT_FPS)))

// Static instance
FFTProcessor& FFTProcessor::getInstance() {
    static FFTProcessor instance;
    return instance;
}

bool FFTProcessor::initialize() {
    LOG_PROCESS_I("Initializing FFT processor");
    setState(FFT_STATE_INITIALIZING);
    
    // Create configuration mutex
    config_mutex_ = xSemaphoreCreateMutex();
    if (!config_mutex_) {
        LOG_PROCESS_E("Failed to create configuration mutex");
        setState(FFT_STATE_ERROR);
        return false;
    }
    
    // Initialize configuration from config manager
    const ConfigManager::ProcessingConfig& config = CONFIG_MGR.getProcessingConfig();
    if (xSemaphoreTake(config_mutex_, pdMS_TO_TICKS(100))) {
        num_highest_ = config.fft_num_highest;
        min_width_ = config.fft_min_width;
        xSemaphoreGive(config_mutex_);
    }
    
    // Allocate processing buffers
    if (!allocateBuffers()) {
        LOG_PROCESS_E("Failed to allocate FFT buffers");
        setState(FFT_STATE_ERROR);
        return false;
    }
    
    // Initialize ESP-DSP
    if (!initializeESPDSP()) {
        LOG_PROCESS_E("Failed to initialize ESP-DSP");
        deallocateBuffers();
        setState(FFT_STATE_ERROR);
        return false;
    }
    
    // Register for configuration changes
    CONFIG_MGR.registerChangeCallback(CONFIG_CATEGORY_PROCESSING, onConfigChange, this);
    
    // Register with system manager
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, SUBSYSTEM_STATUS_READY);
    
    setState(FFT_STATE_RUNNING);
    LOG_PROCESS_I("FFT processor initialized successfully");
    return true;
}

void FFTProcessor::shutdown() {
    LOG_PROCESS_I("Shutting down FFT processor");
    setState(FFT_STATE_SHUTDOWN);
    
    // Stop processing task
    stopProcessing();
    
    // Unregister callbacks
    CONFIG_MGR.unregisterChangeCallback(CONFIG_CATEGORY_PROCESSING);
    
    // Deinitialize ESP-DSP
    deinitializeESPDSP();
    
    // Deallocate buffers
    deallocateBuffers();
    
    // Destroy mutex
    if (config_mutex_) {
        vSemaphoreDelete(config_mutex_);
        config_mutex_ = nullptr;
    }
    
    // Update system manager
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, SUBSYSTEM_STATUS_SHUTDOWN);
    
    setState(FFT_STATE_UNINITIALIZED);
    LOG_PROCESS_I("FFT processor shutdown complete");
}

void FFTProcessor::update() {
    if (fft_state_ != FFT_STATE_RUNNING) {
        return;
    }
    
    // Update statistics periodically
    uint32_t now = millis();
    if (now - last_stats_update_ > 10000) { // Every 10 seconds
        updateStatistics();
        performHealthCheck();
        last_stats_update_ = now;
    }
}

bool FFTProcessor::setNumHighest(int value) {
    if (value <= 0 || value > 20) {
        LOG_PROCESS_W("Invalid num_highest value: %d", value);
        return false;
    }
    
    if (xSemaphoreTake(config_mutex_, pdMS_TO_TICKS(50))) {
        num_highest_ = value;
        xSemaphoreGive(config_mutex_);
        LOG_PROCESS_D("Set num_highest to %d", value);
        return true;
    }
    
    LOG_PROCESS_W("Failed to acquire mutex for num_highest update");
    return false;
}

bool FFTProcessor::setMinWidth(int value) {
    if (value < 0 || value > 50) {
        LOG_PROCESS_W("Invalid min_width value: %d", value);
        return false;
    }
    
    if (xSemaphoreTake(config_mutex_, pdMS_TO_TICKS(50))) {
        min_width_ = value;
        xSemaphoreGive(config_mutex_);
        LOG_PROCESS_D("Set min_width to %d", value);
        return true;
    }
    
    LOG_PROCESS_W("Failed to acquire mutex for min_width update");
    return false;
}

int FFTProcessor::getNumHighest() const {
    int value = 6; // default
    if (xSemaphoreTake(config_mutex_, pdMS_TO_TICKS(50))) {
        value = num_highest_;
        xSemaphoreGive(config_mutex_);
    }
    return value;
}

int FFTProcessor::getMinWidth() const {
    int value = 10; // default
    if (xSemaphoreTake(config_mutex_, pdMS_TO_TICKS(50))) {
        value = min_width_;
        xSemaphoreGive(config_mutex_);
    }
    return value;
}

bool FFTProcessor::startProcessing() {
    if (fft_state_ != FFT_STATE_RUNNING) {
        LOG_PROCESS_E("Cannot start processing - FFT processor not running");
        return false;
    }
    
    if (fft_task_handle_ != nullptr) {
        LOG_PROCESS_W("FFT processing task already running");
        return true;
    }
    
    LOG_PROCESS_I("Starting FFT processing task");
    
    const ConfigManager::ProcessingConfig& config = CONFIG_MGR.getProcessingConfig();
    BaseType_t result = xTaskCreate(
        fftTaskWrapper,
        "fft_task",
        config.fft_task_stack_size,
        this,
        config.fft_task_priority,
        &fft_task_handle_
    );
    
    if (result != pdPASS) {
        LOG_PROCESS_E("Failed to create FFT processing task");
        return false;
    }
    
    processing_start_time_ = millis();
    LOG_PROCESS_I("FFT processing task started successfully");
    return true;
}

bool FFTProcessor::stopProcessing() {
    if (fft_task_handle_ == nullptr) {
        return true;
    }
    
    LOG_PROCESS_I("Stopping FFT processing task");
    
    // Delete the task
    vTaskDelete(fft_task_handle_);
    fft_task_handle_ = nullptr;
    
    LOG_PROCESS_I("FFT processing task stopped");
    return true;
}

bool FFTProcessor::isProcessing() const {
    return fft_task_handle_ != nullptr;
}

float FFTProcessor::getProcessingRate() const {
    if (processing_start_time_ == 0 || processed_frames_ == 0) {
        return 0.0f;
    }
    
    uint32_t elapsed_ms = millis() - processing_start_time_;
    if (elapsed_ms == 0) {
        return 0.0f;
    }
    
    return (float)processed_frames_ * 1000.0f / elapsed_ms;
}

size_t FFTProcessor::getMemoryUsage() const {
    return total_memory_allocated_;
}

bool FFTProcessor::isMemoryHealthy() const {
    size_t free_heap = ESP.getFreeHeap();
    return free_heap > 20480; // At least 20KB free
}

void FFTProcessor::handleFFTError(const char* error, const char* context) {
    error_count_++;
    LOG_PROCESS_E("FFT error in %s: %s", context ? context : "unknown", error);
    
    // Report to system manager
    SYSTEM_MGR.handleCriticalError(error, context);
}

void FFTProcessor::setState(fft_state_t state) {
    if (fft_state_ != state) {
        LOG_PROCESS_D("FFT state change: %d -> %d", fft_state_, state);
        fft_state_ = state;
        
        // Map FFT state to subsystem status
        subsystem_status_t subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED;
        switch (state) {
            case FFT_STATE_UNINITIALIZED: subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED; break;
            case FFT_STATE_INITIALIZING: subsystem_status = SUBSYSTEM_STATUS_INITIALIZING; break;
            case FFT_STATE_RUNNING: subsystem_status = SUBSYSTEM_STATUS_RUNNING; break;
            case FFT_STATE_ERROR: subsystem_status = SUBSYSTEM_STATUS_ERROR; break;
            case FFT_STATE_SHUTDOWN: subsystem_status = SUBSYSTEM_STATUS_SHUTDOWN; break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, subsystem_status);
    }
}

bool FFTProcessor::allocateBuffers() {
    LOG_PROCESS_D("Allocating FFT buffers in PSRAM");
    
    // Calculate buffer sizes
    size_t samples_size = SAMPLES * 2 * sizeof(uint32_t);  // Stereo samples
    size_t fft_input_size = SAMPLES * 2 * sizeof(float);   // Interleaved real/imag
    size_t fft_output_size = SAMPLES * sizeof(float);      // Magnitude output
    size_t temp_size = SAMPLES * sizeof(float);            // Temporary buffer
    
    total_memory_allocated_ = samples_size + fft_input_size + fft_output_size + temp_size;
    
    LOG_PROCESS_D("Buffer sizes: samples=%u, input=%u, output=%u, temp=%u bytes (total=%u)", 
                 samples_size, fft_input_size, fft_output_size, temp_size, total_memory_allocated_);
    
    // Allocate samples buffer
    samples_buffer_ = (uint32_t*)malloc_psram_fallback(samples_size);
    if (!samples_buffer_) {
        LOG_PROCESS_E("Failed to allocate samples buffer (%u bytes)", samples_size);
        return false;
    }
    
    // Allocate FFT input buffer
    fft_input_ = (float*)malloc_psram_fallback(fft_input_size);
    if (!fft_input_) {
        LOG_PROCESS_E("Failed to allocate FFT input buffer (%u bytes)", fft_input_size);
        deallocateBuffers();
        return false;
    }
    
    // Allocate FFT output buffer
    fft_output_ = (float*)malloc_psram_fallback(fft_output_size);
    if (!fft_output_) {
        LOG_PROCESS_E("Failed to allocate FFT output buffer (%u bytes)", fft_output_size);
        deallocateBuffers();
        return false;
    }
    
    // Allocate temporary buffer
    temp_buffer_ = (float*)malloc_psram_fallback(temp_size);
    if (!temp_buffer_) {
        LOG_PROCESS_E("Failed to allocate temporary buffer (%u bytes)", temp_size);
        deallocateBuffers();
        return false;
    }
    
    // Clear all buffers
    memset(samples_buffer_, 0, samples_size);
    memset(fft_input_, 0, fft_input_size);
    memset(fft_output_, 0, fft_output_size);
    memset(temp_buffer_, 0, temp_size);
    
    LOG_PROCESS_I("FFT buffers allocated successfully in PSRAM (total: %u bytes)", total_memory_allocated_);
    return true;
}

void FFTProcessor::deallocateBuffers() {
    LOG_PROCESS_D("Deallocating FFT buffers");
    
    if (samples_buffer_) {
        free_psram_fallback(samples_buffer_);
        samples_buffer_ = nullptr;
    }
    
    if (fft_input_) {
        free_psram_fallback(fft_input_);
        fft_input_ = nullptr;
    }
    
    if (fft_output_) {
        free_psram_fallback(fft_output_);
        fft_output_ = nullptr;
    }
    
    if (temp_buffer_) {
        free_psram_fallback(temp_buffer_);
        temp_buffer_ = nullptr;
    }
    
    total_memory_allocated_ = 0;
    LOG_PROCESS_D("FFT buffers deallocated");
}

bool FFTProcessor::initializeESPDSP() {
    LOG_PROCESS_D("Initializing ESP-DSP for FFT");
    
    esp_err_t ret = dsps_fft2r_init_fc32(nullptr, SAMPLES);
    if (ret != ESP_OK) {
        LOG_PROCESS_E("ESP-DSP FFT initialization failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    LOG_PROCESS_I("ESP-DSP FFT initialized successfully for %d samples", SAMPLES);
    return true;
}

void FFTProcessor::deinitializeESPDSP() {
    LOG_PROCESS_D("Deinitializing ESP-DSP");
    // ESP-DSP doesn't require explicit deinitialization
}

void FFTProcessor::fftTaskWrapper(void* parameter) {
    FFTProcessor* processor = static_cast<FFTProcessor*>(parameter);
    processor->fftTask();
}

void FFTProcessor::fftTask() {
    LOG_PROCESS_I("FFT processing task started");
    
    const ConfigManager::ProcessingConfig& config = CONFIG_MGR.getProcessingConfig();
    uint32_t update_interval_ms = config.fft_update_interval_ms;
    
    // Processing statistics
    uint32_t successful_ffts = 0;
    uint32_t last_memory_check = 0;
    uint32_t initial_heap = ESP.getFreeHeap();
    
    while (true) {
        // Read audio samples from Bluetooth audio
        if (readBtSamples(samples_buffer_, SAMPLES * 2)) {
            successful_ffts++;
            processed_frames_++;
            
            // Log first few successful FFTs
            if (successful_ffts <= 3) {
                LOG_PROCESS_I("FFT %u: Processing samples", successful_ffts);
            }
            
            try {
                // Convert stereo to mono
                convertToMono();
                
                // Perform FFT
                performFFT();
                
                // Extract and process peaks
                fft_peak_t peaks[MAX_BIN - 1]; // Exclude DC bin
                int peak_count = 0;
                extractPeaks(peaks, peak_count);
                
                if (peak_count > 0) {
                    // Normalize magnitudes
                    normalizeMagnitudes(peaks, peak_count);
                    
                    // Sort peaks by frequency index
                    sortPeaksByIndex(peaks, peak_count);
                    
                    // Send to network (if available)
                    if (NETWORK_MGR.isNetworkAvailable()) {
                        // Create binary data for WebSocket transmission
                        size_t data_size = 1 + (peak_count * 2); // 1 byte count + 2 bytes per peak
                        uint8_t* binary_data = (uint8_t*)malloc(data_size);
                        if (binary_data) {
                            binary_data[0] = (uint8_t)peak_count;
                            for (int i = 0; i < peak_count; i++) {
                                binary_data[1 + i * 2] = (uint8_t)peaks[i].index;
                                binary_data[2 + i * 2] = (uint8_t)(peaks[i].magnitude * 255.0f);
                            }
                            
                            NETWORK_MGR.sendWebSocketBinary(binary_data, data_size);
                            free(binary_data);
                        }
                    }
                }
                
            } catch (const std::exception& e) {
                handleFFTError(e.what(), "fft_processing");
            }
            
        } else {
            // No samples available - log occasionally
            if (successful_ffts == 0 && (millis() % 5000 < update_interval_ms)) {
                LOG_PROCESS_W("No audio samples available for FFT processing");
            }
        }
        
        // Memory monitoring every 5 seconds
        uint32_t current_time = millis();
        if (current_time - last_memory_check >= 5000) {
            uint32_t current_heap = ESP.getFreeHeap();
            int32_t heap_change = (int32_t)current_heap - (int32_t)initial_heap;
            
            LOG_PROCESS_D("Memory: Heap=%u, Change=%d, FFTs=%u", 
                         current_heap, heap_change, successful_ffts);
            
            last_memory_check = current_time;
            
            // Memory leak warning
            if (heap_change < -10000) {
                LOG_PROCESS_W("Potential memory leak: Heap dropped %d bytes", -heap_change);
            }
        }
        
        // Task delay
        vTaskDelay(pdMS_TO_TICKS(update_interval_ms));
    }
}

void FFTProcessor::convertToMono() {
    for (int i = 0; i < SAMPLES; i++) {
        int32_t left = (int32_t)samples_buffer_[i * 2];
        int32_t right = (int32_t)samples_buffer_[i * 2 + 1];
        float left_f = left / 2147483648.0f;
        float right_f = right / 2147483648.0f;
        
        // ESP-DSP uses interleaved format: real, imag, real, imag...
        fft_input_[i * 2] = (left_f + right_f) * 0.5f;  // Real part
        fft_input_[i * 2 + 1] = 0.0f;                   // Imaginary part
    }
}

void FFTProcessor::performFFT() {
    // Perform FFT using ESP-DSP
    dsps_fft2r_fc32(fft_input_, SAMPLES);
    dsps_bit_rev_fc32(fft_input_, SAMPLES);
    
    // Convert complex to magnitude
    for (int i = 0; i < SAMPLES / 2; i++) {
        float real = fft_input_[i * 2];
        float imag = fft_input_[i * 2 + 1];
        fft_output_[i] = sqrtf(real * real + imag * imag);
    }
}

void FFTProcessor::extractPeaks(fft_peak_t* peaks, int& peak_count) {
    // Get current configuration
    int current_num_highest = getNumHighest();
    int current_min_width = getMinWidth();
    
    // Create temporary peak array (exclude DC bin 0)
    fft_peak_t temp_peaks[MAX_BIN - 1];
    for (int i = 1; i < MAX_BIN; i++) {
        temp_peaks[i - 1].index = i;
        temp_peaks[i - 1].magnitude = fft_output_[i];
    }
    
    // Sort by magnitude (descending)
    std::sort(temp_peaks, temp_peaks + (MAX_BIN - 1), [](const fft_peak_t& a, const fft_peak_t& b) {
        return a.magnitude > b.magnitude;
    });
    
    // Select peaks with minimum width separation
    peak_count = 0;
    for (int i = 0; i < (MAX_BIN - 1) && peak_count < current_num_highest; i++) {
        int candidate = temp_peaks[i].index;
        bool too_close = false;
        
        for (int j = 0; j < peak_count; j++) {
            if (abs(peaks[j].index - candidate) < current_min_width) {
                too_close = true;
                break;
            }
        }
        
        if (!too_close) {
            peaks[peak_count++] = temp_peaks[i];
        }
    }
}

void FFTProcessor::normalizeMagnitudes(fft_peak_t* peaks, int count) {
    if (count == 0) return;
    
    // Find maximum magnitude in current frame
    float max_mag = peaks[0].magnitude;
    for (int i = 1; i < count; i++) {
        if (peaks[i].magnitude > max_mag) {
            max_mag = peaks[i].magnitude;
        }
    }
    
    // Update smoothed maximum magnitude
    if (max_mag > smoothed_max_magnitude_) {
        smoothed_max_magnitude_ = max_mag;
    } else {
        smoothed_max_magnitude_ = ALPHA * smoothed_max_magnitude_ + (1.0f - ALPHA) * max_mag;
    }
    
    // Calculate gain with limits
    const float MIN_GAIN = 0.1f;
    const float MAX_GAIN = 10.0f;
    float gain = 1.0f / (smoothed_max_magnitude_ + 1e-9f);
    gain = std::min(std::max(gain, MIN_GAIN), MAX_GAIN);
    
    // Apply gain and clamp to [0, 1]
    for (int i = 0; i < count; i++) {
        float scaled = peaks[i].magnitude * gain;
        peaks[i].magnitude = std::min(std::max(scaled, 0.0f), 1.0f);
    }
}

void FFTProcessor::sortPeaksByIndex(fft_peak_t* peaks, int count) {
    std::sort(peaks, peaks + count, [](const fft_peak_t& a, const fft_peak_t& b) {
        return a.index < b.index;
    });
}

bool FFTProcessor::performHealthCheck() {
    bool healthy = true;
    
    // Check memory usage
    if (!isMemoryHealthy()) {
        LOG_PROCESS_W("Health check: Low memory");
        healthy = false;
    }
    
    // Check processing rate
    float rate = getProcessingRate();
    if (rate < 10.0f && processed_frames_ > 100) { // Less than 10 FPS after 100 frames
        LOG_PROCESS_W("Health check: Low processing rate: %.1f FPS", rate);
        healthy = false;
    }
    
    // Check error rate
    if (error_count_ > 0 && processed_frames_ > 0) {
        float error_rate = (float)error_count_ / processed_frames_ * 100.0f;
        if (error_rate > 5.0f) { // More than 5% error rate
            LOG_PROCESS_W("Health check: High error rate: %.1f%%", error_rate);
            healthy = false;
        }
    }
    
    if (healthy) {
        LOG_PROCESS_D("FFT health check passed");
    } else {
        LOG_PROCESS_W("FFT health check failed");
    }
    
    return healthy;
}

void FFTProcessor::logProcessingStats() {
    LOG_PROCESS_I("=== FFT Processing Stats ===");
    LOG_PROCESS_I("State: %d", fft_state_);
    LOG_PROCESS_I("Processed Frames: %u", processed_frames_);
    LOG_PROCESS_I("Processing Rate: %.1f FPS", getProcessingRate());
    LOG_PROCESS_I("Error Count: %u", error_count_);
    LOG_PROCESS_I("Memory Usage: %u bytes", total_memory_allocated_);
    LOG_PROCESS_I("Free Heap: %u bytes", ESP.getFreeHeap());
    LOG_PROCESS_I("Config: num_highest=%d, min_width=%d", getNumHighest(), getMinWidth());
    LOG_PROCESS_I("============================");
}

void FFTProcessor::updateStatistics() {
    logProcessingStats();
}

void FFTProcessor::onConfigChange(config_category_t category, void* user_data) {
    if (category != CONFIG_CATEGORY_PROCESSING || !user_data) {
        return;
    }
    
    FFTProcessor* processor = static_cast<FFTProcessor*>(user_data);
    LOG_PROCESS_I("Processing configuration changed, updating FFT processor");
    
    // Get updated configuration
    const ConfigManager::ProcessingConfig& config = CONFIG_MGR.getProcessingConfig();
    
    // Update configuration
    processor->setNumHighest(config.fft_num_highest);
    processor->setMinWidth(config.fft_min_width);
}
