#ifndef NOW_H
#define NOW_H

#include "config.hpp"

#define DATA_PACKET 0
#define PAIR_PACKET 1
#define CONF_PACKET 2

typedef struct struct_config {  // new structure for pairing
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

typedef struct struct_pairing {  // new structure for pairing
  uint8_t msgType;
  uint8_t channel;
} struct_pairing;

typedef struct device_handler {  // new structure for pairing
  struct_config devices[50];
  size_t deviceCount;
} device_handler;

void now_init();
void now_send_light(const struct_message* message);
void addPeer(const uint8_t* mac_addr, uint8_t chan);

#endif
