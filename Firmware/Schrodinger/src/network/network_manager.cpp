#include "network_manager.hpp"
#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"

// Static instance
NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

bool NetworkManager::initialize() {
    LOG_NETWORK_I("Initializing network manager");
    setState(NETWORK_STATE_INITIALIZING);
    
    // Initialize file system first
    if (!initializeFileSystem()) {
        LOG_NETWORK_E("Failed to initialize file system");
        setState(NETWORK_STATE_ERROR);
        return false;
    }
    
    // Initialize components
    if (!initializeComponents()) {
        LOG_NETWORK_E("Failed to initialize network components");
        setState(NETWORK_STATE_ERROR);
        return false;
    }
    
    // Register for configuration changes
    CONFIG_MGR.registerChangeCallback(CONFIG_CATEGORY_NETWORK, onConfigChange, this);
    
    // Set subsystem status
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, SUBSYSTEM_STATUS_READY);
    
    setState(NETWORK_STATE_READY);
    LOG_NETWORK_I("Network manager initialized successfully");
    return true;
}

void NetworkManager::shutdown() {
    LOG_NETWORK_I("Shutting down network manager");
    setState(NETWORK_STATE_SHUTDOWN);
    
    // Stop web server
    stopWebServer();
    
    // Shutdown components
    ESPNOW_MGR.shutdown();
    WEBSOCKET_SERVER.shutdown();
    WIFI_MGR.shutdown();
    
    // Unregister callbacks
    CONFIG_MGR.unregisterChangeCallback(CONFIG_CATEGORY_NETWORK);
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, SUBSYSTEM_STATUS_SHUTDOWN);
    
    setState(NETWORK_STATE_UNINITIALIZED);
    LOG_NETWORK_I("Network manager shutdown complete");
}

void NetworkManager::update() {
    if (network_state_ != NETWORK_STATE_RUNNING) {
        return;
    }
    
    // Update components
    WIFI_MGR.update();
    WEBSOCKET_SERVER.update();
    ESPNOW_MGR.update();
    
    // Periodic health check
    uint32_t now = millis();
    if (now - last_health_check_ > 30000) { // Every 30 seconds
        performHealthCheck();
        last_health_check_ = now;
    }
}

bool NetworkManager::isNetworkAvailable() const {
    return WIFI_MGR.isConnected() && network_state_ == NETWORK_STATE_RUNNING;
}

bool NetworkManager::isWebServerRunning() const {
    return web_server_ != nullptr && isNetworkAvailable();
}

String NetworkManager::getNetworkInfo() const {
    String info = "Network Status:\n";
    info += "State: " + String(network_state_) + "\n";
    info += "WiFi: ";
    info += (WIFI_MGR.isConnected() ? "Connected" : "Disconnected");
    info += "\n";
    
    if (WIFI_MGR.isConnected()) {
        info += "IP: " + WiFi.localIP().toString() + "\n";
        info += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    }
    
    info += "WebSocket Clients: " + String(getWebSocketClientCount()) + "\n";
    info += "Web Server: ";
    info += (isWebServerRunning() ? "Running" : "Stopped");
    info += "\n";
    info += "Errors: " + String(error_count_) + "\n";
    
    return info;
}

bool NetworkManager::sendWebSocketText(const String& message) {
    if (!isNetworkAvailable()) {
        LOG_NETWORK_W("Cannot send WebSocket message - network not available");
        return false;
    }
    
    return WEBSOCKET_SERVER.sendTextMessage(message);
}

bool NetworkManager::sendWebSocketBinary(const uint8_t* data, size_t length) {
    if (!isNetworkAvailable()) {
        LOG_NETWORK_W("Cannot send WebSocket binary - network not available");
        return false;
    }
    
    return WEBSOCKET_SERVER.sendBinaryMessage(data, length);
}

int NetworkManager::getWebSocketClientCount() const {
    return WEBSOCKET_SERVER.getClientCount();
}

bool NetworkManager::startWebServer() {
    if (!web_server_) {
        LOG_NETWORK_E("Web server instance not created - call initialize() first");
        return false;
    }
    
    if (!WIFI_MGR.isConnected()) {
        LOG_NETWORK_E("Cannot start web server - WiFi not connected");
        return false;
    }
    
    LOG_NETWORK_I("Starting web server");
    
    try {
        // Setup HTTP routes
        setupHTTPRoutes();
        
        // Start the server
        web_server_->begin();
        
        setState(NETWORK_STATE_RUNNING);
        LOG_NETWORK_I("Web server started on port 80");
        return true;
        
    } catch (const std::exception& e) {
        LOG_NETWORK_E("Exception starting web server: %s", e.what());
        return false;
    }
}

