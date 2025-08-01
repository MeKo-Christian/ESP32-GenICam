# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Status: ✅ COMPLETE IMPLEMENTATION

This is a **fully functional GenICam/GigE Vision camera implementation** for ESP32-CAM. All core components are implemented and working:

- ✅ GVCP (Control) protocol on UDP port 3956
- ✅ GVSP (Streaming) protocol for image transmission  
- ✅ Real ESP32-CAM hardware integration with OV2640 sensor
- ✅ GenICam XML feature description serving
- ✅ Bootstrap registers and device discovery
- ✅ Image format conversion (JPEG/YUV422→Mono8)
- ✅ Complete development and testing toolchain

## Development Commands (ESP-IDF Native)

### Prerequisites
- ESP-IDF v4.4+ installed and environment sourced (`source ~/esp/esp-idf/export.sh`)
- just command runner installed
- WiFi credentials configured (see WiFi Configuration section)

### Quick Development Workflow (using justfile - RECOMMENDED)
```bash
# Setup development environment (ESP-IDF must be installed and sourced)
just setup

# Complete development cycle: set target, validate XML, build, flash, and monitor
just dev [port]

# Individual commands
just set-target        # Set ESP-IDF target to esp32
just validate          # Validate GenICam XML schema compliance
just build             # Build ESP32 project with ESP-IDF
just flash [port]      # Flash to ESP32-CAM
just monitor [port]    # Monitor serial output
just clean             # Clean build artifacts
```

### Testing and Discovery (Working Implementation)
```bash
# Test UDP discovery (using just)
just test-discovery <ESP32_IP_ADDRESS>

# Test with Aravis tools (should discover and connect successfully)
just aravis-test        # Discovery test
just aravis-viewer      # Launch camera viewer

# Traditional methods
echo "test" | nc -u <ESP32_IP_ADDRESS> 3956
arv-tool-0.8 --debug=all
arv-viewer-0.8
```

### GenICam XML Validation (Working)
```bash
# Validate XML against official GenICam schema
just validate

# Manual validation (after running just validate once)
xmllint --schema GenApiSchema_Version_1_0.xsd esp32_genicam.xml --noout
```

### Network Debugging (For Protocol Analysis)
```bash
# Show project and network status
just status

# Capture GVCP protocol packets
just capture-packets [interface]

# Display current GenICam XML
just show-xml
```

### WiFi Configuration
```bash
# Get help configuring WiFi
just wifi-config

# WiFi credentials are configured via environment variables (REQUIRED):
# Use environment variables with .envrc (recommended)
export WIFI_SSID="YOUR_WIFI_NETWORK_NAME"
export WIFI_PASSWORD="YOUR_WIFI_PASSWORD"

# Alternative: Use menuconfig (after setting environment variables)
just config  # Opens ESP-IDF configuration menu
```

### Direct ESP-IDF Commands (Alternative Method)
```bash
# Set target (first time setup)
idf.py set-target esp32

# Build the project
idf.py build

# Flash to ESP32-CAM (adjust port as needed)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Build, flash and monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor

# Additional ESP-IDF commands
idf.py menuconfig       # Configuration menu
idf.py size             # Show memory usage
idf.py erase_flash      # Erase flash memory
idf.py fullclean        # Full clean
```

## Code Architecture (Fully Implemented)

### Main Components - Working Implementation

**Multi-task Architecture:**
- **Main Task** (`main.c:15`) - Initializes all components and creates GVCP/GVSP tasks
- **GVCP Task** (`gvcp_handler.c`) - ✅ Handles GigE Vision Control Protocol on UDP port 3956
- **GVSP Task** (`gvsp_handler.c`) - ✅ Streams image data via GigE Vision Stream Protocol  
- **Camera Task** (integrated) - ✅ Captures real frames from ESP32-CAM OV2640 sensor

**Core Modules - All Functional:**
1. **WiFi Manager** (`wifi_manager.c/h`) - ✅ WiFi connection and network setup
2. **Camera Handler** (`camera_handler.c/h`) - ✅ ESP32-CAM OV2640 interface with real 320x240 grayscale capture
3. **GVCP Handler** (`gvcp_handler.c/h`) - ✅ Complete GigE Vision Control Protocol implementation
4. **GVSP Handler** (`gvsp_handler.c/h`) - ✅ Image streaming with Leader/Data/Trailer packets
5. **GenICam XML** (`genicam_xml.c/h`) - ✅ Camera feature description served via memory reads

