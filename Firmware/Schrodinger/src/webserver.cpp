#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "webserver.hpp"
#include "fft.hpp"
#include "SPIFFS.h"
#include "AsyncOTA.h"
#include "esp_log.h"
#include "Arduino_JSON.h"
#include "now.hpp"

// Wi-Fi credentials
const char *ssid = "Raul";
const char *password = "armaghedon";
int channel;

static const char *TAG = "ws";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Initialize Wi-Fi
void wifi_init() {
  uint8_t retryCount = 30;
  ESP_LOGV("wifi", "Server MAC Address: %s", WiFi.macAddress());

  // Set device as both Station and Soft AP
  WiFi.mode(WIFI_AP_STA);

  WiFi.begin(ssid, password);
  ESP_LOGI("wifi", "Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED && retryCount--) {
    delay(1000);
  }

  if (WiFi.status() != WL_CONNECTED){
    WiFi.softAP("Schrodinger");
  }

  ESP_LOGV("wifi", "Soft AP MAC Address: %s", WiFi.softAPmacAddress());

  channel = WiFi.channel();
  ESP_LOGV("wifi", "Station IP Address: ", WiFi.localIP());
  ESP_LOGV("wifi", "Wi-Fi Channel: ", channel);
}

// Initialize SPIFFS
void fs_init() {
  if (!SPIFFS.begin()) {
    ESP_LOGE("wifi", "Error mounting SPIFFS");
  } else {
    ESP_LOGV("wifi", "SPIFFS mounted successfully");
  }
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

  String msg = String((char*)data).substring(0, len);
  JSONVar json = JSON.parse(msg);

  if (JSON.typeof(json) != "object") {
    ESP_LOGW(TAG, "Invalid JSON received");
    return;
  }

  if (!json.hasOwnProperty("id")) {
    ESP_LOGW(TAG, "Missing 'id' field");
    return;
  }

  int id = (int)json["id"];
  ESP_LOGI(TAG, "Received update for device id: %d", id);

  if (json.hasOwnProperty("band")) {
    int band = (int)json["band"];
    ESP_LOGI(TAG, "Set band to %d for id %d", band, id);
    setBand(band, id);
  }

  if (json.hasOwnProperty("lock")) {
    bool lock = (bool)json["lock"];
    ESP_LOGI(TAG, "Set lock to %s for id %d", lock ? "true" : "false", id);
    // stub: updateLock(id, lock);
  }

  if (json.hasOwnProperty("flash")) {
    bool flash = (bool)json["flash"];
    ESP_LOGI(TAG, "Set flash to %s for id %d", flash ? "true" : "false", id);
    setFlash(flash, id);
  }

  if (json.hasOwnProperty("red") && json.hasOwnProperty("green") && json.hasOwnProperty("blue")) {
    const uint8_t rgb[3] = {(uint8_t)json["red"], (uint8_t)json["green"], (uint8_t)json["blue"]};
    ESP_LOGI(TAG, "Set color RGB(%d, %d, %d) for id %d", rgb[0], rgb[1], rgb[2], id);
    setColor(rgb,id);
  }

  // Echo back the packet to all clients
  // ws.textAll(data, len);
}

// WebSocket event handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      ESP_LOGV(TAG,"WebSocket client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      ESP_LOGV(TAG,"WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      ESP_LOGV(TAG,"WebSocket text message received");
      handleWebSocketMessage(arg, data, len); // Enable if needed
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// Initialize WebSocket
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// Notify all WebSocket clients
void notifyClients(String json) {
  ws.textAll(json);
}

// Initialize web server
void webserver_init() {
  initWebSocket();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // request->send(200, "text/html", index_html);
    request->send(SPIFFS,"/client.html" ,"text/html");
  });

  // server.serveStatic("/", SPIFFS, "/");
  AsyncOTA.begin(&server); // Start ElegantOTA

  server.begin();
}
