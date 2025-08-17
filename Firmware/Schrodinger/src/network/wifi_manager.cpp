#include "wifi_manager.hpp"

WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

bool WiFiManager::initialize() {
    LOG_NETWORK_I("WiFi Manager initialization starting...");
    setState(WIFI_STATE_INITIALIZING);
    
    // Register for configuration changes
    CONFIG_MGR.registerChangeCallback(CONFIG_CATEGORY_NETWORK, onConfigChange, this);
    
    // Set WiFi mode to both Station and AP
    WiFi.mode(WIFI_AP_STA);
    
    // Reset connection tracking
    connection_attempts_ = 0;
    last_connection_time_ = 0;
    last_connection_attempt_ = 0;
    error_count_ = 0;
    
    // Log MAC addresses
    LOG_NETWORK_I("Station MAC: %s", WiFi.macAddress().c_str());
    LOG_NETWORK_I("AP MAC: %s", WiFi.softAPmacAddress().c_str());
    
    // Attempt initial connection
    if (!connect()) {
        LOG_NETWORK_W("Initial WiFi connection failed, starting AP mode");
        startAccessPoint();
    }
    
    LOG_NETWORK_I("WiFi Manager initialized successfully");
    return true;
}

void WiFiManager::shutdown() {
    LOG_NETWORK_I("WiFi Manager shutdown initiated");
    
    // Disconnect and stop AP
    disconnect();
    stopAccessPoint();
    
    // Unregister configuration callback
    CONFIG_MGR.unregisterChangeCallback(CONFIG_CATEGORY_NETWORK);
    
    setState(WIFI_STATE_UNINITIALIZED);
    LOG_NETWORK_I("WiFi Manager shutdown completed");
}

void WiFiManager::update() {
    // Monitor connection status
    monitorConnection();
    
    // Perform periodic health check
    static uint32_t last_health_check = 0;
    uint32_t current_time = millis();
    
    if (current_time - last_health_check >= 30000) { // Every 30 seconds
        if (!performHealthCheck()) {
            LOG_NETWORK_W("WiFi health check failed");
            error_count_++;
        }
        last_health_check = current_time;
    }
}

bool WiFiManager::connect() {
    const auto& config = CONFIG_MGR.getNetworkConfig();
    
    LOG_NETWORK_I("Connecting to WiFi SSID: %s", config.wifi_ssid);
    setState(WIFI_STATE_CONNECTING);
    
    return attemptConnection();
}

bool WiFiManager::disconnect() {
    LOG_NETWORK_I("Disconnecting from WiFi");
    
    WiFi.disconnect(true);
    setState(WIFI_STATE_DISCONNECTED);
    
    return true;
}

bool WiFiManager::startAccessPoint() {
    const auto& config = CONFIG_MGR.getNetworkConfig();
    
    LOG_NETWORK_I("Starting Access Point: %s", config.ap_ssid);
    
    bool success = WiFi.softAP(config.ap_ssid);
    if (success) {
        setState(WIFI_STATE_AP_MODE);
        LOG_NETWORK_I("Access Point started successfully");
        LOG_NETWORK_I("AP IP: %s", WiFi.softAPIP().toString().c_str());
    } else {
        LOG_NETWORK_E("Failed to start Access Point");
        setState(WIFI_STATE_ERROR);
    }
    
    return success;
}

bool WiFiManager::stopAccessPoint() {
    LOG_NETWORK_I("Stopping Access Point");
    
    bool success = WiFi.softAPdisconnect(true);
    if (success) {
        LOG_NETWORK_I("Access Point stopped successfully");
    } else {
        LOG_NETWORK_W("Failed to stop Access Point");
    }
    
    return success;
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED && wifi_state_ == WIFI_STATE_CONNECTED;
}

bool WiFiManager::isAPActive() const {
    return wifi_state_ == WIFI_STATE_AP_MODE;
}

String WiFiManager::getLocalIP() const {
    return WiFi.localIP().toString();
}

String WiFiManager::getAPIP() const {
    return WiFi.softAPIP().toString();
}

String WiFiManager::getMACAddress() const {
    return WiFi.macAddress();
}

int WiFiManager::getRSSI() const {
    return WiFi.RSSI();
}

int WiFiManager::getChannel() const {
    return WiFi.channel();
}

bool WiFiManager::updateNetworkConfig(const ConfigManager::NetworkConfig& config) {
    LOG_NETWORK_I("Updating network configuration");
    
    // Update configuration through config manager
    if (!CONFIG_MGR.updateNetworkConfig(config)) {
        LOG_NETWORK_E("Failed to update network configuration");
        return false;
    }
    
    // Reconnect with new settings
    if (isConnected()) {
        LOG_NETWORK_I("Reconnecting with new configuration");
        disconnect();
        delay(1000);
        connect();
    }
    
    LOG_NETWORK_I("Network configuration updated successfully");
    return true;
}

