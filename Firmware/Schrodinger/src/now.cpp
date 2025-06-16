#include "Arduino_JSON.h"
#include "WiFi.h"
#include "esp_now.h"
#include "webserver.hpp"
#include "now.hpp"
#include "Arduino.h"
#include "esp_log.h"

device_handler moodConfig[MAX_DEVICES];
uint8_t        deviceCount = 0;
uint8_t        broadcastAddr[]     = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t        defaultColors[6][3] = {
    {0xFF, 0x00, 0x00},
    {0x00, 0xFF, 0x00},
    {0x00, 0x00, 0xFF},
    {0xFF, 0x70, 0x00},
    {0x00, 0xD0, 0xFF},
    {0x99, 0x00, 0xFF},
};

static void populateDevice(uint8_t index) {
  JSONVar deviceInstance;

  deviceInstance["id"]      = index;
  deviceInstance["band"]    = moodConfig[index].band;
  deviceInstance["lock"]    = moodConfig[index].lock;
  deviceInstance["flash"]   = moodConfig[index].flash;
  deviceInstance["battery"] = moodConfig[index].battery;
  deviceInstance["red"]     = moodConfig[index].rgb[0];
  deviceInstance["green"]   = moodConfig[index].rgb[1];
  deviceInstance["blue"]    = moodConfig[index].rgb[2];

  notifyClients(JSON.stringify(deviceInstance));
}

static void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  ESP_LOGV("now","Packet status : %s", status ? "true" : "false");
}

static int is_peer(const uint8_t* mac_addr) {
  for (int i = 0; i < deviceCount; i++) {
    if (memcmp(mac_addr, moodConfig[i].macAddr, 6) == 0) {
      return i;
    }
  }
  return NOT_PEER;
}

// static void requestDeviceState(const uint8_t* mac_addr, uint8_t index) {
//   esp_err_t      result;
//   struct_request requestPacket;
//
//   requestPacket.destId         = index;
//   requestPacket.msgType        = UPDATE_PACKET;
//   requestPacket.preamble       = PREAMBLE;
//   requestPacket.requestLength  = 1;
//   requestPacket.requestData[0] = SET_REQUEST_STATE;
//
//   result = esp_now_send(mac_addr, (uint8_t*)&requestPacket, sizeof(requestPacket));
// }

static void setDeviceState(const uint8_t* mac_addr, const uint8_t index) {
  esp_err_t      result;
  struct_config  config;

  config.setId          = index;
  config.msgType        = CONF_PACKET;
  config.preamble       = PREAMBLE;
  config.channel        = WiFi.channel();
  config.band           = moodConfig[index].band;
  config.flash          = moodConfig[index].flash;
  memcpy(config.rgb,moodConfig[index].rgb,3);

  result = esp_now_send(mac_addr, (uint8_t*)&config, sizeof(config));
  ESP_LOGI("now","Config send status %d",result);
}

static void addPeer(const uint8_t* mac_addr, uint8_t chan) {
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

static void updateBatteryWeb(uint8_t battery, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]        = index;
  deviceUpdate["battery"]   = battery;
  moodConfig[index].battery = battery;
  notifyClients(JSON.stringify(deviceUpdate));
}

static void updateFlashWeb(uint8_t flash, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]      = index;
  deviceUpdate["flash"]   = flash;
  moodConfig[index].flash = flash;
  notifyClients(JSON.stringify(deviceUpdate));
}

// static void updateLock(uint8_t lock, uint8_t index) {
//   JSONVar deviceUpdate;
//   deviceUpdate["id"]     = index;
//   deviceUpdate["lock"]   = lock;
//   moodConfig[index].lock = lock;
//   notifyClients(JSON.stringify(deviceUpdate));
// }

static void updateBandWeb(uint8_t band, uint8_t index) {
  JSONVar deviceUpdate;
  deviceUpdate["id"]     = index;
  deviceUpdate["band"]   = band;
  deviceUpdate["r"]   = defaultColors[band][0];
  deviceUpdate["g"]   = defaultColors[band][1];
  deviceUpdate["b"]   = defaultColors[band][2];
  moodConfig[index].band = band;
  memcpy(moodConfig[index].rgb, defaultColors[band], 3);

  setDeviceState(broadcastAddr,index);
  notifyClients(JSON.stringify(deviceUpdate));
}

void setFlash(uint8_t flash, uint8_t index){
  struct_update update;

  moodConfig[index].flash = flash;
  update.preamble = PREAMBLE;
  update.msgType = UPDATE_PACKET;
  update.sourceId = index;
  update.updateType = UPDATE_FLASH;
  update.updateData = flash;

  esp_now_send(broadcastAddr, (uint8_t*)&update, sizeof(update));

  // setDeviceState(broadcastAddr,index);
}

