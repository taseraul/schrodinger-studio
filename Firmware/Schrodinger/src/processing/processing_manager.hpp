#ifndef PROCESSING_MANAGER_HPP
#define PROCESSING_MANAGER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "fft_processor.hpp"

// Processing subsystem states
typedef enum {
    PROCESSING_STATE_UNINITIALIZED = 0,
    PROCESSING_STATE_INITIALIZING,
    PROCESSING_STATE_READY,
    PROCESSING_STATE_RUNNING,
    PROCESSING_STATE_ERROR,
    PROCESSING_STATE_SHUTDOWN
} processing_state_t;

// Processing manager class
class ProcessingManager {
public:
    static ProcessingManager& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // State management
    processing_state_t getState() const { return processing_state_; }
    
    // Component access
    FFTProcessor& getFFTProcessor() { return FFT_PROCESSOR; }
    
    // Processing control
    bool startProcessing();
    bool stopProcessing();
    bool isProcessing() const;
    
    // Configuration management
    bool updateProcessingConfig(const ConfigManager::ProcessingConfig& config);
    
    // Statistics and monitoring
    uint32_t getTotalProcessedFrames() const;
    uint32_t getProcessingErrors() const { return error_count_; }
    float getOverallProcessingRate() const;
    
    // Memory management
    size_t getTotalMemoryUsage() const;
    bool isMemoryHealthy() const;
    
    // Error handling
    void handleProcessingError(const char* error, const char* context);
    
private:
    ProcessingManager() = default;
    ~ProcessingManager() = default;
    ProcessingManager(const ProcessingManager&) = delete;
    ProcessingManager& operator=(const ProcessingManager&) = delete;
    
    // Internal state
    processing_state_t processing_state_ = PROCESSING_STATE_UNINITIALIZED;
    
    // Statistics
    uint32_t error_count_ = 0;
    uint32_t last_health_check_ = 0;
    
    // Configuration change callback
    static void onConfigChange(config_category_t category, void* user_data);
    
    // Internal methods
    void setState(processing_state_t state);
    bool initializeComponents();
    bool performHealthCheck();
    void logProcessingStatus();
};

// Global processing manager access
#define PROCESSING_MGR ProcessingManager::getInstance()

#endif // PROCESSING_MANAGER_HPP
