#include "logger.hpp"
#include "esp_heap_caps.h"

// Log level configuration per subsystem
static log_level_t subsystem_log_levels[SUBSYSTEM_COUNT] = {
    LOG_LEVEL_INFO,    // SYSTEM
    LOG_LEVEL_INFO,    // AUDIO
    LOG_LEVEL_INFO,    // NETWORK
    LOG_LEVEL_INFO,    // PROCESSING
    LOG_LEVEL_INFO,    // MEMORY
    LOG_LEVEL_INFO     // CONFIG
};

// Subsystem names for logging
static const char* subsystem_names[SUBSYSTEM_COUNT] = {
    "System",
    "Audio",
    "Network",
    "Processing",
    "Memory",
    "Config"
};

void logger_init() {
    // Set default log levels for ESP-IDF components
    esp_log_level_set(LOG_TAG_SYSTEM, ESP_LOG_INFO);
    esp_log_level_set(LOG_TAG_AUDIO, ESP_LOG_INFO);
    esp_log_level_set(LOG_TAG_NETWORK, ESP_LOG_INFO);
    esp_log_level_set(LOG_TAG_PROCESS, ESP_LOG_INFO);
    esp_log_level_set(LOG_TAG_MEMORY, ESP_LOG_INFO);
    esp_log_level_set(LOG_TAG_CONFIG, ESP_LOG_INFO);
    
    LOG_SYSTEM_I("Logger initialized with default INFO level for all subsystems");
}

void logger_set_level(subsystem_t subsystem, log_level_t level) {
    if (subsystem >= SUBSYSTEM_COUNT) {
        LOG_SYSTEM_E("Invalid subsystem %d for log level setting", subsystem);
        return;
    }
    
    subsystem_log_levels[subsystem] = level;
    
    // Map to ESP-IDF log levels and set them
    esp_log_level_t esp_level;
    switch (level) {
        case LOG_LEVEL_NONE:    esp_level = ESP_LOG_NONE; break;
        case LOG_LEVEL_ERROR:   esp_level = ESP_LOG_ERROR; break;
        case LOG_LEVEL_WARN:    esp_level = ESP_LOG_WARN; break;
        case LOG_LEVEL_INFO:    esp_level = ESP_LOG_INFO; break;
        case LOG_LEVEL_DEBUG:   esp_level = ESP_LOG_DEBUG; break;
        case LOG_LEVEL_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
        default:                esp_level = ESP_LOG_INFO; break;
    }
    
    // Set ESP-IDF log level for the corresponding tag
    const char* tags[] = {
        LOG_TAG_SYSTEM, LOG_TAG_AUDIO, LOG_TAG_NETWORK,
        LOG_TAG_PROCESS, LOG_TAG_MEMORY, LOG_TAG_CONFIG
    };
    
    esp_log_level_set(tags[subsystem], esp_level);
    
    LOG_SYSTEM_I("Set %s subsystem log level to %d", subsystem_names[subsystem], level);
}

log_level_t logger_get_level(subsystem_t subsystem) {
    if (subsystem >= SUBSYSTEM_COUNT) {
        return LOG_LEVEL_INFO; // Default fallback
    }
    return subsystem_log_levels[subsystem];
}

void log_memory_status(const char* context) {
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t free_psram = ESP.getFreePsram();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t min_heap = ESP.getMinFreeHeap();
    
    LOG_MEMORY_I("%s - Heap: %d, PSRAM: %d, Block: %d, Min: %d", 
                 context, free_heap, free_psram, largest_block, min_heap);
}

void log_performance_metric(const char* operation, uint32_t duration_ms) {
    if (duration_ms > 1000) {
        LOG_SYSTEM_W("PERF: %s took %d ms (>1s)", operation, duration_ms);
    } else if (duration_ms > 100) {
        LOG_SYSTEM_I("PERF: %s took %d ms", operation, duration_ms);
    } else {
        LOG_SYSTEM_D("PERF: %s took %d ms", operation, duration_ms);
    }
}

void log_subsystem_status(subsystem_t subsystem, const char* status) {
    if (subsystem >= SUBSYSTEM_COUNT) {
        LOG_SYSTEM_E("Invalid subsystem %d for status logging", subsystem);
        return;
    }
    
    LOG_SYSTEM_I("%s subsystem: %s", subsystem_names[subsystem], status);
}

void log_error_with_context(const char* tag, const char* error, const char* context, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(tag, "ERROR: %s | Context: %s | Code: %d", error, context, error_code);
    } else {
        ESP_LOGE(tag, "ERROR: %s | Context: %s", error, context);
    }
    
    // Also log current memory status for debugging
    log_memory_status("Error Context");
}

void log_startup_banner() {
    LOG_SYSTEM_I("========================================");
    LOG_SYSTEM_I("    SCHRODINGER FIRMWARE STARTING");
    LOG_SYSTEM_I("========================================");
    log_memory_status("Startup");
}

void log_shutdown_banner() {
    LOG_SYSTEM_I("========================================");
    LOG_SYSTEM_I("    SCHRODINGER FIRMWARE SHUTDOWN");
    LOG_SYSTEM_I("========================================");
    log_memory_status("Shutdown");
}
