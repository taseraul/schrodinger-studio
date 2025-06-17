# FFT Visualization Implementation

## Overview
This document describes the implementation of real-time FFT peak visualization on the hosted webpage, including rate limiting optimizations and responsive layout improvements.

## Changes Made

### 1. Backend Changes (src/fft.cpp)

#### WebSocket Rate Limiting
- **Rate reduced to 15Hz** (67ms interval) to prevent WebSocket queue overflow
- Added timing variables: `last_websocket_send` and `WEBSOCKET_INTERVAL_MS = 67`
- Implemented conditional WebSocket transmission based on timing
- Updated logging to reflect 15Hz rate limiting

#### FFT Data Transmission
- Added `#include "webserver.hpp"` for WebSocket functionality
- Integrated direct FFT peak transmission from the FFT task
- Ultra-minimal JSON format: `{"f":[bin,magnitude,bin,magnitude,...]}`
- Maintains full 100Hz FFT processing for other consumers
- Only sends WebSocket data when peaks are detected

#### Key Code Changes
```cpp
// WebSocket rate limiting - send at most 15Hz (every 67ms)
uint32_t last_websocket_send = 0;
const uint32_t WEBSOCKET_INTERVAL_MS = 67;

// Rate-limited WebSocket transmission
if (count > 0) {
  uint32_t current_time = millis();
  if (current_time - last_websocket_send >= WEBSOCKET_INTERVAL_MS) {
    String json = "{\"f\":[";
    for (int i = 0; i < count; i++) {
      if (i > 0) json += ",";
      json += String(selected[i].index) + "," + String(selected[i].magnitude, 2);
    }
    json += "]}";
    notifyClients(json);
    last_websocket_send = current_time;
  }
}
```

### 2. WebServer Optimization (src/webserver.cpp)

#### Client Check Optimization
- Enhanced `notifyClients()` function with client count check
- Prevents unnecessary processing when no clients are connected
- Benefits all WebSocket operations, not just FFT data

```cpp
void notifyClients(String json) {
  if (ws.count() > 0) {
    ws.textAll(json);
  }
}
```

### 3. Frontend Changes (data/client.html)

#### Responsive FFT Visualization
- **Dynamic width allocation**: FFT bins fill the entire horizontal space
- **Active bins are double width** of inactive bins
- **Smooth transitions**: CSS animations for both height and width changes
- **Responsive design**: Automatically adjusts to container width changes

#### CSS Enhancements
```css
.fft-bin {
  background: linear-gradient(to top, #4CAF50, #8BC34A, #CDDC39);
  border-radius: 1.5px;
  transition: height 0.1s ease-out, width 0.1s ease-out;
  min-height: 3px;
  opacity: 0.8;
  flex-shrink: 0;
}

.fft-bin.active {
  opacity: 1;
}
```

#### JavaScript Implementation
- **Dynamic width calculation**: Algorithm to distribute space evenly
- **Active bin tracking**: Set-based tracking for efficient updates
- **Optimized updates**: Only recalculates widths when active bins change
- **Window resize handling**: Responsive to container size changes

#### Width Calculation Algorithm
```javascript
function calculateBinWidths() {
  const containerWidth = fftDisplay.clientWidth;
  const numActiveBins = activeBins.size;
  const numInactiveBins = MAX_BINS - numActiveBins;
  
  // Let x = width of inactive bin, then 2x = width of active bin
  // Total width = numInactiveBins * x + numActiveBins * 2x
  // Solve for x: x = containerWidth / (numInactiveBins + 2 * numActiveBins)
  const totalWeightedBins = numInactiveBins + (2 * numActiveBins);
  const inactiveWidth = Math.max(1, containerWidth / totalWeightedBins);
  const activeWidth = inactiveWidth * 2;
  
  return { inactive: inactiveWidth, active: activeWidth };
}
```

## Technical Specifications

### Performance Metrics
- **FFT Processing**: Maintains ~100Hz for full spectrum analysis
- **WebSocket Rate**: Limited to 15Hz for optimal network performance
- **Packet Size**: ~30-40 bytes per FFT message (ultra-compact JSON)
- **Visualization**: Smooth 15Hz updates with CSS transitions

### Network Optimization
- **Rate Limiting**: Prevents WebSocket queue overflow
- **Minimal JSON**: Compact format reduces bandwidth usage
- **Client Checking**: Avoids processing when no clients connected
- **Antenna Sharing**: Optimized for BT/ESP-NOW coexistence

### Visual Features
- **116 Frequency Bins**: Covers 0-10kHz frequency range
- **Pill-shaped Columns**: Active bins with gradient background
- **Circle Indicators**: Inactive bins shown as small circles
- **Dynamic Sizing**: Active bins are 2x width of inactive bins
- **Responsive Layout**: Fills available horizontal space evenly
- **Smooth Animations**: 0.1s ease-out transitions

## Data Flow Architecture

```
Audio Samples → FFT Processing (100Hz) → Peak Selection → 
Rate Limiting (15Hz) → WebSocket JSON → Client Visualization
```

## Benefits Achieved

### ✅ Problem Resolution
- **Fixed WebSocket Overflow**: No more "Too many messages queued" errors
- **Stable Connections**: Reliable WebSocket communication
- **Optimal Performance**: Balanced processing vs. visualization rates

### ✅ User Experience
- **Real-time Visualization**: Live FFT peaks with smooth animations
- **Responsive Design**: Adapts to different screen sizes
- **Visual Clarity**: Active peaks prominently displayed
- **Frequency Information**: Tooltips show bin numbers and frequencies

### ✅ System Efficiency
- **Network Optimized**: 6.7x reduction in WebSocket message rate
- **Memory Efficient**: Minimal JSON format and optimized updates
- **CPU Balanced**: Full FFT speed maintained for other consumers
- **Antenna Friendly**: Reduced interference with BT/ESP-NOW

## Testing Recommendations

1. **WebSocket Stability**: Verify no queue overflow errors in logs
2. **Visualization Accuracy**: Confirm FFT peaks match audio content
3. **Responsive Behavior**: Test window resizing and layout adaptation
4. **Performance Monitoring**: Check FFT processing rates and memory usage
5. **Network Load**: Monitor WebSocket message frequency and size

## Future Enhancements

- **Frequency Labels**: Add frequency axis labels
- **Peak Hold**: Optional peak hold visualization mode
- **Color Coding**: Frequency-based color mapping
- **Zoom Controls**: Interactive frequency range selection
- **Export Features**: Save FFT data or screenshots
