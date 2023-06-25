#ifndef NOW_H
#define NOW_H

#include "config.hpp"

#define DATA_PACKET 0
#define PAIR_PACKET 1
#define CONF_PACKET 2
#define SET_PACKET  3

#define REQUEST_ALL    0xFF
#define SET_CHANNEL    0x00
#define SET_BASE_COLOR 0x01
#define SET_FLASH_ON   0x02
#define SET_FLASH_OFF  0x03
#define SET_INTENSITY  0x04
#define SET_LOCK       0x05
#define SET_UNLOCK     0x06

// TODO Add preamble for packet collision

typedef struct struct_config {
  uint8_t msgType;
  uint8_t macAddr[6];
  uint8_t channel;
  uint8_t band;
  uint8_t setId;
  uint8_t battery;
} struct_config;

typedef struct struct_message {
  uint8_t msgType;
  uint8_t server_mac[6];
  uint8_t bands[NUM_BANDS];
  uint8_t r[NUM_BANDS];
  uint8_t g[NUM_BANDS];
  uint8_t b[NUM_BANDS];
} struct_message;

typedef struct struct_pairing {
  uint8_t msgType;
  uint8_t channel;
} struct_pairing;

typedef struct struct_request {
  uint8_t msgType;
  uint8_t destId;
  uint8_t requestLength;
  uint8_t requestData[30];
} struct_request;

typedef struct device_handler {
  struct_config devices[50];
  size_t        deviceCount;
} device_handler;

void now_init();
void now_send_light(const struct_message* message);
void addPeer(const uint8_t* mac_addr, uint8_t chan);

#endif
