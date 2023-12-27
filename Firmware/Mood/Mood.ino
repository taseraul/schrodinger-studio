#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "esp_sleep.h"
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <WebSerial.h>
#include "driver/adc.h"
#include <esp_bt.h>
#include <FastLED.h>

AsyncWebServer server(80);

#define BUTTON1 4
// #define BUTTON2 2
// #define BATTERY 4

#define STRIP1 19
#define STRIP2 18
#define STRIP3 17
#define STRIP4 16
#define STRIP5 13
#define STRIP6 27
#define STRIP7 26
#define STRIP8 25
#define STRIP9 32

#define RED 6
#define GREEN 7
#define BLUE 5

#define NUM_BANDS 6

#define DATA_PACKET 0
#define PAIR_PACKET 1
#define CONF_PACKET 2
#define MAX_CHANNEL 13 /* for EU | 11 for USA */

#define MAX_LED_STRIP 2
#define NUM_CH 6

typedef struct struct_config {
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

CRGB leds[NUM_CH][MAX_LED_STRIP];

typedef struct struct_pairing {
  uint8_t msgType;
  uint8_t channel;
} struct_pairing;

struct_config deviceConfig;
struct_message lightData;
struct_pairing pairData;

uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t serverAddress[6];

uint8_t paired = 0;
uint8_t prevButtonStatus = 1;
uint8_t prevPowerSwitchStatus = 1;
uint64_t pairDebounceTimer = 0;
uint64_t batteryDebounceTimer = 0;
uint64_t powerSwitchDebounce = 0;
uint64_t autoUpdateDebounce = 0;

bool isFlashlight = false;
bool auto_update = true;

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  static uint64_t timer = 0;
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  switch (incomingData[0]) {
    case CONF_PACKET:
      memcpy(&deviceConfig, incomingData, sizeof(deviceConfig));
      deviceConfig.battery = 0;//get_battery();
      ESP_ERROR_CHECK(esp_wifi_set_channel(deviceConfig.channel, WIFI_SECOND_CHAN_NONE));
      if (esp_now_init() != ESP_OK) {
        WebSerial.println("Error initializing ESP-NOW");
      }
      memcpy(serverAddress, mac, 6);
      addPeer(serverAddress, deviceConfig.channel);
      esp_now_send(serverAddress, (uint8_t *)&deviceConfig, sizeof(deviceConfig));

      WebSerial.println("Pairing complete");
      WebSerial.println("Id : ");
      WebSerial.println(deviceConfig.setId);
      WebSerial.println("Band : ");
      WebSerial.println(deviceConfig.band);
      WebSerial.println("Battery : ");
      WebSerial.println(deviceConfig.battery);
      WebSerial.println("Channel : ");
      WebSerial.println(deviceConfig.channel);
      paired = 2;
      break;
    case DATA_PACKET:
      if (auto_pair() && !isFlashlight && auto_update) {
        memcpy(&lightData, incomingData, sizeof(lightData));
        if (memcmp(lightData.server_mac, serverAddress, 6) == 0) {
          fillLight();
          // red = (uint32_t)lightData.r[deviceConfig.band] * lightData.bands[deviceConfig.band] / 255;
          // green = (uint32_t)lightData.g[deviceConfig.band] * lightData.bands[deviceConfig.band] / 255;
          // blue = (uint32_t)lightData.b[deviceConfig.band] * lightData.bands[deviceConfig.band] / 255;

          // WebSerial.println("FPS : ");
          // WebSerial.println(1000.0 / (millis() - timer));
          // timer = millis();

          // ledcWrite(0, 255 - red);
          // ledcWrite(1, 255 - green);
          // ledcWrite(2, 255 - blue);
        }
      }
      break;
  }
}

