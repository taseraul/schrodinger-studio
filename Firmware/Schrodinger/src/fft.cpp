#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "esp_dsp.h"
// #include <dsp/transform.h>
// #include <dsp/window.h>
#include "config.hpp"
#include "i2s.hpp"
#include "bt.hpp"
#include "Arduino.h"
#include "arduinoFFT.h"

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
static float fft_real[SAMPLES];
static float fft_img[SAMPLES];

static QueueHandle_t peaks_queue = NULL;

ArduinoFFT<float> FFT = ArduinoFFT<float>(fft_real, fft_img, SAMPLES, SAMPLE_RATE);

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
    fft_real[i] = (left_f + right_f) * 0.5f;
    fft_img[i] = 0;
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
  
  while (true) {
    loop_count++;
    
    if (READ_SAMPLES(samples_copy, sizeof(samples_copy) / sizeof(samples_copy[0]))) {
      ESP_LOGI("fft", "Loop %d: Samples read successfully, processing FFT", loop_count);
      convert_to_mono();

      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      peak_t peaks[MAX_BIN];
      for (int i = 0; i < MAX_BIN; i++) {
        peaks[i].index = i;
        peaks[i].magnitude = fft_real[i];
      }

      qsort(peaks, MAX_BIN, sizeof(peak_t), cmp_peak);

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

      normalize_magnitudes(selected,current_num_highest);
      sort_peaks_by_index(selected,current_num_highest);

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

      send_peaks(selected,current_num_highest);
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

  // Initialize config values safely
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(100))) {
    num_highest = 6;
    min_width = 10;
    xSemaphoreGive(config_mutex);
  }

  xTaskCreate(fft_task, "fft_task", 4096, NULL, 5, NULL);
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
