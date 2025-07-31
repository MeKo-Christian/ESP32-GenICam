# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Quick Development Workflow (using just)
```bash
# Setup development environment
just setup

# Complete development cycle: validate XML, build, flash, and monitor
just dev [port]

# Individual commands
just validate          # Validate GenICam XML schema compliance
just build             # Build ESP32 project
just flash [port]      # Flash to ESP32-CAM
just monitor [port]    # Monitor serial output
just clean             # Clean build artifacts
```

### Build and Flash (Traditional PlatformIO)
```bash
# Build the project
pio run

# Flash to ESP32-CAM (adjust port as needed)
pio run --target upload --upload-port /dev/ttyUSB0

# Monitor serial output
pio device monitor --port /dev/ttyUSB0 --baud 115200

# Build and flash in one command
pio run --target upload --upload-port /dev/ttyUSB0 && pio device monitor --port /dev/ttyUSB0 --baud 115200
```

### WiFi Configuration
```bash
# Get help configuring WiFi
just wifi-config

# WiFi credentials are configured in platformio.ini via build flags:
; -D CONFIG_ESP_WIFI_SSID="YourWiFiSSID"
; -D CONFIG_ESP_WIFI_PASSWORD="YourWiFiPassword"
```

### Testing and Discovery
```bash
# Test UDP discovery (using just)
just test-discovery <ESP32_IP_ADDRESS>

# Test with Aravis tools
just aravis-test        # Discovery test
just aravis-viewer      # Launch camera viewer

# Traditional methods
echo "test" | nc -u <ESP32_IP_ADDRESS> 3956
arv-tool-0.8 --debug=all
arv-viewer-0.8
```

### GenICam XML Validation
```bash
# Validate XML against official GenICam schema
just validate

# Manual validation (after running just validate once)
xmllint --schema GenApiSchema_Version_1_0.xsd esp32_genicam.xml --noout
```

### Network Debugging
```bash
# Show project and network status
just status

# Capture GVCP protocol packets
just capture-packets [interface]

# Display current GenICam XML
just show-xml
```

## Code Architecture

### Main Components

**Multi-task Architecture:**
- **Main Task** (`main.c:15`) - Initializes all components and creates GVCP task
- **GVCP Task** (`gvcp_handler.c`) - Handles GigE Vision Control Protocol on UDP port 3956
- **Camera Task** (integrated) - Captures frames from ESP32-CAM OV2640 sensor
- **GVSP Task** (planned) - Streams image data via GigE Vision Stream Protocol

**Core Modules:**
1. **WiFi Manager** (`wifi_manager.c/h`) - Handles WiFi connection and network setup
2. **Camera Handler** (`camera_handler.c/h`) - ESP32-CAM OV2640 interface with 320x240 grayscale capture
3. **GVCP Handler** (`gvcp_handler.c/h`) - Implements GigE Vision Control Protocol for device discovery and control
4. **GenICam XML** (`genicam_xml.c/h`) - Provides camera feature description served via memory reads

### Protocol Implementation

**GigE Vision Protocol Stack:**
- **GVCP (Control)** - UDP port 3956 for device discovery, bootstrap registers, and memory access
- **GVSP (Streaming)** - Separate UDP port for high-speed image transmission (planned)
- **Bootstrap Registers** - Fixed memory map with device info, XML location pointer
- **GenICam XML** - Camera feature description at memory address 0x10000

**Memory Layout:**
- Bootstrap registers start at offset 0x0000 with device identification
- XML descriptor pointer at `GVBS_XML_URL_0_OFFSET` (0x200) points to "Local:0x10000"
- GenICam XML data served from memory address 0x10000 via GVCP read commands

### Hardware Configuration

**ESP32-CAM Pin Configuration** (in `platformio.ini`):
- Camera module: AI-Thinker ESP32-CAM board
- Resolution: 320x240 pixels (QVGA)
- Format: Mono8 (8-bit grayscale converted from YUV)
- Interface: WiFi for network connectivity

**Camera Pins:** PWDN=32, XCLK=0, SIOD=26, SIOC=27, D7=35, D6=34, D5=39, D4=36, D3=21, D2=19, D1=18, D0=5, VSYNC=25, HREF=23, PCLK=22

## Testing Strategy

**Development Phases:**
1. **GVCP Discovery** - Device appears on network and responds to discovery packets
2. **Bootstrap Registers** - Device identification and XML pointer accessible via memory reads
3. **GenICam XML Download** - XML feature description downloadable via GVCP
4. **Frame Capture** - ESP32-CAM captures and converts frames to Mono8
5. **GVSP Streaming** - Image frames transmitted as UDP packet streams (in progress)

**Integration Testing:**
- Use Aravis library tools (`arv-tool-0.8`, `arv-viewer-0.8`) for protocol validation
- Wireshark packet capture for protocol debugging on UDP port 3956
- Compatible with go-aravis and other GenICam-based vision software

## Project Structure Notes

This is a PlatformIO ESP-IDF project implementing a GenICam-compatible camera for testing industrial vision software. The implementation focuses on GigE Vision protocol compliance over WiFi rather than Ethernet.

The `aravis/` directory contains the reference Aravis library source code for understanding the protocol implementation and testing against a known working client.