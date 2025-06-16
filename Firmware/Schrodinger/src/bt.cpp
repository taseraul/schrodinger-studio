#include "bt.hpp"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bt_monitor.hpp"
#include "esp_log.h"
#include "circular_buffer.hpp"

static const char* TAG = "bt";

NumberFormatConverterStream scaledBt;
BluetoothA2DPSink a2dp_sink(scaledBt);

#define BT_TASK_STACK_SIZE 8192
#define BT_TASK_PRIORITY   10

// Circular buffer for BT samples (16KB in PSRAM)
static CircularBuffer* bt_circular_buffer = nullptr;

// Debug counters
static uint32_t total_samples_received = 0;
static uint32_t buffer_overflow_count = 0;

void receiveBtSamples(const uint8_t* data, uint32_t length) 
{
  static uint32_t callback_count = 0;
  callback_count++;
  
  if (!bt_circular_buffer || !data || length == 0) {
    ESP_LOGW(TAG, "BT callback %d: Invalid params - buffer:%p data:%p length:%d", 
             callback_count, bt_circular_buffer, data, length);
    return;
  }
  
  // Log first few callbacks and then periodically
  if (callback_count <= 5 || callback_count % 100 == 0) {
    ESP_LOGI(TAG, "BT callback %d: Received %d bytes", callback_count, length);
  }
  
  // Update BT state to indicate audio is playing
  bt_state_changed(BT_AUDIO_PLAYING);
  
  // Write samples to circular buffer (converts 16-bit to 32-bit internally)
  if (bt_circular_buffer->write_samples_16bit(data, length)) {
    total_samples_received += length / 4;  // Count sample pairs
    
    // Log periodically for debugging (every 1000 sample pairs)
    if (total_samples_received % 1000 == 0) {
      ESP_LOGI(TAG, "BT samples: %d pairs, buffer: %.1f%%, available: %d pairs", 
               total_samples_received, bt_circular_buffer->get_utilization(),
               bt_circular_buffer->get_sample_count());
    }
  } else {
    buffer_overflow_count++;
    ESP_LOGW(TAG, "BT buffer write failed %d times", buffer_overflow_count);
  }
}

