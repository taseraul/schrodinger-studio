#ifndef FFT_H
#define FFT_H
#include "Arduino.h"

void fft_task_init();
void fft_task_deinit();
bool fft_set_num_highest(int value);
bool fft_set_min_width(int value);
int fft_get_num_highest();
int fft_get_min_width();

#endif
