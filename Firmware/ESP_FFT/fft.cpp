#include <arduinoFFT.h>
#include <math.h>

#include "fft.hpp"
#include "i2s.hpp"
#include "now.hpp"

struct_message lightData;

uint64_t bandHistoryLow[NUM_BANDS];
uint64_t bandHistoryHigh[NUM_BANDS];
uint64_t bandDecayLow[NUM_BANDS];
uint64_t bandDecayHigh[NUM_BANDS];
uint64_t bandValues[NUM_BANDS];
float bands_normalized[NUM_BANDS];
float prev_bands_normalized[NUM_BANDS];
double vReal[SAMPLES];
double vImag[SAMPLES];

arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

void process_samples_for_fft() {
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (int16_t)(get_sample(i * 2) >> 16);
    vReal[i] += (int16_t)(get_sample(i * 2 + 1) >> 16);
    vReal[i] /= 2;
    vImag[i] = 0;
  }
}

void compute_fft() {
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();
}

uint8_t* setLightDataMac() {
  return lightData.server_mac;
}

void calculate_bands_amplitude() {
  for (int i = 2; i < (SAMPLES / 2); i++) {
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
  }
}

void normalize_bands() {
  float band_norm_val;
  for (int i = 0; i < NUM_BANDS; i++) {

    if (((bandHistoryLow[i] + bandDecayLow[i]) > bandHistoryLow[i]) && (bandHistoryHigh[i] > (bandHistoryLow[i] + bandHistoryLow[i]))) {
      bandHistoryLow[i] += bandDecayLow[i];
      bandDecayLow[i] *= 1.05;
    }

    if (((bandHistoryHigh[i] - bandDecayHigh[i]) < bandHistoryHigh[i]) && ((bandHistoryHigh[i] - bandDecayHigh[i]) > bandHistoryLow[i])) {
      bandHistoryHigh[i] -= bandDecayHigh[i];
      bandDecayHigh[i] *= 1.05;
    }

    if (bandHistoryLow[i] > bandValues[i]) {
      bandHistoryLow[i] = bandValues[i];
      bandDecayLow[i] = DECAY;
    }

    if (bandHistoryHigh[i] < bandValues[i]) {
      bandHistoryHigh[i] = bandValues[i];
      bandDecayHigh[i] = DECAY;
    }

    if ((bandHistoryHigh[i] - bandHistoryLow[i]) > (20 * DECAY)) {
      band_norm_val = (bandValues[i] - bandHistoryLow[i]) / (float)(bandHistoryHigh[i] - bandHistoryLow[i]);
    } else {
      band_norm_val = 0;
    }
    if ((band_norm_val - prev_bands_normalized[i]) < -MAX_SLOPE) {
      band_norm_val = prev_bands_normalized[i] - MAX_SLOPE;
    }
    
    if(band_norm_val < LIGHT_CUTOFF){
      band_norm_val = 0;
    }

    bands_normalized[i] = band_norm_val;
    prev_bands_normalized[i] = bands_normalized[i];
    
    // if (i == 0) {
    //   Serial.print("diff:");
    //   Serial.print((bandHistoryHigh[i] - bandHistoryLow[i]));
    //   Serial.print(",");

    //   Serial.print("low:");
    //   Serial.print(bandHistoryLow[i]);
    //   Serial.print(",");

    //   Serial.print("high:");
    //   Serial.print(bandHistoryHigh[i]);
    //   Serial.print(",");

    //   Serial.print("val:");
    //   Serial.println(bandValues[i]);
    // }

    lightData.bands[i] = bands_normalized[i] * 255;
  }
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

void init_fft() {
  std::fill_n(bandDecayLow, NUM_BANDS, DECAY);
  std::fill_n(bandDecayHigh, NUM_BANDS, DECAY);
}