bool readBtSamples(uint32_t* dest, size_t length) {
  static uint32_t read_attempts = 0;
  read_attempts++;
  
  if (!bt_circular_buffer || !dest || length < SAMPLES * 2) {
    ESP_LOGW(TAG, "Read attempt %d: Invalid params - buffer:%p dest:%p length:%d", 
             read_attempts, bt_circular_buffer, dest, length);
    return false;
  }

  size_t available_samples = bt_circular_buffer->get_sample_count();
  
  // Log first few attempts and then periodically
  if (read_attempts <= 10 || read_attempts % 100 == 0) {
    ESP_LOGI(TAG, "Read attempt %d: Available samples: %d, need: %d", 
             read_attempts, available_samples, SAMPLES);
  }

  // Read exactly SAMPLES sample pairs from circular buffer
  bool success = bt_circular_buffer->read_samples_32bit(dest, SAMPLES);
  
  if (success && (read_attempts <= 10 || read_attempts % 100 == 0)) {
    ESP_LOGI(TAG, "Read attempt %d: SUCCESS - Read %d sample pairs", read_attempts, SAMPLES);
  } else if (!success && read_attempts % 50 == 0) {
    ESP_LOGW(TAG, "Read attempt %d: FAILED - Not enough samples (have: %d, need: %d)", 
             read_attempts, available_samples, SAMPLES);
  }
  
  return success;
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
  ESP_LOGI(TAG, "Initializing Bluetooth A2DP Sink...");
  
  // Print memory before BT init
  uint32_t heapBefore = ESP.getFreeHeap();
  ESP_LOGI(TAG, "Free heap before BT init: %d bytes", heapBefore);
  
  // Check if we have enough memory to initialize Bluetooth
  if (heapBefore < 80000) {
    ESP_LOGW(TAG, "Low memory before BT init (%d bytes). BT may fail.", heapBefore);
  }
  
  // CRITICAL: Temporarily disable WiFi to avoid coexistence conflicts during BT init
  ESP_LOGI(TAG, "Temporarily disabling WiFi for BT initialization...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);  // Allow WiFi to fully shut down
  
  // Initialize circular buffer for BT samples (16KB in PSRAM)
  bt_circular_buffer = new CircularBuffer(16384);  // 16KB
  if (!bt_circular_buffer || !bt_circular_buffer->init()) {
    ESP_LOGE(TAG, "Failed to initialize BT circular buffer");
    if (bt_circular_buffer) {
      delete bt_circular_buffer;
      bt_circular_buffer = nullptr;
    }
    return;
  }
  ESP_LOGI(TAG, "BT circular buffer initialized successfully");
  
  // Configure I2S settings BEFORE starting A2DP to prevent conflicts
  // Use different pins than the existing I2S RX configuration
  i2s_pin_config_t my_pin_config = {
      .bck_io_num   = 26,  // Changed from 14 to avoid conflict
      .ws_io_num    = 25,  // Changed from 15 to avoid conflict  
      .data_out_num = 32,
      .data_in_num  = I2S_PIN_NO_CHANGE
  };

  i2s_config_t i2s_config = {
      .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate          = 44100,
      .bits_per_sample      = (i2s_bits_per_sample_t)16,  // Changed from 32 to 16 to save memory
      .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags     = 0,
      .dma_buf_count        = 4,  // Further reduced to save memory
      .dma_buf_len          = 32, // Further reduced to save memory
      .use_apll             = false,  // Disabled to save memory
      .tx_desc_auto_clear   = true
  };
  
  // Set I2S configuration before starting A2DP
  a2dp_sink.set_i2s_config(i2s_config);
  a2dp_sink.set_pin_config(my_pin_config);
  
  // Set up the stream reader callback
  a2dp_sink.set_stream_reader(receiveBtSamples, false);
  
  // Configure A2DP with optimized settings for memory usage
  a2dp_sink.set_auto_reconnect(false); // Disable auto-reconnect to save resources
  
  ESP_LOGI(TAG, "Starting A2DP sink with name: Schrodinger2");
  
  // Monitor memory during BT start
  uint32_t heapDuringStart = ESP.getFreeHeap();
  ESP_LOGI(TAG, "Heap during BT start: %d bytes", heapDuringStart);
  
  // Start the A2DP sink with error handling
  try {
    a2dp_sink.start("Schrodinger2");
    ESP_LOGI(TAG, "A2DP sink started successfully");
  } catch (const std::exception& e) {
    ESP_LOGE(TAG, "A2DP sink start failed: %s", e.what());
    return;
  } catch (...) {
    ESP_LOGE(TAG, "A2DP sink start failed with unknown exception");
    return;
  }
  
  // Print memory after BT init
  uint32_t heapAfter = ESP.getFreeHeap();
  ESP_LOGI(TAG, "Free heap after BT init: %d bytes", heapAfter);
  ESP_LOGI(TAG, "BT initialization consumed: %d bytes", heapBefore - heapAfter);
  
  if (heapAfter < 30000) {
    ESP_LOGW(TAG, "Very low memory after BT init. System may be unstable.");
  }
  
  // Re-enable WiFi after successful BT initialization
  ESP_LOGI(TAG, "Re-enabling WiFi after BT initialization...");
  WiFi.mode(WIFI_AP_STA);
  delay(500);  // Allow WiFi to initialize
  
  ESP_LOGI(TAG, "Bluetooth A2DP Sink initialized successfully");
}

void bt_deinit() {
  ESP_LOGI(TAG, "Deinitializing Bluetooth A2DP Sink...");
  
  // Stop A2DP sink
  a2dp_sink.end();
  
  // Clean up circular buffer
  if (bt_circular_buffer) {
    bt_circular_buffer->deinit();
    delete bt_circular_buffer;
    bt_circular_buffer = nullptr;
    ESP_LOGI(TAG, "BT circular buffer cleaned up");
  }
  
  // Reset counters
  total_samples_received = 0;
  buffer_overflow_count = 0;
  
  ESP_LOGI(TAG, "Bluetooth A2DP Sink deinitialized");
}

// Function to re-initialize WiFi connection after BT is started
void bt_post_init_wifi_restore() {
  ESP_LOGI(TAG, "Restoring WiFi connection after BT initialization...");
  
  // Re-establish WiFi connection
  WiFi.begin("Raul", "armaghedon");
  
  // Wait for connection with timeout
  int retryCount = 10;
  while (WiFi.status() != WL_CONNECTED && retryCount--) {
    delay(1000);
    ESP_LOGI(TAG, "Reconnecting to WiFi... (%d attempts left)", retryCount);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGW(TAG, "Failed to reconnect to WiFi, starting AP mode");
    WiFi.softAP("Schrodinger");
  } else {
    ESP_LOGI(TAG, "WiFi reconnected successfully");
  }
}