void setBand(uint8_t band, uint8_t index) {
  struct_update update;

  moodConfig[index].band = band;
  update.preamble = PREAMBLE;
  update.msgType = UPDATE_PACKET;
  update.sourceId = index;
  update.updateType = UPDATE_BAND;
  update.updateData = band;

  esp_now_send(broadcastAddr, (uint8_t*)&update, sizeof(update));

  // memcpy(moodConfig[index].rgb, defaultColors[band], 3);
  // setDeviceState(broadcastAddr,index);
}

// static void updateColor(const uint8_t* color, uint8_t index) {
//   JSONVar deviceUpdate;
//   deviceUpdate["id"]       = index;
//   deviceUpdate["r"]        = color[0];
//   deviceUpdate["g"]        = color[1];
//   deviceUpdate["b"]        = color[2];
//   moodConfig[index].rgb[0] = color[0];
//   moodConfig[index].rgb[1] = color[1];
//   moodConfig[index].rgb[2] = color[2];
//   notifyClients(JSON.stringify(deviceUpdate));
// }

void setColor(const uint8_t* color, uint8_t index) {
  moodConfig[index].rgb[0] = color[0];
  moodConfig[index].rgb[1] = color[1];
  moodConfig[index].rgb[2] = color[2];
  setDeviceState(broadcastAddr,index);
}

static void updateDevice(const struct_update* packet) {
  switch (packet->updateType) {
    case UPDATE_BATTERY:
      updateBatteryWeb(packet->updateData, packet->sourceId);
      break;
    case UPDATE_FLASH:
      updateFlashWeb(packet->updateData, packet->sourceId);
      break;
    // case UPDATE_LOCK:
    //   updateLock(packet->updateData[0], packet->sourceId);
    //   break;
    case UPDATE_BAND:
      updateBandWeb(packet->updateData, packet->sourceId);
      break;
    // case UPDATE_COLOR:
    //   updateColor(packet->updateData, packet->sourceId);
    //   break;
    // case UPDATE_FULL:
    //   updateBand(packet->updateData, packet->sourceId);
    //   break;
    default:
      ESP_LOGW("now","Update device unknown type %d",packet->updateType);
      break;
  }
}

static void pairDevice(const uint8_t* mac_addr, uint8_t battery) {
  uint8_t       index;
  esp_err_t     result;
  index = is_peer(mac_addr);
  struct_config config;

  config.preamble = PREAMBLE;
  config.msgType  = CONF_PACKET;
  config.channel  = WiFi.channel();
  addPeer(mac_addr, config.channel);

  if (index != NOT_PEER) {
    config.setId              = index;
    moodConfig[index].battery = battery;

    updateBatteryWeb(battery, index);
    setDeviceState(mac_addr, index);

  } else {
    config.setId = deviceCount;
    
    moodConfig[deviceCount].band     = deviceCount % 6;
    moodConfig[deviceCount].battery  = battery;
    moodConfig[deviceCount].isPaired = 1;
    moodConfig[deviceCount].flash    = 0;
    moodConfig[deviceCount].lock     = 0;
    memcpy(moodConfig[deviceCount].macAddr, mac_addr, 6);
    memcpy(moodConfig[deviceCount].rgb, defaultColors[deviceCount % 6], 3);


    populateDevice(config.setId);
    setDeviceState(mac_addr, deviceCount);
    deviceCount++;
  }

  esp_now_del_peer(mac_addr);
}

void OnDataRecv(const uint8_t* mac_addr, const uint8_t* incomingData, int len) {
  ESP_LOGV("now","Rceived espnow message");
  ESP_LOG_BUFFER_HEX_LEVEL("now", incomingData, len, ESP_LOG_DEBUG);
  String payload;
  if (incomingData[0] == PREAMBLE) {
    uint8_t type = incomingData[1];
    switch (type) {
      case PAIR_PACKET:
        ESP_LOGV("now","Pair request");
        pairDevice(mac_addr, incomingData[2]);
        break;
      case UPDATE_PACKET:
        ESP_LOGV("now","Device update");
        updateDevice((const struct_update*)incomingData);
        break;
      default:
        ESP_LOGW("now","Packet type %d not expected",type);
    }
  }
  else {
    ESP_LOGW("now","Message failed preamble check");
  }
}

void now_send_frequency_data(const struct_frequency_data* freq_data) {
  if (!freq_data) return;
  
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t*)freq_data, sizeof(struct_frequency_data));
  if (result != ESP_OK) {
    ESP_LOGW("now", "Failed to send frequency data: %d", result);
  }
}

void now_init() {
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  addPeer(broadcastAddr, WiFi.channel());
}
