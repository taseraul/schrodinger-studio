/*
  Streaming of sound data with Bluetooth to other Bluetooth device.
  We generate 2 tones which will be sent to the 2 channels.
  
  Copyright (C) 2020 Phil Schatzmann
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "BluetoothA2DPSource.h"
#include <math.h>
#include "esp_log.h"

#define c3_frequency 130.81

BluetoothA2DPSource a2dp_source;

// The supported audio codec in ESP32 A2DP is SBC. SBC audio stream is encoded
// from PCM data normally formatted as 44.1kHz sampling rate, two-channel 16-bit sample data
int32_t get_data_channels(Frame *frame, int32_t channel_len) {
  size_t result = 0;
  uint32_t samples[256];
  
  i2s_read(I2S_NUM_0, samples, channel_len * 8, &result, portMAX_DELAY);

  for (int i = 0; i < channel_len; i++) {
    frame[i].channel1 = samples[i*2]>>16;
    frame[i].channel2 = samples[i*2+1]>>16;
  }
  Serial.printf("res: %d\n", result);

  return channel_len;
}

void setup() {
  // Serial.begin(115200);
  a2dp_source.set_auto_reconnect(false);
  a2dp_source.start("[AV] Samsung Soundbar Q6T-Series", get_data_channels);
  a2dp_source.set_volume(100);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Interrupt level 1, default 0
    .dma_buf_count = 10,
    .dma_buf_len = 64,
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256
  };


  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

  static const i2s_pin_config_t pin_config = {
    .bck_io_num = 14,
    .ws_io_num = 15,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 22
  };

  i2s_set_pin(I2S_NUM_0, &pin_config);
  uint32_t bits_cfg = (I2S_BITS_PER_CHAN_32BIT << 16) | I2S_BITS_PER_SAMPLE_32BIT;
  i2s_set_clk(I2S_NUM_0, 44100, bits_cfg, I2S_CHANNEL_STEREO);
}

void loop() {
  delay(10);
}