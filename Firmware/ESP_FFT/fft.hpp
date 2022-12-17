#ifndef FFT_H
#define FFT_H

#include "config.hpp"

double get_band_normalized(int i);
void process_fft();
void send_light_data();
uint8_t *lightDataMac();

#endif
