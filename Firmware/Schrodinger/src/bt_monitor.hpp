#ifndef BT_MONITOR_HPP
#define BT_MONITOR_HPP

#include "Arduino.h"

// Bluetooth connection state monitoring
enum BtConnectionState {
    BT_DISCONNECTED = 0,
    BT_CONNECTING = 1,
    BT_CONNECTED = 2,
    BT_AUDIO_PLAYING = 3
};

// Global variables for BT state monitoring
extern volatile BtConnectionState bt_state;
extern unsigned long bt_connection_time;
extern unsigned long last_audio_activity;

// Function declarations
void bt_monitor_init();
void bt_state_changed(BtConnectionState new_state);
BtConnectionState get_bt_state();
bool is_bt_audio_active();
void bt_connection_callback();
void bt_audio_callback();

#endif
