#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

//FFT

#define NUM_BANDS 6
#define NOISE 200000
#define LOWER_ATTENUATION (double)4000000.0

//I2S
#define SAMPLING_FREQ 44100
#define SAMPLES 1024

#endif
