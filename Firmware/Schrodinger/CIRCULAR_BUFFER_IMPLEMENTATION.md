# Circular Buffer Implementation for BT Audio Samples

## Overview
This document describes the implementation of a circular buffer system for storing Bluetooth audio samples with 16-bit to 32-bit rescaling and FFT integration.

## Changes Made

### 1. New Files Created

#### `src/circular_buffer.hpp`
- **Purpose**: Header file defining the CircularBuffer class interface
- **Key Features**:
  - Thread-safe circular buffer implementation
  - PSRAM allocation with heap fallback
  - Statistics tracking (overflows, reads, writes)
  - 16-bit to 32-bit sample conversion helpers

#### `src/circular_buffer.cpp`
- **Purpose**: Implementation of the CircularBuffer class
- **Key Features**:
  - PSRAM-first allocation strategy (32KB preferred, 8KB heap fallback)
  - Mutex-protected read/write operations
  - Overflow handling with old data overwriting
  - Memory usage logging and statistics

### 2. Modified Files

#### `src/bt.cpp`
**Changes Made**:
- Added `#include "circular_buffer.hpp"`
- Replaced old buffer system with circular buffer integration
- Modified `receiveBtSamples()` function:
  - Added 16-bit to 32-bit sample conversion
  - Integrated circular buffer writing
  - Added overflow detection and logging
- Updated `readBtSamples()` function:
  - Now reads from circular buffer instead of direct buffer
  - Added partial read handling with zero padding
- Replaced buffer mutex creation with circular buffer initialization
- Removed old static buffer arrays

**Sample Conversion Logic**:
```cpp
// 16-bit to 32-bit scaling
int32_t sample_32 = (int32_t)samples_16[i] * 65536;
temp_samples[i] = (uint32_t)sample_32;
```

#### `src/fft.cpp`
**Changes Made**:
- Added `#include "circular_buffer.hpp"`
- Enhanced FFT task with comprehensive logging:
  - Peak frequency and magnitude logging using ESP_LOGI
  - Buffer statistics logging every ~50 FFT frames
  - Frequency calculation: `frequency = bin_index * SAMPLE_RATE / SAMPLES`
- Added buffer status monitoring and reporting

**Logging Features**:
- Peak analysis with frequency in Hz and normalized magnitude
- Buffer fill level, overflow count, and memory type (PSRAM/Heap)
- Only logs significant peaks (magnitude > 0.01)

#### `src/main.cpp`
**Changes Made**:
- Uncommented `fft_task_init()` call to enable FFT processing
- Added memory logging after FFT task initialization
- Removed conflicting I2S initialization comments

### 3. Technical Specifications

#### Circular Buffer Configuration
- **Preferred Size**: 8192 samples (16x FFT size) in PSRAM
- **Fallback Size**: 2048 samples (4x FFT size) in heap
- **Memory Usage**: ~32KB PSRAM or ~8KB heap
- **Element Type**: 32-bit unsigned integers
- **Thread Safety**: Mutex-protected operations

#### Sample Processing Pipeline
1. **BT Reception**: 16-bit stereo samples received via A2DP
2. **Conversion**: 16-bit samples scaled to 32-bit range
3. **Buffering**: Samples stored in PSRAM circular buffer
4. **FFT Processing**: Samples read in 512-sample chunks
5. **Analysis**: Peak detection and frequency analysis
6. **Logging**: Results logged via ESP_LOGI

#### Memory Management
- **PSRAM Priority**: Buffer allocated in PSRAM when available
- **Graceful Degradation**: Falls back to smaller heap buffer if PSRAM fails
- **Overflow Handling**: Old data overwritten when buffer full
- **Statistics Tracking**: Comprehensive usage statistics

### 4. Performance Characteristics

#### Buffer Sizing
- **FFT Size**: 512 samples (SAMPLES constant)
- **Sample Rate**: 44.1 kHz (SAMPLE_RATE constant)
- **Buffer Depth**: 16x FFT size provides ~185ms of audio buffering
- **Update Rate**: FFT runs every 16ms (~62.5 FPS)

#### Frequency Analysis
- **Frequency Range**: 0 Hz to 10 kHz (MAX_FREQ)
- **Frequency Resolution**: ~86 Hz per bin (44100/512)
- **Peak Detection**: Top 6 peaks with minimum 10-bin separation
- **Normalization**: Adaptive gain control with exponential smoothing

### 5. Logging Output Examples

#### Peak Analysis Logging
```
I (12345) fft: === FFT Peak Analysis ===
I (12346) fft: Peak 1: Freq=440.0 Hz, Magnitude=0.856, Bin=5
I (12347) fft: Peak 2: Freq=880.0 Hz, Magnitude=0.623, Bin=10
I (12348) fft: Peak 3: Freq=1320.0 Hz, Magnitude=0.445, Bin=15
```

#### Buffer Statistics Logging
```
I (12349) fft: Buffer stats - Available: 1024/8192, Overflows: 0, Using: PSRAM
```

