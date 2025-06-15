#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "esp_sleep.h"
#include <ESPAsyncWebServer.h>
#include "driver/adc.h"
#include <esp_bt.h>
#include "../../packet.h"

AsyncWebServer server(80);

#define BUTTON1 3
#define BUTTON2 2
#define BATTERY 4

#define RED 6
#define GREEN 7
#define BLUE 5

#define NUM_BANDS 6

#define DATA_PACKET 0x00u
#define PAIR_PACKET 0x01u
#define CONF_PACKET 0x02u
#define UPDATE_PACKET 0x03u

#define UPDATE_BATTERY 0x00u
#define UPDATE_FLASH 0x01u
#define UPDATE_LOCK 0x02u
#define UPDATE_BAND 0x03u
#define UPDATE_COLOR 0x04u
#define UPDATE_FULL 0x05u

#define MAX_CHANNEL 13 /* for EU | 11 for USA */
#define PREAMBLE 0xAAu

struct_config deviceConfig;
struct_message lightData;
struct_pairing pairData;
struct_update update = {PREAMBLE, 0, 0, 0, 0 };

uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t serverAddress[6];

uint8_t paired = 0;
uint8_t prevBandButton = 1;
uint8_t prevFlashButton = 1;
uint64_t bandButtonDebounce = 0;
uint64_t batteryDebounceTimer = 0;
uint64_t flashButtonDebounce = 0;
uint64_t autoUpdateDebounce = 0;

bool isFlashlight = false;
bool auto_update = true;

int auto_pair();

void updateFlash(uint8_t flash) {
  deviceConfig.flash = flash;

  update.msgType = UPDATE_PACKET;
  update.sourceId = deviceConfig.setId;
  update.updateType = UPDATE_FLASH;
  update.updateData = flash;

  if (flash) {
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    ledcWrite(2, 0);
  } else {
    ledcWrite(0, 255);
    ledcWrite(1, 255);
    ledcWrite(2, 255);
  }

  esp_now_send(serverAddress, (uint8_t *)&update, sizeof(update));
}

void updateBand(uint8_t band){
  deviceConfig.band = band;

  update.msgType = UPDATE_PACKET;
  update.sourceId = deviceConfig.setId;
  update.updateType = UPDATE_BAND;
  update.updateData = deviceConfig.band;

  esp_now_send(serverAddress, (uint8_t *)&update, sizeof(update));
}

uint8_t get_battery() {
  int battReading = analogReadMilliVolts(BATTERY);
  uint8_t batteryPercentage = (battReading - 1500) / 600.0 * 100;
  if (batteryPercentage > 100)
    batteryPercentage = 100;
  return batteryPercentage;
}

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

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  static uint64_t timer = 0;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  Serial.println("received message");
  if (incomingData[0] == PREAMBLE) {
    switch (incomingData[1]) {
      case CONF_PACKET:
        if (deviceConfig.setId == 0xFF || incomingData[3] == deviceConfig.setId){
          memcpy(&deviceConfig, incomingData, sizeof(deviceConfig));
          memcpy(serverAddress, mac, 6);
          addPeer(serverAddress, deviceConfig.channel);

          ESP_ERROR_CHECK(esp_wifi_set_channel(deviceConfig.channel, WIFI_SECOND_CHAN_NONE));
          
          if (esp_now_init() != ESP_OK) {
            Serial.println("Error initializing ESP-NOW");
            return;
          }
          
          esp_now_register_send_cb(OnDataSent);
          esp_now_register_recv_cb(OnDataRecv);
          paired = 2;
        }
        break;
      case DATA_PACKET:
        if (auto_pair()) {
          memcpy(&lightData, incomingData, sizeof(lightData));
          if (memcmp(mac, serverAddress, 6) == 0) {
            red = (uint32_t)deviceConfig.rgb[0] * lightData.data[deviceConfig.band] / 255;
            green = (uint32_t)deviceConfig.rgb[1] * lightData.data[deviceConfig.band] / 255;
            blue = (uint32_t)deviceConfig.rgb[2] * lightData.data[deviceConfig.band] / 255;

            Serial.println("FPS : ");
            Serial.println(1000.0 / (millis() - timer));
            timer = millis();

            if (!isFlashlight && auto_update) {
              ledcWrite(0, 255 - red);
              ledcWrite(1, 255 - green);
              ledcWrite(2, 255 - blue);
            }
          }
        }
        break;
      case UPDATE_PACKET:
        memcpy(&update, incomingData, sizeof(update));
        Serial.println("Update Packet");
        // for(int i = 0; i< len; i++)
        //   Serial.printf("%d ",incomingData[i]);
        if (update.sourceId == deviceConfig.setId){
          switch(update.updateType){
            case UPDATE_BAND:
              Serial.println("Update Band");
              updateBand(update.updateData);
            break;
            case UPDATE_FLASH:
              Serial.println("Update Flash");
              updateFlash(update.updateData);
            break;
          }
        }
    }
  }
}

