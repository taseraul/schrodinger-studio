#include "bt_audio.hpp"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../core/memory_utils.hpp"
#include "circular_buffer.hpp"
#include "WiFi.h"

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
    LOG_AUDIO_W("BT callback %d: Invalid params - buffer:%p data:%p length:%d", 
                callback_count, bt_circular_buffer, data, length);
    return;
  }
  
  // Log first few callbacks and then periodically
  if (callback_count <= 5 || callback_count % 100 == 0) {
    LOG_AUDIO_I("BT callback %d: Received %d bytes", callback_count, length);
  }
  
  // Audio is playing (bt_monitor functionality removed)
  
  // Write samples to circular buffer (converts 16-bit to 32-bit internally)
  if (bt_circular_buffer->write_samples_16bit(data, length)) {
    total_samples_received += length / 4;  // Count sample pairs
    
    // Log periodically for debugging (every 1000 sample pairs)
    if (total_samples_received % 1000 == 0) {
      LOG_AUDIO_I("BT samples: %d pairs, buffer: %.1f%%, available: %d pairs", 
                  total_samples_received, bt_circular_buffer->get_utilization(),
                  bt_circular_buffer->get_sample_count());
    }
  } else {
    buffer_overflow_count++;
    LOG_AUDIO_W("BT buffer write failed %d times", buffer_overflow_count);
  }
}

bool readBtSamples(uint32_t* dest, size_t length) {
  static uint32_t read_attempts = 0;
  read_attempts++;
  
  const auto& config = CONFIG_MGR.getAudioConfig();
  uint16_t samples_needed = config.samples;
  
  if (!bt_circular_buffer || !dest || length < samples_needed * 2) {
    LOG_AUDIO_W("Read attempt %d: Invalid params - buffer:%p dest:%p length:%d", 
                read_attempts, bt_circular_buffer, dest, length);
    return false;
  }

  size_t available_samples = bt_circular_buffer->get_sample_count();
  
  // Log first few attempts and then periodically
  if (read_attempts <= 10 || read_attempts % 100 == 0) {
    LOG_AUDIO_I("Read attempt %d: Available samples: %d, need: %d", 
                read_attempts, available_samples, samples_needed);
  }

  // Read exactly samples_needed sample pairs from circular buffer
  bool success = bt_circular_buffer->read_samples_32bit(dest, samples_needed);
  
  if (success && (read_attempts <= 10 || read_attempts % 100 == 0)) {
    LOG_AUDIO_I("Read attempt %d: SUCCESS - Read %d sample pairs", read_attempts, samples_needed);
  } else if (!success && read_attempts % 50 == 0) {
    LOG_AUDIO_W("Read attempt %d: FAILED - Not enough samples (have: %d, need: %d)", 
                read_attempts, available_samples, samples_needed);
  }
  
  return success;
}

void bt_init() {
  LOG_AUDIO_I("Initializing Bluetooth A2DP Sink...");
  SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_INITIALIZING);
  
  // Get configuration
  const auto& config = CONFIG_MGR.getNetworkConfig();
  
  // Print memory before BT init
  uint32_t heapBefore = ESP.getFreeHeap();
  log_memory_status("BT Init Start");
  
  // Check if we have enough memory to initialize Bluetooth
  if (heapBefore < 80000) {
    LOG_AUDIO_W("Low memory before BT init (%d bytes). BT may fail.", heapBefore);
  }
  
  // CRITICAL: Temporarily disable WiFi to avoid coexistence conflicts during BT init
  LOG_AUDIO_I("Temporarily disabling WiFi for BT initialization...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);  // Allow WiFi to fully shut down
  
  // Initialize circular buffer for BT samples (16KB in PSRAM)
  bt_circular_buffer = new CircularBuffer(16384);  // 16KB
  if (!bt_circular_buffer || !bt_circular_buffer->init()) {
    LOG_AUDIO_E("Failed to initialize BT circular buffer");
    if (bt_circular_buffer) {
      delete bt_circular_buffer;
      bt_circular_buffer = nullptr;
    }
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_ERROR);
    return;
  }
  LOG_AUDIO_I("BT circular buffer initialized successfully");
  
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
  
  LOG_AUDIO_I("Starting A2DP sink with name: %s", config.bt_device_name);
  
  // Monitor memory during BT start
  uint32_t heapDuringStart = ESP.getFreeHeap();
  LOG_AUDIO_D("Heap during BT start: %d bytes", heapDuringStart);
  
  // Start the A2DP sink with error handling
  try {
    a2dp_sink.start(config.bt_device_name);
    LOG_AUDIO_I("A2DP sink started successfully");
  } catch (const std::exception& e) {
    log_error_with_context(LOG_TAG_AUDIO, "A2DP sink start failed", e.what());
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_ERROR);
    return;
  } catch (...) {
    log_error_with_context(LOG_TAG_AUDIO, "A2DP sink start failed", "Unknown exception");
    SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_ERROR);
    return;
  }
  
  // Print memory after BT init
  uint32_t heapAfter = ESP.getFreeHeap();
  log_memory_status("BT Init Complete");
  log_performance_metric("BT Initialization", millis() - heapBefore);
  
  if (heapAfter < 30000) {
    LOG_AUDIO_W("Very low memory after BT init. System may be unstable.");
  }
  
  // Re-enable WiFi after successful BT initialization
  LOG_AUDIO_I("Re-enabling WiFi after BT initialization...");
  WiFi.mode(WIFI_AP_STA);
  delay(500);  // Allow WiFi to initialize
  
  SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_RUNNING);
  LOG_AUDIO_I("Bluetooth A2DP Sink initialized successfully");
}

void bt_deinit() {
  LOG_AUDIO_I("Deinitializing Bluetooth A2DP Sink...");
  SYSTEM_MGR.setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_SHUTDOWN);
  
  // Stop A2DP sink
  a2dp_sink.end();
  
  // Clean up circular buffer
  if (bt_circular_buffer) {
    bt_circular_buffer->deinit();
    delete bt_circular_buffer;
    bt_circular_buffer = nullptr;
    LOG_AUDIO_I("BT circular buffer cleaned up");
  }
  
  // Reset counters
  total_samples_received = 0;
  buffer_overflow_count = 0;
  
  LOG_AUDIO_I("Bluetooth A2DP Sink deinitialized");
}

// Function to re-initialize WiFi connection after BT is started
void bt_post_init_wifi_restore() {
  LOG_AUDIO_I("Restoring WiFi connection after BT initialization...");
  
  // Get WiFi credentials from config
  const auto& config = CONFIG_MGR.getNetworkConfig();
  
  // Re-establish WiFi connection
  WiFi.begin(config.wifi_ssid, config.wifi_password);
  
  // Wait for connection with timeout
  int retryCount = 10;
  while (WiFi.status() != WL_CONNECTED && retryCount--) {
    delay(1000);
    LOG_AUDIO_D("Reconnecting to WiFi... (%d attempts left)", retryCount);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    LOG_AUDIO_W("Failed to reconnect to WiFi, starting AP mode");
    WiFi.softAP(config.ap_ssid);
  } else {
    LOG_AUDIO_I("WiFi reconnected successfully");
  }
}
