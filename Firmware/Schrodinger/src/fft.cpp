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
#include "Arduino.h"

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

typedef struct {
    int length;      // Number of magnitudes
    float *values;   // Dynamically allocated array of magnitudes
} peak_msg_t;

static uint32_t samples_copy[SAMPLES * 2];
static float fft_input[SAMPLES * 2];  // ESP-DSP requires interleaved real/imag format
static float fft_output[SAMPLES];     // Magnitude output
static float temp_real[SAMPLES];      // Static allocation to avoid stack overflow

static QueueHandle_t peaks_queue = NULL;

// ESP-DSP FFT initialization
static bool esp_dsp_initialized = false;

static SemaphoreHandle_t config_mutex = NULL;
static int num_highest = 6;
static int min_width = 10;

static float smoothed_max_mag = 1e-6f; // initialize to small positive number

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

static bool send_peaks(peak_t *magnitudes, int length) {
    if (peaks_queue == NULL) return false;
    
    TickType_t timeout_ticks = 0;
    
    // Allocate message struct and values buffer
    peak_msg_t *msg = (peak_msg_t *)malloc(sizeof(peak_msg_t));
    if (!msg) return false;
    msg->length = length;
    msg->values = (float *)malloc(length * sizeof(float));
    if (!msg->values) {
        free(msg);
        return false;
    }

    // Copy magnitudes
    memcpy(msg->values, magnitudes, length * sizeof(float));

    // Send pointer to queue
    if (xQueueSend(peaks_queue, &msg, timeout_ticks) != pdTRUE) {
        free(msg->values);
        free(msg);
        return false;
    }

    return true;
}

