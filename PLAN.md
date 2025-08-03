# ESP32-CAM GenICam Implementation Summary

## Project Overview

This project implements a GenICam-compatible camera using an ESP32-CAM module that provides GigE Vision protocol compatibility over WiFi. The implementation enables existing industrial vision software (Aravis, Spinnaker, Vimba, go-aravis) to discover, control, and capture images from the ESP32-CAM as if it were a standard GigE Vision camera.

## Implementation Status: ✅ COMPLETE

The core GenICam/GigE Vision implementation is **fully functional** with the following completed components:

### ✅ Core Protocol Implementation

- **GVCP (GigE Vision Control Protocol)** - UDP port 3956 for device discovery and control
- **GVSP (GigE Vision Stream Protocol)** - UDP streaming for image transmission
- **Bootstrap Registers** - Standard GigE Vision device identification and configuration
- **GenICam XML** - Camera feature description served via memory reads

### ✅ Hardware Integration

- **ESP32-CAM Support** - Real camera capture using ESP-IDF camera driver
- **Image Processing** - JPEG/YUV422/RGB565 to Mono8 format conversion
- **WiFi Connectivity** - Network communication for protocol compliance
- **Pin Configuration** - AI-Thinker ESP32-CAM board support

### ✅ Image Streaming

- **Real Frame Capture** - Actual ESP32-CAM OV2640 sensor integration
- **GVSP Packet Structure** - Leader/Data/Trailer packet transmission
- **Acquisition Control** - Start/Stop commands via GVCP
- **Format Support** - Mono8 (8-bit grayscale) at 320x240 resolution

## Architecture

The implementation uses a multi-task ESP-IDF architecture:

1. **Main Task** - Initializes components and coordinates tasks
2. **GVCP Task** - Handles control protocol on UDP port 3956
3. **GVSP Task** - Manages image streaming protocol
4. **Camera Task** - Captures frames from ESP32-CAM hardware

**Core Modules:**

- `wifi_manager.c/h` - Network connectivity management
- `gvcp_handler.c/h` - GigE Vision Control Protocol implementation
- `gvsp_handler.c/h` - GigE Vision Stream Protocol implementation
- `camera_handler.c/h` - ESP32-CAM hardware interface
- `genicam_xml.c/h` - GenICam feature description serving

## Development Workflow

The project uses a **justfile-based workflow** for streamlined development:

```bash
# Complete development cycle
just dev [port]              # Validate XML, build, flash, and monitor

# Individual operations  
just setup                   # Setup development environment
just validate                # Validate GenICam XML schema compliance
just build                   # Build ESP32 project
just flash [port]            # Flash to ESP32-CAM
just monitor [port]          # Monitor serial output

# Testing and debugging
just test-discovery <ip>     # Test UDP discovery
just aravis-test            # Test with Aravis tools
just aravis-viewer          # Launch camera viewer
just capture-packets        # Network protocol debugging
```

## Testing Strategy

The project now supports a **hybrid testing architecture** for faster, more maintainable validation:

### Host-Based Testing (Fast, Platform-Independent)

- Protocol logic (GVCP, GVSP)
- GenICam XML generation
- Register map operations
- Packet structures and state machines
- Error handling and boundary tests

### ESP-IDF Integration Tests (Hardware-Specific)

- Camera sensor integration and FreeRTOS task coordination
- UDP socket creation and WiFi connection
- Real-world acquisition, packet sending, and performance validation

### Host Testing Workflow (via `make` in `tests/host/`):

- Fast unit tests: `gvcp_protocol`, `bootstrap`, `registers`, `genicam_xml`
- Platform abstraction enables shared code between ESP32 and host
- Mocks for logging, networking, and time

### ESP-IDF Test Coverage

- WiFi manager behavior and connectivity
- Camera hardware initialization and frame validity
- GVCP socket binding on port 3956

### Automation

- GitHub Actions runs host tests automatically on push and PR
- Future: support CI stubs or simulated hardware for ESP tests

## Future Enhancements (Optional)

The core implementation is complete and functional. Potential extensions include:

### Stability & Reliability
- [x] **GVCP Error Handling** - Add NACK handling and graceful unknown command handling
- [x] **Frame Buffering** - Buffer last N frames for basic resend if Aravis requests it
- [x] **Heartbeat Monitoring** - Connection health checking and recovery
- [x] **Enhanced Command Validation** - Comprehensive input validation for memory operations
- [x] **Connection State Management** - Robust state validation and cleanup functions
- [x] **Automatic Socket Recreation** - Network failure detection and recovery
- [x] **Frame Sequence Tracking** - Detect lost, duplicate, and out-of-order frames
- [x] **Enhanced Statistics** - 16 diagnostic registers for real-time monitoring

