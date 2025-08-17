#include "espnow_manager.hpp"
#include "network_manager.hpp"

// Static instance
ESPNowManager& ESPNowManager::getInstance() {
    static ESPNowManager instance;
    return instance;
}

bool ESPNowManager::initialize() {
    LOG_NETWORK_I("Initializing ESP-NOW manager");
    setState(ESPNOW_STATE_INITIALIZING);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        LOG_NETWORK_E("Failed to initialize ESP-NOW");
        setState(ESPNOW_STATE_ERROR);
        return false;
    }
    
    // Register callbacks
    if (esp_now_register_send_cb(onDataSent) != ESP_OK) {
        LOG_NETWORK_E("Failed to register ESP-NOW send callback");
        setState(ESPNOW_STATE_ERROR);
        return false;
    }
    
    if (esp_now_register_recv_cb(onDataReceived) != ESP_OK) {
        LOG_NETWORK_E("Failed to register ESP-NOW receive callback");
        setState(ESPNOW_STATE_ERROR);
        return false;
    }
    
    // Add broadcast peer
    if (!addPeer(broadcast_addr_, WiFi.channel())) {
        LOG_NETWORK_E("Failed to add broadcast peer");
        setState(ESPNOW_STATE_ERROR);
        return false;
    }
    
    // Initialize device configurations
    device_count_ = 0;
    memset(mood_config_, 0, sizeof(mood_config_));
    
    // Register with system manager
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, SUBSYSTEM_STATUS_READY);
    
    setState(ESPNOW_STATE_READY);
    LOG_NETWORK_I("ESP-NOW manager initialized successfully");
    return true;
}

void ESPNowManager::shutdown() {
    LOG_NETWORK_I("Shutting down ESP-NOW manager");
    setState(ESPNOW_STATE_SHUTDOWN);
    
    // Deinitialize ESP-NOW
    esp_now_deinit();
    
    // Clear device configurations
    device_count_ = 0;
    memset(mood_config_, 0, sizeof(mood_config_));
    
    // Update system manager
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, SUBSYSTEM_STATUS_SHUTDOWN);
    
    setState(ESPNOW_STATE_UNINITIALIZED);
    LOG_NETWORK_I("ESP-NOW manager shutdown complete");
}

void ESPNowManager::update() {
    if (espnow_state_ != ESPNOW_STATE_READY) {
        return;
    }
    
    // Periodic health check
    static uint32_t last_health_check = 0;
    uint32_t now = millis();
    if (now - last_health_check > 30000) { // Every 30 seconds
        performHealthCheck();
        last_health_check = now;
    }
}

bool ESPNowManager::isPeerDevice(const uint8_t* mac_addr) const {
    return getPeerIndex(mac_addr) != NOT_PEER;
}

int ESPNowManager::getPeerIndex(const uint8_t* mac_addr) const {
    for (int i = 0; i < device_count_; i++) {
        if (memcmp(mac_addr, mood_config_[i].macAddr, 6) == 0) {
            return i;
        }
    }
    return NOT_PEER;
}

bool ESPNowManager::sendLightData(const struct_message* message) {
    if (espnow_state_ != ESPNOW_STATE_READY) {
        LOG_NETWORK_W("Cannot send light data - ESP-NOW not ready");
        return false;
    }
    
    esp_err_t result = esp_now_send(broadcast_addr_, (uint8_t*)message, sizeof(struct_message));
    if (result == ESP_OK) {
        messages_sent_++;
        LOG_NETWORK_D("Light data sent successfully");
        return true;
    } else {
        error_count_++;
        LOG_NETWORK_E("Failed to send light data: %s", esp_err_to_name(result));
        return false;
    }
}

bool ESPNowManager::setDeviceFlash(uint8_t flash, uint8_t index) {
    if (espnow_state_ != ESPNOW_STATE_READY) {
        LOG_NETWORK_W("Cannot set device flash - ESP-NOW not ready");
        return false;
    }
    
    if (index >= device_count_) {
        LOG_NETWORK_E("Invalid device index: %d", index);
        return false;
    }
    
    struct_update update;
    update.preamble = PREAMBLE;
    update.msgType = UPDATE_PACKET;
    update.sourceId = index;
    update.updateType = UPDATE_FLASH;
    update.updateData = flash;
    
    mood_config_[index].flash = flash;
    
    esp_err_t result = esp_now_send(broadcast_addr_, (uint8_t*)&update, sizeof(update));
    if (result == ESP_OK) {
        messages_sent_++;
        LOG_NETWORK_D("Device flash set successfully for device %d", index);
        return true;
    } else {
        error_count_++;
        LOG_NETWORK_E("Failed to set device flash: %s", esp_err_to_name(result));
        return false;
    }
}

