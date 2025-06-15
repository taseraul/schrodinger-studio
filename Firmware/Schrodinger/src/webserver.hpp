#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "Arduino.h"

void wifi_init();
void webserver_init();
int  get_channel();
void fs_init();
void setServerMac(uint8_t* mac);
void notifyClients(String json);

#endif
