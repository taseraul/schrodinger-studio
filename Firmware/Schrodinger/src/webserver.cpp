#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "webserver.hpp"
#include "fft.hpp"
#include "SPIFFS.h"
#include "esp_log.h"
#include "Arduino_JSON.h"
#include "now.hpp"
#include "esp_heap_caps.h"
#include "memory_manager.hpp"
#include "esp_wifi.h"
#include <map>
#include <set>

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

  esp_wifi_set_ps(WIFI_PS_NONE);  // Disable WiFi power save for better coexistence
  
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

  channel = WiFi.channel();
  
  ESP_LOGI("wifi", "Soft AP MAC Address: %s", WiFi.softAPmacAddress());
  ESP_LOGI("wifi", "Station IP Address: ", WiFi.localIP());
  ESP_LOGI("wifi", "Wi-Fi Channel: ", channel);
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

  ESP_LOGI(TAG, "Received update for device");
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

  // Use PSRAM buffer instead of String for message processing
  char* msg_buffer = (char*)malloc_psram_fallback(len + 1);
  if (!msg_buffer) {
    ESP_LOGW(TAG, "Failed to allocate message buffer");
    return;
  }
  
  memcpy(msg_buffer, data, len);
  msg_buffer[len] = '\0';  // Null terminate
  
  JSONVar json = JSON.parse(msg_buffer);
  free_psram_fallback(msg_buffer);

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

// Client performance tracking
struct ClientStats {
  uint32_t messages_sent = 0;
  uint32_t messages_failed = 0;
  uint32_t last_success_time = 0;
  uint32_t consecutive_failures = 0;
  bool is_responsive = true;
};

static std::map<uint32_t, ClientStats> client_stats;
static uint32_t last_stats_cleanup = 0;

// Enhanced WebSocket client management with adaptive rate limiting
void notifyClients(String json) {
  if (ws.count() == 0) {
    return; // No clients connected
  }
  
  // // Check memory before attempting to send
  // uint32_t free_heap = ESP.getFreeHeap();
  // if (free_heap < 15000) { // Critical memory threshold
  //   ESP_LOGW(TAG, "Low memory (%d bytes), skipping WebSocket send", free_heap);
  //   return;
  // }
  
  // Clean up unresponsive clients first
  ws.cleanupClients();
  
  // Check if we still have clients after cleanup
  if (ws.count() == 0) {
    return;
  }
  
  // Use direct buffer method to avoid String → shared_ptr conversion
  const char* json_cstr = json.c_str();
  size_t json_len = json.length();
  
  // Validate buffer before sending
  if (!json_cstr || json_len == 0 || json_len > 4096) {
    ESP_LOGW(TAG, "Invalid WebSocket message: ptr=%p, len=%d", json_cstr, json_len);
    return;
  }
  
  try{
    ws.textAll(json_cstr);
  } catch (const std::exception& e) {
    ESP_LOGE(TAG, "WebSocket send exception: %s", e.what());
  } catch (...) {
    ESP_LOGE(TAG, "WebSocket send unknown exception");
  }
}

// Send binary WebSocket data with optimized protocol
void sendBinaryBatch(const uint8_t* data, size_t length) {
  if (ws.count() == 0) {
    return; // No clients connected
  }
  
  // // Check memory before attempting to send
  // uint32_t free_heap = ESP.getFreeHeap();
  // if (free_heap < 15000) { // Critical memory threshold
  //   ESP_LOGW(TAG, "Low memory (%d bytes), skipping binary WebSocket send", free_heap);
  //   return;
  // }
  
  // Clean up unresponsive clients first
  ws.cleanupClients();
  
  // Check if we still have clients after cleanup
  if (ws.count() == 0) {
    return;
  }
  
  // Validate binary data
  if (!data || length == 0 || length > 2048) {
    ESP_LOGW(TAG, "Invalid binary WebSocket data: ptr=%p, len=%d", data, length);
    return;
  }
  
  try {
    ws.binaryAll(data, length);
  } catch (const std::exception& e) {
    ESP_LOGE(TAG, "Binary WebSocket send exception: %s", e.what());
  } catch (...) {
    ESP_LOGE(TAG, "Binary WebSocket send unknown exception");
  }
}