bool ESPNowManager::setDeviceBand(uint8_t band, uint8_t index) {
    if (espnow_state_ != ESPNOW_STATE_READY) {
        LOG_NETWORK_W("Cannot set device band - ESP-NOW not ready");
        return false;
    }
    
    if (index >= device_count_) {
        LOG_NETWORK_E("Invalid device index: %d", index);
        return false;
    }
    
    if (band >= 6) {
        LOG_NETWORK_E("Invalid band index: %d", band);
        return false;
    }
    
    struct_update update;
    update.preamble = PREAMBLE;
    update.msgType = UPDATE_PACKET;
    update.sourceId = index;
    update.updateType = UPDATE_BAND;
    update.updateData = band;
    
    mood_config_[index].band = band;
    
    esp_err_t result = esp_now_send(broadcast_addr_, (uint8_t*)&update, sizeof(update));
    if (result == ESP_OK) {
        messages_sent_++;
        LOG_NETWORK_D("Device band set successfully for device %d", index);
        return true;
    } else {
        error_count_++;
        LOG_NETWORK_E("Failed to set device band: %s", esp_err_to_name(result));
        return false;
    }
}

bool ESPNowManager::setDeviceColor(const uint8_t* color, uint8_t index) {
    if (espnow_state_ != ESPNOW_STATE_READY) {
        LOG_NETWORK_W("Cannot set device color - ESP-NOW not ready");
        return false;
    }
    
    if (index >= device_count_) {
        LOG_NETWORK_E("Invalid device index: %d", index);
        return false;
    }
    
    if (!color) {
        LOG_NETWORK_E("Invalid color pointer");
        return false;
    }
    
    // Update local configuration
    memcpy(mood_config_[index].rgb, color, 3);
    
    // Send configuration to device
    setDeviceState(broadcast_addr_, index);
    
    LOG_NETWORK_D("Device color set successfully for device %d", index);
    return true;
}

void ESPNowManager::handleESPNowError(const char* error, const char* context) {
    error_count_++;
    LOG_NETWORK_E("ESP-NOW error in %s: %s", context ? context : "unknown", error);
    
    // Report to system manager
    SYSTEM_MGR.handleCriticalError(error, context);
}

void ESPNowManager::setState(espnow_state_t state) {
    if (espnow_state_ != state) {
        LOG_NETWORK_D("ESP-NOW state change: %d -> %d", espnow_state_, state);
        espnow_state_ = state;
        
        // Map ESP-NOW state to subsystem status
        subsystem_status_t subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED;
        switch (state) {
            case ESPNOW_STATE_UNINITIALIZED: subsystem_status = SUBSYSTEM_STATUS_UNINITIALIZED; break;
            case ESPNOW_STATE_INITIALIZING: subsystem_status = SUBSYSTEM_STATUS_INITIALIZING; break;
            case ESPNOW_STATE_READY: subsystem_status = SUBSYSTEM_STATUS_READY; break;
            case ESPNOW_STATE_ERROR: subsystem_status = SUBSYSTEM_STATUS_ERROR; break;
            case ESPNOW_STATE_SHUTDOWN: subsystem_status = SUBSYSTEM_STATUS_SHUTDOWN; break;
        }
        
        SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_NETWORK, subsystem_status);
    }
}

bool ESPNowManager::addPeer(const uint8_t* mac_addr, uint8_t channel) {
    esp_now_peer_info_t peer;
    
    // Remove peer if it already exists
    esp_now_del_peer(mac_addr);
    
    // Setup peer info
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = channel;
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac_addr, 6);
    
    esp_err_t result = esp_now_add_peer(&peer);
    if (result != ESP_OK) {
        LOG_NETWORK_E("Failed to add peer: %s", esp_err_to_name(result));
        return false;
    }
    
    LOG_NETWORK_D("Peer added successfully");
    return true;
}

