#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include "logger.hpp"

// Configuration constants (moved from old config.hpp)
#define SAMPLE_RATE 44100
#define SAMPLES 1024
#define NUM_BANDS 16
#define DECAY 2
#define FALLBACK_ATTENUATION 0.5
#define MAX_SLOPE 0.8f
#define LIGHT_CUTOFF 0.1f

// Configuration categories
typedef enum {
    CONFIG_CATEGORY_SYSTEM = 0,
    CONFIG_CATEGORY_AUDIO,
    CONFIG_CATEGORY_NETWORK,
    CONFIG_CATEGORY_PROCESSING,
    CONFIG_CATEGORY_COUNT
} config_category_t;

// Configuration manager class
class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Audio configuration
    struct AudioConfig {
        uint32_t sample_rate = SAMPLE_RATE;
        uint16_t samples = SAMPLES;
        uint8_t num_bands = NUM_BANDS;
        uint16_t decay = DECAY;
        double fallback_attenuation = FALLBACK_ATTENUATION;
        float max_slope = MAX_SLOPE;
        float light_cutoff = LIGHT_CUTOFF;
    };
    
    // Network configuration
    struct NetworkConfig {
        const char* wifi_ssid = "Raul";
        const char* wifi_password = "armaghedon";
        const char* ap_ssid = "Schrodinger";
        const char* bt_device_name = "Schrodinger2";
        uint16_t http_port = 80;
        uint32_t websocket_max_clients = 8;
        uint32_t websocket_timeout_ms = 30000;
    };
    
    // Processing configuration
    struct ProcessingConfig {
        uint16_t fft_samples = SAMPLES;
        uint8_t fft_num_highest = 6;
        uint8_t fft_min_width = 10;
        uint32_t fft_task_stack_size = 8192;
        uint8_t fft_task_priority = 5;
        uint32_t fft_update_interval_ms = 16;
    };
    
    // System configuration
    struct SystemConfig {
        uint32_t memory_check_interval_ms = 5000;
        uint32_t health_check_interval_ms = 10000;
        uint32_t critical_heap_threshold = 15000;
        uint32_t low_heap_threshold = 30000;
        log_level_t default_log_level = LOG_LEVEL_INFO;
        bool enable_performance_monitoring = true;
        bool enable_memory_monitoring = true;
    };
    
    // Configuration access
    const AudioConfig& getAudioConfig() const { return audio_config_; }
    const NetworkConfig& getNetworkConfig() const { return network_config_; }
    const ProcessingConfig& getProcessingConfig() const { return processing_config_; }
    const SystemConfig& getSystemConfig() const { return system_config_; }
    
    // Configuration updates
    bool updateAudioConfig(const AudioConfig& config);
    bool updateNetworkConfig(const NetworkConfig& config);
    bool updateProcessingConfig(const ProcessingConfig& config);
    bool updateSystemConfig(const SystemConfig& config);
    
    // Individual parameter access
    uint32_t getSampleRate() const { return audio_config_.sample_rate; }
    uint16_t getSamples() const { return audio_config_.samples; }
    uint8_t getNumBands() const { return audio_config_.num_bands; }
    
    const char* getWiFiSSID() const { return network_config_.wifi_ssid; }
    const char* getWiFiPassword() const { return network_config_.wifi_password; }
    const char* getAPSSID() const { return network_config_.ap_ssid; }
    const char* getBTDeviceName() const { return network_config_.bt_device_name; }
    
    uint8_t getFFTNumHighest() const { return processing_config_.fft_num_highest; }
    uint8_t getFFTMinWidth() const { return processing_config_.fft_min_width; }
    
    // Configuration validation
    bool validateConfig(config_category_t category) const;
    bool validateAllConfigs() const;
    
    // Configuration persistence (future enhancement)
    bool saveConfig(config_category_t category);
    bool loadConfig(config_category_t category);
    bool saveAllConfigs();
    bool loadAllConfigs();
    
    // Configuration change notifications
    typedef void (*config_change_callback_t)(config_category_t category, void* user_data);
    bool registerChangeCallback(config_category_t category, config_change_callback_t callback, void* user_data);
    void unregisterChangeCallback(config_category_t category);
    
private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    // Configuration storage
    AudioConfig audio_config_;
    NetworkConfig network_config_;
    ProcessingConfig processing_config_;
    SystemConfig system_config_;
    
    // Change notification callbacks
    struct ChangeCallback {
        config_change_callback_t callback;
        void* user_data;
        bool active;
    };
    ChangeCallback change_callbacks_[CONFIG_CATEGORY_COUNT];
    
    // Internal methods
    void initializeDefaults();
    void notifyConfigChange(config_category_t category);
    bool validateAudioConfig() const;
    bool validateNetworkConfig() const;
    bool validateProcessingConfig() const;
    bool validateSystemConfig() const;
};

// Global config manager access
#define CONFIG_MGR ConfigManager::getInstance()

#endif // CONFIG_MANAGER_HPP
