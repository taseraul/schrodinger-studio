#ifndef WEBSERVER_H
#define WEBSERVER_H

void wifi_init();
void webserver_init();
int  get_channel();
void initFS();
void webserver_init();
void notifyClients(String json);
void setServerMac(uint8_t* mac);

#endif
