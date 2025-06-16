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
#include "circular_buffer.hpp"
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
    
    // Check if queue is full and clean up old messages to prevent memory leak
    UBaseType_t queue_count = uxQueueMessagesWaiting(peaks_queue);
    if (queue_count >= MAX_LED_QUEUE) {
        // Queue is full, remove and free old messages
        peak_msg_t *old_msg;
        while (xQueueReceive(peaks_queue, &old_msg, 0) == pdTRUE) {
            if (old_msg) {
                if (old_msg->values) {
                    free(old_msg->values);
                }
                free(old_msg);
            }
        }
        ESP_LOGW("fft", "Cleared full peaks queue to prevent memory leak");
    }
    
    TickType_t timeout_ticks = 0;
    
    // Allocate message struct and values buffer
    peak_msg_t *msg = (peak_msg_t *)malloc(sizeof(peak_msg_t));
    if (!msg) {
        ESP_LOGE("fft", "Failed to allocate peak message");
        return false;
    }
    msg->length = length;
    msg->values = (float *)malloc(length * sizeof(float));
    if (!msg->values) {
        free(msg);
        ESP_LOGE("fft", "Failed to allocate peak values");
        return false;
    }

    // Copy magnitudes (copy the magnitude values, not the whole peak_t struct)
    for (int i = 0; i < length; i++) {
        msg->values[i] = magnitudes[i].magnitude;
    }

    // Send pointer to queue
    if (xQueueSend(peaks_queue, &msg, timeout_ticks) != pdTRUE) {
        free(msg->values);
        free(msg);
        ESP_LOGW("fft", "Failed to send peak message to queue");
        return false;
    }

    return true;
}

static void fft_task(void *param) {
  static int fft_process_counter = 0;
  static int heartbeat_counter = 0;
  
  ESP_LOGI("fft", "FFT task started successfully");
  
  while (true) {
    // Heartbeat logging to confirm task is running
    if (++heartbeat_counter >= 1000) {
      heartbeat_counter = 0;
      ESP_LOGI("fft", "FFT task heartbeat - Buffer: %d/%d, Overflows: %d", 
               bt_circular_buffer.available(), bt_circular_buffer.capacity(),
               bt_circular_buffer.get_overflow_count());
    }
    
    // Aggressively consume data to prevent buffer overflow
    bool data_processed = false;
    int buffers_consumed = 0;
    
    // Process multiple buffers per cycle to catch up with incoming data
    for (int i = 0; i < 5; i++) {
      ESP_LOGD("fft", "Attempting to read samples, iteration %d", i);
      
      if (READ_SAMPLES(samples_copy, sizeof(samples_copy) / sizeof(samples_copy[0]))) {
        data_processed = true;
        buffers_consumed++;
        ESP_LOGD("fft", "Successfully read buffer %d, total consumed: %d", i, buffers_consumed);
        
        // Do FFT processing on every buffer
        convert_to_mono();

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        peak_t peaks[MAX_BIN];
        for (int j = 0; j < MAX_BIN; j++) {
          peaks[j].index = j;
          peaks[j].magnitude = fft_real[j];
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

        for (int k = 0; k < MAX_BIN && count < current_num_highest; k++) {
          int candidate = peaks[k].index;
          bool too_close = false;
          for (int j = 0; j < count; j++) {
            if (abs(selected[j].index - candidate) < current_min_width) {
              too_close = true;
              break;
            }
          }
          if (!too_close) {
            selected[count++] = peaks[k];
          }
        }

        normalize_magnitudes(selected,current_num_highest);
        sort_peaks_by_index(selected,current_num_highest);

        // Log peaks in single line format every time
        ESP_LOGI("fft", "Peaks: %.3f %.3f %.3f %.3f %.3f %.3f", 
                 selected[0].magnitude, selected[1].magnitude, selected[2].magnitude,
                 selected[3].magnitude, selected[4].magnitude, selected[5].magnitude);

        send_peaks(selected,current_num_highest);
        
        // Yield CPU after each FFT to allow BT task to run
        taskYIELD();
      } else {
        ESP_LOGD("fft", "No more data available at iteration %d", i);
        break; // No more data available
      }
    }
    
    if (buffers_consumed > 0) {
      ESP_LOGD("fft", "Consumed %d buffers this cycle", buffers_consumed);
    }
    
    // Much more aggressive timing - prioritize data consumption
    if (data_processed) {
      vTaskDelay(pdMS_TO_TICKS(1)); // Very short delay when processing data
    } else {
      vTaskDelay(pdMS_TO_TICKS(5)); // Short delay when no data
    }
  }
}

void fft_task_init() {
  ESP_LOGI("fft", "Initializing FFT task...");
  
  peaks_queue = xQueueCreate(MAX_LED_QUEUE, sizeof(peak_msg_t *));
  if (!peaks_queue) {
    ESP_LOGE("fft", "Failed to create peaks queue");
    return;
  }

  config_mutex = xSemaphoreCreateMutex();
  if (!config_mutex) {
    ESP_LOGE("fft", "Config mutex creation failed");
    return;
  }

  // Initialize config values safely
  if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(100))) {
    num_highest = 6;
    min_width = 10;
    xSemaphoreGive(config_mutex);
  }

  // Create FFT task with lower priority than BT task
  TaskHandle_t fft_task_handle = NULL;
  BaseType_t result = xTaskCreate(fft_task, "fft_task", 8192, NULL, 3, &fft_task_handle);
  
  if (result != pdPASS) {
    ESP_LOGE("fft", "Failed to create FFT task, error: %d", result);
    return;
  }
  
  if (fft_task_handle == NULL) {
    ESP_LOGE("fft", "FFT task handle is NULL");
    return;
  }
  
  ESP_LOGI("fft", "FFT task created successfully with handle: %p", fft_task_handle);
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
