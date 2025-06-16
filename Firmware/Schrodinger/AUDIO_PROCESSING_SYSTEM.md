## Critical Fixes Applied

### Stack Overflow Resolution
- **Problem**: FFT task was crashing with "Stack canary watchpoint triggered"
- **Root Cause**: Large arrays allocated on 4KB stack (peak_t peaks[MAX_BIN] ≈ 928 bytes)
- **Solution**: 
  - Increased FFT task stack from 4KB to 12KB
  - Moved large arrays to PSRAM using `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
  - Used static arrays instead of stack allocation for selected peaks

### Memory Management Improvements
- **Problem**: Heap fragmentation causing system instability
- **Root Cause**: Large buffers allocated on limited heap (~39KB available)
- **Solution**:
  - All large buffers now use PSRAM (4MB available)
  - Pre-allocated static buffers for ESP-NOW and WebSocket data
  - Eliminated dynamic allocation in critical audio processing path

### Race Condition Fixes
- **Problem**: Potential buffer access conflicts between BT and FFT tasks
- **Solution**: 
  - Improved mutex handling with proper timeouts
  - Added buffer lifecycle management
  - Enhanced error checking and recovery

## Troubleshooting

### Common Issues
1. **Audio Dropouts**: Check circular buffer size, reduce FFT rate
2. **Device Sync Issues**: Verify ESP-NOW channel configuration
3. **Web Interface Lag**: Check WiFi signal strength
4. **Memory Issues**: Monitor heap usage, adjust buffer sizes
5. **Stack Overflow**: Ensure FFT task has sufficient stack (12KB minimum)

### Debug Features
- Serial logging with configurable levels
- Memory monitoring and reporting
- ESP-NOW transmission status
- WebSocket connection status
- PSRAM allocation tracking
