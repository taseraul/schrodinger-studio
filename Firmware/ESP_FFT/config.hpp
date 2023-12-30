#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

//FFT

#define NUM_BANDS 6
#define DECAY 500
#define FALLBACK_ATTENUATION (double)4000000.0
#define MAX_SLOPE 0.1
#define LIGHT_CUTOFF 0.2

//I2S
#define SAMPLING_FREQ 44100
#define SAMPLES 1024

#endif