bool ESPNowManager::removePeer(const uint8_t* mac_addr) {
    esp_err_t result = esp_now_del_peer(mac_addr);
    if (result != ESP_OK) {
        LOG_NETWORK_E("Failed to remove peer: %s", esp_err_to_name(result));
        return false;
    }
    
    LOG_NETWORK_D("Peer removed successfully");
    return true;
}

void ESPNowManager::setDeviceState(const uint8_t* mac_addr, uint8_t index) {
    if (index >= device_count_) {
        LOG_NETWORK_E("Invalid device index for state setting: %d", index);
        return;
    }
    
    struct_config config;
    config.preamble = PREAMBLE;
    config.msgType = CONF_PACKET;
    config.setId = index;
    config.channel = WiFi.channel();
    config.band = mood_config_[index].band;
    config.flash = mood_config_[index].flash;
    memcpy(config.rgb, mood_config_[index].rgb, 3);
    
    esp_err_t result = esp_now_send(mac_addr, (uint8_t*)&config, sizeof(config));
    if (result == ESP_OK) {
        messages_sent_++;
        LOG_NETWORK_D("Device state sent successfully for device %d", index);
    } else {
        error_count_++;
        LOG_NETWORK_E("Failed to send device state: %s", esp_err_to_name(result));
    }
}

void ESPNowManager::pairDevice(const uint8_t* mac_addr, uint8_t battery) {
    int index = getPeerIndex(mac_addr);
    struct_config config;
    
    config.preamble = PREAMBLE;
    config.msgType = CONF_PACKET;
    config.channel = WiFi.channel();
    
    // Add peer for communication
    addPeer(mac_addr, config.channel);
    
    if (index != NOT_PEER) {
        // Device already exists, update battery and send state
        config.setId = index;
        mood_config_[index].battery = battery;
        updateBatteryWeb(battery, index);
        setDeviceState(mac_addr, index);
        LOG_NETWORK_I("Re-paired existing device %d", index);
    } else {
        // New device, add to configuration
        if (device_count_ >= MAX_DEVICES) {
            LOG_NETWORK_E("Maximum device count reached, cannot pair new device");
            removePeer(mac_addr);
            return;
        }
        
        config.setId = device_count_;
        
        // Initialize device configuration
        mood_config_[device_count_].band = device_count_ % 6;
        mood_config_[device_count_].battery = battery;
        mood_config_[device_count_].isPaired = 1;
        mood_config_[device_count_].flash = 0;
        mood_config_[device_count_].lock = 0;
        memcpy(mood_config_[device_count_].macAddr, mac_addr, 6);
        memcpy(mood_config_[device_count_].rgb, default_colors_[device_count_ % 6], 3);
        
        populateDeviceForWeb(device_count_);
        setDeviceState(mac_addr, device_count_);
        
        LOG_NETWORK_I("Paired new device %d", device_count_);
        device_count_++;
    }
    
    // Remove peer after configuration (will be re-added when needed)
    removePeer(mac_addr);
}

void ESPNowManager::updateDevice(const struct_update* packet) {
    if (!packet) {
        LOG_NETWORK_E("Invalid update packet");
        return;
    }
    
    switch (packet->updateType) {
        case UPDATE_BATTERY:
            updateBatteryWeb(packet->updateData, packet->sourceId);
            break;
        case UPDATE_FLASH:
            updateFlashWeb(packet->updateData, packet->sourceId);
            break;
        case UPDATE_BAND:
            updateBandWeb(packet->updateData, packet->sourceId);
            break;
        default:
            LOG_NETWORK_W("Unknown update type: %d", packet->updateType);
            break;
    }
}

void ESPNowManager::populateDeviceForWeb(uint8_t index) {
    if (index >= device_count_) {
        return;
    }
    
    // Send device information to web clients via WebSocket
    if (NETWORK_MGR.isNetworkAvailable()) {
        JSONVar deviceInstance;
        deviceInstance["id"] = index;
        deviceInstance["band"] = mood_config_[index].band;
        deviceInstance["lock"] = mood_config_[index].lock;
        deviceInstance["flash"] = mood_config_[index].flash;
        deviceInstance["battery"] = mood_config_[index].battery;
        deviceInstance["red"] = mood_config_[index].rgb[0];
        deviceInstance["green"] = mood_config_[index].rgb[1];
        deviceInstance["blue"] = mood_config_[index].rgb[2];
        
        String json_string = JSON.stringify(deviceInstance);
        NETWORK_MGR.sendWebSocketText(json_string);
    }
}

