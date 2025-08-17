#include "config_manager.hpp"

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::initialize() {
    LOG_CONFIG_I("Config Manager initialization starting...");
    
    // Initialize default configurations
    initializeDefaults();
    
    // Initialize change callbacks
    for (int i = 0; i < CONFIG_CATEGORY_COUNT; i++) {
        change_callbacks_[i].callback = nullptr;
        change_callbacks_[i].user_data = nullptr;
        change_callbacks_[i].active = false;
    }
    
    // Validate all configurations
    if (!validateAllConfigs()) {
        LOG_CONFIG_E("Configuration validation failed");
        return false;
    }
    
    LOG_CONFIG_I("Config Manager initialized successfully");
    return true;
}

void ConfigManager::shutdown() {
    LOG_CONFIG_I("Config Manager shutdown initiated");
    
    // Clear all change callbacks
    for (int i = 0; i < CONFIG_CATEGORY_COUNT; i++) {
        change_callbacks_[i].active = false;
        change_callbacks_[i].callback = nullptr;
        change_callbacks_[i].user_data = nullptr;
    }
    
    LOG_CONFIG_I("Config Manager shutdown completed");
}

bool ConfigManager::updateAudioConfig(const AudioConfig& config) {
    LOG_CONFIG_I("Updating audio configuration");
    
    // Store old config for rollback if needed
    AudioConfig old_config = audio_config_;
    audio_config_ = config;
    
    // Validate new configuration
    if (!validateAudioConfig()) {
        LOG_CONFIG_E("Audio configuration validation failed, rolling back");
        audio_config_ = old_config;
        return false;
    }
    
    // Notify change
    notifyConfigChange(CONFIG_CATEGORY_AUDIO);
    
    LOG_CONFIG_I("Audio configuration updated successfully");
    return true;
}

bool ConfigManager::updateNetworkConfig(const NetworkConfig& config) {
    LOG_CONFIG_I("Updating network configuration");
    
    // Store old config for rollback if needed
    NetworkConfig old_config = network_config_;
    network_config_ = config;
    
    // Validate new configuration
    if (!validateNetworkConfig()) {
        LOG_CONFIG_E("Network configuration validation failed, rolling back");
        network_config_ = old_config;
        return false;
    }
    
    // Notify change
    notifyConfigChange(CONFIG_CATEGORY_NETWORK);
    
    LOG_CONFIG_I("Network configuration updated successfully");
    return true;
}

bool ConfigManager::updateProcessingConfig(const ProcessingConfig& config) {
    LOG_CONFIG_I("Updating processing configuration");
    
    // Store old config for rollback if needed
    ProcessingConfig old_config = processing_config_;
    processing_config_ = config;
    
    // Validate new configuration
    if (!validateProcessingConfig()) {
        LOG_CONFIG_E("Processing configuration validation failed, rolling back");
        processing_config_ = old_config;
        return false;
    }
    
    // Notify change
    notifyConfigChange(CONFIG_CATEGORY_PROCESSING);
    
    LOG_CONFIG_I("Processing configuration updated successfully");
    return true;
}

bool ConfigManager::updateSystemConfig(const SystemConfig& config) {
    LOG_CONFIG_I("Updating system configuration");
    
    // Store old config for rollback if needed
    SystemConfig old_config = system_config_;
    system_config_ = config;
    
    // Validate new configuration
    if (!validateSystemConfig()) {
        LOG_CONFIG_E("System configuration validation failed, rolling back");
        system_config_ = old_config;
        return false;
    }
    
    // Notify change
    notifyConfigChange(CONFIG_CATEGORY_SYSTEM);
    
    LOG_CONFIG_I("System configuration updated successfully");
    return true;
}

bool ConfigManager::validateConfig(config_category_t category) const {
    switch (category) {
        case CONFIG_CATEGORY_SYSTEM:
            return validateSystemConfig();
        case CONFIG_CATEGORY_AUDIO:
            return validateAudioConfig();
        case CONFIG_CATEGORY_NETWORK:
            return validateNetworkConfig();
        case CONFIG_CATEGORY_PROCESSING:
            return validateProcessingConfig();
        default:
            LOG_CONFIG_E("Invalid configuration category: %d", category);
            return false;
    }
}

bool ConfigManager::validateAllConfigs() const {
    bool all_valid = true;
    
    for (int i = 0; i < CONFIG_CATEGORY_COUNT; i++) {
        if (!validateConfig((config_category_t)i)) {
            all_valid = false;
        }
    }
    
    if (all_valid) {
        LOG_CONFIG_I("All configurations validated successfully");
    } else {
        LOG_CONFIG_E("One or more configurations failed validation");
    }
    
    return all_valid;
}

bool ConfigManager::saveConfig(config_category_t category) {
    // TODO: Implement configuration persistence
    LOG_CONFIG_W("Configuration persistence not yet implemented");
    return false;
}

