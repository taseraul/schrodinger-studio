#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "esp_dsp.h"
#include "esp_timer.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"
#include "config.hpp"
#include "i2s.hpp"
#include "bt.hpp"
#include "webserver.hpp"
#include "memory_manager.hpp"
#include "Arduino.h"
#include "driver/i2s.h"

// #define READ_SAMPLES read_all_samples
#define READ_SAMPLES readBtSamples

#define MAX_FREQ 10000
#define MAX_BIN ((MAX_FREQ * SAMPLES) / SAMPLE_RATE)

// Time constant for smoothing in seconds
#define SMOOTH_TIME_SEC 10.0f

// Derived alpha for exponential smoothing per FFT frame (assuming ~100 fps)
#define FFT_FPS 100.0f
#define ALPHA (1.0f - expf(-1.0f / (SMOOTH_TIME_SEC * FFT_FPS)))

typedef struct {
  int index;
  float magnitude;
} peak_t;

// PSRAM-allocated buffers for FFT processing
static uint32_t* samples_copy = nullptr;
static float* fft_input = nullptr;     // ESP-DSP requires interleaved real/imag format
static float* fft_output = nullptr;    // Magnitude output
static float* temp_real = nullptr;     // PSRAM allocation to save internal RAM

// ESP-DSP FFT initialization
static bool esp_dsp_initialized = false;

static SemaphoreHandle_t config_mutex = NULL;
static int num_highest = 6;
static int min_width = 10;

static float smoothed_max_mag = 1e-6f; // initialize to small positive number

// WebSocket management
static uint32_t websocket_send_count = 0;
static uint32_t websocket_send_failures = 0;
static uint32_t last_websocket_health_check = 0;

// Buffer allocation and deallocation functions
static bool allocate_fft_buffers() {
    ESP_LOGI("fft", "Allocating FFT buffers in PSRAM...");
    
    // Calculate buffer sizes
    size_t samples_size = SAMPLES * 2 * sizeof(uint32_t);  // ~4KB
    size_t fft_input_size = SAMPLES * 2 * sizeof(float);   // ~4KB
    size_t fft_output_size = SAMPLES * sizeof(float);      // ~2KB
    size_t temp_real_size = SAMPLES * sizeof(float);       // ~2KB
    
    ESP_LOGI("fft", "Buffer sizes: samples=%d, input=%d, output=%d, temp=%d bytes", 
             samples_size, fft_input_size, fft_output_size, temp_real_size);
    
    // Allocate samples_copy buffer in PSRAM
    samples_copy = (uint32_t*)malloc_psram_fallback(samples_size);
    if (!samples_copy) {
        ESP_LOGE("fft", "Failed to allocate samples_copy buffer (%d bytes)", samples_size);
        return false;
    }
    
    // Allocate fft_input buffer in PSRAM
    fft_input = (float*)malloc_psram_fallback(fft_input_size);
    if (!fft_input) {
        ESP_LOGE("fft", "Failed to allocate fft_input buffer (%d bytes)", fft_input_size);
        free_psram_fallback(samples_copy);
        samples_copy = nullptr;
        return false;
    }
    
    // Allocate fft_output buffer in PSRAM
    fft_output = (float*)malloc_psram_fallback(fft_output_size);
    if (!fft_output) {
        ESP_LOGE("fft", "Failed to allocate fft_output buffer (%d bytes)", fft_output_size);
        free_psram_fallback(samples_copy);
        free_psram_fallback(fft_input);
        samples_copy = nullptr;
        fft_input = nullptr;
        return false;
    }
    
    // Allocate temp_real buffer in PSRAM
    temp_real = (float*)malloc_psram_fallback(temp_real_size);
    if (!temp_real) {
        ESP_LOGE("fft", "Failed to allocate temp_real buffer (%d bytes)", temp_real_size);
        free_psram_fallback(samples_copy);
        free_psram_fallback(fft_input);
        free_psram_fallback(fft_output);
        samples_copy = nullptr;
        fft_input = nullptr;
        fft_output = nullptr;
        return false;
    }
    
    // Clear all buffers
    memset(samples_copy, 0, samples_size);
    memset(fft_input, 0, fft_input_size);
    memset(fft_output, 0, fft_output_size);
    memset(temp_real, 0, temp_real_size);
    
    ESP_LOGI("fft", "FFT buffers allocated successfully in PSRAM (total: %d bytes)", 
             samples_size + fft_input_size + fft_output_size + temp_real_size);
    
    return true;
}