bool NetworkManager::stopWebServer() {
    if (web_server_ == nullptr) {
        return true;
    }
    
    LOG_NETWORK_I("Stopping web server");
    
    try {
        web_server_->end();
        delete web_server_;
        web_server_ = nullptr;
        
        LOG_NETWORK_I("Web server stopped");
        return true;
        
    } catch (const std::exception& e) {
        LOG_NETWORK_E("Exception stopping web server: %s", e.what());
        return false;
    }
}

bool NetworkManager::initializeFileSystem() {
    if (file_system_initialized_) {
        return true;
    }
    
    LOG_NETWORK_I("Initializing SPIFFS file system");
    
    if (!SPIFFS.begin(true)) {
        LOG_NETWORK_E("Failed to mount SPIFFS");
        return false;
    }
    
    // Check available space
    size_t total_bytes = SPIFFS.totalBytes();
    size_t used_bytes = SPIFFS.usedBytes();
    
    LOG_NETWORK_I("SPIFFS mounted - Total: %u bytes, Used: %u bytes, Free: %u bytes", 
                     total_bytes, used_bytes, total_bytes - used_bytes);
    
    file_system_initialized_ = true;
    return true;
}

bool NetworkManager::updateNetworkConfig(const ConfigManager::NetworkConfig& config) {
    LOG_NETWORK_I("Updating network configuration");
    
    // Update WiFi configuration
    if (!WIFI_MGR.updateNetworkConfig(config)) {
        LOG_NETWORK_E("Failed to update WiFi configuration");
        return false;
    }
    
    LOG_NETWORK_I("Network configuration updated successfully");
    return true;
}

uint32_t NetworkManager::getTotalWebSocketMessages() const {
    return WEBSOCKET_SERVER.getTotalMessagesSent();
}

void NetworkManager::handleNetworkError(const char* error, const char* context) {
    error_count_++;
    LOG_NETWORK_E("Network error in %s: %s", context ? context : "unknown", error);
    
    // Report to system manager
    SYSTEM_MGR.handleCriticalError(error, context);
    
    // Attempt recovery for certain errors
    if (strstr(error, "connection") || strstr(error, "timeout")) {
        LOG_NETWORK_I("Attempting network recovery");
        
        // Try to reconnect WiFi
        if (!WIFI_MGR.isConnected()) {
            WIFI_MGR.connect();
        }
    }
}

void NetworkManager::setState(network_state_t state) {
    if (network_state_ != state) {
        LOG_NETWORK_D("Network state change: %d -> %d", network_state_, state);
        network_state_ = state;
        
        // Map network state to subsystem status
        subsystem_status_t subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED;
        switch (state) {
            case NETWORK_STATE_UNINITIALIZED: subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED; break;
            case NETWORK_STATE_INITIALIZING: subsystem_status = SUBSYSTEM_STATUS_INITIALIZING; break;
            case NETWORK_STATE_READY: subsystem_status = SUBSYSTEM_STATUS_READY; break;
            case NETWORK_STATE_RUNNING: subsystem_status = SUBSYSTEM_STATUS_RUNNING; break;
            case NETWORK_STATE_ERROR: subsystem_status = SUBSYSTEM_STATUS_ERROR; break;
            case NETWORK_STATE_SHUTDOWN: subsystem_status = SUBSYSTEM_STATUS_SHUTDOWN; break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, subsystem_status);
    }
}

bool NetworkManager::initializeComponents() {
    LOG_NETWORK_D("Initializing network components");
    
    // Initialize WiFi manager
    if (!WIFI_MGR.initialize()) {
        LOG_NETWORK_E("Failed to initialize WiFi manager");
        return false;
    }
    
    // Create web server instance first
    try {
        web_server_ = new AsyncWebServer(80);
        if (!web_server_) {
            LOG_NETWORK_E("Failed to allocate web server");
            return false;
        }
        LOG_NETWORK_D("Web server instance created");
    } catch (const std::exception& e) {
        LOG_NETWORK_E("Exception creating web server: %s", e.what());
        return false;
    }
    
    // Initialize WebSocket server (pass web server instance)
    if (!WEBSOCKET_SERVER.initialize(web_server_)) {
        LOG_NETWORK_E("Failed to initialize WebSocket server");
        return false;
    }
    
    // Initialize ESP-NOW manager
    if (!ESPNOW_MGR.initialize()) {
        LOG_NETWORK_E("Failed to initialize ESP-NOW manager");
        return false;
    }
    
    LOG_NETWORK_D("Network components initialized successfully");
    return true;
}