void setup() {
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.softAPdisconnect();

  ledcSetup(0, 20000, 8);
  ledcSetup(1, 20000, 8);
  ledcSetup(2, 20000, 8);

  ledcAttachPin(RED, 0);
  ledcAttachPin(GREEN, 1);
  ledcAttachPin(BLUE, 2);

  uint8_t battery = get_battery();

  ledcWrite(0, battery * 2.55);
  ledcWrite(1, 255 - battery * 2.55);
  ledcWrite(2, 255);
  
  delay(1000);

  ledcWrite(0, 255);
  ledcWrite(1, 255);
  ledcWrite(2, 255);

  deviceConfig.channel = 1;
  deviceConfig.setId = 0xFF;

  Serial.println("START");
}

int auto_pair() {
  static unsigned long pairRequestDebounce;
  switch (paired) {
    case 0:

      Serial.println("Pairing request on channel ");
      Serial.println(deviceConfig.channel);

      ESP_ERROR_CHECK(esp_wifi_set_channel(deviceConfig.channel, WIFI_SECOND_CHAN_NONE));
      if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
      }
      addPeer(broadcastAddress, deviceConfig.channel);

      pairData.msgType = PAIR_PACKET;
      pairData.preamble = PREAMBLE;
      pairData.battery = get_battery();

      esp_now_register_send_cb(OnDataSent);
      esp_now_register_recv_cb(OnDataRecv);

      esp_now_send(broadcastAddress, (uint8_t *)&pairData, sizeof(pairData));
      pairRequestDebounce = millis();
      paired = 1;
      break;

    case 1:
      if (millis() - pairRequestDebounce > 100) {
        pairRequestDebounce = millis();
        deviceConfig.channel++;
        if (deviceConfig.channel > MAX_CHANNEL) {
          deviceConfig.channel = 1;
        }
        paired = 0;
      }
      break;

    case 2:
      return 1;
      break;
    case 3:
      return 0;
      break;
  }
  return 0;
}

void loop() {
  if (auto_pair()) {
    if ((millis() - bandButtonDebounce) > 200) {

      uint8_t bandButton = digitalRead(BUTTON2);

      if (bandButton != prevBandButton) {
        bandButtonDebounce = millis();

        if (bandButton == 0){
          deviceConfig.band += 1;
          deviceConfig.band = deviceConfig.band % 6;

          updateBand(deviceConfig.band);
        }

        prevBandButton = bandButton;
      }
    }

    if ((millis() - batteryDebounceTimer) > 300000) {
      update.msgType = UPDATE_PACKET;
      update.sourceId = deviceConfig.setId;
      update.updateType = UPDATE_BATTERY;
      update.updateData = get_battery();

      esp_now_send(serverAddress, (uint8_t *)&update, sizeof(update));
      batteryDebounceTimer = millis();
    }
  }

  if ((millis() - flashButtonDebounce) > 200) {

      uint8_t flashButton = digitalRead(BUTTON1);
      
      if (flashButton != prevFlashButton){
        flashButtonDebounce = millis();
        
        if (flashButton == 0){
          updateFlash(!deviceConfig.flash);
        }
      }
      
      prevFlashButton = flashButton;
  }

  if ((millis() - autoUpdateDebounce) > 1000) {
    auto_update = true;
  }
}

void displayColor(uint8_t red, uint8_t green, uint8_t blue) {
  if (!isFlashlight) {
    ledcWrite(0, 255 - red);
    ledcWrite(1, 255 - green);
    ledcWrite(2, 255 - blue);
    auto_update = false;
    autoUpdateDebounce = millis();
  }
}
