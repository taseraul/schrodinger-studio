#include "core/logger.hpp"
#include "core/system_manager.hpp"
#include "core/config_manager.hpp"
#include "audio/audio_manager.hpp"
#include "network/network_manager.hpp"
#include "processing/processing_manager.hpp"
#include "core/memory_utils.hpp"
#include "Arduino.h"

// Application state
typedef enum {
    APP_STATE_INITIALIZING = 0,
    APP_STATE_RUNNING,
    APP_STATE_ERROR,
    APP_STATE_SHUTDOWN
} app_state_t;

static app_state_t app_state = APP_STATE_INITIALIZING;
static uint32_t last_status_log = 0;
static uint32_t initialization_start_time = 0;

// Forward declarations
bool initializeSystem();
void shutdownSystem();
void updateSystem();
void logSystemStatus();
void handleSystemError(const char* error);

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow serial to initialize
    
    initialization_start_time = millis();
    
    // Initialize logging first
    logger_init();
    LOG_SYSTEM_I("=== Schrodinger Audio System Starting ===");
    LOG_SYSTEM_I("Firmware Version: 2.0.0");
    LOG_SYSTEM_I("ESP32 Chip: %s", ESP.getChipModel());
    LOG_SYSTEM_I("CPU Frequency: %d MHz", ESP.getCpuFreqMHz());
    LOG_SYSTEM_I("Free Heap: %u bytes", ESP.getFreeHeap());
    LOG_SYSTEM_I("PSRAM Size: %u bytes", ESP.getPsramSize());
    LOG_SYSTEM_I("Flash Size: %u bytes", ESP.getFlashChipSize());
    
    // Initialize system
    if (initializeSystem()) {
        app_state = APP_STATE_RUNNING;
        uint32_t init_time = millis() - initialization_start_time;
        LOG_SYSTEM_I("System initialization completed in %u ms", init_time);
        LOG_SYSTEM_I("=== System Ready ===");
    } else {
        app_state = APP_STATE_ERROR;
        LOG_SYSTEM_E("System initialization failed");
        handleSystemError("System initialization failed");
    }
}

