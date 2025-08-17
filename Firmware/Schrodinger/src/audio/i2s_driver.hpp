#ifndef I2S_HELPER_H
#define I2S_HELPER_H
#include "Arduino.h"

void i2s_init();
void i2s_deinit();
bool read_all_samples(uint32_t* dest, size_t length);

#endif
