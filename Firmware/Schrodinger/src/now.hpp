#ifndef NOW_H
#define NOW_H

#include "Arduino.h"
#include "config.hpp"
#include "../../packet.h"

void now_init();
void now_send_light(const struct_message* message);
void setFlash(uint8_t flash, uint8_t index);
void setBand(uint8_t band, uint8_t index);
void setColor(const uint8_t* color, uint8_t index);


#endif