// Clean up stats for disconnected clients
void cleanupClientStats() {
  auto& clients = ws.getClients();
  std::set<uint32_t> active_clients;
  
  // Collect active client IDs
  for (auto& client : clients) {
    if (client.status() == WS_CONNECTED) {
      active_clients.insert(client.id());
    }
  }
  
  // Remove stats for disconnected clients
  auto it = client_stats.begin();
  while (it != client_stats.end()) {
    if (active_clients.find(it->first) == active_clients.end()) {
      ESP_LOGD(TAG, "Removing stats for disconnected client %d", it->first);
      it = client_stats.erase(it);
    } else {
      ++it;
    }
  }
  
  ESP_LOGI(TAG, "Client stats cleanup: %d active clients, %d stats entries", 
           active_clients.size(), client_stats.size());
}

// Enhanced WebSocket health monitoring and cleanup
void websocketHealthCheck() {
  static uint32_t last_health_check = 0;
  uint32_t current_time = millis();
  
  // Run health check every 10 seconds
  if (current_time - last_health_check < 10000) {
    return;
  }
  
  last_health_check = current_time;
  
  if (ws.count() == 0) {
    return; // No clients to check
  }
  
  // Get memory status
  uint32_t free_heap = ESP.getFreeHeap();
  
  // Aggressive cleanup if memory is low
  if (free_heap < 20000) {
    ESP_LOGW(TAG, "Low memory (%d bytes), performing aggressive WebSocket cleanup", free_heap);
    
    // Clean up clients multiple times to ensure unresponsive ones are removed
    for (int i = 0; i < 3; i++) {
      ws.cleanupClients(1); // Keep only 1 client max when memory is low
      if (ws.count() <= 1) break;
    }
    
    ESP_LOGI(TAG, "After cleanup: %d clients remaining, %d bytes free", ws.count(), ESP.getFreeHeap());
  } else {
    // Normal cleanup - remove unresponsive clients
    ws.cleanupClients(2); // Allow up to 8 clients normally
  }
  
  // Log WebSocket health status
  if (ws.count() > 0) {
    ESP_LOGD(TAG, "WebSocket health: %d clients, %d bytes free", ws.count(), free_heap);
  }
}

// Initialize web server
void webserver_init() {
  Serial.println("Initializing Web Server...");
  
  // Print memory before webserver init
  Serial.printf("Free heap before webserver init: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM before webserver init: %d bytes\n", ESP.getFreePsram());
  
  initWebSocket();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Check memory health before serving files
    if (!check_memory_health()) {
      ESP_LOGW(TAG, "Memory health check failed before serving file");
      
      // Try to recover memory
      force_garbage_collection();
      
      // Check again after cleanup
      if (!check_memory_health()) {
        char errorMsg[128];
        snprintf(errorMsg, sizeof(errorMsg), 
          "Service temporarily unavailable - insufficient memory (%d bytes free)", 
          ESP.getFreeHeap());
        request->send(503, "text/plain", errorMsg);
        return;
      }
    }
    
    // Additional check for largest free block to ensure we can handle the file
    size_t largestBlock = get_largest_free_block();
    if (largestBlock < 8192) {  // Need at least 8KB contiguous for file serving
      ESP_LOGW(TAG, "Insufficient contiguous memory: %d bytes largest block", largestBlock);
      force_garbage_collection();
      
      largestBlock = get_largest_free_block();
      if (largestBlock < 8192) {
        request->send(503, "text/plain", "Service temporarily unavailable - memory fragmentation");
        return;
      }
    }
    
    // Log memory usage for debugging
    ESP_LOGI(TAG, "Serving main page with %d bytes heap free, %d largest block", 
             ESP.getFreeHeap(), largestBlock);
    request->send(SPIFFS,"/client.html" ,"text/html");
  });

  // Add error handler for better debugging
  server.onNotFound([](AsyncWebServerRequest *request) {
    ESP_LOGW(TAG, "404 - File not found: %s", request->url().c_str());
    request->send(404, "text/plain", "File not found");
  });

  server.begin();
  
  // Print memory after webserver init
  Serial.printf("Free heap after webserver init: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM after webserver init: %d bytes\n", ESP.getFreePsram());
  Serial.println("Web Server initialized successfully");
}
