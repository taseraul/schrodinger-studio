#ifndef WEBSOCKET_SERVER_HPP
#define WEBSOCKET_SERVER_HPP

#include "../core/logger.hpp"
#include "../core/config_manager.hpp"
#include "../core/system_manager.hpp"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "Arduino_JSON.h"
#include <map>
#include <set>

// WebSocket server states
typedef enum {
    WS_STATE_UNINITIALIZED = 0,
    WS_STATE_INITIALIZING,
    WS_STATE_RUNNING,
    WS_STATE_ERROR,
    WS_STATE_SHUTDOWN
} websocket_state_t;

// Client performance tracking
struct ClientStats {
    uint32_t messages_sent = 0;
    uint32_t messages_failed = 0;
    uint32_t last_success_time = 0;
    uint32_t consecutive_failures = 0;
    bool is_responsive = true;
};

// WebSocket server class
class WebSocketServer {
public:
    static WebSocketServer& getInstance();
    
    // Lifecycle management
    bool initialize(AsyncWebServer* server);
    void shutdown();
    void update();
    
    // State management
    websocket_state_t getState() const { return ws_state_; }
    
    // Client management
    int getClientCount() const;
    void performHealthCheck();
    void cleanupClients();
    
    // Message sending
    bool sendTextMessage(const String& message);
    bool sendBinaryMessage(const uint8_t* data, size_t length);
    
    // Statistics
    uint32_t getTotalMessagesSent() const { return total_messages_sent_; }
    uint32_t getTotalMessagesFailed() const { return total_messages_failed_; }
    
    // Error handling
    void handleWebSocketError(const char* error, const char* context);
    
private:
    WebSocketServer() = default;
    ~WebSocketServer() = default;
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;
    
    // Internal state
    websocket_state_t ws_state_ = WS_STATE_UNINITIALIZED;
    AsyncWebSocket* ws_ = nullptr;
    
    // Client tracking
    std::map<uint32_t, ClientStats> client_stats_;
    uint32_t last_stats_cleanup_ = 0;
    
    // Statistics
    uint32_t total_messages_sent_ = 0;
    uint32_t total_messages_failed_ = 0;
    uint32_t error_count_ = 0;
    
    // Event handlers
    static void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                                AwsEventType type, void* arg, uint8_t* data, size_t len);
    void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);
    
    // Internal methods
    void setState(websocket_state_t state);
    bool sendToClient(AsyncWebSocketClient& client, const char* data, size_t length, bool is_binary = false);
    void updateClientStats(uint32_t client_id, bool success);
    void cleanupClientStats();
    bool isMemoryHealthy() const;
};

// Global WebSocket server access
#define WEBSOCKET_SERVER WebSocketServer::getInstance()

#endif // WEBSOCKET_SERVER_HPP