void ESPNowManager::updateBatteryWeb(uint8_t battery, uint8_t index) {
    if (index >= device_count_) {
        return;
    }
    
    mood_config_[index].battery = battery;
    
    if (NETWORK_MGR.isNetworkAvailable()) {
        JSONVar deviceUpdate;
        deviceUpdate["id"] = index;
        deviceUpdate["battery"] = battery;
        
        String json_string = JSON.stringify(deviceUpdate);
        NETWORK_MGR.sendWebSocketText(json_string);
    }
}

void ESPNowManager::updateFlashWeb(uint8_t flash, uint8_t index) {
    if (index >= device_count_) {
        return;
    }
    
    mood_config_[index].flash = flash;
    
    if (NETWORK_MGR.isNetworkAvailable()) {
        JSONVar deviceUpdate;
        deviceUpdate["id"] = index;
        deviceUpdate["flash"] = flash;
        
        String json_string = JSON.stringify(deviceUpdate);
        NETWORK_MGR.sendWebSocketText(json_string);
    }
}

void ESPNowManager::updateBandWeb(uint8_t band, uint8_t index) {
    if (index >= device_count_ || band >= 6) {
        return;
    }
    
    mood_config_[index].band = band;
    memcpy(mood_config_[index].rgb, default_colors_[band], 3);
    
    // Send configuration to device
    setDeviceState(broadcast_addr_, index);
    
    if (NETWORK_MGR.isNetworkAvailable()) {
        JSONVar deviceUpdate;
        deviceUpdate["id"] = index;
        deviceUpdate["band"] = band;
        deviceUpdate["r"] = default_colors_[band][0];
        deviceUpdate["g"] = default_colors_[band][1];
        deviceUpdate["b"] = default_colors_[band][2];
        
        String json_string = JSON.stringify(deviceUpdate);
        NETWORK_MGR.sendWebSocketText(json_string);
    }
}

bool ESPNowManager::performHealthCheck() {
    bool healthy = true;
    
    // Check ESP-NOW state
    if (espnow_state_ != ESPNOW_STATE_READY) {
        LOG_NETWORK_W("Health check: ESP-NOW not ready");
        healthy = false;
    }
    
    // Check error rate
    if (messages_sent_ > 0) {
        float error_rate = (float)error_count_ / messages_sent_ * 100.0f;
        if (error_rate > 10.0f) { // More than 10% error rate
            LOG_NETWORK_W("Health check: High ESP-NOW error rate: %.1f%%", error_rate);
            healthy = false;
        }
    }
    
    if (healthy) {
        LOG_NETWORK_D("ESP-NOW health check passed");
    } else {
        LOG_NETWORK_W("ESP-NOW health check failed");
    }
    
    return healthy;
}

// Static callback functions
void ESPNowManager::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    ESPNowManager& instance = getInstance();
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        LOG_NETWORK_V("ESP-NOW packet sent successfully");
    } else {
        instance.error_count_++;
        LOG_NETWORK_W("ESP-NOW packet send failed");
    }
}

void ESPNowManager::onDataReceived(const uint8_t* mac_addr, const uint8_t* data, int len) {
    ESPNowManager& instance = getInstance();
    instance.messages_received_++;
    
    LOG_NETWORK_V("Received ESP-NOW message, length: %d", len);
    LOG_NETWORK_V("Data: %02X %02X %02X %02X", data[0], data[1], data[2], data[3]);
    
    if (len < 2) {
        LOG_NETWORK_W("ESP-NOW message too short");
        return;
    }
    
    if (data[0] == PREAMBLE) {
        uint8_t type = data[1];
        switch (type) {
            case PAIR_PACKET:
                if (len >= 3) {
                    LOG_NETWORK_I("ESP-NOW pair request received");
                    instance.pairDevice(mac_addr, data[2]);
                } else {
                    LOG_NETWORK_W("Invalid pair packet length");
                }
                break;
            case UPDATE_PACKET:
                if (len >= sizeof(struct_update)) {
                    LOG_NETWORK_D("ESP-NOW device update received");
                    instance.updateDevice((const struct_update*)data);
                } else {
                    LOG_NETWORK_W("Invalid update packet length");
                }
                break;
            default:
                LOG_NETWORK_W("Unknown ESP-NOW packet type: %d", type);
                break;
        }
    } else {
        LOG_NETWORK_W("ESP-NOW message failed preamble check");
    }
}
