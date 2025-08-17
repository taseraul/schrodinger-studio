#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "webserver.hpp"
#include "../processing/fft_processor.hpp"
#include "SPIFFS.h"
#include "esp_log.h"
#include "Arduino_JSON.h"
#include "espnow_manager.hpp"
#include "esp_heap_caps.h"
#include "../core/memory_utils.hpp"
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
    // TODO: Integrate with ESP-NOW manager for device control
    // ESPNOW_MGR.setBand(band, id);
  }

  if (json.hasOwnProperty("lock")) {
    bool lock = (bool)json["lock"];
    ESP_LOGI(TAG, "Set lock to %s for id %d", lock ? "true" : "false", id);
    // TODO: Implement lock functionality
  }

  if (json.hasOwnProperty("flash")) {
    bool flash = (bool)json["flash"];
    ESP_LOGI(TAG, "Set flash to %s for id %d", flash ? "true" : "false", id);
    // TODO: Integrate with ESP-NOW manager for device control
    // ESPNOW_MGR.setFlash(flash, id);
  }

  if (json.hasOwnProperty("red") && json.hasOwnProperty("green") && json.hasOwnProperty("blue")) {
    const uint8_t rgb[3] = {(uint8_t)json["red"], (uint8_t)json["green"], (uint8_t)json["blue"]};
    ESP_LOGI(TAG, "Set color RGB(%d, %d, %d) for id %d", rgb[0], rgb[1], rgb[2], id);
    // TODO: Integrate with ESP-NOW manager for device control
    // ESPNOW_MGR.setColor(rgb, id);
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
  
  // Check memory before attempting to send
  uint32_t free_heap = ESP.getFreeHeap();
  if (free_heap < 15000) { // Critical memory threshold
    ESP_LOGW(TAG, "Low memory (%d bytes), skipping WebSocket send", free_heap);
    return;
  }
  
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
  
  uint32_t current_time = millis();
  uint32_t successful_sends = 0;
  uint32_t failed_sends = 0;
  uint32_t skipped_sends = 0;
  
  try {
    // Send to each client individually with health monitoring
    auto& clients = ws.getClients();
    for (auto& client : clients) {
      if (client.status() != WS_CONNECTED) {
        continue;
      }
      
      uint32_t client_id = client.id();
      ClientStats& stats = client_stats[client_id];
      
      // Check if client is responsive
      if (!stats.is_responsive) {
        skipped_sends++;
        continue;
      }
      
      // Check client queue health before sending
      if (client.queueIsFull()) {
        stats.consecutive_failures++;
        failed_sends++;
        
        // Mark client as unresponsive after 5 consecutive failures
        if (stats.consecutive_failures >= 5) {
          stats.is_responsive = false;
          ESP_LOGW(TAG, "Client %d marked unresponsive (queue full)", client_id);
          
          // Close unresponsive clients to free resources
          client.close(1000, "Queue overflow");
          continue;
        }
        
        ESP_LOGD(TAG, "Client %d queue full, skipping send", client_id);
        continue;
      }
      
      // Attempt to send to this client
      bool send_success = client.text((const uint8_t*)json_cstr, json_len);
      
      if (send_success) {
        stats.messages_sent++;
        stats.last_success_time = current_time;
        stats.consecutive_failures = 0;
        stats.is_responsive = true;
        successful_sends++;
      } else {
        stats.messages_failed++;
        stats.consecutive_failures++;
        failed_sends++;
        
        // Mark as unresponsive after 3 consecutive send failures
        if (stats.consecutive_failures >= 3) {
          stats.is_responsive = false;
          ESP_LOGW(TAG, "Client %d marked unresponsive (send failures)", client_id);
        }
      }
    }
    
    // Log send statistics periodically
    static uint32_t send_count = 0;
    send_count++;
    
    if (send_count % 50 == 0) { // Log every 50th send
      ESP_LOGI(TAG, "WebSocket send #%d: %d OK, %d failed, %d skipped (%d bytes to %d clients)", 
               send_count, successful_sends, failed_sends, skipped_sends, json_len, ws.count());
    }
    
    // Clean up client stats periodically
    if (current_time - last_stats_cleanup > 30000) { // Every 30 seconds
      cleanupClientStats();
      last_stats_cleanup = current_time;
    }
    
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
  
  // Check memory before attempting to send
  uint32_t free_heap = ESP.getFreeHeap();
  if (free_heap < 15000) { // Critical memory threshold
    ESP_LOGW(TAG, "Low memory (%d bytes), skipping binary WebSocket send", free_heap);
    return;
  }
  
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
  
  uint32_t current_time = millis();
  uint32_t successful_sends = 0;
  uint32_t failed_sends = 0;
  uint32_t skipped_sends = 0;
  
  try {
    // Send to each client individually with health monitoring
    auto& clients = ws.getClients();
    for (auto& client : clients) {
      if (client.status() != WS_CONNECTED) {
        continue;
      }
      
      uint32_t client_id = client.id();
      ClientStats& stats = client_stats[client_id];
      
      // Check if client is responsive
      if (!stats.is_responsive) {
        skipped_sends++;
        continue;
      }
      
      // Check client queue health before sending
      if (client.queueIsFull()) {
        stats.consecutive_failures++;
        failed_sends++;
        
        // Mark client as unresponsive after 5 consecutive failures
        if (stats.consecutive_failures >= 5) {
          stats.is_responsive = false;
          ESP_LOGW(TAG, "Client %d marked unresponsive (binary queue full)", client_id);
          
          // Close unresponsive clients to free resources
          client.close(1000, "Binary queue overflow");
          continue;
        }
        
        ESP_LOGD(TAG, "Client %d binary queue full, skipping send", client_id);
        continue;
      }
      
      // Attempt to send binary data to this client
      bool send_success = client.binary(data, length);
      
      if (send_success) {
        stats.messages_sent++;
        stats.last_success_time = current_time;
        stats.consecutive_failures = 0;
        stats.is_responsive = true;
        successful_sends++;
      } else {
        stats.messages_failed++;
        stats.consecutive_failures++;
        failed_sends++;
        
        // Mark as unresponsive after 3 consecutive send failures
        if (stats.consecutive_failures >= 3) {
          stats.is_responsive = false;
          ESP_LOGW(TAG, "Client %d marked unresponsive (binary send failures)", client_id);
        }
      }
    }
    
    // Log send statistics periodically
    static uint32_t binary_send_count = 0;
    binary_send_count++;
    
    if (binary_send_count % 50 == 0) { // Log every 50th send
      ESP_LOGI(TAG, "Binary WebSocket send #%d: %d OK, %d failed, %d skipped (%d bytes to %d clients)", 
               binary_send_count, successful_sends, failed_sends, skipped_sends, length, ws.count());
    }
    
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

// Get WebSocket client count
int getWebSocketClientCount() {
  return ws.count();
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
    ws.cleanupClients(8); // Allow up to 8 clients normally
  }
  
  // Log WebSocket health status
  if (ws.count() > 0) {
    ESP_LOGI(TAG, "WebSocket health: %d clients, %d bytes free", ws.count(), free_heap);
  }
}

// Initialize web server
void webserver_init() {
  Serial.println("Initializing Web Server...");
  
  // Print memory before webserver init
  Serial.printf("Free heap before webserver init: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM before webserver init: %d bytes\n", ESP.getFreePsram());
  
  initWebSocket();

  // Add memory status endpoint for debugging
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Use minimal memory for status response
    char json[256];
    snprintf(json, sizeof(json), 
      "{\"heap\":%d,\"psram\":%d,\"wifi_connected\":%s,\"wifi_rssi\":%d,\"uptime\":%lu}",
      ESP.getFreeHeap(), ESP.getFreePsram(),
      WiFi.status() == WL_CONNECTED ? "true" : "false",
      WiFi.RSSI(),
      millis()
    );
    request->send(200, "application/json", json);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Simple memory check before serving files
    uint32_t free_heap = ESP.getFreeHeap();
    if (free_heap < 10000) {  // Need at least 10KB for file serving
      ESP_LOGW(TAG, "Low memory (%d bytes), cannot serve file", free_heap);
      char errorMsg[128];
      snprintf(errorMsg, sizeof(errorMsg), 
        "Service temporarily unavailable - insufficient memory (%d bytes free)", 
        free_heap);
      request->send(503, "text/plain", errorMsg);
      return;
    }
    
    // Log memory usage for debugging
    ESP_LOGI(TAG, "Serving main page with %d bytes heap free", free_heap);
    request->send(SPIFFS,"/client.html" ,"text/html");
  });

  // Add lightweight memory info endpoint
  server.on("/mem", HTTP_GET, [](AsyncWebServerRequest *request) {
    char response[64];
    snprintf(response, sizeof(response), "Heap: %d, PSRAM: %d", 
             ESP.getFreeHeap(), ESP.getFreePsram());
    request->send(200, "text/plain", response);
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
