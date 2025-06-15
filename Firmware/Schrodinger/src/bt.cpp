#include "bt.hpp"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

NumberFormatConverterStream scaledBt;
BluetoothA2DPSink a2dp_sink(scaledBt);

#define BT_TASK_STACK_SIZE 8192
#define BT_TASK_PRIORITY   10

static uint32_t samples[SAMPLES * 2];   // For writing from I2S

// Mutex for protecting buffers
static SemaphoreHandle_t buffer_mutex;

// Flag to indicate new data is ready
static volatile bool buffer_ready = false;

void receiveBtSamples(const uint8_t* data, uint32_t length) 
{
  ESP_LOGI("bt","Samples read %d",length);
  // if (xSemaphoreTake(buffer_mutex, 0)) {
    memcpy(samples,data,length);      
    buffer_ready = true;
  //   xSemaphoreGive(buffer_mutex);
  // }
}

bool readBtSamples(uint32_t* dest, size_t length){
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

// // High-priority task: reads samples from BT
// void bt_task(void *param) {
//   size_t bytes_read;
//   uint8_t temp_buffer[SAMPLES * 8];  // 2 channels × 4 bytes = 8 bytes/sample pair

//   while (true) {
//     size_t res = scaledBt.readBytes(temp_buffer, sizeof(temp_buffer));

//     if (res == sizeof(temp_buffer)) {
//       if (xSemaphoreTake(buffer_mutex, 0)) {
//         memcpy(samples, temp_buffer, sizeof(temp_buffer));
//         buffer_ready = true;
//         xSemaphoreGive(buffer_mutex);
//         ESP_LOGI("bt","Samples read");
//       }
//     }

//     vTaskDelay(pdMS_TO_TICKS(2));
//   }
// }

void bt_init() {

  // i2s_pin_config_t my_pin_config = {
  //     .bck_io_num   = 14,
  //     .ws_io_num    = 15,
  //     .data_out_num = 32,
  //     .data_in_num  = I2S_PIN_NO_CHANGE};

  // static i2s_config_t i2s_config = {
  //     .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  //     .sample_rate          = 44100,     // updated automatically by A2DP
  //     .bits_per_sample      = (i2s_bits_per_sample_t)32,
  //     .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
  //     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
  //     .intr_alloc_flags     = 0,     // default interrupt priority
  //     .dma_buf_count        = 8,
  //     .dma_buf_len          = 64,
  //     .use_apll             = true,
  //     .tx_desc_auto_clear   = true     // avoiding noise in case of data unavailability
  // };
  // a2dp_sink.set_i2s_config(i2s_config);
  // a2dp_sink.set_pin_config(my_pin_config);
  buffer_mutex = xSemaphoreCreateMutex();
  if (!buffer_mutex) {
    Serial.println("Buffer mutex creation failed");
    return;
  }
  a2dp_sink.set_stream_reader(receiveBtSamples,false);

  // scaledBt.begin(16,32);
  a2dp_sink.start("Schrodinger2");

  // xTaskCreatePinnedToCore(
  //   bt_task,
  //   "BT Read Task",
  //   BT_TASK_STACK_SIZE,
  //   NULL,
  //   BT_TASK_PRIORITY,
  //   NULL,
  //   0  // Pin to Core 0
  // );
}