/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp-now-esp32-arduino-ide/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <esp_now.h>
#include <WiFi.h>

#define RED 6
#define GREEN 7
#define BLUE 5

#define MAIN_BAND 0
#define SEC_BAND 1

#define NUM_BANDS 6

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
  uint8_t bands[NUM_BANDS];
} struct_message;

// Create a struct_message called myData
struct_message myData;

// // callback function that will be executed when data is received
// void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
//   memcpy(&myData, incomingData, sizeof(myData));
//   uint8_t intensity[NUM_BANDS];
//   for (int i = 0; i < NUM_BANDS; i++) {
//     intensity[i] = myData.bands[i] / 32;
//     // memset(leds+i*4,0x00,sizeof(CRGB)*8);
//     // memset(leds+i*4,0xAB,sizeof(CRGB)*intensity);
//     // Serial.print(myData.bands[i]);
//     // Serial.print(" ");
//   }
//   Serial.write(27);     // ESC command
//   Serial.print("[2J");  // clear screen command
//   Serial.write(27);
//   Serial.print("[H");  // cursor to home command
//   for (int i = 0; i < 8; i++) {
//     for (int j = 0; j < NUM_BANDS; j++) {
//       if (intensity[j] < i)
//         Serial.print(" ");
//       else
//         Serial.write(219);
//     }
//     Serial.println(" ");
//   }
//   Serial.println(" ");
//   FastLED.show();
// }


// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  ledcWrite(0, 255-myData.bands[0]);
  ledcWrite(1, 255-myData.bands[1]);
  ledcWrite(2, 255-myData.bands[2]);
  // ledcWrite(0, 255-myData.bands[3]);
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);
  
  ledcSetup(0, 5000, 8);
  ledcSetup(1, 5000, 8);
  ledcSetup(2, 5000, 8);

  ledcAttachPin(RED, 0);
  ledcAttachPin(GREEN, 1);
  ledcAttachPin(BLUE, 2);

  ledcWrite(0, 255);
  ledcWrite(1, 255);
  ledcWrite(2, 255);


  Serial.println("START");
}

void loop() {
  
}