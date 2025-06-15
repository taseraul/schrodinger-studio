#include "i2s.hpp"
#include "config.hpp"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define I2S_TASK_STACK_SIZE 8192
#define I2S_TASK_PRIORITY   15

static uint32_t samples[SAMPLES * 2];   // For writing from I2S

// Mutex for protecting buffers
static SemaphoreHandle_t buffer_mutex;

// Flag to indicate new data is ready
static volatile bool buffer_ready = false;

// High-priority task: reads samples from I2S
void i2s_read_task(void *param) {
  size_t bytes_read;
  uint8_t temp_buffer[SAMPLES * 8];  // 2 channels × 4 bytes = 8 bytes/sample pair

  while (true) {
    // Non-blocking I2S read
    esp_err_t res = i2s_read(
      I2S_NUM_0,
      temp_buffer,
      sizeof(temp_buffer),
      &bytes_read,
      0 // non-blocking
    );

    if (res == ESP_OK && bytes_read == sizeof(temp_buffer)) {
      if (xSemaphoreTake(buffer_mutex, 0)) {
        memcpy(samples, temp_buffer, sizeof(temp_buffer));
        buffer_ready = true;
        xSemaphoreGive(buffer_mutex);
      }
    }

    taskYIELD();  // Prevent CPU hogging
  }
}

void i2s_init() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
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

  buffer_mutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    i2s_read_task,
    "I2S Read Task",
    I2S_TASK_STACK_SIZE,
    NULL,
    I2S_TASK_PRIORITY,
    NULL,
    0  // Pin to Core 0
  );
}

// Thread-safe, non-blocking access to the latest samples
bool read_all_samples(uint32_t* dest, size_t length) {
  if (length < SAMPLES * 2) return false;

  bool copied = false;
  if (buffer_ready && xSemaphoreTake(buffer_mutex, 0)) {
    memcpy(dest, samples, sizeof(samples));
    buffer_ready = false;
    xSemaphoreGive(buffer_mutex);
    copied = true;
  }
  return copied;
}