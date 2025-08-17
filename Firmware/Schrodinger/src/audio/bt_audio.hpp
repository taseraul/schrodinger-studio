#ifndef BT_AUDIO_HPP
#define BT_AUDIO_HPP

#include <stdint.h>
#include <stddef.h>

// Bluetooth audio interface functions
void bt_init();
void bt_deinit();
void bt_post_init_wifi_restore();

// Audio sample interface
bool readBtSamples(uint32_t* dest, size_t length);

// Internal callback (used by A2DP library)
void receiveBtSamples(const uint8_t* data, uint32_t length);

#endif // BT_AUDIO_HPP
