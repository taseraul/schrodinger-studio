#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "esp_log.h"
#include "Arduino.h"

// Subsystem log tags
#define LOG_TAG_SYSTEM    "SYSTEM"
#define LOG_TAG_AUDIO     "AUDIO"
#define LOG_TAG_NETWORK   "NETWORK"
#define LOG_TAG_PROCESS   "PROCESS"
#define LOG_TAG_MEMORY    "MEMORY"
#define LOG_TAG_CONFIG    "CONFIG"

// Log levels per subsystem
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} log_level_t;

// Subsystem identifiers
typedef enum {
    SUBSYSTEM_SYSTEM = 0,
    SUBSYSTEM_AUDIO,
    SUBSYSTEM_NETWORK,
    SUBSYSTEM_PROCESSING,
    SUBSYSTEM_MEMORY,
    SUBSYSTEM_CONFIG,
    SUBSYSTEM_COUNT
} subsystem_t;

// Logger initialization and configuration
void logger_init();
void logger_set_level(subsystem_t subsystem, log_level_t level);
log_level_t logger_get_level(subsystem_t subsystem);

// Enhanced logging macros with context
#define LOG_SYSTEM_E(format, ...) ESP_LOGE(LOG_TAG_SYSTEM, format, ##__VA_ARGS__)
#define LOG_SYSTEM_W(format, ...) ESP_LOGW(LOG_TAG_SYSTEM, format, ##__VA_ARGS__)
#define LOG_SYSTEM_I(format, ...) ESP_LOGI(LOG_TAG_SYSTEM, format, ##__VA_ARGS__)
#define LOG_SYSTEM_D(format, ...) ESP_LOGD(LOG_TAG_SYSTEM, format, ##__VA_ARGS__)
#define LOG_SYSTEM_V(format, ...) ESP_LOGV(LOG_TAG_SYSTEM, format, ##__VA_ARGS__)

#define LOG_AUDIO_E(format, ...) ESP_LOGE(LOG_TAG_AUDIO, format, ##__VA_ARGS__)
#define LOG_AUDIO_W(format, ...) ESP_LOGW(LOG_TAG_AUDIO, format, ##__VA_ARGS__)
#define LOG_AUDIO_I(format, ...) ESP_LOGI(LOG_TAG_AUDIO, format, ##__VA_ARGS__)
#define LOG_AUDIO_D(format, ...) ESP_LOGD(LOG_TAG_AUDIO, format, ##__VA_ARGS__)
#define LOG_AUDIO_V(format, ...) ESP_LOGV(LOG_TAG_AUDIO, format, ##__VA_ARGS__)

#define LOG_NETWORK_E(format, ...) ESP_LOGE(LOG_TAG_NETWORK, format, ##__VA_ARGS__)
#define LOG_NETWORK_W(format, ...) ESP_LOGW(LOG_TAG_NETWORK, format, ##__VA_ARGS__)
#define LOG_NETWORK_I(format, ...) ESP_LOGI(LOG_TAG_NETWORK, format, ##__VA_ARGS__)
#define LOG_NETWORK_D(format, ...) ESP_LOGD(LOG_TAG_NETWORK, format, ##__VA_ARGS__)
#define LOG_NETWORK_V(format, ...) ESP_LOGV(LOG_TAG_NETWORK, format, ##__VA_ARGS__)

#define LOG_PROCESS_E(format, ...) ESP_LOGE(LOG_TAG_PROCESS, format, ##__VA_ARGS__)
#define LOG_PROCESS_W(format, ...) ESP_LOGW(LOG_TAG_PROCESS, format, ##__VA_ARGS__)
#define LOG_PROCESS_I(format, ...) ESP_LOGI(LOG_TAG_PROCESS, format, ##__VA_ARGS__)
#define LOG_PROCESS_D(format, ...) ESP_LOGD(LOG_TAG_PROCESS, format, ##__VA_ARGS__)
#define LOG_PROCESS_V(format, ...) ESP_LOGV(LOG_TAG_PROCESS, format, ##__VA_ARGS__)

#define LOG_MEMORY_E(format, ...) ESP_LOGE(LOG_TAG_MEMORY, format, ##__VA_ARGS__)
#define LOG_MEMORY_W(format, ...) ESP_LOGW(LOG_TAG_MEMORY, format, ##__VA_ARGS__)
#define LOG_MEMORY_I(format, ...) ESP_LOGI(LOG_TAG_MEMORY, format, ##__VA_ARGS__)
#define LOG_MEMORY_D(format, ...) ESP_LOGD(LOG_TAG_MEMORY, format, ##__VA_ARGS__)
#define LOG_MEMORY_V(format, ...) ESP_LOGV(LOG_TAG_MEMORY, format, ##__VA_ARGS__)

#define LOG_CONFIG_E(format, ...) ESP_LOGE(LOG_TAG_CONFIG, format, ##__VA_ARGS__)
#define LOG_CONFIG_W(format, ...) ESP_LOGW(LOG_TAG_CONFIG, format, ##__VA_ARGS__)
#define LOG_CONFIG_I(format, ...) ESP_LOGI(LOG_TAG_CONFIG, format, ##__VA_ARGS__)
#define LOG_CONFIG_D(format, ...) ESP_LOGD(LOG_TAG_CONFIG, format, ##__VA_ARGS__)
#define LOG_CONFIG_V(format, ...) ESP_LOGV(LOG_TAG_CONFIG, format, ##__VA_ARGS__)

// Performance and memory logging utilities
void log_memory_status(const char* context);
void log_performance_metric(const char* operation, uint32_t duration_ms);
void log_subsystem_status(subsystem_t subsystem, const char* status);

// Error correlation utilities
void log_error_with_context(const char* tag, const char* error, const char* context, int error_code = 0);
void log_startup_banner();
void log_shutdown_banner();

#endif // LOGGER_HPP
