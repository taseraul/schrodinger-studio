# Bluetooth Webserver Conflict Fix - Enhanced Solution

## Problem Description
After flashing the firmware, the webserver becomes inaccessible when connecting to a Bluetooth sink and crashes/reboots when refreshing the webpage. The system starts with only ~35KB heap after initialization, which drops to just 9KB when Bluetooth connects and webpage is refreshed, causing a critical memory crash.

## Root Cause Analysis
The issue was caused by severe memory constraints and conflicts between WiFi and Bluetooth Classic on the ESP32:

1. **Critical Memory Shortage**: System starts with only ~35KB heap, far below safe operating levels
2. **Memory Competition**: Both WiFi and Bluetooth Classic share the same memory pool
3. **Missing Memory Release**: BLE memory wasn't being released for Classic Bluetooth
4. **Resource Starvation**: Bluetooth A2DP connection consumed memory needed by the webserver
5. **Memory Fragmentation**: Large allocations failing due to fragmented heap
6. **No Memory Management**: No system to detect, prevent, and recover from low memory conditions

## Solution Implemented

### 1. Memory Management (Primary Fix)
- **File**: `src/main.cpp`
- **Change**: Uncommented `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` to free BLE memory for Classic Bluetooth
- **Impact**: Frees up ~64KB of memory for WiFi and Bluetooth Classic coexistence

### 2. Initialization Order Optimization
- **File**: `src/main.cpp`
- **Changes**:
  - Initialize WiFi first to establish stable network connection
  - Add 1-second delay before Bluetooth initialization
  - Add memory status logging after each initialization step

### 3. Bluetooth State Monitoring
- **Files**: `src/bt_monitor.hpp`, `src/bt_monitor.cpp`
- **Features**:
  - Track Bluetooth connection states (DISCONNECTED, CONNECTING, CONNECTED, AUDIO_PLAYING)
  - Monitor audio activity timestamps
  - Provide state query functions for other components

### 4. Enhanced Memory Monitoring
- **File**: `src/main.cpp`
- **Features**:
  - Continuous memory monitoring every 5 seconds
  - Critical memory warnings (< 20KB heap)
  - Low memory warnings (< 50KB heap)
  - Automatic restart as last resort for critical memory conditions

### 5. Webserver Improvements
- **File**: `src/webserver.cpp`
- **Features**:
  - Memory check before serving large files
  - Enhanced status endpoint with Bluetooth state information
  - Better error handling and logging
  - Service unavailable response when memory is critically low

### 6. Bluetooth Integration
- **File**: `src/bt.cpp`
- **Changes**:
  - Added state monitoring integration
  - Disabled auto-reconnect to save resources
  - Enhanced logging and error handling
  - Memory status reporting during initialization

### 7. Advanced Memory Manager (NEW)
- **Files**: `src/memory_manager.hpp`, `src/memory_manager.cpp`
- **Features**:
  - Comprehensive memory health monitoring
  - Automatic garbage collection and defragmentation
  - PSRAM fallback allocation for large objects
  - Memory fragmentation detection
  - Detailed memory statistics and reporting

### 8. Build Optimization
- **File**: `platformio.ini`
- **Changes**:
  - Enabled PSRAM malloc capabilities
  - Optimized compiler flags for memory usage
  - Reduced debug level to save memory
  - Configured AsyncTCP for lower memory usage

## New Features Added

### Status Endpoint
Access `http://[device-ip]/status` to get real-time system information:
```json
{
  "heap": 123456,
  "psram": 654321,
  "wifi_connected": true,
  "wifi_rssi": -45,
  "bt_state": 2,
  "bt_audio_active": false,
  "uptime": 12345678
}
```

### Bluetooth States
- `0`: BT_DISCONNECTED
- `1`: BT_CONNECTING  
- `2`: BT_CONNECTED
- `3`: BT_AUDIO_PLAYING

## Testing Instructions

### 1. Flash and Monitor
```bash
pio run --target upload
pio device monitor
```

### 2. Test Sequence
1. **Initial Test**: Access webserver at device IP - should work
2. **Connect Bluetooth**: Pair and connect a Bluetooth audio device
3. **Webserver Test**: Try accessing webserver again - should now work (previously failed)
4. **Play Audio**: Start playing audio through Bluetooth
5. **Concurrent Test**: Access webserver while audio is playing - should work
6. **Disconnect Test**: Disconnect Bluetooth and test webserver - should work
7. **Status Check**: Access `/status` endpoint to monitor system health

### 3. Monitor Serial Output
Watch for these key messages:
- Memory status reports every 5 seconds
- Bluetooth state changes
- Memory warnings if they occur
- Initialization sequence with memory usage

### 4. Expected Behavior
- Webserver should remain accessible throughout all Bluetooth operations
- Memory usage should be stable
- No service interruptions when switching between WiFi and Bluetooth usage

## Technical Details

### Memory Optimization
- Released ~64KB BLE memory for Classic Bluetooth
- Added PSRAM utilization (board has PSRAM enabled)
- Implemented memory threshold monitoring
- Added graceful degradation for low memory conditions

### Resource Management
- Disabled Bluetooth auto-reconnect to reduce overhead
- Optimized initialization sequence
- Added proper error handling and recovery mechanisms

### Monitoring and Debugging
- Real-time memory and Bluetooth state monitoring
- Enhanced logging throughout the system
- Status endpoint for remote monitoring
- Automatic recovery mechanisms

## Files Modified/Created
1. `src/main.cpp` - Enhanced memory management and monitoring
2. `src/bt.cpp` - Bluetooth optimization and state integration
3. `src/webserver.cpp` - Enhanced error handling and status reporting
4. `src/bt_monitor.hpp` - New Bluetooth state monitoring header
5. `src/bt_monitor.cpp` - New Bluetooth state monitoring implementation
6. `src/memory_manager.hpp` - New advanced memory management header
7. `src/memory_manager.cpp` - New advanced memory management implementation
8. `platformio.ini` - Build optimization and PSRAM configuration

## Verification
The solution addresses the core memory conflict issue while adding robust monitoring and recovery mechanisms. The webserver should now remain accessible regardless of Bluetooth connection state.
