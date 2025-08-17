#include "websocket_server.hpp"
#include "../core/memory_utils.hpp"

WebSocketServer& WebSocketServer::getInstance() {
    static WebSocketServer instance;
    return instance;
}

bool WebSocketServer::initialize(AsyncWebServer* server) {
    if (!server) {
        LOG_NETWORK_E("Cannot initialize WebSocket server with null HTTP server");
        return false;
    }
    
    LOG_NETWORK_I("WebSocket Server initialization starting...");
    setState(WS_STATE_INITIALIZING);
    
    // Create WebSocket instance
    ws_ = new AsyncWebSocket("/ws");
    if (!ws_) {
        LOG_NETWORK_E("Failed to create WebSocket instance");
        setState(WS_STATE_ERROR);
        return false;
    }
    
    // Set event handler
    ws_->onEvent(onWebSocketEvent);
    
    // Add WebSocket handler to HTTP server
    server->addHandler(ws_);
    
    // Reset statistics
    total_messages_sent_ = 0;
    total_messages_failed_ = 0;
    error_count_ = 0;
    last_stats_cleanup_ = millis();
    
    setState(WS_STATE_RUNNING);
    LOG_NETWORK_I("WebSocket Server initialized successfully");
    
    return true;
}

void WebSocketServer::shutdown() {
    LOG_NETWORK_I("WebSocket Server shutdown initiated");
    setState(WS_STATE_SHUTDOWN);
    
    // Clean up all clients
    if (ws_) {
        ws_->cleanupClients();
        delete ws_;
        ws_ = nullptr;
    }
    
    // Clear client stats
    client_stats_.clear();
    
    LOG_NETWORK_I("WebSocket Server shutdown completed");
}

void WebSocketServer::update() {
    if (ws_state_ != WS_STATE_RUNNING) {
        return;
    }
    
    // Perform periodic health check
    static uint32_t last_health_check = 0;
    uint32_t current_time = millis();
    
    if (current_time - last_health_check >= 10000) { // Every 10 seconds
        performHealthCheck();
        last_health_check = current_time;
    }
    
    // Clean up client stats periodically
    if (current_time - last_stats_cleanup_ >= 30000) { // Every 30 seconds
        cleanupClientStats();
        last_stats_cleanup_ = current_time;
    }
}

int WebSocketServer::getClientCount() const {
    return ws_ ? ws_->count() : 0;
}

void WebSocketServer::performHealthCheck() {
    if (!ws_) {
        return;
    }
    
    int client_count = ws_->count();
    
    // Check memory health
    if (!isMemoryHealthy()) {
        LOG_NETWORK_W("Low memory (%d bytes), performing aggressive WebSocket cleanup", 
                      ESP.getFreeHeap());
        
        // Aggressive cleanup when memory is low
        for (int i = 0; i < 3; i++) {
            ws_->cleanupClients(1); // Keep only 1 client max when memory is low
            if (ws_->count() <= 1) break;
        }
        
        LOG_NETWORK_I("After cleanup: %d clients remaining, %d bytes free", 
                      ws_->count(), ESP.getFreeHeap());
    } else {
        // Normal cleanup - remove unresponsive clients
        const auto& config = CONFIG_MGR.getNetworkConfig();
        ws_->cleanupClients(config.websocket_max_clients);
    }
    
    // Log health status
    if (client_count > 0) {
        LOG_NETWORK_D("WebSocket health: %d clients, %d bytes free", 
                      client_count, ESP.getFreeHeap());
    }
}

void WebSocketServer::cleanupClients() {
    if (!ws_) {
        return;
    }
    
    ws_->cleanupClients();
}