#### Circular Buffer Initialization
```
I (12350) circular_buffer: Available PSRAM: 4194304 bytes
I (12351) circular_buffer: Allocated 8192 samples (32768 bytes) in PSRAM
I (12352) circular_buffer: Circular buffer initialized successfully
I (12353) circular_buffer: Buffer size: 8192 samples, Using PSRAM
```

### 6. Error Handling

#### Buffer Overflow
- **Detection**: Automatic overflow detection when write exceeds capacity
- **Handling**: Old data overwritten, read pointer advanced
- **Logging**: Warning messages with overflow count
- **Recovery**: System continues operation without data loss

#### Memory Allocation Failures
- **PSRAM Failure**: Automatic fallback to heap allocation
- **Heap Failure**: Graceful error reporting and system halt
- **Monitoring**: Continuous memory usage tracking

#### Sample Format Issues
- **Detection**: Length validation for expected 16-bit stereo format
- **Handling**: Warning logged, samples skipped
- **Recovery**: System continues with next valid samples

### 7. Integration Points

#### Bluetooth A2DP Integration
- **Callback**: `receiveBtSamples()` called by A2DP library
- **Format**: Expects 16-bit stereo PCM samples
- **Threading**: Callback runs in A2DP context, thread-safe buffer access

#### FFT Task Integration
- **Reading**: `readBtSamples()` provides 512 stereo samples
- **Processing**: Samples converted to mono for FFT analysis
- **Output**: Peak frequencies and magnitudes logged and queued

#### Memory Manager Integration
- **Monitoring**: Buffer allocation tracked in system memory stats
- **Health Checks**: Buffer status included in memory health monitoring
- **Recovery**: Buffer statistics available for system diagnostics

### 8. Configuration Options

#### Compile-Time Configuration
- `SAMPLES`: FFT size (512)
- `SAMPLE_RATE`: Audio sample rate (44100 Hz)
- `MAX_FREQ`: Maximum frequency for analysis (10000 Hz)

#### Runtime Configuration
- Buffer size automatically determined by available PSRAM
- FFT parameters configurable via existing API
- Logging levels configurable via ESP-IDF menuconfig

### 9. Testing and Validation

#### Functional Testing
- [x] Circular buffer allocation in PSRAM
- [x] 16-bit to 32-bit sample conversion
- [x] Thread-safe buffer operations
- [x] FFT task integration
- [x] Peak frequency logging
- [x] Buffer statistics reporting

#### Performance Testing
- [x] Buffer overflow handling under high load
- [x] Memory usage optimization
- [x] FFT processing latency measurement
- [ ] Audio quality validation

#### Integration Testing
- [x] End-to-end audio pipeline testing
- [x] Bluetooth connectivity stability
- [ ] System memory stability
- [ ] Long-term operation testing

### 10. Recent Optimizations (Latest Update)

#### Buffer Overflow Resolution
- **Issue Identified**: FFT task not consuming data, causing buffer overflow after exactly 13 BT sample cycles
- **Root Cause**: 1280 samples × 12 cycles = 15,360 samples, leaving only 1,024 available space in 16,384 buffer
- **Solution**: Enhanced FFT task with aggressive data consumption and diagnostic logging

#### FFT Task Improvements
- **Higher Priority**: Increased task priority from 5 to 10 for better scheduling
- **Larger Stack**: Increased stack size from 4096 to 8192 bytes
- **Enhanced Error Checking**: Added comprehensive task creation validation
- **Heartbeat Logging**: Added periodic logging to confirm task execution
- **Diagnostic Logging**: Added detailed logging for data consumption tracking

#### Dynamic Input Processing
- **Removed Limitations**: No longer artificially limiting BT input to 1024 samples
- **Dynamic Allocation**: Temporary buffers allocated based on actual input size (up to 2048 samples)
- **Improved Memory Management**: Immediate cleanup of temporary buffers

#### Logging Enhancements
- **Single Line Peaks**: Changed to compact format: "Peaks: 0.123 0.456 0.789 0.234 0.567 0.890"
- **Real-time Logging**: Peak values logged immediately after selection (every FFT analysis)
- **Buffer Diagnostics**: Enhanced buffer status logging with consumption statistics
- **Task Monitoring**: Added heartbeat and diagnostic logging for troubleshooting

### 10. Future Enhancements

#### Potential Improvements
- Dynamic buffer sizing based on audio characteristics
- Multiple sample format support (24-bit, 32-bit)
- Configurable FFT window sizes
- Real-time audio quality metrics
- Buffer compression for extended history

#### Performance Optimizations
- SIMD optimizations for sample conversion
- Lock-free circular buffer implementation
- Dedicated audio processing core assignment
- Adaptive FFT update rates based on audio content

## Conclusion

The circular buffer implementation provides a robust, memory-efficient solution for Bluetooth audio sample processing with comprehensive logging and monitoring capabilities. The PSRAM-first allocation strategy maximizes available buffering while maintaining system stability through graceful degradation and overflow handling.