static void deallocate_fft_buffers() {
    ESP_LOGI("fft", "Deallocating FFT buffers...");
    
    if (samples_copy) {
        free_psram_fallback(samples_copy);
        samples_copy = nullptr;
    }
    
    if (fft_input) {
        free_psram_fallback(fft_input);
        fft_input = nullptr;
    }
    
    if (fft_output) {
        free_psram_fallback(fft_output);
        fft_output = nullptr;
    }
    
    if (temp_real) {
        free_psram_fallback(temp_real);
        temp_real = nullptr;
    }
    
    ESP_LOGI("fft", "FFT buffers deallocated");
}

// Call this every FFT frame with the current magnitudes array and length
// It returns the normalized magnitudes scaled by gain
static void normalize_magnitudes(peak_t *mags, size_t length) {
    // 1) Find max magnitude in this frame
    float max_mag = mags[0].magnitude;
    // 2) Update smoothed max magnitude (exponential moving max)
    if (max_mag > smoothed_max_mag) {
        // New peak: approach max quickly
        smoothed_max_mag = max_mag;
    } else {
        // Decay slowly over time
        smoothed_max_mag = ALPHA * smoothed_max_mag + (1.0f - ALPHA) * max_mag;
    }

    // 3) Calculate gain = 1 / smoothed_max_mag, clamp to avoid huge gains
    const float MIN_GAIN = 0.1f;
    const float MAX_GAIN = 10.0f;
    float gain = 1.0f / (smoothed_max_mag + 1e-9f);
    gain = std::min(std::max(gain, MIN_GAIN), MAX_GAIN);

    // 4) Apply gain and clamp output to [0..1]
    for (int i = 0; i < length; i++) {
        float scaled = mags[i].magnitude * gain;
        if (scaled > 1.0f) scaled = 1.0f;
        if (scaled < 0.0f) scaled = 0.0f;
        mags[i].magnitude = scaled;
    }
}

static int peak_index_compare(const void *a, const void *b) {
    const peak_t *pa = (const peak_t *)a;
    const peak_t *pb = (const peak_t *)b;
    return (pa->index > pb->index) - (pa->index < pb->index);
}

static void sort_peaks_by_index(peak_t *arr, size_t len) {
    qsort(arr, len, sizeof(peak_t), peak_index_compare);
}

static int cmp_peak(const void *a, const void *b) {
  float diff = ((peak_t *)b)->magnitude - ((peak_t *)a)->magnitude;
  return (diff > 0) ? 1 : (diff < 0) ? -1 : 0;
}

static void convert_to_mono() {
  for (int i = 0; i < SAMPLES; i++) {
    int32_t left = (int32_t)samples_copy[i * 2];
    int32_t right = (int32_t)samples_copy[i * 2 + 1];
    float left_f = left / 2147483648.0f;
    float right_f = right / 2147483648.0f;
    // ESP-DSP uses interleaved format: real, imag, real, imag...
    fft_input[i * 2] = (left_f + right_f) * 0.5f;  // Real part
    fft_input[i * 2 + 1] = 0.0f;                   // Imaginary part
  }
}

