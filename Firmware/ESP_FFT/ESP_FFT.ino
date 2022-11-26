/*
  Streaming of sound data with Bluetooth to other Bluetooth device.
  We generate 2 tones which will be sent to the 2 channels.
  
  Copyright (C) 2020 Phil Schatzmann
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "driver/i2s.h"
#include <math.h>
#include <arduinoFFT.h>
#include "esp_log.h"
#include <esp_now.h>

#include <WiFi.h>
// #include <AsyncTCP.h>
// #include <ESPAsyncWebServer.h>

#define SAMPLES 1024
#define SAMPLING_FREQ 44100
#define NUM_BANDS 6
#define NOISE 1000
#define BAND_ATTENUATION 10000

uint8_t broadcastAddress[] = { 0x7C, 0xDF, 0xA1, 0xBD, 0x1D, 0x18 };
uint64_t bandValues[NUM_BANDS];

typedef struct struct_message {
  uint8_t bands[NUM_BANDS];
} struct_message;

struct_message myData;

esp_now_peer_info_t peerInfo;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

uint32_t samples[SAMPLES * 2];
double vReal[SAMPLES];
double vImag[SAMPLES];
arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

// Replace with your network credentials
const char *ssid = "Home_mansarda";
const char *password = "hifihifi1";

bool ledState = 0;
const int ledPin = 2;

// Create AsyncWebServer object on port 80
// AsyncWebServer server(80);
// AsyncWebSocket ws("/ws");

// const char index_html[] PROGMEM = R"rawliteral(
// <!DOCTYPE HTML><html>
// <head>
//   <title>ESP Web Server</title>
//   <meta name="viewport" content="width=device-width, initial-scale=1">
//   <link rel="icon" href="data:,">
//   <style>
//   html {
//     font-family: Arial, Helvetica, sans-serif;
//     text-align: center;
//   }
//   h1 {
//     font-size: 1.8rem;
//     color: white;
//   }
//   h2{
//     font-size: 1.5rem;
//     font-weight: bold;
//     color: #143642;
//   }
//   .topnav {
//     overflow: hidden;
//     background-color: #143642;
//   }
//   body {
//     margin: 0;
//   }
//   .content {
//     padding: 30px;
//     max-width: 600px;
//     margin: 0 auto;
//   }
//   .card {
//     background-color: #F8F7F9;;
//     box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
//     padding-top:10px;
//     padding-bottom:20px;
//     height: 200px;
//     display: flex;
//     flex-direction:row;
//   }
//   .bar{
//       margin: 10px;
//       width: 50px;
//       height: 200px;
//       background-color: blue;
//   }

//   </style>
// <title>ESP Web Server</title>
// <meta name="viewport" content="width=device-width, initial-scale=1">
// <link rel="icon" href="data:,">
// </head>
// <body>
//   <div class="topnav">
//     <h1>ESP WebSocket Server</h1>
//   </div>
//   <div class="content">
//     <div class="card">
//       <div id="band0" class="bar"></div>
//       <div id="band1" class="bar"></div>
//       <div id="band2" class="bar"></div>
//       <div id="band3" class="bar"></div>
//       <div id="band4" class="bar"></div>
//       <div id="band5" class="bar"></div>
//       <div id="band6" class="bar"></div>
//       <div id="band7" class="bar"></div>
//     </div>
//   </div>
// <script>
//   var gateway = `ws://192.168.0.199/ws`;
//   var websocket;
//   window.addEventListener('load', onLoad);
//   function initWebSocket() {
//     console.log('Trying to open a WebSocket connection...');
//     websocket = new WebSocket(gateway);
//     websocket.onopen    = onOpen;
//     websocket.onclose   = onClose;
//     websocket.onmessage = onMessage; // <-- add this line
//   }
//   function onOpen(event) {
//     console.log('Connection opened');
//   }
//   function onClose(event) {
//     console.log('Connection closed');
//     setTimeout(initWebSocket, 2000);
//   }
//   function onMessage(event) {
//     var text = event.data;
//     var numbers = text.match(/(\d[\d\.]*)/g);
//     for (let i = 0; i < numbers.length; i++) {
//         const element = numbers[i];

//         var value_cap;
//         if(element > 255)
//             value_cap = 255;
//         else
//             value_cap = element;
//         document.getElementById("band"+i).style.height = value_cap/255 * 100 + "%";

//     }
//   }
//   function onLoad(event) {
//     initWebSocket();
//   }
//   function toggle(){
//     websocket.send('toggle');
//   }
// </script>
// </body>
// </html>
// )rawliteral";

// void notifyClients() {
//   ws.textAll(String(ledState));
// }

// void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
//   AwsFrameInfo *info = (AwsFrameInfo *)arg;
//   if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
//     data[len] = 0;
//     if (strcmp((char *)data, "toggle") == 0) {
//       ledState = !ledState;
//       notifyClients();
//     }
//   }
// }

// void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
//              void *arg, uint8_t *data, size_t len) {
//   switch (type) {
//     case WS_EVT_CONNECT:
//       Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
//       break;
//     case WS_EVT_DISCONNECT:
//       Serial.printf("WebSocket client #%u disconnected\n", client->id());
//       break;
//     case WS_EVT_DATA:
//       handleWebSocketMessage(arg, data, len);
//       break;
//     case WS_EVT_PONG:
//     case WS_EVT_ERROR:
//       break;
//   }
// }

// String processor(const String &var) {
//   Serial.println(var);
//   if (var == "STATE") {
//     if (ledState) {
//       return "ON";
//     } else {
//       return "OFF";
//     }
//   }
//   return String();
// }

// void initWebSocket() {
//   ws.onEvent(onEvent);
//   server.addHandler(&ws);
// }


void setup() {
  Serial.begin(115200);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Interrupt level 1, default 0
    .dma_buf_count = 10,
    .dma_buf_len = 64,
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256
  };


  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

  static const i2s_pin_config_t pin_config = {
    .bck_io_num = 14,
    .ws_io_num = 15,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 22
  };

  i2s_set_pin(I2S_NUM_0, &pin_config);
  uint32_t bits_cfg = (I2S_BITS_PER_CHAN_32BIT << 16) | I2S_BITS_PER_SAMPLE_32BIT;
  i2s_set_clk(I2S_NUM_0, 44100, bits_cfg, I2S_CHANNEL_STEREO);
  WiFi.mode(WIFI_STA);

  // WiFi.begin(ssid, password);
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(1000);
  //   Serial.println("Connecting to WiFi..");
  // }

  // Print ESP Local IP Address
  Serial.println(WiFi.localIP());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // initWebSocket();

  // // Route for root / web page
  // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  //   request->send_P(200, "text/html", index_html, processor);
  // });

  // // Start server
  // server.begin();

  Serial.println("START");
}

void loop() {
  // Serial.println("Loop");
  long long startime = millis();

  // Reset bandValues[]
  for (int i = 0; i < NUM_BANDS; i++) {
    bandValues[i] = 0;
  }

  size_t bytes_read;
  i2s_read(I2S_NUM_0, samples, SAMPLES * 8, &bytes_read, portMAX_DELAY);

  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (int16_t)(samples[i * 2] >> 16);
    vReal[i] += (int16_t)(samples[i * 2 + 1] >> 16);
    vReal[i] /= 2;
    vImag[i] = 0;
    // Serial.print((int16_t)(samples[i * 2+1] >> 16));
    // Serial.print(" ");
  }
  // Serial.println(" ");

  // Compute FFT
  // FFT.DCRemoval();
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();

  Serial.write(27);     // ESC command
  Serial.print("[2J");  // clear screen command
  Serial.write(27);
  Serial.print("[H");  // cursor to home command
  // Analyse FFT results
  for (int i = 2; i < (SAMPLES / 2); i++) {  // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
    if (vReal[i] > NOISE) {                  // Add a crude noise filter

      if (i<=3 )           bandValues[0]  += (int)vReal[i];
      if (i>3   && i<=7  ) bandValues[1]  += (int)vReal[i];
      if (i>7   && i<=18  ) bandValues[2]  += (int)vReal[i];
      if (i>18   && i<=48  ) bandValues[3]  += (int)vReal[i];
      if (i>48   && i<=128  ) bandValues[4]  += (int)vReal[i];
      if (i>128             ) bandValues[5]  += (int)vReal[i];

      // if (i <= 3) bandValues[0] += (int)vReal[i];
      // if (i > 3 && i <= 6) bandValues[1] += (int)vReal[i];
      // if (i > 6 && i <= 13) bandValues[2] += (int)vReal[i];
      // if (i > 13 && i <= 27) bandValues[3] += (int)vReal[i];
      // if (i > 27 && i <= 55) bandValues[4] += (int)vReal[i];
      // if (i > 55 && i <= 112) bandValues[5] += (int)vReal[i];
      // if (i > 112 && i <= 229) bandValues[6] += (int)vReal[i];
      // if (i > 229) bandValues[7] += (int)vReal[i];
    }
    // Serial.print(vReal[i]);
    // Serial.print(" ");
  }
  // Serial.println(" ");
  for (int i = 0; i < NUM_BANDS; i++) {
    myData.bands[i] = bandValues[i] / BAND_ATTENUATION;
  }
  // // Serial.write(27);     // ESC command
  // // Serial.print("[2J");  // clear screen command
  // // Serial.write(27);
  // // Serial.print("[H");  // cursor to home command
  // // for (int i = 0; i < NUM_BANDS; i++){
  // //   Serial.print(bandValues[i]/BAND_ATTENUATION);
  // //   Serial.print(" ");
  // // }

  // // String ws_values(bandValues[0] / BAND_ATTENUATION);
  // // for (int i = 1; i < NUM_BANDS; i++) {
  // //   ws_values = ws_values + " " + String(bandValues[i] / BAND_ATTENUATION);
  // // }
  // // ws.textAll(ws_values);
  // // ws.cleanupClients();

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  if (result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.println("Error sending the data");
  }

  Serial.print("time:");
  Serial.println(millis() - startime);
}
