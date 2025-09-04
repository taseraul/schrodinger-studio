#include "Arduino.h"

#define DATA_PACKET  0x00u
#define PAIR_PACKET   0x01u
#define CONF_PACKET   0x02u
#define UPDATE_PACKET 0x03u

#define SET_TARGET_ALL    0xFFu
#define SET_BAND          0x00u
#define SET_BASE_COLOR    0x01u
#define SET_FLASH_ON      0x02u
#define SET_FLASH_OFF     0x03u
#define SET_INTENSITY     0x04u
#define SET_LOCK          0x05u
#define SET_UNLOCK        0x06u
#define SET_REQUEST_STATE 0x07u

#define UPDATE_BATTERY 0x00u
#define UPDATE_FLASH   0x01u
#define UPDATE_LOCK    0x02u
#define UPDATE_BAND    0x03u
#define UPDATE_COLOR   0x04u
#define UPDATE_FULL    0x05u

#define BATTERY_WALL_ADAPTER 0xFFu
#define NOT_PEER             0xFFu
#define PREAMBLE             0xAAu

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

typedef struct device_handler {
  uint8_t macAddr[6];
  uint8_t band;
  uint8_t battery;
  uint8_t lock;
  uint8_t flash;
  uint8_t rgb[3];
  uint8_t isPaired;
} device_handler;