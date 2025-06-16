#include "Arduino.h"

typedef struct struct_config {
  uint8_t preamble;
  uint8_t msgType;
  uint8_t channel;
  uint8_t setId;
  uint8_t band;
  uint8_t flash;
  uint8_t rgb[3];
} struct_config;

typedef struct struct_message {
  uint8_t preamble;
  uint8_t msgType;
  uint8_t size;
  uint8_t data[20];
} struct_message;

typedef struct struct_pairing {
  uint8_t preamble;
  uint8_t msgType;
  uint8_t battery;
} struct_pairing;

typedef struct struct_update {
  uint8_t preamble;
  uint8_t msgType;
  uint8_t sourceId;
  uint8_t updateType;
  uint8_t updateData;
} struct_update;

typedef struct struct_frequency_data {
  uint8_t preamble;
  uint8_t msgType;
  uint8_t size;
  uint8_t bands[6];  // Frequency band magnitudes (0-255)
} struct_frequency_data;

typedef struct device_handler {
  uint8_t macAddr[6];
  uint8_t band;
  uint8_t battery;
  uint8_t lock;
  uint8_t flash;
  uint8_t rgb[3];
  uint8_t isPaired;
} device_handler;
