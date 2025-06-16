#ifndef NOW_H
#define NOW_H

#include "Arduino.h"
#include "config.hpp"
#include "../../packet.h"

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

void now_init();
void now_send_light(const struct_message* message);
void now_send_frequency_data(const struct_frequency_data* freq_data);
void setFlash(uint8_t flash, uint8_t index);
void setBand(uint8_t band, uint8_t index);
void setColor(const uint8_t* color, uint8_t index);


#endif
