#include "i2s.hpp"
#include "config.hpp"
#include "memory_manager.hpp"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#define I2S_TASK_STACK_SIZE 8192
#define I2S_TASK_PRIORITY   15

static const char* TAG = "i2s";

// PSRAM-allocated buffers for I2S processing
static uint32_t* samples = nullptr;        // Main sample buffer in PSRAM
static uint8_t* temp_buffer = nullptr;     // Temporary I2S read buffer in PSRAM

// Mutex for protecting buffers
static SemaphoreHandle_t buffer_mutex;

// Flag to indicate new data is ready
static volatile bool buffer_ready = false;

// Buffer allocation and deallocation functions
static bool allocate_i2s_buffers() {
    ESP_LOGI(TAG, "Allocating I2S buffers in PSRAM...");
    
    // Calculate buffer sizes
    size_t samples_size = SAMPLES * 2 * sizeof(uint32_t);  // ~4KB
    size_t temp_buffer_size = SAMPLES * 8;                 // ~4KB
    
    ESP_LOGI(TAG, "Buffer sizes: samples=%d, temp=%d bytes", samples_size, temp_buffer_size);
    
    // Allocate main samples buffer in PSRAM
    samples = (uint32_t*)malloc_psram_fallback(samples_size);
    if (!samples) {
        ESP_LOGE(TAG, "Failed to allocate samples buffer (%d bytes)", samples_size);
        return false;
    }
    
    // Allocate temporary buffer in PSRAM
    temp_buffer = (uint8_t*)malloc_psram_fallback(temp_buffer_size);
    if (!temp_buffer) {
        ESP_LOGE(TAG, "Failed to allocate temp_buffer (%d bytes)", temp_buffer_size);
        free_psram_fallback(samples);
        samples = nullptr;
        return false;
    }
    
    // Clear buffers
    memset(samples, 0, samples_size);
    memset(temp_buffer, 0, temp_buffer_size);
    
    ESP_LOGI(TAG, "I2S buffers allocated successfully in PSRAM (total: %d bytes)", 
             samples_size + temp_buffer_size);
    
    return true;
}

static void deallocate_i2s_buffers() {
    ESP_LOGI(TAG, "Deallocating I2S buffers...");
    
    if (samples) {
        free_psram_fallback(samples);
        samples = nullptr;
    }
    
    if (temp_buffer) {
        free_psram_fallback(temp_buffer);
        temp_buffer = nullptr;
    }
    
    ESP_LOGI(TAG, "I2S buffers deallocated");
}

// High-priority task: reads samples from I2S
void i2s_read_task(void *param) {
  size_t bytes_read;
  const size_t temp_buffer_size = SAMPLES * 8;  // 2 channels × 4 bytes = 8 bytes/sample pair

  while (true) {
    // Non-blocking I2S read
    esp_err_t res = i2s_read(
      I2S_NUM_0,
      temp_buffer,
      temp_buffer_size,
      &bytes_read,
      0 // non-blocking
    );

    if (res == ESP_OK && bytes_read == temp_buffer_size) {
      if (xSemaphoreTake(buffer_mutex, 0)) {
        memcpy(samples, temp_buffer, temp_buffer_size);
        buffer_ready = true;
        xSemaphoreGive(buffer_mutex);
      }
    }

    taskYIELD();  // Prevent CPU hogging
  }
}

void i2s_init() {
  ESP_LOGI(TAG, "Initializing I2S...");
  
  // Allocate I2S buffers in PSRAM first
  if (!allocate_i2s_buffers()) {
    ESP_LOGE(TAG, "Failed to allocate I2S buffers - I2S initialization aborted");
    return;
  }
  
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
  if (!buffer_mutex) {
    ESP_LOGE(TAG, "Failed to create buffer mutex");
    deallocate_i2s_buffers();
    return;
  }

  xTaskCreatePinnedToCore(
    i2s_read_task,
    "I2S Read Task",
    I2S_TASK_STACK_SIZE,
    NULL,
    I2S_TASK_PRIORITY,
    NULL,
    0  // Pin to Core 0
  );
  
  ESP_LOGI(TAG, "I2S initialized successfully with PSRAM buffers");
}

void i2s_deinit() {
  ESP_LOGI(TAG, "Deinitializing I2S...");
  deallocate_i2s_buffers();
  if (buffer_mutex) {
    vSemaphoreDelete(buffer_mutex);
    buffer_mutex = nullptr;
  }
  ESP_LOGI(TAG, "I2S deinitialized");
}

// Thread-safe, non-blocking access to the latest samples
bool read_all_samples(uint32_t* dest, size_t length) {
  if (length < SAMPLES * 2 || !samples) return false;

  bool copied = false;
  if (buffer_ready && xSemaphoreTake(buffer_mutex, 0)) {
    memcpy(dest, samples, SAMPLES * 2 * sizeof(uint32_t));
    buffer_ready = false;
    xSemaphoreGive(buffer_mutex);
    copied = true;
  }
  return copied;
}
