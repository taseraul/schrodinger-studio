#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "webserver.hpp"
#include "fft.hpp"
#include "SPIFFS.h"
#include <AsyncElegantOTA.h>

// Wi-Fi credentials
const char *ssid = "";
const char *password = "";
int channel;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Initialize Wi-Fi
void wifi_init() {
  uint8_t retryCount = 30;
  Serial.println();
  Serial.print("Server MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Set device as both Station and Soft AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Schrodinger");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED && retryCount--) {
    delay(1000);
    Serial.println("Connecting to Wi-Fi...");
  }

  Serial.print("Soft AP MAC Address: ");
  Serial.println(WiFi.softAPmacAddress());

  channel = WiFi.channel();
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(channel);
}

// Initialize SPIFFS
void initFS() {
  if (!SPIFFS.begin()) {
    Serial.println("Error mounting SPIFFS");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
}

// Handle incoming WebSocket messages (currently unused)
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    // Handle WebSocket text message here
  }
}

// WebSocket event handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      Serial.println("WebSocket text message received");
      // handleWebSocketMessage(arg, data, len); // Enable if needed
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
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.serveStatic("/", SPIFFS, "/");
  AsyncElegantOTA.begin(&server); // Start ElegantOTA

  server.begin();
}