void fillLight() {
  for (int i = 0; i < 6; i++) {
    leds[i][1].r = (uint32_t)lightData.r[i] * lightData.bands[i] / 255;
    leds[i][1].g = (uint32_t)lightData.g[i] * lightData.bands[i] / 255;
    leds[i][1].b = (uint32_t)lightData.b[i] * lightData.bands[i] / 255;
  }
  FastLED.show();
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  WebSerial.print("\r\nLast Packet Send Status:\t");
  WebSerial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  pinMode(BUTTON1, INPUT_PULLUP);
  // pinMode(BUTTON2, INPUT_PULLUP);
  // analogSetAttenuation(ADC_11db);
  delay(1000);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.softAPdisconnect();
  if (!digitalRead(BUTTON1)) {
    WiFi.softAP("Mood", NULL);
    WebSerial.begin(&server);
    AsyncElegantOTA.begin(&server);
    server.begin();
    paired = 3;
  } else {
    WiFi.mode(WIFI_STA);
  }

  FastLED.addLeds<WS2811, STRIP1, RBG>(leds[0], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[0], 0x7F, 6);

  FastLED.addLeds<WS2811, STRIP2, RBG>(leds[1], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[1], 0x7F, 6);

  FastLED.addLeds<WS2811, STRIP3, RBG>(leds[2], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[2], 0x7F, 6);

  FastLED.addLeds<WS2811, STRIP4, RBG>(leds[3], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[3], 0x7F, 6);

  FastLED.addLeds<WS2811, STRIP5, RBG>(leds[4], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[4], 0x7F, 6);

  FastLED.addLeds<WS2811, STRIP6, RBG>(leds[5], MAX_LED_STRIP);  // GRB ordering is assumed
  memset(leds[5], 0x7F, 6);

  FastLED.show();

  // ledcSetup(0, 20000, 8);
  // ledcSetup(1, 20000, 8);
  // ledcSetup(2, 20000, 8);

  // ledcAttachPin(RED, 0);
  // ledcAttachPin(GREEN, 1);
  // ledcAttachPin(BLUE, 2);

  delay(1000);

  // ledcWrite(0, 255);
  // ledcWrite(1, 255);
  // ledcWrite(2, 255);

  memset(leds[0], 0x00, 6);
  memset(leds[1], 0x00, 6);
  memset(leds[2], 0x00, 6);
  memset(leds[3], 0x00, 6);
  memset(leds[4], 0x00, 6);
  memset(leds[5], 0x00, 6);

  FastLED.show();

  pairData.channel = 1;

  deviceConfig.battery = 0xFF;  //get_battery();

  WebSerial.println("START");
}

void addPeer(const uint8_t *mac_addr, uint8_t chan) {
  esp_now_peer_info_t peer;
  ESP_ERROR_CHECK(esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE));
  esp_now_del_peer(mac_addr);
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, sizeof(uint8_t[6]));
  if (esp_now_add_peer(&peer) != ESP_OK) {
    WebSerial.println("Failed to add peer");
    return;
  }
}

