#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "WiFi.h"

// WiFi connection states
typedef enum {
    WIFI_STATE_UNINITIALIZED = 0,
    WIFI_STATE_INITIALIZING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

// WiFi manager class
class WiFiManager {
public:
    static WiFiManager& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // Connection management
    bool connect();
    bool disconnect();
    bool startAccessPoint();
    bool stopAccessPoint();
    
    // State management
    wifi_state_t getState() const { return wifi_state_; }
    bool isConnected() const;
    bool isAPActive() const;
    
    // Network information
    String getLocalIP() const;
    String getAPIP() const;
    String getMACAddress() const;
    int getRSSI() const;
    int getChannel() const;
    
    // Configuration
    bool updateNetworkConfig(const ConfigManager::NetworkConfig& config);
    
    // Status monitoring
    uint32_t getConnectionAttempts() const { return connection_attempts_; }
    uint32_t getLastConnectionTime() const { return last_connection_time_; }
    
    // Error handling
    void handleWiFiError(const char* error, const char* context);
    
private:
    WiFiManager() = default;
    ~WiFiManager() = default;
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    
    // Internal state
    wifi_state_t wifi_state_ = WIFI_STATE_UNINITIALIZED;
    
    // Connection tracking
    uint32_t connection_attempts_ = 0;
    uint32_t last_connection_time_ = 0;
    uint32_t last_connection_attempt_ = 0;
    uint32_t error_count_ = 0;
    
    // Configuration change callback
    static void onConfigChange(config_category_t category, void* user_data);
    
    // Internal methods
    void setState(wifi_state_t state);
    bool attemptConnection();
    void monitorConnection();
    bool performHealthCheck();
};

// Global WiFi manager access
#define WIFI_MGR WiFiManager::getInstance()

#endif // WIFI_MANAGER_HPP
