#include "bt.hpp"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* TAG = "bt";

char *btName = "Schrodinger_Test";

BluetoothA2DPSink a2dp_sink;

#define BT_TASK_STACK_SIZE 8192
#define BT_TASK_PRIORITY   10

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
  
  // Configure I2S settings BEFORE starting A2DP to prevent conflicts
  // Use different pins than the existing I2S RX configuration
  i2s_pin_config_t my_pin_config = {
      // .mck_io_num   = 0,
      .bck_io_num   = 14,
      .ws_io_num    = 13,  
      .data_out_num = 15,
      .data_in_num  = 4,
  };

  i2s_config_t i2s_config = {
      .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate          = 44100,
      .bits_per_sample      = (i2s_bits_per_sample_t)32,  // Changed from 32 to 16 to save memory
      .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags     = 0,
      .dma_buf_count        = 10,
      .dma_buf_len          = 64,
      .use_apll             = true,  // Disabled to save memory
      .tx_desc_auto_clear   = true
  };
  
  // Set I2S configuration before starting A2DP
  a2dp_sink.set_i2s_config(i2s_config);
  a2dp_sink.set_pin_config(my_pin_config);
  a2dp_sink.set_i2s_port(I2S_NUM_0);
  
  // Set up the stream reader callback
  // a2dp_sink.set_stream_reader(receiveBtSamples, true);
  // a2dp_sink.set_i2s_active(true);
  
  // Configure A2DP with optimized settings for memory usage
  // a2dp_sink.set_auto_reconnect(true); // Disable auto-reconnect to save resources
  
  ESP_LOGI(TAG, "Starting A2DP sink with name: Schrodinger2");
  
  // Monitor memory during BT start
  uint32_t heapDuringStart = ESP.getFreeHeap();
  ESP_LOGI(TAG, "Heap during BT start: %d bytes", heapDuringStart);
  
  // Start the A2DP sink with error handling
  try {
    a2dp_sink.start("Schrodinger2",true);
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
