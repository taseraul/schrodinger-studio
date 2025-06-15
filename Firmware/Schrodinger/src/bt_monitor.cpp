#include "bt_monitor.hpp"
#include "esp_log.h"

static const char* TAG = "bt_monitor";

// Global variables for BT state monitoring
volatile BtConnectionState bt_state = BT_DISCONNECTED;
unsigned long bt_connection_time = 0;
unsigned long last_audio_activity = 0;

void bt_monitor_init() {
    bt_state = BT_DISCONNECTED;
    bt_connection_time = 0;
    last_audio_activity = 0;
    ESP_LOGI(TAG, "Bluetooth monitor initialized");
}

void bt_state_changed(BtConnectionState new_state) {
    if (bt_state != new_state) {
        ESP_LOGI(TAG, "BT State changed from %d to %d", bt_state, new_state);
        bt_state = new_state;
        
        switch (new_state) {
            case BT_CONNECTED:
                bt_connection_time = millis();
                ESP_LOGI(TAG, "Bluetooth device connected");
                break;
            case BT_AUDIO_PLAYING:
                last_audio_activity = millis();
                ESP_LOGI(TAG, "Bluetooth audio started");
                break;
            case BT_DISCONNECTED:
                bt_connection_time = 0;
                last_audio_activity = 0;
                ESP_LOGI(TAG, "Bluetooth device disconnected");
                break;
            default:
                break;
        }
    }
}

BtConnectionState get_bt_state() {
    return bt_state;
}

bool is_bt_audio_active() {
    return (bt_state == BT_AUDIO_PLAYING) && 
           (millis() - last_audio_activity < 5000); // Consider active if audio within last 5 seconds
}

void bt_connection_callback() {
    // This would be called when BT connection state changes
    // Implementation depends on the A2DP library callbacks
}

void bt_audio_callback() {
    // This would be called when BT audio state changes
    // Implementation depends on the A2DP library callbacks
}