bool WebSocketServer::sendTextMessage(const String& message) {
    if (!ws_ || ws_state_ != WS_STATE_RUNNING) {
        return false;
    }
    
    if (ws_->count() == 0) {
        return true; // No clients, but not an error
    }
    
    // Check memory before sending
    if (!isMemoryHealthy()) {
        LOG_NETWORK_W("Low memory, skipping WebSocket text send");
        return false;
    }
    
    // Validate message
    const char* msg_cstr = message.c_str();
    size_t msg_len = message.length();
    
    if (!msg_cstr || msg_len == 0 || msg_len > 4096) {
        LOG_NETWORK_W("Invalid WebSocket text message: ptr=%p, len=%d", msg_cstr, msg_len);
        return false;
    }
    
    uint32_t successful_sends = 0;
    uint32_t failed_sends = 0;
    uint32_t skipped_sends = 0;
    
    try {
        // Send to each client individually with health monitoring
        auto& clients = ws_->getClients();
        for (auto& client : clients) {
            if (client.status() != WS_CONNECTED) {
                continue;
            }
            
            uint32_t client_id = client.id();
            ClientStats& stats = client_stats_[client_id];
            
            // Check if client is responsive
            if (!stats.is_responsive) {
                skipped_sends++;
                continue;
            }
            
            // Check client queue health
            if (client.queueIsFull()) {
                updateClientStats(client_id, false);
                failed_sends++;
                
                // Mark as unresponsive after too many failures
                if (stats.consecutive_failures >= 5) {
                    stats.is_responsive = false;
                    LOG_NETWORK_W("Client %d marked unresponsive (queue full)", client_id);
                    client.close(1000, "Queue overflow");
                }
                continue;
            }
            
            // Send to client
            bool success = client.text((const uint8_t*)msg_cstr, msg_len);
            updateClientStats(client_id, success);
            
            if (success) {
                successful_sends++;
            } else {
                failed_sends++;
            }
        }
        
        // Update global statistics
        total_messages_sent_ += successful_sends;
        total_messages_failed_ += failed_sends;
        
        // Log statistics periodically
        static uint32_t send_count = 0;
        send_count++;
        
        if (send_count % 50 == 0) {
            LOG_NETWORK_I("WebSocket text send #%d: %d OK, %d failed, %d skipped (%d bytes to %d clients)", 
                          send_count, successful_sends, failed_sends, skipped_sends, msg_len, ws_->count());
        }
        
        return successful_sends > 0;
        
    } catch (const std::exception& e) {
        LOG_NETWORK_E("WebSocket text send exception: %s", e.what());
        return false;
    } catch (...) {
        LOG_NETWORK_E("WebSocket text send unknown exception");
        return false;
    }
}

bool WebSocketServer::sendBinaryMessage(const uint8_t* data, size_t length) {
    if (!ws_ || ws_state_ != WS_STATE_RUNNING) {
        return false;
    }
    
    if (ws_->count() == 0) {
        return true; // No clients, but not an error
    }
    
    // Check memory before sending
    if (!isMemoryHealthy()) {
        LOG_NETWORK_W("Low memory, skipping WebSocket binary send");
        return false;
    }
    
    // Validate data
    if (!data || length == 0 || length > 2048) {
        LOG_NETWORK_W("Invalid WebSocket binary data: ptr=%p, len=%d", data, length);
        return false;
    }
    
    uint32_t successful_sends = 0;
    uint32_t failed_sends = 0;
    uint32_t skipped_sends = 0;
    
    try {
        // Send to each client individually
        auto& clients = ws_->getClients();
        for (auto& client : clients) {
            if (client.status() != WS_CONNECTED) {
                continue;
            }
            
            uint32_t client_id = client.id();
            ClientStats& stats = client_stats_[client_id];
            
            // Check if client is responsive
            if (!stats.is_responsive) {
                skipped_sends++;
                continue;
            }
            
            // Check client queue health
            if (client.queueIsFull()) {
                updateClientStats(client_id, false);
                failed_sends++;
                
                // Mark as unresponsive after too many failures
                if (stats.consecutive_failures >= 5) {
                    stats.is_responsive = false;
                    LOG_NETWORK_W("Client %d marked unresponsive (binary queue full)", client_id);
                    client.close(1000, "Binary queue overflow");
                }
                continue;
            }
            
            // Send binary data to client
            bool success = client.binary(data, length);
            updateClientStats(client_id, success);
            
            if (success) {
                successful_sends++;
            } else {
                failed_sends++;
            }
        }
        
        // Update global statistics
        total_messages_sent_ += successful_sends;
        total_messages_failed_ += failed_sends;
        
        // Log statistics periodically
        static uint32_t binary_send_count = 0;
        binary_send_count++;
        
        if (binary_send_count % 50 == 0) {
            LOG_NETWORK_I("WebSocket binary send #%d: %d OK, %d failed, %d skipped (%d bytes to %d clients)", 
                          binary_send_count, successful_sends, failed_sends, skipped_sends, length, ws_->count());
        }
        
        return successful_sends > 0;
        
    } catch (const std::exception& e) {
        LOG_NETWORK_E("WebSocket binary send exception: %s", e.what());
        return false;
    } catch (...) {
        LOG_NETWORK_E("WebSocket binary send unknown exception");
        return false;
    }
}

void WebSocketServer::handleWebSocketError(const char* error, const char* context) {
    error_count_++;
    log_error_with_context(LOG_TAG_NETWORK, error, context);
    
    // Set error state if too many errors
    if (error_count_ > 10) {
        setState(WS_STATE_ERROR);
        SYSTEM_MGR.handleCriticalError("Too many WebSocket errors", "WebSocket Server");
    }
}

