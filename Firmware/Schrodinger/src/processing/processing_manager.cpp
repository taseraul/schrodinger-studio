#include "processing_manager.hpp"

// Static instance
ProcessingManager& ProcessingManager::getInstance() {
    static ProcessingManager instance;
    return instance;
}

bool ProcessingManager::initialize() {
    LOG_PROCESS_I("Initializing processing manager");
    setState(PROCESSING_STATE_INITIALIZING);
    
    // Initialize components
    if (!initializeComponents()) {
        LOG_PROCESS_E("Failed to initialize processing components");
        setState(PROCESSING_STATE_ERROR);
        return false;
    }
    
    // Register for configuration changes
    CONFIG_MGR.registerChangeCallback(CONFIG_CATEGORY_PROCESSING, onConfigChange, this);
    
    // Register with system manager
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, SUBSYSTEM_STATUS_READY);
    
    setState(PROCESSING_STATE_READY);
    LOG_PROCESS_I("Processing manager initialized successfully");
    return true;
}

void ProcessingManager::shutdown() {
    LOG_PROCESS_I("Shutting down processing manager");
    setState(PROCESSING_STATE_SHUTDOWN);
    
    // Stop processing
    stopProcessing();
    
    // Shutdown components
    FFT_PROCESSOR.shutdown();
    
    // Unregister callbacks
    CONFIG_MGR.unregisterChangeCallback(CONFIG_CATEGORY_PROCESSING);
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, SUBSYSTEM_STATUS_SHUTDOWN);
    
    setState(PROCESSING_STATE_UNINITIALIZED);
    LOG_PROCESS_I("Processing manager shutdown complete");
}

void ProcessingManager::update() {
    if (processing_state_ != PROCESSING_STATE_RUNNING && processing_state_ != PROCESSING_STATE_READY) {
        return;
    }
    
    // Update components
    FFT_PROCESSOR.update();
    
    // Periodic health check
    uint32_t now = millis();
    if (now - last_health_check_ > 30000) { // Every 30 seconds
        performHealthCheck();
        last_health_check_ = now;
    }
}

bool ProcessingManager::startProcessing() {
    if (processing_state_ != PROCESSING_STATE_READY) {
        LOG_PROCESS_E("Cannot start processing - manager not ready");
        return false;
    }
    
    LOG_PROCESS_I("Starting processing subsystem");
    
    // Start FFT processor
    if (!FFT_PROCESSOR.startProcessing()) {
        LOG_PROCESS_E("Failed to start FFT processor");
        return false;
    }
    
    setState(PROCESSING_STATE_RUNNING);
    LOG_PROCESS_I("Processing subsystem started successfully");
    return true;
}

bool ProcessingManager::stopProcessing() {
    if (processing_state_ != PROCESSING_STATE_RUNNING) {
        return true;
    }
    
    LOG_PROCESS_I("Stopping processing subsystem");
    
    // Stop FFT processor
    FFT_PROCESSOR.stopProcessing();
    
    setState(PROCESSING_STATE_READY);
    LOG_PROCESS_I("Processing subsystem stopped");
    return true;
}

bool ProcessingManager::isProcessing() const {
    return processing_state_ == PROCESSING_STATE_RUNNING && FFT_PROCESSOR.isProcessing();
}

bool ProcessingManager::updateProcessingConfig(const ConfigManager::ProcessingConfig& config) {
    LOG_PROCESS_I("Updating processing configuration");
    
    // Update FFT processor configuration
    if (!FFT_PROCESSOR.setNumHighest(config.fft_num_highest)) {
        LOG_PROCESS_W("Failed to update FFT num_highest");
    }
    
    if (!FFT_PROCESSOR.setMinWidth(config.fft_min_width)) {
        LOG_PROCESS_W("Failed to update FFT min_width");
    }
    
    LOG_PROCESS_I("Processing configuration updated");
    return true;
}

uint32_t ProcessingManager::getTotalProcessedFrames() const {
    return FFT_PROCESSOR.getProcessedFrames();
}

float ProcessingManager::getOverallProcessingRate() const {
    return FFT_PROCESSOR.getProcessingRate();
}

size_t ProcessingManager::getTotalMemoryUsage() const {
    return FFT_PROCESSOR.getMemoryUsage();
}

bool ProcessingManager::isMemoryHealthy() const {
    return FFT_PROCESSOR.isMemoryHealthy();
}