static void fft_task(void *param) {
  ESP_LOGI("fft", "FFT task started");
  uint32_t successful_ffts = 0;
  
  // Memory monitoring variables
  uint32_t last_memory_check = 0;
  uint32_t initial_heap = ESP.getFreeHeap();
  
  // Adaptive WebSocket batching - adjust rate based on client health
  uint32_t last_websocket_send = 0;
  uint32_t websocket_interval_ms = 167; // Start at 6Hz, adapt based on client health
  const uint32_t MIN_WEBSOCKET_INTERVAL_MS = 100; // Max 10Hz
  const uint32_t MAX_WEBSOCKET_INTERVAL_MS = 500; // Min 2Hz
  int batch_size = 5; // Adaptive batch size
  
  // Use PSRAM-allocated buffer for WebSocket batching (binary format)
  const size_t BATCH_BUFFER_SIZE = 2048;
  uint8_t* fft_batch = (uint8_t*)malloc_psram_fallback(BATCH_BUFFER_SIZE);
  if (!fft_batch) {
    ESP_LOGE("fft", "Failed to allocate WebSocket batch buffer");
    return;
  }
  memset(fft_batch, 0, BATCH_BUFFER_SIZE);
  int batch_count = 0;
  
  // WebSocket health monitoring
  uint32_t websocket_sends_attempted = 0;
  uint32_t websocket_sends_skipped = 0;
  
  size_t bytesRead;

  while (true) {
    // if (READ_SAMPLES(samples_copy, SAMPLES * 2)) {
    // if (false){
    if (i2s_read(I2S_NUM_0, samples_copy, SAMPLES * 2, &bytesRead, portMAX_DELAY) == ESP_OK) {
      // ESP_LOGI("fft", "FFT : Processing samples");
      successful_ffts++;
      
      // Minimal logging for first few FFTs
      if (successful_ffts <= 3) {
        ESP_LOGI("fft", "FFT %d: Processing samples", successful_ffts);
      }
      
      // Convert to mono
      convert_to_mono();

      // FFT Compute (ESP-DSP)
      dsps_fft2r_fc32(fft_input, SAMPLES);
      dsps_bit_rev_fc32(fft_input, SAMPLES);

      // Complex to Magnitude
      for (int i = 0; i < SAMPLES/2; i++) {
        float real = fft_input[i * 2];
        float imag = fft_input[i * 2 + 1];
        fft_output[i] = sqrtf(real * real + imag * imag);
      }

      // Peak preparation (skip DC bin 0)
      peak_t peaks[MAX_BIN - 1];  // Exclude bin 0 (DC)
      for (int i = 1; i < MAX_BIN; i++) {  // Start from bin 1, skip DC
        peaks[i-1].index = i;
        peaks[i-1].magnitude = fft_output[i];
      }

      // Peak sorting
      qsort(peaks, MAX_BIN - 1, sizeof(peak_t), cmp_peak);  // Exclude DC bin

      int current_num_highest;
      int current_min_width;

      // Lock config to read current values
      if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(10))) {
        current_num_highest = num_highest;
        current_min_width = min_width;
        xSemaphoreGive(config_mutex);
      } else {
        // fallback to defaults if mutex not available
        current_num_highest = 6;
        current_min_width = 10;
      }

      // Peak selection
      peak_t selected[current_num_highest];
      int count = 0;

      memset(selected,0, sizeof(selected));

      for (int i = 0; i < MAX_BIN && count < current_num_highest; i++) {
        int candidate = peaks[i].index;
        bool too_close = false;
        for (int j = 0; j < count; j++) {
          if (abs(selected[j].index - candidate) < current_min_width) {
            too_close = true;
            break;
          }
        }
        if (!too_close) {
          selected[count++] = peaks[i];
        }
      }

      // Normalization
      normalize_magnitudes(selected,current_num_highest);

      // Final sorting
      sort_peaks_by_index(selected,current_num_highest);

      // Binary WebSocket protocol - highly optimized
      // if (count > 0) {
      //   // Binary format: [frame_count][frame1_data][frame2_data]...
      //   // Each frame: [peak_count][bin1][mag1][bin2][mag2]...
      //   // bin: uint8_t (0-115), mag: uint8_t (0-255 scaled)
        
      //   // Calculate required buffer size for this frame
      //   size_t frame_size = 1 + (count * 2); // 1 byte count + 2 bytes per peak
        
      //   // Check if we have space in batch buffer
      //   if (batch_count == 0) {
      //     // First frame - write frame count placeholder and first frame
      //     fft_batch[0] = 1; // Will be updated when batch is complete
      //     fft_batch[1] = (uint8_t)count; // Peak count for this frame
          
      //     // Write peak data
      //     for (int i = 0; i < count; i++) {
      //       fft_batch[2 + i * 2] = (uint8_t)selected[i].index; // Bin index (0-115)
      //       fft_batch[3 + i * 2] = (uint8_t)(selected[i].magnitude * 255.0f); // Magnitude (0-255)
      //     }
      //     batch_count = 1;
      //   } else {
      //     // Additional frame - append to batch
      //     size_t current_batch_size = 1; // Frame count byte
          
      //     // Calculate current batch size
      //     for (int f = 0; f < batch_count; f++) {
      //       size_t frame_offset = 1; // Skip frame count
      //       for (int prev_f = 0; prev_f < f; prev_f++) {
      //         uint8_t prev_peak_count = fft_batch[frame_offset];
      //         frame_offset += 1 + (prev_peak_count * 2);
      //       }
      //       uint8_t peak_count = fft_batch[frame_offset];
      //       current_batch_size += 1 + (peak_count * 2);
      //     }
          
      //     // Check if new frame fits
      //     if (current_batch_size + frame_size < BATCH_BUFFER_SIZE) {
      //       // Append new frame
      //       fft_batch[current_batch_size] = (uint8_t)count;
      //       for (int i = 0; i < count; i++) {
      //         fft_batch[current_batch_size + 1 + i * 2] = (uint8_t)selected[i].index;
      //         fft_batch[current_batch_size + 2 + i * 2] = (uint8_t)(selected[i].magnitude * 255.0f);
      //       }
      //       batch_count++;
      //       fft_batch[0] = (uint8_t)batch_count; // Update frame count
      //     } else {
      //       // Buffer full, send current batch and start new one
      //       sendBinaryBatch(fft_batch, current_batch_size);
      //       websocket_sends_attempted++;
      //       last_websocket_send = millis();
            
      //       // Start new batch with current frame
      //       fft_batch[0] = 1;
      //       fft_batch[1] = (uint8_t)count;
      //       for (int i = 0; i < count; i++) {
      //         fft_batch[2 + i * 2] = (uint8_t)selected[i].index;
      //         fft_batch[3 + i * 2] = (uint8_t)(selected[i].magnitude * 255.0f);
      //       }
      //       batch_count = 1;
      //     }
      //   }
        
      //   // Send batch when we have enough cycles or enough time has passed
      //   uint32_t current_time = millis();
      //   if (batch_count >= batch_size || (current_time - last_websocket_send >= websocket_interval_ms)) {
      //     // Calculate final batch size
      //     size_t final_batch_size = 1; // Frame count byte
      //     for (int f = 0; f < batch_count; f++) {
      //       size_t frame_offset = 1;
      //       for (int prev_f = 0; prev_f < f; prev_f++) {
      //         uint8_t prev_peak_count = fft_batch[frame_offset];
      //         frame_offset += 1 + (prev_peak_count * 2);
      //       }
      //       uint8_t peak_count = fft_batch[frame_offset];
      //       final_batch_size += 1 + (peak_count * 2);
      //     }
          
      //     sendBinaryBatch(fft_batch, final_batch_size);
      //     websocket_sends_attempted++;
      //     last_websocket_send = current_time;
          
      //     // Reset batch
      //     batch_count = 0;
      //   }
      // }

      // Memory monitoring every 5 seconds
      uint32_t current_time = millis();
      if (current_time - last_memory_check >= 5000) {
        uint32_t current_heap = ESP.getFreeHeap();
        int32_t heap_change = (int32_t)current_heap - (int32_t)initial_heap;
        
        
        last_memory_check = current_time;
        
        // WebSocket health monitoring
        if (websocket_sends_attempted > 0) {
          float success_rate = (float)(websocket_sends_attempted - websocket_sends_skipped) / websocket_sends_attempted * 100.0f;
          ESP_LOGI("fft", "WebSocket: %d attempted, %d skipped, %.1f%% success", 
                   websocket_sends_attempted, websocket_sends_skipped, success_rate);
        }
        
        // Reset counters
        websocket_sends_attempted = 0;
        websocket_sends_skipped = 0;
        
        // Memory leak warning
        if (heap_change < -10000) {
          ESP_LOGW("fft", "MEMORY LEAK: Heap dropped %d bytes", -heap_change);
        }
      }
      
    } else {
      // Minimal logging when no samples available
      uint32_t current_time = millis();
      if (successful_ffts == 0 && current_time % 5000 < 100) {
        ESP_LOGW("fft", "No BT samples available for FFT");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(16));
  }
  
  // Cleanup WebSocket batch buffer
  if (fft_batch) {
    free_psram_fallback(fft_batch);
  }
}