bool ConfigManager::loadConfig(config_category_t category) {
    // TODO: Implement configuration persistence
    LOG_CONFIG_W("Configuration persistence not yet implemented");
    return false;
}

bool ConfigManager::saveAllConfigs() {
    // TODO: Implement configuration persistence
    LOG_CONFIG_W("Configuration persistence not yet implemented");
    return false;
}

bool ConfigManager::loadAllConfigs() {
    // TODO: Implement configuration persistence
    LOG_CONFIG_W("Configuration persistence not yet implemented");
    return false;
}

bool ConfigManager::registerChangeCallback(config_category_t category, config_change_callback_t callback, void* user_data) {
    if (category >= CONFIG_CATEGORY_COUNT) {
        LOG_CONFIG_E("Invalid category %d for callback registration", category);
        return false;
    }
    
    if (change_callbacks_[category].active) {
        LOG_CONFIG_W("Overriding existing callback for category %d", category);
    }
    
    change_callbacks_[category].callback = callback;
    change_callbacks_[category].user_data = user_data;
    change_callbacks_[category].active = true;
    
    LOG_CONFIG_D("Registered change callback for category %d", category);
    return true;
}

void ConfigManager::unregisterChangeCallback(config_category_t category) {
    if (category >= CONFIG_CATEGORY_COUNT) {
        LOG_CONFIG_E("Invalid category %d for callback unregistration", category);
        return;
    }
    
    change_callbacks_[category].active = false;
    change_callbacks_[category].callback = nullptr;
    change_callbacks_[category].user_data = nullptr;
    
    LOG_CONFIG_D("Unregistered change callback for category %d", category);
}

void ConfigManager::initializeDefaults() {
    LOG_CONFIG_D("Initializing default configurations");
    
    // Audio config defaults are set in struct initialization
    // Network config defaults are set in struct initialization
    // Processing config defaults are set in struct initialization
    // System config defaults are set in struct initialization
    
    LOG_CONFIG_D("Default configurations initialized");
}

void ConfigManager::notifyConfigChange(config_category_t category) {
    if (category >= CONFIG_CATEGORY_COUNT) {
        return;
    }
    
    if (change_callbacks_[category].active && change_callbacks_[category].callback) {
        LOG_CONFIG_D("Notifying configuration change for category %d", category);
        change_callbacks_[category].callback(category, change_callbacks_[category].user_data);
    }
}

bool ConfigManager::validateAudioConfig() const {
    // Validate sample rate
    if (audio_config_.sample_rate < 8000 || audio_config_.sample_rate > 96000) {
        LOG_CONFIG_E("Invalid sample rate: %d (must be 8000-96000)", audio_config_.sample_rate);
        return false;
    }
    
    // Validate samples (must be power of 2)
    if (audio_config_.samples < 64 || audio_config_.samples > 4096 || 
        (audio_config_.samples & (audio_config_.samples - 1)) != 0) {
        LOG_CONFIG_E("Invalid samples: %d (must be power of 2, 64-4096)", audio_config_.samples);
        return false;
    }
    
    // Validate num_bands
    if (audio_config_.num_bands < 1 || audio_config_.num_bands > 32) {
        LOG_CONFIG_E("Invalid num_bands: %d (must be 1-32)", audio_config_.num_bands);
        return false;
    }
    
    // Validate decay
    if (audio_config_.decay < 1 || audio_config_.decay > 10000) {
        LOG_CONFIG_E("Invalid decay: %d (must be 1-10000)", audio_config_.decay);
        return false;
    }
    
    // Validate max_slope
    if (audio_config_.max_slope < 0.0f || audio_config_.max_slope > 1.0f) {
        LOG_CONFIG_E("Invalid max_slope: %f (must be 0.0-1.0)", audio_config_.max_slope);
        return false;
    }
    
    // Validate light_cutoff
    if (audio_config_.light_cutoff < 0.0f || audio_config_.light_cutoff > 1.0f) {
        LOG_CONFIG_E("Invalid light_cutoff: %f (must be 0.0-1.0)", audio_config_.light_cutoff);
        return false;
    }
    
    LOG_CONFIG_D("Audio configuration validation passed");
    return true;
}