void ProcessingManager::handleProcessingError(const char* error, const char* context) {
    error_count_++;
    LOG_PROCESS_E("Processing error in %s: %s", context ? context : "unknown", error);
    
    // Report to system manager
    SYSTEM_MGR.handleCriticalError(error, context);
}

void ProcessingManager::setState(processing_state_t state) {
    if (processing_state_ != state) {
        LOG_PROCESS_D("Processing state change: %d -> %d", processing_state_, state);
        processing_state_ = state;
        
        // Map processing state to subsystem status
        subsystem_status_t subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED;
        switch (state) {
            case PROCESSING_STATE_UNINITIALIZED: subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED; break;
            case PROCESSING_STATE_INITIALIZING: subsystem_status = SUBSYSTEM_STATUS_INITIALIZING; break;
            case PROCESSING_STATE_READY: subsystem_status = SUBSYSTEM_STATUS_READY; break;
            case PROCESSING_STATE_RUNNING: subsystem_status = SUBSYSTEM_STATUS_RUNNING; break;
            case PROCESSING_STATE_ERROR: subsystem_status = SUBSYSTEM_STATUS_ERROR; break;
            case PROCESSING_STATE_SHUTDOWN: subsystem_status = SUBSYSTEM_STATUS_SHUTDOWN; break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_PROCESSING, subsystem_status);
    }
}

bool ProcessingManager::initializeComponents() {
    LOG_PROCESS_D("Initializing processing components");
    
    // Initialize FFT processor
    if (!FFT_PROCESSOR.initialize()) {
        LOG_PROCESS_E("Failed to initialize FFT processor");
        return false;
    }
    
    LOG_PROCESS_D("Processing components initialized successfully");
    return true;
}

bool ProcessingManager::performHealthCheck() {
    bool healthy = true;
    
    // Check FFT processor health
    if (FFT_PROCESSOR.getState() != FFT_STATE_RUNNING && processing_state_ == PROCESSING_STATE_RUNNING) {
        LOG_PROCESS_W("Health check: FFT processor not running");
        healthy = false;
    }
    
    // Check memory usage
    if (!isMemoryHealthy()) {
        LOG_PROCESS_W("Health check: Processing memory unhealthy");
        healthy = false;
    }
    
    // Check processing rate
    float rate = getOverallProcessingRate();
    if (rate < 5.0f && getTotalProcessedFrames() > 50) { // Less than 5 FPS after 50 frames
        LOG_PROCESS_W("Health check: Low processing rate: %.1f FPS", rate);
        healthy = false;
    }
    
    // Check error rate
    uint32_t total_frames = getTotalProcessedFrames();
    if (error_count_ > 0 && total_frames > 0) {
        float error_rate = (float)error_count_ / total_frames * 100.0f;
        if (error_rate > 10.0f) { // More than 10% error rate
            LOG_PROCESS_W("Health check: High error rate: %.1f%%", error_rate);
            healthy = false;
        }
    }
    
    if (healthy) {
        LOG_PROCESS_D("Processing health check passed");
    } else {
        LOG_PROCESS_W("Processing health check failed");
    }
    
    return healthy;
}

void ProcessingManager::logProcessingStatus() {
    LOG_PROCESS_I("=== Processing Status ===");
    LOG_PROCESS_I("State: %d", processing_state_);
    LOG_PROCESS_I("FFT State: %d", FFT_PROCESSOR.getState());
    LOG_PROCESS_I("Processing: %s", isProcessing() ? "Yes" : "No");
    LOG_PROCESS_I("Total Frames: %u", getTotalProcessedFrames());
    LOG_PROCESS_I("Processing Rate: %.1f FPS", getOverallProcessingRate());
    LOG_PROCESS_I("Total Errors: %u", error_count_);
    LOG_PROCESS_I("Memory Usage: %u bytes", getTotalMemoryUsage());
    LOG_PROCESS_I("Free Heap: %u bytes", ESP.getFreeHeap());
    LOG_PROCESS_I("========================");
}

void ProcessingManager::onConfigChange(config_category_t category, void* user_data) {
    if (category != CONFIG_CATEGORY_PROCESSING || !user_data) {
        return;
    }
    
    ProcessingManager* manager = static_cast<ProcessingManager*>(user_data);
    LOG_PROCESS_I("Processing configuration changed, updating components");
    
    // Get updated configuration
    ConfigManager::ProcessingConfig config = CONFIG_MGR.getProcessingConfig();
    
    // Update processing configuration
    manager->updateProcessingConfig(config);
}
