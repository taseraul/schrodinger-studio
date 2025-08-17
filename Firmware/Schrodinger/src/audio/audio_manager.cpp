#include "audio_manager.hpp"
#include "bt_audio.hpp"
#include "i2s_driver.hpp"

AudioManager& AudioManager::getInstance() {
    static AudioManager instance;
    return instance;
}

bool AudioManager::initialize() {
    LOG_AUDIO_I("Audio Manager initialization starting...");
    setState(AUDIO_STATE_INITIALIZING);
    
    // Register for configuration changes
    CONFIG_MGR.registerChangeCallback(CONFIG_CATEGORY_AUDIO, onConfigChange, this);
    
    // Initialize Bluetooth audio by default
    if (!initializeBluetoothAudio()) {
        LOG_AUDIO_E("Failed to initialize Bluetooth audio");
        setState(AUDIO_STATE_ERROR);
        return false;
    }
    
    // Reset statistics
    total_samples_processed_ = 0;
    last_activity_time_ = millis();
    error_count_ = 0;
    
    setState(AUDIO_STATE_READY);
    LOG_AUDIO_I("Audio Manager initialized successfully");
    
    return true;
}

void AudioManager::shutdown() {
    LOG_AUDIO_I("Audio Manager shutdown initiated");
    setState(AUDIO_STATE_SHUTDOWN);
    
    // Disable all audio sources
    disableBluetoothAudio();
    disableI2SAudio();
    
    // Unregister configuration callback
    CONFIG_MGR.unregisterChangeCallback(CONFIG_CATEGORY_AUDIO);
    
    setActiveSource(AUDIO_SOURCE_NONE);
    LOG_AUDIO_I("Audio Manager shutdown completed");
}

void AudioManager::update() {
    if (audio_state_ != AUDIO_STATE_READY && audio_state_ != AUDIO_STATE_STREAMING) {
        return;
    }
    
    // Perform periodic health check
    static uint32_t last_health_check = 0;
    uint32_t current_time = millis();
    
    if (current_time - last_health_check >= 10000) { // Every 10 seconds
        if (!performHealthCheck()) {
            LOG_AUDIO_W("Audio health check failed");
            error_count_++;
        }
        last_health_check = current_time;
    }
    
    // Update statistics
    updateStatistics();
}

bool AudioManager::enableBluetoothAudio() {
    LOG_AUDIO_I("Enabling Bluetooth audio...");
    
    if (!initializeBluetoothAudio()) {
        LOG_AUDIO_E("Failed to enable Bluetooth audio");
        return false;
    }
    
    setActiveSource(AUDIO_SOURCE_BLUETOOTH);
    LOG_AUDIO_I("Bluetooth audio enabled successfully");
    return true;
}

bool AudioManager::disableBluetoothAudio() {
    LOG_AUDIO_I("Disabling Bluetooth audio...");
    
    if (active_source_ == AUDIO_SOURCE_BLUETOOTH) {
        setActiveSource(AUDIO_SOURCE_NONE);
    }
    
    bt_deinit();
    LOG_AUDIO_I("Bluetooth audio disabled");
    return true;
}

bool AudioManager::enableI2SAudio() {
    LOG_AUDIO_I("Enabling I2S audio...");
    
    if (!initializeI2SAudio()) {
        LOG_AUDIO_E("Failed to enable I2S audio");
        return false;
    }
    
    setActiveSource(AUDIO_SOURCE_I2S);
    LOG_AUDIO_I("I2S audio enabled successfully");
    return true;
}

bool AudioManager::disableI2SAudio() {
    LOG_AUDIO_I("Disabling I2S audio...");
    
    if (active_source_ == AUDIO_SOURCE_I2S) {
        setActiveSource(AUDIO_SOURCE_NONE);
    }
    
    // TODO: Add I2S deinit when i2s_driver is refactored
    LOG_AUDIO_I("I2S audio disabled");
    return true;
}

bool AudioManager::readAudioSamples(uint32_t* dest, size_t length) {
    if (audio_state_ != AUDIO_STATE_READY && audio_state_ != AUDIO_STATE_STREAMING) {
        return false;
    }
    
    bool success = false;
    
    switch (active_source_) {
        case AUDIO_SOURCE_BLUETOOTH:
            success = readBtSamples(dest, length);
            break;
            
        case AUDIO_SOURCE_I2S:
            // TODO: Implement I2S sample reading when i2s_driver is refactored
            success = false;
            break;
            
        default:
            success = false;
            break;
    }
    
    if (success) {
        const auto& config = CONFIG_MGR.getAudioConfig();
        total_samples_processed_ += config.samples;
        last_activity_time_ = millis();
        
        // Update state to streaming if we were just ready
        if (audio_state_ == AUDIO_STATE_READY) {
            setState(AUDIO_STATE_STREAMING);
        }
    }
    
    return success;
}

size_t AudioManager::getAvailableSamples() const {
    // TODO: Implement when circular buffer interface is standardized
    return 0;
}

