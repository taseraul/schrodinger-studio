#include <WiFi.h>
#include "now.hpp"
#include <Arduino_JSON.h>
#include <esp_now.h>
#include "webserver.hpp"

esp_now_peer_info_t slave;
struct_pairing pairingData;
device_handler moodConfig;
uint8_t broadcastAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void addPeer(const uint8_t *mac_addr, uint8_t chan) {
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

String deviceToJson(uint8_t index) {
  JSONVar deviceInstance;
  
  deviceInstance["msgType"] = CONF_PACKET;
  deviceInstance["id"] = moodConfig.devices[index].setId;
  deviceInstance["band"] = moodConfig.devices[index].band;
  deviceInstance["battery"] = moodConfig.devices[index].battery;
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           moodConfig.devices[index].macAddr[0], moodConfig.devices[index].macAddr[1], moodConfig.devices[index].macAddr[2], moodConfig.devices[index].macAddr[3], moodConfig.devices[index].macAddr[4], moodConfig.devices[index].macAddr[5]);
  String mac(macStr);
  deviceInstance["mac"] = mac;

  return JSON.stringify(deviceInstance);
}

void now_send_config(const struct_config *config) {
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t *)config, sizeof(*config));
}


void now_send_light(const struct_message *message) {
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t *)message, sizeof(*message));
}

void printMAC(const uint8_t *mac_addr) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.print("Delivery Fail to ");
    printMAC(mac_addr);
    Serial.println();
  }
}

int is_peer(const uint8_t *mac_addr, uint8_t *isPeer) {
  *isPeer = 0;
  for (int i = 0; i < moodConfig.deviceCount; i++) {
    if (!memcmp(mac_addr, moodConfig.devices[i].macAddr, 6)) {
      *isPeer = 1;
      return i;
    }
  }
  return 0;
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  Serial.print(len);
  Serial.print(" bytes of data received from : ");
  printMAC(mac_addr);
  Serial.println();
  String payload;
  uint8_t isPeer;
  uint8_t index;
  uint8_t type = incomingData[0];
  switch (type) {
    case CONF_PACKET:
      Serial.println("CONF PACKET");
      index = is_peer(mac_addr, &isPeer);
      if (isPeer) {
        memcpy((uint8_t *)&moodConfig.devices[index], incomingData, sizeof(struct_config));
        notifyClients(deviceToJson(index));
      }
      break;

    case DATA_PACKET:
      Serial.println("DATA PACKET");
      break;

    case PAIR_PACKET:
      memcpy(&pairingData, incomingData, sizeof(pairingData));
      index = is_peer(mac_addr, &isPeer);
      Serial.print("Is peer : ");
      Serial.println(isPeer);
      Serial.print("Index : ");
      Serial.println(index);

      if (isPeer) {
        Serial.println("resend pair response");
        addPeer(mac_addr, get_channel());
        esp_err_t result = esp_now_send(mac_addr, (uint8_t *)&moodConfig.devices[index], sizeof(struct_config));
        esp_now_del_peer(mac_addr);
      } else {
        index = moodConfig.deviceCount;
        Serial.print("Pairing request from: ");
        printMAC(mac_addr);
        Serial.println();
        Serial.println(pairingData.channel);
        memcpy(moodConfig.devices[index].macAddr, mac_addr, 6);
        moodConfig.devices[index].setId = index;
        moodConfig.devices[index].channel = get_channel();
        moodConfig.devices[index].band = index % 6;
        moodConfig.devices[index].msgType = CONF_PACKET;
        Serial.println("send pair response");
        addPeer(mac_addr, get_channel());
        esp_err_t result = esp_now_send(mac_addr, (uint8_t *)&moodConfig.devices[index], sizeof(struct_config));
        moodConfig.deviceCount++;
        esp_now_del_peer(mac_addr);
      }

      notifyClients(deviceToJson(index));

      Serial.print("Id : ");
      Serial.println(moodConfig.devices[index].setId);
      Serial.print("Band : ");
      Serial.println(moodConfig.devices[index].band);
      Serial.print("Battery : ");
      Serial.println(moodConfig.devices[index].battery);
      Serial.print("Channel : ");
      Serial.println(moodConfig.devices[index].channel);

      break;
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

  addPeer(broadcastAddr, get_channel());
}