static void fft_task(void *param) {
  ESP_LOGI("fft", "FFT task started");
  uint32_t loop_count = 0;
  uint32_t successful_ffts = 0;
  uint32_t total_fft_time_us = 0;
  uint32_t max_fft_time_us = 0;
  uint32_t min_fft_time_us = UINT32_MAX;
  
  // Detailed step timing accumulators
  uint32_t total_mono_time_us = 0;
  uint32_t total_windowing_time_us = 0;
  uint32_t total_compute_time_us = 0;
  uint32_t total_magnitude_time_us = 0;
  uint32_t total_peak_prep_time_us = 0;
  uint32_t total_peak_sort_time_us = 0;
  uint32_t total_peak_select_time_us = 0;
  uint32_t total_normalize_time_us = 0;
  uint32_t total_final_sort_time_us = 0;
  uint32_t total_send_time_us = 0;
  
  while (true) {
    loop_count++;
    
    if (READ_SAMPLES(samples_copy, sizeof(samples_copy) / sizeof(samples_copy[0]))) {
      successful_ffts++;
      
      // Start timing FFT processing
      uint32_t fft_start_time = esp_timer_get_time();
      uint32_t step_start_time, step_end_time;
      
      if (successful_ffts <= 5 || successful_ffts % 100 == 0) {
        ESP_LOGI("fft", "FFT %d: Processing samples", successful_ffts);
      }
      
      // Step 1: Mono conversion
      step_start_time = esp_timer_get_time();
      convert_to_mono();
      step_end_time = esp_timer_get_time();
      total_mono_time_us += (step_end_time - step_start_time);

      // Debug: Check input data
      if (successful_ffts <= 3) {
        ESP_LOGI("fft", "Input samples: [0]=%.3f [1]=%.3f [2]=%.3f [3]=%.3f", 
                 fft_input[0], fft_input[2], fft_input[4], fft_input[6]);
      }

      // Step 2: FFT Windowing (ESP-DSP) - TEMPORARILY DISABLED FOR DEBUGGING
      step_start_time = esp_timer_get_time();
      // Skip windowing to test if it's causing the zero output issue
      // TODO: Re-enable windowing once FFT is working correctly
      step_end_time = esp_timer_get_time();
      total_windowing_time_us += (step_end_time - step_start_time);

      // Debug: Check windowed data
      if (successful_ffts <= 3) {
        ESP_LOGI("fft", "Windowed: [0]=%.3f [1]=%.3f [2]=%.3f [3]=%.3f", 
                 fft_input[0], fft_input[2], fft_input[4], fft_input[6]);
      }

      // Step 3: FFT Compute (ESP-DSP)
      step_start_time = esp_timer_get_time();
      dsps_fft2r_fc32(fft_input, SAMPLES);
      dsps_bit_rev_fc32(fft_input, SAMPLES);
      step_end_time = esp_timer_get_time();
      total_compute_time_us += (step_end_time - step_start_time);

      // Debug: Check FFT output
      if (successful_ffts <= 3) {
        ESP_LOGI("fft", "FFT out: [0]=%.3f+%.3fi [1]=%.3f+%.3fi [2]=%.3f+%.3fi", 
                 fft_input[0], fft_input[1], fft_input[2], fft_input[3], fft_input[4], fft_input[5]);
      }

      // Step 4: Complex to Magnitude (ESP-DSP)
      step_start_time = esp_timer_get_time();
      for (int i = 0; i < SAMPLES/2; i++) {
        float real = fft_input[i * 2];
        float imag = fft_input[i * 2 + 1];
        fft_output[i] = sqrtf(real * real + imag * imag);
      }
      step_end_time = esp_timer_get_time();
      total_magnitude_time_us += (step_end_time - step_start_time);

      // Debug: Check magnitude output
      if (successful_ffts <= 3) {
        ESP_LOGI("fft", "Magnitudes: [0]=%.3f [1]=%.3f [2]=%.3f [10]=%.3f [50]=%.3f", 
                 fft_output[0], fft_output[1], fft_output[2], fft_output[10], fft_output[50]);
      }

      // Step 5: Peak preparation (skip DC bin 0)
      step_start_time = esp_timer_get_time();
      peak_t peaks[MAX_BIN - 1];  // Exclude bin 0 (DC)
      for (int i = 1; i < MAX_BIN; i++) {  // Start from bin 1, skip DC
        peaks[i-1].index = i;
        peaks[i-1].magnitude = fft_output[i];
      }
      step_end_time = esp_timer_get_time();
      total_peak_prep_time_us += (step_end_time - step_start_time);

      // Step 6: Peak sorting
      step_start_time = esp_timer_get_time();
      qsort(peaks, MAX_BIN - 1, sizeof(peak_t), cmp_peak);  // Exclude DC bin
      step_end_time = esp_timer_get_time();
      total_peak_sort_time_us += (step_end_time - step_start_time);

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

      // Step 7: Peak selection
      step_start_time = esp_timer_get_time();
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
      step_end_time = esp_timer_get_time();
      total_peak_select_time_us += (step_end_time - step_start_time);

      // Step 8: Normalization
      step_start_time = esp_timer_get_time();
      normalize_magnitudes(selected,current_num_highest);
      step_end_time = esp_timer_get_time();
      total_normalize_time_us += (step_end_time - step_start_time);

      // Step 9: Final sorting
      step_start_time = esp_timer_get_time();
      sort_peaks_by_index(selected,current_num_highest);
      step_end_time = esp_timer_get_time();
      total_final_sort_time_us += (step_end_time - step_start_time);

      // Log FFT peaks with frequencies and magnitudes
      if (count > 0) {
        char log_buffer[256];
        int pos = 0;
        pos += snprintf(log_buffer + pos, sizeof(log_buffer) - pos, "FFT Peaks: ");
        
        for (int i = 0; i < count; i++) {
          // Convert bin index to frequency: freq = (bin * SAMPLE_RATE) / SAMPLES
          float frequency = (float)(selected[i].index * SAMPLE_RATE) / SAMPLES;
          pos += snprintf(log_buffer + pos, sizeof(log_buffer) - pos, 
                         "[%.0fHz: %.2f] ", frequency, selected[i].magnitude);
          
          // Prevent buffer overflow
          if (pos >= sizeof(log_buffer) - 20) break;
        }
        
        ESP_LOGI("fft", "%s", log_buffer);
      }

      // Step 10: Send peaks
      step_start_time = esp_timer_get_time();
      send_peaks(selected,current_num_highest);
      step_end_time = esp_timer_get_time();
      total_send_time_us += (step_end_time - step_start_time);
      
      // End timing and calculate performance metrics
      uint32_t fft_end_time = esp_timer_get_time();
      uint32_t fft_duration_us = fft_end_time - fft_start_time;
      
      // Update statistics
      total_fft_time_us += fft_duration_us;
      if (fft_duration_us > max_fft_time_us) {
        max_fft_time_us = fft_duration_us;
      }
      if (fft_duration_us < min_fft_time_us) {
        min_fft_time_us = fft_duration_us;
      }
      
      // Report benchmark every 100 successful FFTs
      if (successful_ffts % 100 == 0) {
        uint32_t avg_fft_time_us = total_fft_time_us / successful_ffts;
        float avg_fft_time_ms = avg_fft_time_us / 1000.0f;
        float max_fft_time_ms = max_fft_time_us / 1000.0f;
        float min_fft_time_ms = min_fft_time_us / 1000.0f;
        float fft_rate_hz = 1000000.0f / avg_fft_time_us;
        
        ESP_LOGI("fft", "BENCHMARK #%d: Avg=%.2fms, Max=%.2fms, Min=%.2fms, Rate=%.1fHz", 
                 successful_ffts, avg_fft_time_ms, max_fft_time_ms, min_fft_time_ms, fft_rate_hz);
        
        // Detailed step breakdown (average times in milliseconds)
        float avg_mono_ms = (total_mono_time_us / successful_ffts) / 1000.0f;
        float avg_windowing_ms = (total_windowing_time_us / successful_ffts) / 1000.0f;
        float avg_compute_ms = (total_compute_time_us / successful_ffts) / 1000.0f;
        float avg_magnitude_ms = (total_magnitude_time_us / successful_ffts) / 1000.0f;
        float avg_peak_prep_ms = (total_peak_prep_time_us / successful_ffts) / 1000.0f;
        float avg_peak_sort_ms = (total_peak_sort_time_us / successful_ffts) / 1000.0f;
        float avg_peak_select_ms = (total_peak_select_time_us / successful_ffts) / 1000.0f;
        float avg_normalize_ms = (total_normalize_time_us / successful_ffts) / 1000.0f;
        float avg_final_sort_ms = (total_final_sort_time_us / successful_ffts) / 1000.0f;
        float avg_send_ms = (total_send_time_us / successful_ffts) / 1000.0f;
        
        ESP_LOGI("fft", "STEP BREAKDOWN: Mono=%.2f Wind=%.2f Comp=%.2f Mag=%.2f Prep=%.2f Sort1=%.2f Sel=%.2f Norm=%.2f Sort2=%.2f Send=%.2f", 
                 avg_mono_ms, avg_windowing_ms, avg_compute_ms, avg_magnitude_ms, 
                 avg_peak_prep_ms, avg_peak_sort_ms, avg_peak_select_ms, 
                 avg_normalize_ms, avg_final_sort_ms, avg_send_ms);
        
        // Expected rate for 44.1kHz audio with 512 samples is ~86Hz
        if (fft_rate_hz < 50.0f) {
          ESP_LOGW("fft", "WARNING: FFT rate %.1fHz is too slow for real-time processing!", fft_rate_hz);
        }
      }
      
    } else {
      // Log periodically when no samples are available
      if (loop_count % 1000 == 0) {
        ESP_LOGW("fft", "Loop %d: No samples available for FFT processing", loop_count);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

void fft_task_init() {
  peaks_queue = xQueueCreate(MAX_LED_QUEUE, sizeof(peak_msg_t *));

  config_mutex = xSemaphoreCreateMutex();
  if (!config_mutex) {
    Serial.println("Config mutex creation failed");
    return;
  }

  // Initialize ESP-DSP
  esp_err_t ret = dsps_fft2r_init_fc32(NULL, SAMPLES);
  if (ret != ESP_OK) {
    ESP_LOGE("fft", "ESP-DSP FFT initialization failed: %s", esp_err_to_name(ret));
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
