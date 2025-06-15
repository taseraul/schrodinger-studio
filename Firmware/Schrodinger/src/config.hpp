#ifndef CONFIG_H
#define CONFIG_H

#include "Arduino.h"
#include "stdint.h"

//FFT
#define NUM_BANDS 6
#define DECAY 500
#define FALLBACK_ATTENUATION (double)4000000.0
#define MAX_SLOPE 0.2
#define LIGHT_CUTOFF 0.2
#define MAX_DEVICES 30
#define MAX_LED_QUEUE 3

//I2S,FFT
#define SAMPLE_RATE 44100
#define SAMPLES 512

#endif