### Performance Optimizations  
- [x] **Precise Timestamps** - Use `esp_timer_get_time()` for GVSP leader/trailer timestamps
- [ ] **Camera Performance** - DMA or double-buffering for capture to avoid blocking
- [ ] **Packet Timing** - Optimized delay tuning for different network conditions
- [ ] **Resolution Scaling** - Support for larger image formats beyond 320x240

### Advanced Features
- [x] **Camera Controls** - ExposureTime, Gain, TriggerMode, WhiteBalance
- [x] **Pixel Formats** - Additional formats (RGB565, YUV422, RGB888) + existing (Mono8, JPEG)
- [x] **Compression** - JPEG streaming support for higher throughput

### Testing & Automation
- [ ] **Integration Tests** - Scripted testing using `arv-tool` + `tshark` to confirm full handshake + frame delivery
- [ ] **Protocol Validation** - Automated compliance testing against GenICam/GigE Vision standards
- [ ] **Performance Benchmarks** - Frame rate and latency measurement tools


### Interface & Usability
- [x] **Web Configuration** - Browser-based camera parameter adjustment
- [ ] **Serial Interface** - Runtime configuration via UART commands
- [x] **Status LEDs** - Visual indicators for connection and streaming status

## Project Structure

```
ESP32GenICam/
├── src/                    # Core implementation (6 modules)
├── components/             # ESP32-camera driver integration
├── tools/schema/           # GenICam XML validation schema
├── tests/host/             # Fast unit tests for host testing
├── justfile               # Development workflow commands
├── platformio.ini         # Build configuration and WiFi settings
└── CLAUDE.md              # Developer guidance and architecture notes
```


## Success Criteria: ✅ ACHIEVED

- [x] ESP32-CAM appears as GenICam-compatible device on network
- [x] Compatible with existing Aravis-based vision software
- [x] Real camera image capture and streaming functional
- [x] GigE Vision protocol compliance for control and streaming
- [x] Complete development workflow with validation and testing tools

The ESP32-CAM GenICam implementation is **production-ready** for internal testing of industrial vision software stacks.
---

## Appendix A: Host Testing Infrastructure Details

### Architecture

The test infrastructure separates hardware-dependent and platform-independent logic for high test coverage and short feedback cycles. Protocol logic is refactored into pure C modules with no ESP-IDF dependencies, organized under `src/gvcp`, `src/gvsp`, and `src/genicam`.

### Directory Structure

```
src/
├── gvcp/
│   ├── protocol.c/h   # Packet validation, header creation
│   ├── bootstrap.c/h  # Bootstrap register logic
│   └── discovery.c/h  # Discovery logic
├── gvsp/
│   ├── streaming.c/h  # Stream state machine
│   └── packets.c/h    # GVSP packet format
├── genicam/
│   ├── registers.c/h  # Register map + access rules
│   └── xml.c/h        # XML generation
├── utils/
│   ├── platform.h     # Platform abstraction
│   └── byte_order.c/h # Endianness

platform_esp32.c       # ESP32 implementation of `platform_interface_t`
platform_host.c        # Host implementation (used in tests)
```

### Host Tests

**File: **``

```makefile
# Build & run all tests
make run-tests
```

**Test files:**

- `test_gvcp_protocol.c`
- `test_bootstrap.c`
- `test_registers.c`
- `test_genicam_xml.c`

**Example: test\_gvcp\_protocol.c**

```c
void test_packet_validation() {
    gvcp_header_t header = { .packet_type = 0x42, .command = htons(0x0002) };
    assert(gvcp_validate_packet_header(&header, sizeof(header)));
}
```

### Mock Infrastructure

**File: **``

```c
int mock_network_send(...) {
    printf("MOCK: Sending packet\n");
    return len;
}
```

Used via `platform_interface_t` for unit testing send logic without real sockets.

### Platform Interface

**File: **``

```c
typedef struct {
    void (*log_info)(...);
    int (*network_send)(...);
    uint32_t (*get_time_ms)(void);
    void* (*malloc)(size_t);
    void (*free)(void*);
} platform_interface_t;
extern const platform_interface_t* platform;
```

### ESP Integration

Minimal Unity-based tests run with `idf.py test`. These focus on:

- Camera frame acquisition
- GVCP socket creation
- WiFi status

---

## Appendix B: Platform Abstraction Interface

**File: src/utils/platform.h**

```c
// Platform abstraction for ESP32 vs host testing
typedef struct {
    void (*log_info)(const char* tag, const char* format, ...);
    void (*log_error)(const char* tag, const char* format, ...);
    void (*log_warn)(const char* tag, const char* format, ...);
    int (*network_send)(const void* data, size_t len, void* addr);
    uint32_t (*get_time_ms)(void);
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);
} platform_interface_t;

extern const platform_interface_t* platform;
```

Both host and ESP32 implementations provide this interface for shared protocol logic.