void fft_task_init() {
  ESP_LOGI("fft", "Initializing FFT task...");
  
  // Allocate FFT buffers in PSRAM first
  if (!allocate_fft_buffers()) {
    ESP_LOGE("fft", "Failed to allocate FFT buffers - FFT task initialization aborted");
    return;
  }

  config_mutex = xSemaphoreCreateMutex();
  if (!config_mutex) {
    ESP_LOGE("fft", "Config mutex creation failed");
    deallocate_fft_buffers();
    return;
  }

  // Initialize ESP-DSP
  esp_err_t ret = dsps_fft2r_init_fc32(NULL, SAMPLES);
  if (ret != ESP_OK) {
    ESP_LOGE("fft", "ESP-DSP FFT initialization failed: %s", esp_err_to_name(ret));
    deallocate_fft_buffers();
    return;
  }
  ESP_LOGI("fft", "ESP-DSP FFT initialized successfully for %d samples", SAMPLES);

  // Initialize config values safely
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(100))) {
    num_highest = 6;
    min_width = 10;
    xSemaphoreGive(config_mutex);
  }

  xTaskCreate(fft_task, "fft_task", 8192, NULL, 5, NULL);
  ESP_LOGI("fft", "FFT task initialized successfully with PSRAM buffers");
}

void fft_task_deinit() {
  ESP_LOGI("fft", "Deinitializing FFT task...");
  deallocate_fft_buffers();
  ESP_LOGI("fft", "FFT task deinitialized");
}

// Thread-safe setters/getters

bool fft_set_num_highest(int value) {
  if (value <= 0) return false;
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(50))) {
    num_highest = value;
    xSemaphoreGive(config_mutex);
    return true;
  }
  return false;
}

bool fft_set_min_width(int value) {
  if (value < 0) return false;
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(50))) {
    min_width = value;
    xSemaphoreGive(config_mutex);
    return true;
  }
  return false;
}

int fft_get_num_highest() {
  int value = 5;
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(50))) {
    value = num_highest;
    xSemaphoreGive(config_mutex);
  }
  return value;
}

int fft_get_min_width() {
  int value = 3;
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(50))) {
    value = min_width;
    xSemaphoreGive(config_mutex);
  }
  return value;
}