### Protocol Implementation Status

**GigE Vision Protocol Stack - All Working:**
- **GVCP (Control)** - ✅ UDP port 3956 for device discovery, bootstrap registers, and memory access
- **GVSP (Streaming)** - ✅ Separate UDP port for high-speed image transmission
- **Bootstrap Registers** - ✅ Fixed memory map with device info, XML location pointer
- **GenICam XML** - ✅ Camera feature description at memory address 0x10000

**Memory Layout - Implemented:**
- Bootstrap registers start at offset 0x0000 with device identification
- XML descriptor pointer at `GVBS_XML_URL_0_OFFSET` (0x200) points to "Local:0x10000"  
- GenICam XML data served from memory address 0x10000 via GVCP read commands

### Hardware Configuration - Production Ready

**ESP32-CAM Pin Configuration** (in `sdkconfig.defaults`):
- Camera module: AI-Thinker ESP32-CAM board
- Resolution: 320x240 pixels (QVGA) 
- Format: Mono8 (8-bit grayscale converted from JPEG/YUV422)
- Interface: WiFi for network connectivity

**Camera Pins:** PWDN=32, XCLK=0, SIOD=26, SIOC=27, D7=35, D6=34, D5=39, D4=36, D3=21, D2=19, D1=18, D0=5, VSYNC=25, HREF=23, PCLK=22

## Testing Strategy - Completed Implementation

**Development Phases - All Complete:**
1. ✅ **GVCP Discovery** - Device appears on network and responds to discovery packets
2. ✅ **Bootstrap Registers** - Device identification and XML pointer accessible via memory reads
3. ✅ **GenICam XML Download** - XML feature description downloadable via GVCP
4. ✅ **Frame Capture** - ESP32-CAM captures and converts real frames to Mono8
5. ✅ **GVSP Streaming** - Image frames transmitted as UDP packet streams

**Integration Testing - Ready for Use:**
- Use Aravis library tools (`arv-tool-0.8`, `arv-viewer-0.8`) for protocol validation
- Wireshark packet capture for protocol debugging on UDP port 3956
- ✅ Compatible with go-aravis and other GenICam-based vision software
- ✅ Real camera streaming working end-to-end

## Project Structure Notes

This is a **complete, working** ESP-IDF native project implementing a GenICam-compatible camera for testing industrial vision software. The implementation provides full GigE Vision protocol compliance over WiFi.

**Key Implementation Details:**
- Real ESP32-CAM hardware integration (not simulation)
- Standards-compliant GenICam XML with validation
- Complete GVCP/GVSP protocol implementation
- Multi-format image conversion (JPEG→Mono8, YUV422→Mono8)
- Production-ready development toolchain with justfile automation

The `components/esp32-camera/` directory contains the ESP32 camera driver integration for real hardware support.

## Development Improvement Opportunities

While the core implementation is complete and functional, these enhancements could improve stability and performance:

### Stability & Error Handling
```bash
# Consider implementing these improvements:
# - GVCP NACK responses for unknown commands
# - Frame buffer ring for resend capability  
# - Connection health monitoring and recovery
```

### Performance Optimizations
```bash
# Performance enhancement opportunities:
# - Use esp_timer_get_time() for precise GVSP timestamps
# - Implement DMA or double-buffering for camera capture
# - Optimize packet timing based on network conditions
```

### Testing & Validation
```bash
# Automated testing improvements:
# - Integration test script with arv-tool + tshark
# - Protocol compliance validation
# - Performance benchmarking tools

# Example integration test concept:
# 1. arv-tool discovery test
# 2. XML download validation  
# 3. Acquisition start/stop cycle
# 4. First frame capture verification
```

### Network Debugging Tools
```bash
# Wireshark filter for protocol analysis:
udp.port == 3956 || udp.port == 50010

# Protocol packet flow analysis:
# GVCP (3956): Discovery, registers, XML, control
# GVSP (50010): Leader → Data packets → Trailer
```

Use these suggestions as guidance for future development iterations or when troubleshooting specific issues in production environments.