#include "bt.hpp"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bt_monitor.hpp"
#include "circular_buffer.hpp"
#include "esp_log.h"

static const char* TAG = "bt";

NumberFormatConverterStream scaledBt;
BluetoothA2DPSink a2dp_sink(scaledBt);

#define BT_TASK_STACK_SIZE 8192
#define BT_TASK_PRIORITY   10

// Dynamic buffer for sample conversion - allocate based on incoming data size
#define MAX_BT_SAMPLES 2048  // Maximum samples we can handle per callback

void receiveBtSamples(const uint8_t* data, uint32_t length) 
{
  // Safety checks
  if (!data || length == 0) {
    ESP_LOGW(TAG, "Invalid data received: data=%p, length=%d", data, length);
    return;
  }
  
  ESP_LOGD(TAG, "BT samples received: %d bytes", length);
  
  // Update BT state to indicate audio is playing
  bt_state_changed(BT_AUDIO_PLAYING);
  
  // Determine sample format and convert to 32-bit
  size_t sample_count = 0;
  
  // Assume 16-bit stereo samples (most common A2DP format)
  if (length % 4 == 0) { // 16-bit stereo: 2 bytes per channel * 2 channels = 4 bytes per sample pair
    sample_count = length / 4;
    
    // Handle dynamic input size - allocate temporary buffer as needed
    if (sample_count > MAX_BT_SAMPLES) {
      ESP_LOGW(TAG, "Sample count very large: %d, limiting to %d", sample_count, MAX_BT_SAMPLES);
      sample_count = MAX_BT_SAMPLES;
    }
    
    // Allocate temporary buffer for this batch
    uint32_t* temp_samples = (uint32_t*)malloc(sample_count * 2 * sizeof(uint32_t));
    if (!temp_samples) {
      ESP_LOGE(TAG, "Failed to allocate temp buffer for %d samples", sample_count * 2);
      return;
    }
    
    const int16_t* samples_16 = (const int16_t*)data;
    
    // Convert 16-bit samples to 32-bit and store in temp buffer
    for (size_t i = 0; i < sample_count * 2; i++) {
      // Scale 16-bit to 32-bit range
      int32_t sample_32 = (int32_t)samples_16[i] * 65536;
      temp_samples[i] = (uint32_t)sample_32;
    }
    
    // Write to circular buffer
    size_t written = bt_circular_buffer.write(temp_samples, sample_count * 2);
    if (written < sample_count * 2) {
      ESP_LOGD(TAG, "Circular buffer partial write: %d/%d samples", written, sample_count * 2);
    }
    
    // Free temporary buffer
    free(temp_samples);
  } else {
    ESP_LOGW(TAG, "Unexpected sample format, length: %d", length);
  }
}

bool readBtSamples(uint32_t* dest, size_t length) {
  ESP_LOGD(TAG, "readBtSamples called, length=%d, required=%d", length, SAMPLES * 2);
  
  if (length < SAMPLES * 2) {
    ESP_LOGW(TAG, "readBtSamples: length too small %d < %d", length, SAMPLES * 2);
    return false;
  }
  
  // Check buffer status before reading
  size_t available = bt_circular_buffer.available();
  ESP_LOGD(TAG, "Buffer has %d samples available", available);
  
  // Try to read exactly SAMPLES * 2 samples from circular buffer
  size_t read_count = bt_circular_buffer.read(dest, SAMPLES * 2);
  
  ESP_LOGD(TAG, "Read %d samples from buffer", read_count);
  
  if (read_count == SAMPLES * 2) {
    ESP_LOGD(TAG, "Full read successful: %d samples", read_count);
    return true;
  } else if (read_count > 0) {
    // Partial read - pad with zeros
    memset(&dest[read_count], 0, (SAMPLES * 2 - read_count) * sizeof(uint32_t));
    ESP_LOGD(TAG, "Partial read: %d/%d samples, padded with zeros", read_count, SAMPLES * 2);
    return true;
  }
  
  ESP_LOGD(TAG, "No samples available for reading");
  return false; // No samples available
}

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
  
  // Initialize circular buffer for BT samples
  // Use 16384 samples (32x FFT size) for better buffering capacity
  if (!bt_circular_buffer.init(16384)) {
    ESP_LOGE(TAG, "Failed to initialize circular buffer");
    return;
  }
  
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
