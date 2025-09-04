#include "i2s.hpp"
#include "fft.hpp"
#include "webserver.hpp"
#include "now.hpp"
#include "bt.hpp"
#include "memory_manager.hpp"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "WiFi.h"

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    ESP_LOGI(TAG, "=== SCHRODINGER FIRMWARE START ===");
    
    ESP_LOGI(TAG, "Initial heap: %d bytes, PSRAM: %d bytes", ESP.getFreeHeap(), ESP.getFreePsram()); // Print initial memory status
    
    // Release BLE memory for Classic Bluetooth to free up resources
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release BLE memory: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "BLE memory released successfully");
    }
    ESP_LOGI(TAG, "After BLE release - Heap: %d bytes", ESP.getFreeHeap());
    
    // Initialize WiFi first to establish network connection
    wifi_init();
    ESP_LOGI(TAG, "After WiFi init - Heap: %d bytes", ESP.getFreeHeap());
    
    // Small delay to ensure WiFi is stable before starting Bluetooth
    ESP_LOGI(TAG, "Waiting before Bluetooth initialization...");
    delay(2000);  // Increased delay to ensure stability
    
    // Initialize Bluetooth A2DP sink
    ESP_LOGI(TAG, "Starting Bluetooth A2DP initialization...");
    bt_init();
    ESP_LOGI(TAG, "After BT init - Heap: %d bytes", ESP.getFreeHeap());
    
    // Restore WiFi connection after BT initialization
    bt_post_init_wifi_restore();
    ESP_LOGI(TAG, "After WiFi restore - Heap: %d bytes", ESP.getFreeHeap());
    
    // Initialize file system and other components
    fs_init();
    ESP_LOGI(TAG, "After FS init - Heap: %d bytes", ESP.getFreeHeap());
    
    now_init();
    ESP_LOGI(TAG, "After NOW init - Heap: %d bytes", ESP.getFreeHeap());
    
    webserver_init();
    ESP_LOGI(TAG, "After webserver init - Heap: %d bytes", ESP.getFreeHeap());
    
    // Print final memory status
    ESP_LOGI(TAG, "=== INITIALIZATION COMPLETE ===");
    ESP_LOGI(TAG, "Free heap: %d bytes", ESP.getFreeHeap());
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", ESP.getFreePsram());
    ESP_LOGI(TAG, "Largest free block: %d bytes", ESP.getMaxAllocHeap());
    
    // Force garbage collection
    ESP.getHeapSize(); // This can trigger cleanup
    
    // IMPORTANT: Do NOT initialize I2S separately when using Bluetooth A2DP
    // The A2DP library handles its own I2S initialization on different pins
    // Initialize FFT task to process Bluetooth audio samples
    fft_task_init();
    ESP_LOGI(TAG, "After FFT init - Heap: %d bytes", ESP.getFreeHeap());
    // i2s_init();  // This conflicts with A2DP I2S initialization
    
    ESP_LOGI(TAG, "System initialization completed successfully");
}

// Memory monitoring variables
unsigned long lastMemoryCheck = 0;
const unsigned long MEMORY_CHECK_INTERVAL = 5000; // Check every 5 seconds
uint32_t initialHeap = 0;
uint32_t minHeapSeen = UINT32_MAX;

void loop() {
    // Memory monitoring and recovery
    unsigned long currentTime = millis();
    if (currentTime - lastMemoryCheck >= MEMORY_CHECK_INTERVAL) {
        lastMemoryCheck = currentTime;
        
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t freePsram = ESP.getFreePsram();
        uint32_t largestBlock = ESP.getMaxAllocHeap();
        
        // Track minimum heap seen
        if (freeHeap < minHeapSeen) {
            minHeapSeen = freeHeap;
        }
        
        // Initialize baseline on first run
        if (initialHeap == 0) {
            initialHeap = freeHeap;
        }
        
        int32_t heapChange = (int32_t)freeHeap - (int32_t)initialHeap;
        
        // Enhanced memory logging
        ESP_LOGI(TAG, "MEMORY: Heap=%d (Δ%d), Min=%d, PSRAM=%d, Block=%d", 
                 freeHeap, heapChange, minHeapSeen, freePsram, largestBlock);
        
        // Perform WebSocket health check and cleanup
        websocketHealthCheck();
        
        // Check memory health using memory manager
        if (!check_memory_health()) {
            Serial.println("Memory health check failed - attempting recovery");
            
            // Force garbage collection
            force_garbage_collection();
            
            // Check again after cleanup
            uint32_t freeHeapAfter = ESP.getFreeHeap();
            Serial.printf("Memory after cleanup: %d bytes (recovered %d bytes)\n", 
                         freeHeapAfter, freeHeapAfter - freeHeap);
            
            // If still critical after cleanup, restart
            if (freeHeapAfter < CRITICAL_HEAP_THRESHOLD) {
                Serial.println("CRITICAL: Memory still critically low after cleanup - restarting");
                print_memory_info();
                delay(1000);
                ESP.restart();
            }
        }
        
        // Low memory warning
        if (freeHeap < LOW_HEAP_THRESHOLD) {
            Serial.println("WARNING: Low heap memory detected");
            
            // Proactive garbage collection for low memory
            force_garbage_collection();
        }
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}
