#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "wifi_manager.hpp"
#include "websocket_server.hpp"
#include "espnow_manager.hpp"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"

// Network subsystem states
typedef enum {
    NETWORK_STATE_UNINITIALIZED = 0,
    NETWORK_STATE_INITIALIZING,
    NETWORK_STATE_READY,
    NETWORK_STATE_RUNNING,
    NETWORK_STATE_ERROR,
    NETWORK_STATE_SHUTDOWN
} network_state_t;

// Network manager class
class NetworkManager {
public:
    static NetworkManager& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // State management
    network_state_t getState() const { return network_state_; }
    
    // Component access
    WiFiManager& getWiFiManager() { return WIFI_MGR; }
    WebSocketServer& getWebSocketServer() { return WEBSOCKET_SERVER; }
    ESPNowManager& getESPNowManager() { return ESPNOW_MGR; }
    
    // Network status
    bool isNetworkAvailable() const;
    bool isWebServerRunning() const;
    String getNetworkInfo() const;
    
    // WebSocket interface (convenience methods)
    bool sendWebSocketText(const String& message);
    bool sendWebSocketBinary(const uint8_t* data, size_t length);
    int getWebSocketClientCount() const;
    
    // HTTP server management
    bool startWebServer();
    bool stopWebServer();
    
    // File system management
    bool initializeFileSystem();
    
    // Configuration management
    bool updateNetworkConfig(const ConfigManager::NetworkConfig& config);
    
    // Statistics and monitoring
    uint32_t getTotalWebSocketMessages() const;
    uint32_t getNetworkErrors() const { return error_count_; }
    
    // Error handling
    void handleNetworkError(const char* error, const char* context);
    
private:
    NetworkManager() = default;
    ~NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    
    // Internal state
    network_state_t network_state_ = NETWORK_STATE_UNINITIALIZED;
    AsyncWebServer* web_server_ = nullptr;
    bool file_system_initialized_ = false;
    
    // Statistics
    uint32_t error_count_ = 0;
    uint32_t last_health_check_ = 0;
    
    // Configuration change callback
    static void onConfigChange(config_category_t category, void* user_data);
    
    // Internal methods
    void setState(network_state_t state);
    bool initializeComponents();
    void setupHTTPRoutes();
    bool performHealthCheck();
    void logNetworkStatus();
};

// Global network manager access
#define NETWORK_MGR NetworkManager::getInstance()

#endif // NETWORK_MANAGER_HPP