void WebSocketServer::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    WebSocketServer& instance = getInstance();
    
    switch (type) {
        case WS_EVT_CONNECT:
            LOG_NETWORK_I("WebSocket client #%u connected from %s", 
                          client->id(), client->remoteIP().toString().c_str());
            break;
            
        case WS_EVT_DISCONNECT:
            LOG_NETWORK_I("WebSocket client #%u disconnected", client->id());
            break;
            
        case WS_EVT_DATA:
            LOG_NETWORK_D("WebSocket data received from client #%u", client->id());
            instance.handleWebSocketMessage(arg, data, len);
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebSocketServer::handleWebSocketMessage(void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
        return;
    }
    
    // Use PSRAM buffer for message processing
    char* msg_buffer = (char*)malloc_psram_fallback(len + 1);
    if (!msg_buffer) {
        LOG_NETWORK_W("Failed to allocate WebSocket message buffer");
        return;
    }
    
    memcpy(msg_buffer, data, len);
    msg_buffer[len] = '\0';
    
    JSONVar json = JSON.parse(msg_buffer);
    free_psram_fallback(msg_buffer);
    
    if (JSON.typeof(json) != "object") {
        LOG_NETWORK_W("Invalid JSON received in WebSocket message");
        return;
    }
    
    if (!json.hasOwnProperty("id")) {
        LOG_NETWORK_W("WebSocket message missing 'id' field");
        return;
    }
    
    int id = (int)json["id"];
    LOG_NETWORK_I("WebSocket message for device id: %d", id);
    
    // TODO: Process WebSocket commands
    // This would typically involve calling functions to handle device control
    // For now, just log the received commands
    
    if (json.hasOwnProperty("band")) {
        int band = (int)json["band"];
        LOG_NETWORK_I("Set band to %d for id %d", band, id);
    }
    
    if (json.hasOwnProperty("flash")) {
        bool flash = (bool)json["flash"];
        LOG_NETWORK_I("Set flash to %s for id %d", flash ? "true" : "false", id);
    }
    
    if (json.hasOwnProperty("red") && json.hasOwnProperty("green") && json.hasOwnProperty("blue")) {
        uint8_t r = (uint8_t)json["red"];
        uint8_t g = (uint8_t)json["green"];
        uint8_t b = (uint8_t)json["blue"];
        LOG_NETWORK_I("Set color RGB(%d, %d, %d) for id %d", r, g, b, id);
    }
}

void WebSocketServer::setState(websocket_state_t state) {
    if (ws_state_ != state) {
        const char* state_names[] = {
            "UNINITIALIZED", "INITIALIZING", "RUNNING", "ERROR", "SHUTDOWN"
        };
        
        LOG_NETWORK_I("WebSocket state: %s -> %s", 
                      state_names[ws_state_], state_names[state]);
        ws_state_ = state;
    }
}

void WebSocketServer::updateClientStats(uint32_t client_id, bool success) {
    ClientStats& stats = client_stats_[client_id];
    uint32_t current_time = millis();
    
    if (success) {
        stats.messages_sent++;
        stats.last_success_time = current_time;
        stats.consecutive_failures = 0;
        stats.is_responsive = true;
    } else {
        stats.messages_failed++;
        stats.consecutive_failures++;
        
        // Mark as unresponsive after 3 consecutive failures
        if (stats.consecutive_failures >= 3) {
            stats.is_responsive = false;
            LOG_NETWORK_W("Client %d marked unresponsive (send failures)", client_id);
        }
    }
}

void WebSocketServer::cleanupClientStats() {
    if (!ws_) {
        return;
    }
    
    auto& clients = ws_->getClients();
    std::set<uint32_t> active_clients;
    
    // Collect active client IDs
    for (auto& client : clients) {
        if (client.status() == WS_CONNECTED) {
            active_clients.insert(client.id());
        }
    }
    
    // Remove stats for disconnected clients
    auto it = client_stats_.begin();
    while (it != client_stats_.end()) {
        if (active_clients.find(it->first) == active_clients.end()) {
            LOG_NETWORK_D("Removing stats for disconnected client %d", it->first);
            it = client_stats_.erase(it);
        } else {
            ++it;
        }
    }
    
    LOG_NETWORK_D("Client stats cleanup: %d active clients, %d stats entries", 
                  active_clients.size(), client_stats_.size());
}

bool WebSocketServer::isMemoryHealthy() const {
    uint32_t free_heap = ESP.getFreeHeap();
    return free_heap >= 15000; // Critical memory threshold
}