void WiFiManager::handleWiFiError(const char* error, const char* context) {
    error_count_++;
    log_error_with_context(LOG_TAG_NETWORK, error, context);
    
    // Set error state if too many errors
    if (error_count_ > 5) {
        setState(WIFI_STATE_ERROR);
        SYSTEM_MGR.handleCriticalError("Too many WiFi errors", "WiFi Manager");
    }
}

void WiFiManager::onConfigChange(config_category_t category, void* user_data) {
    if (category != CONFIG_CATEGORY_NETWORK || !user_data) {
        return;
    }
    
    WiFiManager* manager = static_cast<WiFiManager*>(user_data);
    LOG_NETWORK_I("Network configuration changed, applying updates...");
    
    // Reconnect with new configuration
    if (manager->isConnected()) {
        manager->disconnect();
        delay(1000);
        manager->connect();
    }
}

void WiFiManager::setState(wifi_state_t state) {
    if (wifi_state_ != state) {
        const char* state_names[] = {
            "UNINITIALIZED", "INITIALIZING", "CONNECTING", "CONNECTED", 
            "AP_MODE", "DISCONNECTED", "ERROR"
        };
        
        LOG_NETWORK_I("WiFi state: %s -> %s", 
                      state_names[wifi_state_], state_names[state]);
        wifi_state_ = state;
        
        // Update system manager with network subsystem status
        subsystem_status_t sys_status;
        switch (state) {
            case WIFI_STATE_UNINITIALIZED:
                sys_status = SUBSYSTEM_STATUS_UNINITIALIZED;
                break;
            case WIFI_STATE_INITIALIZING:
            case WIFI_STATE_CONNECTING:
                sys_status = SUBSYSTEM_STATUS_INITIALIZING;
                break;
            case WIFI_STATE_CONNECTED:
            case WIFI_STATE_AP_MODE:
                sys_status = SUBSYSTEM_STATUS_RUNNING;
                break;
            case WIFI_STATE_DISCONNECTED:
                sys_status = SUBSYSTEM_STATUS_READY;
                break;
            case WIFI_STATE_ERROR:
                sys_status = SUBSYSTEM_STATUS_ERROR;
                break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, sys_status);
    }
}

bool WiFiManager::attemptConnection() {
    const auto& config = CONFIG_MGR.getNetworkConfig();
    
    connection_attempts_++;
    last_connection_attempt_ = millis();
    
    LOG_NETWORK_D("WiFi connection attempt %d", connection_attempts_);
    
    // Begin connection
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    
    // Wait for connection with timeout
    uint8_t retryCount = 30;
    while (WiFi.status() != WL_CONNECTED && retryCount--) {
        delay(1000);
        LOG_NETWORK_D("Connecting... (%d attempts left)", retryCount);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        setState(WIFI_STATE_CONNECTED);
        last_connection_time_ = millis();
        
        LOG_NETWORK_I("WiFi connected successfully");
        LOG_NETWORK_I("IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_NETWORK_I("RSSI: %d dBm", WiFi.RSSI());
        LOG_NETWORK_I("Channel: %d", WiFi.channel());
        
        return true;
    } else {
        LOG_NETWORK_W("WiFi connection failed after %d attempts", connection_attempts_);
        setState(WIFI_STATE_DISCONNECTED);
        return false;
    }
}

void WiFiManager::monitorConnection() {
    static uint32_t last_monitor_check = 0;
    uint32_t current_time = millis();
    
    // Check connection status every 5 seconds
    if (current_time - last_monitor_check < 5000) {
        return;
    }
    
    last_monitor_check = current_time;
    
    // Check if we think we're connected but actually aren't
    if (wifi_state_ == WIFI_STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
        LOG_NETWORK_W("WiFi connection lost, attempting reconnection");
        setState(WIFI_STATE_DISCONNECTED);
        
        // Attempt reconnection
        if (!connect()) {
            LOG_NETWORK_W("Reconnection failed, starting AP mode");
            startAccessPoint();
        }
    }
    
    // Log connection status periodically
    if (wifi_state_ == WIFI_STATE_CONNECTED) {
        static uint32_t last_status_log = 0;
        if (current_time - last_status_log >= 60000) { // Every minute
            LOG_NETWORK_D("WiFi status: IP=%s, RSSI=%d dBm, Channel=%d", 
                          WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
            last_status_log = current_time;
        }
    }
}

bool WiFiManager::performHealthCheck() {
    // Check if WiFi is in a good state
    if (wifi_state_ == WIFI_STATE_ERROR) {
        LOG_NETWORK_W("WiFi in error state");
        return false;
    }
    
    // Check connection stability
    if (wifi_state_ == WIFI_STATE_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            LOG_NETWORK_W("WiFi connection unstable");
            return false;
        }
        
        // Check signal strength
        int rssi = WiFi.RSSI();
        if (rssi < -80) {
            LOG_NETWORK_W("Weak WiFi signal: %d dBm", rssi);
            return false;
        }
    }
    
    // Check error count
    if (error_count_ > 3) {
        LOG_NETWORK_W("High WiFi error count: %d", error_count_);
        return false;
    }
    
    LOG_NETWORK_D("WiFi health check passed");
    return true;
}
