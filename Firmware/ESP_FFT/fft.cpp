#include <math.h>
#include <arduinoFFT.h>

#include "fft.hpp"
#include "i2s.hpp"
#include "now.hpp"

struct_message lightData;

uint64_t bandValues[NUM_BANDS];
double bands_normalized[NUM_BANDS];
double vReal[SAMPLES];
double vImag[SAMPLES];

arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

void process_samples_for_fft() {
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (int16_t)(get_sample(i * 2) >> 16);
    vReal[i] += (int16_t)(get_sample(i * 2 + 1) >> 16);
    vReal[i] /= 2;
    vImag[i] = 0;
    // Serial.print(" ");
    // Serial.print(vReal[i]);
  }
  // Serial.println(" ");
}

void compute_fft() {
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();
}

uint8_t *lightDataMac() {
  return lightData.server_mac;
}

void calculate_bands_amplitude() {
  for (int i = 2; i < (SAMPLES / 2); i++) {
    // if (vReal[i] > NOISE) {

      if (i <= 3) {
        if ((int)vReal[i] > bandValues[0])
          bandValues[0] = (int)vReal[i];
      }
      if (i > 3 && i <= 7) {
        if ((int)vReal[i] > bandValues[1])
          bandValues[1] = (int)vReal[i];
      }
      if (i > 7 && i <= 18) {
        if ((int)vReal[i] > bandValues[2])
          bandValues[2] = (int)vReal[i];
      }
      if (i > 18 && i <= 48) {
        if ((int)vReal[i] > bandValues[3])
          bandValues[3] = (int)vReal[i];
      }
      if (i > 48 && i <= 128) {
        if ((int)vReal[i] > bandValues[4])
          bandValues[4] = (int)vReal[i];
      }
      if (i > 128) {
        if ((int)vReal[i] > bandValues[5])
          bandValues[5] = (int)vReal[i];
      }
    // }
    // Serial.print(" ");
    // Serial.print(vReal[i]);
  }
  // Serial.println(" ");

  // Serial.print(" ");
  // Serial.print(bandValues[0]);
  // Serial.print(" ");
  // Serial.print(bandValues[1]);
  // Serial.print(" ");
  // Serial.print(bandValues[2]);
  // Serial.print(" ");
  // Serial.print(bandValues[3]);
  // Serial.print(" ");
  // Serial.print(bandValues[4]);
  // Serial.print(" ");
  // Serial.print(bandValues[5]);
  // Serial.println(" ");
}

void normalize_bands() {
  for (int i = 0; i < NUM_BANDS; i++) {
    bands_normalized[i] = bandValues[i] / LOWER_ATTENUATION;
    if (bands_normalized[i] > 1) {
      bands_normalized[i] = 1;
    }
    lightData.bands[i] = bands_normalized[i] * 255;

    // Serial.print(" ");
    // Serial.print(bands_normalized[i]);
  }
  // Serial.println(" ");
}

void reset_bands() {
  for (int i = 0; i < NUM_BANDS; i++) {
    bandValues[i] = 0;
  }
}

void process_fft() {
  process_samples_for_fft();
  compute_fft();
  calculate_bands_amplitude();
  normalize_bands();
  reset_bands();
}

double get_band_normalized(int i) {
  return bands_normalized[i];
}

void send_light_data() {
  lightData.msgType = DATA_PACKET;
  lightData.r[0] = 255;
  lightData.g[0] = 0;
  lightData.b[0] = 0;

  lightData.r[1] = 0;
  lightData.g[1] = 255;
  lightData.b[1] = 0;

  lightData.r[2] = 0;
  lightData.g[2] = 0;
  lightData.b[2] = 255;

  lightData.r[3] = 255;
  lightData.g[3] = 255;
  lightData.b[3] = 0;

  lightData.r[4] = 0;
  lightData.g[4] = 255;
  lightData.b[4] = 255;

  lightData.r[5] = 255;
  lightData.g[5] = 0;
  lightData.b[5] = 255;

  now_send_light(&lightData);
}
