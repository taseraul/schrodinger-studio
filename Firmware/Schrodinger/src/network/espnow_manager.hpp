#ifndef ESPNOW_MANAGER_HPP
#define ESPNOW_MANAGER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "esp_now.h"
#include "WiFi.h"
#include "Arduino_JSON.h"

// ESP-NOW packet types
#define DATA_PACKET   0x00u
#define PAIR_PACKET   0x01u
#define CONF_PACKET   0x02u
#define UPDATE_PACKET 0x03u

// Configuration commands
#define SET_TARGET_ALL    0xFFu
#define SET_BAND          0x00u
#define SET_BASE_COLOR    0x01u
#define SET_FLASH_ON      0x02u
#define SET_FLASH_OFF     0x03u
#define SET_INTENSITY     0x04u
#define SET_LOCK          0x05u
#define SET_UNLOCK        0x06u
#define SET_REQUEST_STATE 0x07u

// Update types
#define UPDATE_BATTERY 0x00u
#define UPDATE_FLASH   0x01u
#define UPDATE_LOCK    0x02u
#define UPDATE_BAND    0x03u
#define UPDATE_COLOR   0x04u
#define UPDATE_FULL    0x05u

// Constants
#define BATTERY_WALL_ADAPTER 0xFFu
#define NOT_PEER             0xFFu
#define PREAMBLE             0xAAu
#define MAX_DEVICES          30

// ESP-NOW manager states
typedef enum {
    ESPNOW_STATE_UNINITIALIZED = 0,
    ESPNOW_STATE_INITIALIZING,
    ESPNOW_STATE_READY,
    ESPNOW_STATE_ERROR,
    ESPNOW_STATE_SHUTDOWN
} espnow_state_t;

// Data structures (inferred from usage)
typedef struct {
    uint8_t preamble;
    uint8_t msgType;
    uint8_t destId;
    uint8_t requestLength;
    uint8_t requestData[16];
} struct_request;

typedef struct {
    uint8_t preamble;
    uint8_t msgType;
    uint8_t setId;
    uint8_t channel;
    uint8_t band;
    uint8_t flash;
    uint8_t rgb[3];
} struct_config;

typedef struct {
    uint8_t preamble;
    uint8_t msgType;
    uint8_t sourceId;
    uint8_t updateType;
    uint8_t updateData;
} struct_update;

typedef struct {
    uint8_t preamble;
    uint8_t msgType;
    uint8_t data[32];
} struct_message;

// Device handler structure
typedef struct {
    uint8_t macAddr[6];
    uint8_t band;
    uint8_t battery;
    uint8_t isPaired;
    uint8_t flash;
    uint8_t lock;
    uint8_t rgb[3];
} device_handler;

// ESP-NOW manager class
class ESPNowManager {
public:
    static ESPNowManager& getInstance();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    void update();
    
    // State management
    espnow_state_t getState() const { return espnow_state_; }
    
    // Device management
    uint8_t getDeviceCount() const { return device_count_; }
    bool isPeerDevice(const uint8_t* mac_addr) const;
    int getPeerIndex(const uint8_t* mac_addr) const;
    
    // Communication
    bool sendLightData(const struct_message* message);
    bool setDeviceFlash(uint8_t flash, uint8_t index);
    bool setDeviceBand(uint8_t band, uint8_t index);
    bool setDeviceColor(const uint8_t* color, uint8_t index);
    
    // Statistics
    uint32_t getTotalMessagesSent() const { return messages_sent_; }
    uint32_t getTotalMessagesReceived() const { return messages_received_; }
    uint32_t getErrorCount() const { return error_count_; }
    
    // Error handling
    void handleESPNowError(const char* error, const char* context);
    
private:
    ESPNowManager() = default;
    ~ESPNowManager() = default;
    ESPNowManager(const ESPNowManager&) = delete;
    ESPNowManager& operator=(const ESPNowManager&) = delete;
    
    // Internal state
    espnow_state_t espnow_state_ = ESPNOW_STATE_UNINITIALIZED;
    
    // Device management
    device_handler mood_config_[MAX_DEVICES];
    uint8_t device_count_ = 0;
    uint8_t broadcast_addr_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t default_colors_[6][3] = {
        {0xFF, 0x00, 0x00}, // Red
        {0x00, 0xFF, 0x00}, // Green
        {0x00, 0x00, 0xFF}, // Blue
        {0xFF, 0x70, 0x00}, // Orange
        {0x00, 0xD0, 0xFF}, // Cyan
        {0x99, 0x00, 0xFF}  // Purple
    };
    
    // Statistics
    uint32_t messages_sent_ = 0;
    uint32_t messages_received_ = 0;
    uint32_t error_count_ = 0;
    
    // ESP-NOW callbacks
    static void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
    static void onDataReceived(const uint8_t* mac_addr, const uint8_t* data, int len);
    
    // Internal methods
    void setState(espnow_state_t state);
    bool addPeer(const uint8_t* mac_addr, uint8_t channel);
    bool removePeer(const uint8_t* mac_addr);
    void setDeviceState(const uint8_t* mac_addr, uint8_t index);
    void pairDevice(const uint8_t* mac_addr, uint8_t battery);
    void updateDevice(const struct_update* packet);
    void populateDeviceForWeb(uint8_t index);
    void updateBatteryWeb(uint8_t battery, uint8_t index);
    void updateFlashWeb(uint8_t flash, uint8_t index);
    void updateBandWeb(uint8_t band, uint8_t index);
    bool performHealthCheck();
};

// Global ESP-NOW manager access
#define ESPNOW_MGR ESPNowManager::getInstance()

#endif // ESPNOW_MANAGER_HPP