int auto_pair() {
  static unsigned long pairRequestDebounce;
  switch (paired) {
    case 0:

      WebSerial.println("Pairing request on channel ");
      WebSerial.println(pairData.channel);

      ESP_ERROR_CHECK(esp_wifi_set_channel(pairData.channel, WIFI_SECOND_CHAN_NONE));
      if (esp_now_init() != ESP_OK) {
        WebSerial.println("Error initializing ESP-NOW");
      }
      addPeer(broadcastAddress, pairData.channel);
      pairData.msgType = PAIR_PACKET;

      esp_now_register_send_cb(OnDataSent);
      esp_now_register_recv_cb(OnDataRecv);

      esp_now_send(broadcastAddress, (uint8_t *)&pairData, sizeof(pairData));
      pairRequestDebounce = millis();
      paired = 1;
      break;

    case 1:
      if (millis() - pairRequestDebounce > 100) {
        pairRequestDebounce = millis();
        pairData.channel++;
        if (pairData.channel > MAX_CHANNEL) {
          pairData.channel = 1;
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
    // if ((millis() - pairDebounceTimer) > 200) {
      // if (digitalRead(BUTTON2) == 0 && prevButtonStatus == 1) {
      //   prevButtonStatus = 0;
      //   pairDebounceTimer = millis();
      // }
      //  else if (digitalRead(BUTTON2) == 1 && prevButtonStatus == 0) {
      //   if ((millis() - pairDebounceTimer) < 3000) {
      //     deviceConfig.band += 1;
      //     deviceConfig.band = deviceConfig.band % 6;
      //     deviceConfig.battery = get_battery();
      //     esp_now_send(serverAddress, (uint8_t *)&deviceConfig, sizeof(deviceConfig));
      //     WebSerial.println("Band : ");
      //     WebSerial.println(deviceConfig.band);
      //     displayColor(lightData.r[deviceConfig.band], lightData.g[deviceConfig.band], lightData.b[deviceConfig.band]);
      //   } else {
      //     WebSerial.println("Pairing reset");
      //     paired = 0;
      //   }
      //   prevButtonStatus = 1;
      //   pairDebounceTimer = millis();
      // }
    // }
  }

  // if ((millis() - batteryDebounceTimer) > 300000) {
  //   deviceConfig.battery = get_battery();
  //   esp_now_send(serverAddress, (uint8_t *)&deviceConfig, sizeof(deviceConfig));
  //   WebSerial.println("Battery:");
  //   WebSerial.println(deviceConfig.battery);
  //   batteryDebounceTimer = millis();
  // }

  // if ((millis() - powerSwitchDebounce) > 200) {

  //   if (digitalRead(BUTTON1) == 0 && prevPowerSwitchStatus == 1) {
  //     prevPowerSwitchStatus = 0;
  //     powerSwitchDebounce = millis();
  //   } else if (digitalRead(BUTTON1) == 1 && prevPowerSwitchStatus == 0) {
  //     if ((millis() - powerSwitchDebounce) < 5000) {
  //       toggleFlashLight();
  //     }

  //     prevPowerSwitchStatus = 1;
  //     powerSwitchDebounce = millis();
  //   } else if (digitalRead(BUTTON1) == 0 && prevPowerSwitchStatus == 0) {
  //     if ((millis() - powerSwitchDebounce) >= 5000) {
  //       turnOff();
  //     }
  //   }
  // }
  // if ((millis() - autoUpdateDebounce) > 1000) {
  //   auto_update = true;
  // }
}

// void toggleFlashLight() {
//   isFlashlight = !isFlashlight;
//   WebSerial.println("Flash Toggle");

//   if (isFlashlight) {
//     ledcWrite(0, 0);
//     ledcWrite(1, 0);
//     ledcWrite(2, 0);
//   } else {
//     ledcWrite(0, 255);
//     ledcWrite(1, 255);
//     ledcWrite(2, 255);
//   }
// }

// void turnOff() {
//   WebSerial.println("OFF");
//   isFlashlight = true;
//   ledcWrite(0, 252);
//   ledcWrite(1, 252);
//   ledcWrite(2, 252);
//   delay(2000);
//   gpio_deep_sleep_hold_dis();
//   esp_sleep_config_gpio_isolate();
//   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_CPU, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);
//   esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
//   esp_deep_sleep_enable_gpio_wakeup(0b1100, ESP_GPIO_WAKEUP_GPIO_LOW);
//   ledcWrite(0, 255);
//   ledcWrite(1, 255);
//   ledcWrite(2, 255);
//   WiFi.mode(WIFI_OFF);
//   esp_wifi_stop();
//   esp_bt_controller_disable();
//   adc_power_off();
//   delay(1000);
//   esp_deep_sleep_start();
// }

// void displayColor(uint8_t red, uint8_t green, uint8_t blue) {
//   // if (!isFlashlight) {
//     ledcWrite(0, 255 - red);
//     ledcWrite(1, 255 - green);
//     ledcWrite(2, 255 - blue);
//     auto_update = false;
//     autoUpdateDebounce = millis();
//   // }
// }

// uint8_t get_battery() {
//   int battReading = analogReadMilliVolts(BATTERY);
//   uint8_t batteryPercentage = (battReading - 1500) / 600.0 * 100;
//   if (batteryPercentage > 100)
//     batteryPercentage = 100;
//   return batteryPercentage;
// }