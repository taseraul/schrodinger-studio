#include "now.hpp"
#include <Arduino_JSON.h>
#include <WiFi.h>
#include <esp_now.h>
#include "webserver.hpp"

device_handler moodConfig[MAX_DEVICES];
uint8_t        deviceCount;
uint8_t        broadcastAddr[]     = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t        defaultColors[6][3] = {
    {0xFF, 0x00, 0x00},
    {0x00, 0xFF, 0x00},
    {0x00, 0x00, 0xFF},
    {0xFF, 0x70, 0x00},
    {0x00, 0xD0, 0xFF},
    {0x99, 0x00, 0xFF},
};

void addPeer(const uint8_t* mac_addr, uint8_t chan) {
  esp_now_peer_info_t peer;
  esp_now_del_peer(mac_addr);
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, sizeof(uint8_t[6]));
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

static void populateDevice(uint8_t index) {
  JSONVar deviceInstance;

  deviceInstance["id"]      = index;
  deviceInstance["band"]    = moodConfig[index].band;
  deviceInstance["lock"]    = moodConfig[index].lock;
  deviceInstance["flash"]   = moodConfig[index].flash;
  deviceInstance["battery"] = moodConfig[index].battery;
  deviceInstance["r"]       = moodConfig[index].rgb[0];
  deviceInstance["g"]       = moodConfig[index].rgb[1];
  deviceInstance["b"]       = moodConfig[index].rgb[2];

  notifyClients(JSON.stringify(deviceInstance));
}

void now_send_config(const struct_config* config) {
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t*)config, sizeof(*config));
}

void now_send_light(const struct_message* message) {
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t*)message, sizeof(*message));
}

static void updateBattery(uint8_t battery, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]        = index;
  deviceUpdate["battery"]   = battery;
  moodConfig[index].battery = battery;
  notifyClients(JSON.stringify(deviceUpdate));
}

static void updateFlash(uint8_t flash, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]      = index;
  deviceUpdate["flash"]   = flash;
  moodConfig[index].flash = flash;
  notifyClients(JSON.stringify(deviceUpdate));
}

static void updateLock(uint8_t lock, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]     = index;
  deviceUpdate["lock"]   = lock;
  moodConfig[index].lock = lock;
  notifyClients(JSON.stringify(deviceUpdate));
}

static void updateBand(uint8_t band, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]     = index;
  deviceUpdate["band"]   = band;
  moodConfig[index].band = band;
  notifyClients(JSON.stringify(deviceUpdate));
}

static void updateColor(const uint8_t* color, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]       = index;
  deviceUpdate["r"]        = color[0];
  deviceUpdate["g"]        = color[1];
  deviceUpdate["b"]        = color[2];
  moodConfig[index].rgb[0] = color[0];
  moodConfig[index].rgb[1] = color[1];
  moodConfig[index].rgb[2] = color[2];
  notifyClients(JSON.stringify(deviceUpdate));
}

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
}

int is_peer(const uint8_t* mac_addr) {
  for (int i = 0; i < deviceCount; i++) {
    if (memcmp(mac_addr, moodConfig[i].macAddr, 6) == 0) {
      return i;
    }
  }
  return NOT_PEER;
}

static void requestDeviceState(const uint8_t* mac_addr, uint8_t index) {
  esp_err_t      result;
  struct_request requestPacket;

  requestPacket.destId         = index;
  requestPacket.msgType        = SET_ID_PACKET;
  requestPacket.preamble       = PREAMBLE;
  requestPacket.requestLength  = 1;
  requestPacket.requestData[0] = SET_REQUEST_STATE;

  result = esp_now_send(mac_addr, (uint8_t*)&requestPacket, sizeof(requestPacket));
}

static void setDeviceState(const uint8_t* mac_addr, const uint8_t index) {
  esp_err_t      result;
  struct_request requestPacket;

  requestPacket.destId         = index;
  requestPacket.msgType        = SET_ID_PACKET;
  requestPacket.preamble       = PREAMBLE;
  requestPacket.requestLength  = 8;
  requestPacket.requestData[0] = SET_BAND;
  requestPacket.requestData[1] = moodConfig[index].band;
  requestPacket.requestData[2] = moodConfig[index].flash ? SET_FLASH_ON : SET_FLASH_OFF;
  requestPacket.requestData[3] = moodConfig[index].lock ? SET_LOCK : SET_UNLOCK;
  requestPacket.requestData[4] = SET_BASE_COLOR;
  requestPacket.requestData[5] = moodConfig[index].rgb[0];
  requestPacket.requestData[6] = moodConfig[index].rgb[1];
  requestPacket.requestData[7] = moodConfig[index].rgb[2];

  result = esp_now_send(mac_addr, (uint8_t*)&requestPacket, sizeof(requestPacket));
}

static void pairDevice(const uint8_t* mac_addr, uint8_t battery) {
  uint8_t       index;
  esp_err_t     result;
  struct_config config;
  index = is_peer(mac_addr);

  addPeer(mac_addr, get_channel());
  config.preamble = PREAMBLE;
  config.msgType  = CONF_PACKET;
  config.channel  = get_channel();
  setServerMac(config.macAddr);

  if (index != NOT_PEER) {
    config.setId              = index;
    moodConfig[index].battery = battery;

    result = esp_now_send(mac_addr, (uint8_t*)&config, sizeof(config));

    updateBattery(battery, index);

    if (result == ESP_OK)
      setDeviceState(mac_addr, index);

    esp_now_del_peer(mac_addr);
  } else {

    config.setId = deviceCount;

    result = esp_now_send(mac_addr, (uint8_t*)&config, sizeof(config));

    moodConfig[deviceCount].band     = deviceCount % 6;
    moodConfig[deviceCount].battery  = battery;
    moodConfig[deviceCount].isPaired = 1;
    moodConfig[deviceCount].flash    = 0;
    moodConfig[deviceCount].lock     = 0;
    memcpy(moodConfig[deviceCount].macAddr, mac_addr, 6);
    memcpy(moodConfig[deviceCount].rgb, defaultColors[deviceCount % 6], 3);

    deviceCount++;

    if (result == ESP_OK) {
      populateDevice(config.setId);
      setDeviceState(mac_addr, index);
    }
  }

  esp_now_del_peer(mac_addr);
}

static void updateDevice(const struct_update* packet) {
  switch (packet->updateType) {
    case UPDATE_BATTERY:
      updateBattery(packet->updateData[0], packet->sourceId);
      break;
    case UPDATE_FLASH:
      updateFlash(packet->updateData[0], packet->sourceId);
      break;
    case UPDATE_LOCK:
      updateLock(packet->updateData[0], packet->sourceId);
      break;
    case UPDATE_BAND:
      updateBand(packet->updateData[0], packet->sourceId);
      break;
    case UPDATE_COLOR:
      updateColor(packet->updateData, packet->sourceId);
      break;
  }
}

void OnDataRecv(const uint8_t* mac_addr, const uint8_t* incomingData, int len) {
  Serial.print("pair req");
  String payload;
  // if (incomingData[0] == PREAMBLE) {
    uint8_t type = incomingData[0];
    switch (type) {

      case PAIR_PACKET:
        pairDevice(mac_addr, incomingData[1]);
        break;
      case UPDATE_PACKET:
        updateDevice((const struct_update*)incomingData);
        break;
    }
  // }
  // else
  //   Serial.print("pair fail");
}

void now_init() {
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  addPeer(broadcastAddr, get_channel());
}