bool AudioManager::updateAudioConfig(const ConfigManager::AudioConfig& config) {
    LOG_AUDIO_I("Updating audio configuration");
    
    // Update configuration through config manager
    if (!CONFIG_MGR.updateAudioConfig(config)) {
        LOG_AUDIO_E("Failed to update audio configuration");
        return false;
    }
    
    LOG_AUDIO_I("Audio configuration updated successfully");
    return true;
}

bool AudioManager::isAudioActive() const {
    return (audio_state_ == AUDIO_STATE_STREAMING) && 
           (millis() - last_activity_time_ < 5000); // Active within last 5 seconds
}

float AudioManager::getBufferUtilization() const {
    // TODO: Implement when circular buffer interface is standardized
    return 0.0f;
}

uint32_t AudioManager::getTotalSamplesProcessed() const {
    return total_samples_processed_;
}

void AudioManager::handleAudioError(const char* error, const char* context) {
    error_count_++;
    log_error_with_context(LOG_TAG_AUDIO, error, context);
    
    // Set error state if too many errors
    if (error_count_ > 10) {
        setState(AUDIO_STATE_ERROR);
        SYSTEM_MGR.handleCriticalError("Too many audio errors", "Audio Manager");
    }
}

void AudioManager::onConfigChange(config_category_t category, void* user_data) {
    if (category != CONFIG_CATEGORY_AUDIO || !user_data) {
        return;
    }
    
    AudioManager* manager = static_cast<AudioManager*>(user_data);
    LOG_AUDIO_I("Audio configuration changed, applying updates...");
    
    // TODO: Apply configuration changes to active audio sources
    // This might require reinitializing some components
}

bool AudioManager::initializeBluetoothAudio() {
    LOG_AUDIO_D("Initializing Bluetooth audio subsystem");
    
    // Initialize Bluetooth A2DP
    bt_init();
    
    // Check if initialization was successful by checking subsystem status
    if (SYSTEM_MGR.getSubsystemStatus(SUBSYSTEM_AUDIO) == SUBSYSTEM_STATUS_ERROR) {
        LOG_AUDIO_E("Bluetooth audio initialization failed");
        return false;
    }
    
    LOG_AUDIO_D("Bluetooth audio subsystem initialized");
    return true;
}

bool AudioManager::initializeI2SAudio() {
    LOG_AUDIO_D("Initializing I2S audio subsystem");
    
    // TODO: Initialize I2S when i2s_driver is refactored
    LOG_AUDIO_W("I2S audio initialization not yet implemented");
    return false;
}

void AudioManager::setState(audio_state_t state) {
    if (audio_state_ != state) {
        const char* state_names[] = {
            "UNINITIALIZED", "INITIALIZING", "READY", "STREAMING", "ERROR", "SHUTDOWN"
        };
        
        LOG_AUDIO_I("Audio state: %s -> %s", 
                    state_names[audio_state_], state_names[state]);
        audio_state_ = state;
        
        // Update system manager with audio subsystem status
        subsystem_status_t sys_status;
        switch (state) {
            case AUDIO_STATE_UNINITIALIZED:
                sys_status = SUBSYSTEM_STATUS_UNINITIALIZED;
                break;
            case AUDIO_STATE_INITIALIZING:
                sys_status = SUBSYSTEM_STATUS_INITIALIZING;
                break;
            case AUDIO_STATE_READY:
            case AUDIO_STATE_STREAMING:
                sys_status = SUBSYSTEM_STATUS_RUNNING;
                break;
            case AUDIO_STATE_ERROR:
                sys_status = SUBSYSTEM_STATUS_ERROR;
                break;
            case AUDIO_STATE_SHUTDOWN:
                sys_status = SUBSYSTEM_STATUS_SHUTDOWN;
                break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, sys_status);
    }
}

void AudioManager::setActiveSource(audio_source_t source) {
    if (active_source_ != source) {
        const char* source_names[] = {
            "NONE", "BLUETOOTH", "I2S"
        };
        
        LOG_AUDIO_I("Audio source: %s -> %s", 
                    source_names[active_source_], source_names[source]);
        active_source_ = source;
    }
}

void AudioManager::updateStatistics() {
    // Update activity state based on recent sample processing
    uint32_t current_time = millis();
    bool was_active = isAudioActive();
    bool is_active = (current_time - last_activity_time_ < 5000);
    
    // Log state changes
    if (was_active != is_active) {
        if (is_active) {
            LOG_AUDIO_I("Audio activity detected");
        } else {
            LOG_AUDIO_I("Audio activity stopped");
            // Return to ready state if we were streaming
            if (audio_state_ == AUDIO_STATE_STREAMING) {
                setState(AUDIO_STATE_READY);
            }
        }
    }
}

bool AudioManager::performHealthCheck() {
    // Check if we have an active source
    if (active_source_ == AUDIO_SOURCE_NONE) {
        LOG_AUDIO_W("No active audio source");
        return false;
    }
    
    // Check system memory health
    if (!SYSTEM_MGR.isMemoryHealthy()) {
        LOG_AUDIO_W("System memory unhealthy");
        return false;
    }
    
    // Check error count
    if (error_count_ > 5) {
        LOG_AUDIO_W("High error count: %d", error_count_);
        return false;
    }
    
    LOG_AUDIO_D("Audio health check passed");
    return true;
}