void loop() {
    switch (app_state) {
        case APP_STATE_RUNNING:
            updateSystem();
            break;
            
        case APP_STATE_ERROR:
            // In error state, try to recover every 30 seconds
            if (millis() % 30000 < 100) {
                LOG_SYSTEM_W("Attempting system recovery...");
                if (initializeSystem()) {
                    app_state = APP_STATE_RUNNING;
                    LOG_SYSTEM_I("System recovery successful");
                }
            }
            delay(1000);
            break;
            
        case APP_STATE_SHUTDOWN:
            // System is shutting down
            delay(1000);
            break;
            
        default:
            delay(100);
            break;
    }
    
    // Periodic status logging
    uint32_t now = millis();
    if (now - last_status_log > 60000) { // Every 60 seconds
        logSystemStatus();
        last_status_log = now;
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}

bool initializeSystem() {
    LOG_SYSTEM_I("Initializing system components...");
    
    try {
        // Initialize system manager (includes memory management)
        if (!SYSTEM_MGR.initialize()) {
            LOG_SYSTEM_E("Failed to initialize system manager");
            return false;
        }
        LOG_SYSTEM_I("System manager initialized");
        
        // Initialize configuration manager
        if (!CONFIG_MGR.initialize()) {
            LOG_SYSTEM_E("Failed to initialize configuration manager");
            return false;
        }
        LOG_SYSTEM_I("Configuration manager initialized");
        
        // Initialize audio subsystem
        if (!AUDIO_MGR.initialize()) {
            LOG_SYSTEM_E("Failed to initialize audio manager");
            return false;
        }
        LOG_SYSTEM_I("Audio manager initialized");
        
        // Initialize network subsystem
        if (!NETWORK_MGR.initialize()) {
            LOG_SYSTEM_E("Failed to initialize network manager");
            return false;
        }
        LOG_SYSTEM_I("Network manager initialized");
        
        // Initialize processing subsystem
        if (!PROCESSING_MGR.initialize()) {
            LOG_SYSTEM_E("Failed to initialize processing manager");
            return false;
        }
        LOG_SYSTEM_I("Processing manager initialized");
        
        // Start network services
        if (!NETWORK_MGR.startWebServer()) {
            LOG_SYSTEM_W("Failed to start web server (will retry)");
        }
        
        // Start audio processing
        if (!AUDIO_MGR.enableBluetoothAudio()) {
            LOG_SYSTEM_W("Failed to start Bluetooth audio (will retry)");
        }
        
        // Start signal processing
        if (!PROCESSING_MGR.startProcessing()) {
            LOG_SYSTEM_W("Failed to start processing (will retry)");
        }
        
        LOG_SYSTEM_I("All subsystems initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_SYSTEM_E("Exception during initialization: %s", e.what());
        return false;
    } catch (...) {
        LOG_SYSTEM_E("Unknown exception during initialization");
        return false;
    }
}

void shutdownSystem() {
    LOG_SYSTEM_I("Shutting down system...");
    app_state = APP_STATE_SHUTDOWN;
    
    try {
        // Shutdown in reverse order
        PROCESSING_MGR.shutdown();
        NETWORK_MGR.shutdown();
        AUDIO_MGR.shutdown();
        CONFIG_MGR.shutdown();
        SYSTEM_MGR.shutdown();
        
        LOG_SYSTEM_I("System shutdown complete");
        
    } catch (const std::exception& e) {
        LOG_SYSTEM_E("Exception during shutdown: %s", e.what());
    } catch (...) {
        LOG_SYSTEM_E("Unknown exception during shutdown");
    }
}

void updateSystem() {
    try {
        // Update all subsystems
        SYSTEM_MGR.run();
        // Config manager doesn't have update method
        AUDIO_MGR.update();
        NETWORK_MGR.update();
        PROCESSING_MGR.update();
        
        // Check for critical errors
        if (SYSTEM_MGR.getSystemState() == SYSTEM_STATE_ERROR) {
            LOG_SYSTEM_E("System manager reported critical error");
            handleSystemError("System manager error");
        }
        
    } catch (const std::exception& e) {
        LOG_SYSTEM_E("Exception during system update: %s", e.what());
        handleSystemError("System update exception");
    } catch (...) {
        LOG_SYSTEM_E("Unknown exception during system update");
        handleSystemError("Unknown system update exception");
    }
}

void logSystemStatus() {
    LOG_SYSTEM_I("=== System Status Report ===");
    LOG_SYSTEM_I("Uptime: %u seconds", millis() / 1000);
    LOG_SYSTEM_I("App State: %d", app_state);
    LOG_SYSTEM_I("System State: %d", SYSTEM_MGR.getSystemState());
    LOG_SYSTEM_I("Free Heap: %u bytes", ESP.getFreeHeap());
    LOG_SYSTEM_I("Min Free Heap: %u bytes", ESP.getMinFreeHeap());
    
    // Subsystem status
    LOG_SYSTEM_I("Audio: %s", AUDIO_MGR.isAudioActive() ? "Active" : "Inactive");
    LOG_SYSTEM_I("Network: %s", NETWORK_MGR.isNetworkAvailable() ? "Available" : "Unavailable");
    LOG_SYSTEM_I("Processing: %s", PROCESSING_MGR.isProcessing() ? "Running" : "Stopped");
    
    // Performance metrics
    LOG_SYSTEM_I("Audio Samples: %u", AUDIO_MGR.getTotalSamplesProcessed());
    LOG_SYSTEM_I("Processing Rate: %.1f FPS", PROCESSING_MGR.getOverallProcessingRate());
    LOG_SYSTEM_I("WebSocket Clients: %d", NETWORK_MGR.getWebSocketClientCount());
    
    // Memory usage
    size_t network_memory = 0; // Network manager doesn't expose memory usage
    size_t processing_memory = PROCESSING_MGR.getTotalMemoryUsage();
    LOG_SYSTEM_I("Memory Usage - Processing: %u bytes", processing_memory);
    
    // Error counts
    LOG_SYSTEM_I("Errors - Network: %u, Processing: %u",
                 NETWORK_MGR.getNetworkErrors(),
                 PROCESSING_MGR.getProcessingErrors());
    
    LOG_SYSTEM_I("============================");
    
    // Memory health check
    if (ESP.getFreeHeap() < 20480) { // Less than 20KB
        LOG_SYSTEM_W("Low memory warning: %u bytes free", ESP.getFreeHeap());
    }
    
    // Performance health check
    float processing_rate = PROCESSING_MGR.getOverallProcessingRate();
    if (processing_rate < 5.0f && PROCESSING_MGR.getTotalProcessedFrames() > 100) {
        LOG_SYSTEM_W("Low processing rate warning: %.1f FPS", processing_rate);
    }
}

void handleSystemError(const char* error) {
    LOG_SYSTEM_E("System error: %s", error);
    app_state = APP_STATE_ERROR;
    
    // Try to save critical state or perform emergency shutdown
    try {
        // Stop processing to free resources
        PROCESSING_MGR.stopProcessing();
        AUDIO_MGR.disableBluetoothAudio();
        
        LOG_SYSTEM_I("Emergency resource cleanup completed");
        
    } catch (...) {
        LOG_SYSTEM_E("Failed to perform emergency cleanup");
    }
}

// Note: Arduino framework handles app_main() internally
// We only need setup() and loop() functions