bool ConfigManager::validateNetworkConfig() const {
    // Validate WiFi SSID
    if (!network_config_.wifi_ssid || strlen(network_config_.wifi_ssid) == 0) {
        LOG_CONFIG_E("WiFi SSID cannot be empty");
        return false;
    }
    
    if (strlen(network_config_.wifi_ssid) > 32) {
        LOG_CONFIG_E("WiFi SSID too long: %d chars (max 32)", strlen(network_config_.wifi_ssid));
        return false;
    }
    
    // Validate WiFi password
    if (!network_config_.wifi_password) {
        LOG_CONFIG_E("WiFi password cannot be null");
        return false;
    }
    
    if (strlen(network_config_.wifi_password) > 64) {
        LOG_CONFIG_E("WiFi password too long: %d chars (max 64)", strlen(network_config_.wifi_password));
        return false;
    }
    
    // Validate AP SSID
    if (!network_config_.ap_ssid || strlen(network_config_.ap_ssid) == 0) {
        LOG_CONFIG_E("AP SSID cannot be empty");
        return false;
    }
    
    // Validate BT device name
    if (!network_config_.bt_device_name || strlen(network_config_.bt_device_name) == 0) {
        LOG_CONFIG_E("BT device name cannot be empty");
        return false;
    }
    
    // Validate HTTP port
    if (network_config_.http_port < 1 || network_config_.http_port > 65535) {
        LOG_CONFIG_E("Invalid HTTP port: %d (must be 1-65535)", network_config_.http_port);
        return false;
    }
    
    // Validate WebSocket max clients
    if (network_config_.websocket_max_clients < 1 || network_config_.websocket_max_clients > 32) {
        LOG_CONFIG_E("Invalid WebSocket max clients: %d (must be 1-32)", network_config_.websocket_max_clients);
        return false;
    }
    
    LOG_CONFIG_D("Network configuration validation passed");
    return true;
}

bool ConfigManager::validateProcessingConfig() const {
    // Validate FFT samples (must be power of 2)
    if (processing_config_.fft_samples < 64 || processing_config_.fft_samples > 4096 ||
        (processing_config_.fft_samples & (processing_config_.fft_samples - 1)) != 0) {
        LOG_CONFIG_E("Invalid FFT samples: %d (must be power of 2, 64-4096)", processing_config_.fft_samples);
        return false;
    }
    
    // Validate FFT num_highest
    if (processing_config_.fft_num_highest < 1 || processing_config_.fft_num_highest > 32) {
        LOG_CONFIG_E("Invalid FFT num_highest: %d (must be 1-32)", processing_config_.fft_num_highest);
        return false;
    }
    
    // Validate FFT min_width
    if (processing_config_.fft_min_width > 100) {
        LOG_CONFIG_E("Invalid FFT min_width: %d (must be <= 100)", processing_config_.fft_min_width);
        return false;
    }
    
    // Validate task stack size
    if (processing_config_.fft_task_stack_size < 2048 || processing_config_.fft_task_stack_size > 32768) {
        LOG_CONFIG_E("Invalid FFT task stack size: %d (must be 2048-32768)", processing_config_.fft_task_stack_size);
        return false;
    }
    
    // Validate task priority
    if (processing_config_.fft_task_priority < 1 || processing_config_.fft_task_priority > 25) {
        LOG_CONFIG_E("Invalid FFT task priority: %d (must be 1-25)", processing_config_.fft_task_priority);
        return false;
    }
    
    // Validate update interval
    if (processing_config_.fft_update_interval_ms < 1 || processing_config_.fft_update_interval_ms > 1000) {
        LOG_CONFIG_E("Invalid FFT update interval: %d ms (must be 1-1000)", processing_config_.fft_update_interval_ms);
        return false;
    }
    
    LOG_CONFIG_D("Processing configuration validation passed");
    return true;
}

bool ConfigManager::validateSystemConfig() const {
    // Validate memory check interval
    if (system_config_.memory_check_interval_ms < 1000 || system_config_.memory_check_interval_ms > 60000) {
        LOG_CONFIG_E("Invalid memory check interval: %d ms (must be 1000-60000)", system_config_.memory_check_interval_ms);
        return false;
    }
    
    // Validate health check interval
    if (system_config_.health_check_interval_ms < 1000 || system_config_.health_check_interval_ms > 300000) {
        LOG_CONFIG_E("Invalid health check interval: %d ms (must be 1000-300000)", system_config_.health_check_interval_ms);
        return false;
    }
    
    // Validate heap thresholds
    if (system_config_.critical_heap_threshold < 5000 || system_config_.critical_heap_threshold > 100000) {
        LOG_CONFIG_E("Invalid critical heap threshold: %d (must be 5000-100000)", system_config_.critical_heap_threshold);
        return false;
    }
    
    if (system_config_.low_heap_threshold <= system_config_.critical_heap_threshold || 
        system_config_.low_heap_threshold > 200000) {
        LOG_CONFIG_E("Invalid low heap threshold: %d (must be > critical and <= 200000)", system_config_.low_heap_threshold);
        return false;
    }
    
    // Validate log level
    if (system_config_.default_log_level < LOG_LEVEL_NONE || system_config_.default_log_level > LOG_LEVEL_VERBOSE) {
        LOG_CONFIG_E("Invalid default log level: %d", system_config_.default_log_level);
        return false;
    }
    
    LOG_CONFIG_D("System configuration validation passed");
    return true;
}
