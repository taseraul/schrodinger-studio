#ifndef AUDIO_MANAGER_HPP
#define AUDIO_MANAGER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"

// Audio subsystem states
typedef enum {
    AUDIO_STATE_UNINITIALIZED = 0,
    AUDIO_STATE_INITIALIZING,
    AUDIO_STATE_READY,
    AUDIO_STATE_STREAMING,
    AUDIO_STATE_ERROR,
    AUDIO_STATE_SHUTDOWN
} audio_state_t;

// Audio source types
typedef enum {
    AUDIO_SOURCE_NONE = 0,
    AUDIO_SOURCE_BLUETOOTH,
    AUDIO_SOURCE_I2S,
    AUDIO_SOURCE_COUNT
} audio_source_t;

// Audio manager class
class AudioManager {
public:
    static AudioManager& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // State management
    audio_state_t getState() const { return audio_state_; }
    audio_source_t getActiveSource() const { return active_source_; }
    
    // Audio source management
    bool enableBluetoothAudio();
    bool disableBluetoothAudio();
    bool enableI2SAudio();
    bool disableI2SAudio();
    
    // Audio data interface
    bool readAudioSamples(uint32_t* dest, size_t length);
    size_t getAvailableSamples() const;
    
    // Configuration management
    bool updateAudioConfig(const ConfigManager::AudioConfig& config);
    
    // Status and monitoring
    bool isAudioActive() const;
    float getBufferUtilization() const;
    uint32_t getTotalSamplesProcessed() const;
    
    // Error handling
    void handleAudioError(const char* error, const char* context);
    
private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    
    // Internal state
    audio_state_t audio_state_ = AUDIO_STATE_UNINITIALIZED;
    audio_source_t active_source_ = AUDIO_SOURCE_NONE;
    
    // Statistics
    uint32_t total_samples_processed_ = 0;
    uint32_t last_activity_time_ = 0;
    uint32_t error_count_ = 0;
    
    // Configuration change callback
    static void onConfigChange(config_category_t category, void* user_data);
    
    // Internal methods
    bool initializeBluetoothAudio();
    bool initializeI2SAudio();
    void setState(audio_state_t state);
    void setActiveSource(audio_source_t source);
    void updateStatistics();
    bool performHealthCheck();
};

// Global audio manager access
#define AUDIO_MGR AudioManager::getInstance()

#endif // AUDIO_MANAGER_HPP
