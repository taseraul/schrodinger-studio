#ifndef BT_CPP
#define BT_CPP

#include "Arduino.h"

bool readBtSamples(uint32_t* dest, size_t length);
void bt_init();
void bt_deinit();
void bt_post_init_wifi_restore();

#endif // !BT_CPP