void NetworkManager::setupHTTPRoutes() {
    if (!web_server_) {
        return;
    }
    
    LOG_NETWORK_D("Setting up HTTP routes");
    
    // Serve static files from SPIFFS
    web_server_->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API endpoints
    web_server_->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String status = getNetworkInfo();
        request->send(200, "text/plain", status);
    });
    
    web_server_->on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Simple system status response
        String system_info = "{\"status\":\"running\",\"heap\":" + String(ESP.getFreeHeap()) + "}";
        request->send(200, "application/json", system_info);
    });
    
    web_server_->on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Simple config response
        String config_json = "{\"network\":{\"status\":\"configured\"}}";
        request->send(200, "application/json", config_json);
    });
    
    // Handle 404
    web_server_->onNotFound([](AsyncWebServerRequest *request) {
        LOG_NETWORK_W("HTTP 404: %s", request->url().c_str());
        request->send(404, "text/plain", "Not Found");
    });
    
    LOG_NETWORK_D("HTTP routes configured");
}

bool NetworkManager::performHealthCheck() {
    bool healthy = true;
    
    // Check WiFi connection
    if (!WIFI_MGR.isConnected()) {
        LOG_NETWORK_W("Health check: WiFi not connected");
        healthy = false;
    }
    
    // Check WebSocket server state
    if (WEBSOCKET_SERVER.getState() != WS_STATE_RUNNING) {
        LOG_NETWORK_W("Health check: WebSocket server not running");
        healthy = false;
    }
    
    // Check memory usage
    size_t free_heap = ESP.getFreeHeap();
    if (free_heap < 10240) { // Less than 10KB
        LOG_NETWORK_W("Health check: Low memory - %u bytes free", free_heap);
        healthy = false;
    }
    
    // Check file system
    if (file_system_initialized_) {
        size_t free_space = SPIFFS.totalBytes() - SPIFFS.usedBytes();
        if (free_space < 1024) { // Less than 1KB
            LOG_NETWORK_W("Health check: Low SPIFFS space - %u bytes free", free_space);
            healthy = false;
        }
    }
    
    if (healthy) {
        LOG_NETWORK_D("Network health check passed");
    } else {
        LOG_NETWORK_W("Network health check failed");
        error_count_++;
    }
    
    return healthy;
}

void NetworkManager::logNetworkStatus() {
    LOG_NETWORK_I("=== Network Status ===");
    LOG_NETWORK_I("State: %d", network_state_);
    LOG_NETWORK_I("WiFi Connected: %s", WIFI_MGR.isConnected() ? "Yes" : "No");
    
    if (WIFI_MGR.isConnected()) {
        LOG_NETWORK_I("IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_NETWORK_I("Signal Strength: %d dBm", WiFi.RSSI());
    }
    
    LOG_NETWORK_I("WebSocket Clients: %d", getWebSocketClientCount());
    LOG_NETWORK_I("Web Server: %s", isWebServerRunning() ? "Running" : "Stopped");
    LOG_NETWORK_I("Total Errors: %u", error_count_);
    LOG_NETWORK_I("Free Heap: %u bytes", ESP.getFreeHeap());
    
    if (file_system_initialized_) {
        LOG_NETWORK_I("SPIFFS Free: %u bytes", SPIFFS.totalBytes() - SPIFFS.usedBytes());
    }
    
    LOG_NETWORK_I("=====================");
}

void NetworkManager::onConfigChange(config_category_t category, void* user_data) {
    if (category != CONFIG_CATEGORY_NETWORK || !user_data) {
        return;
    }
    
    NetworkManager* manager = static_cast<NetworkManager*>(user_data);
    LOG_NETWORK_I("Network configuration changed, updating components");
    
    // Get updated configuration
    ConfigManager::NetworkConfig config = CONFIG_MGR.getNetworkConfig();
    
    // Update network configuration
    manager->updateNetworkConfig(config);
}
