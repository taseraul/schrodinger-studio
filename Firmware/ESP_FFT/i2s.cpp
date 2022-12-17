#include "i2s.hpp"
#include "driver/i2s.h"

uint32_t samples[SAMPLES * 2];

void i2s_init() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Interrupt level 1, default 0
    .dma_buf_count = 10,
    .dma_buf_len = 256,
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

void i2s_read_samples() {
  size_t bytes_read;
  if (i2s_read(I2S_NUM_0, samples, SAMPLES * 8, &bytes_read, portMAX_DELAY) != ESP_OK) {
    Serial.println("I2S read error");
  }  
}

uint32_t get_sample(int i){
  return samples[i];
}