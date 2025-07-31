# ESP32-CAM GenICam (GigE Vision) Compatible Camera

A PlatformIO project to make an ESP32-CAM GenICam-compatible for testing industrial vision software.

## Quick Start

### Prerequisites
1. **PlatformIO Core** - Install from [PlatformIO Install Guide](https://platformio.org/install/cli)
2. **VSCode with PlatformIO Extension** (recommended)
3. **ESP32-CAM module** (AI-Thinker or compatible)
4. **WiFi network** for testing

### Build & Flash
```bash
# Clone and navigate to project
cd ESP32GenICam

# Configure WiFi (edit platformio.ini)
# Uncomment and set your WiFi credentials:
# -D CONFIG_ESP_WIFI_SSID="YourWiFiSSID"
# -D CONFIG_ESP_WIFI_PASSWORD="YourWiFiPassword"

# Build and upload
pio run
pio run --target upload --upload-port /dev/ttyUSB0

# Monitor output
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

### Test Discovery
```bash
# Send test packet to ESP32's IP on port 3956
echo "test" | nc -u <ESP32_IP_ADDRESS> 3956

# Use Aravis tools to discover camera
arv-tool-0.8 --debug=all
```

## Project Overview

This project implements a GenICam-compatible camera using GigE Vision protocol over WiFi. The ESP32-CAM appears as a network camera that can be discovered and controlled by existing industrial vision software using libraries like Aravis.

### Architecture

The implementation consists of four main components:

1. **GVCP (GigE Vision Control Protocol)** - UDP port 3956 for device discovery and control
2. **GenICam XML Description** - Camera feature description served via memory reads  
3. **Register Space** - Memory-mapped configuration and status registers
4. **GVSP (GigE Vision Stream Protocol)** - UDP streaming of image frames

## Technical Details

### GenICam and GigE Vision Basics

**GenICam** (Generic Interface for Cameras) defines how camera features are described and accessed using XML device descriptions. **GigE Vision** is the transport standard that works over Ethernet/WiFi using UDP/IP.

GigE Vision has four key components:
- **GVCP** - Device discovery, configuration, and control (UDP port 3956)
- **GVSP** - High-speed image data transmission (separate UDP port)
- **Device Discovery** - Network detection mechanism
- **GenICam XML** - On-camera feature description file

### Network Protocol Implementation

**UDP Port Setup:** The camera listens on UDP port 3956 for all GVCP traffic including discovery packets and control commands. Responses use the same port.

**Device Discovery:** Implements GigE Vision discovery by parsing broadcast packets on port 3956 and responding with device identification (MAC, IP, version, XML pointer).

**GVCP Commands:**
- **Read Memory/Registers** - Returns requested data bytes for XML download and feature reads
- **Write Memory/Registers** - Accepts configuration changes (AcquisitionStart/Stop, settings)
- **Bootstrap Registers** - Standard device info at known addresses

### Device Memory Map

**Bootstrap Registers** at fixed addresses contain:
- Device information strings (Manufacturer, Model, Version, Serial)
- XML descriptor pointer (e.g., `Local:0x10000`)
- Stream channel configuration (port numbers, packet size)

**GenICam XML File** describes camera features:
- Device identity matching bootstrap registers
- Image format controls (Width, Height, PixelFormat)
- Acquisition controls (Start/Stop commands)
- Stream parameters (packet size, destination port)

### Image Streaming (GVSP)

**Frame Structure:** Each image is transmitted as a sequence of UDP packets:
1. **Leader packet** - Frame start with image info (size, format, block ID)
2. **Data packets** - Image payload chunks (~1400 bytes each)
3. **Trailer packet** - Frame end marker

**Pixel Format:** Uses Mono8 (8-bit grayscale) converted from camera's YUV output.

**Streaming Control:** Triggered by AcquisitionStart/Stop commands via GVCP.

## Development Notes

### PlatformIO Structure
- Source files in `src/` directory
- Configuration in `platformio.ini`
- Custom libraries in `lib/` (if needed)
- Build with `pio run`, upload with `pio run --target upload`

### Multi-task Architecture
- **GVCP Task** - Handles discovery and control on port 3956
- **Camera Task** - Captures frames from ESP32-CAM sensor
- **GVSP Task** - Streams images when acquisition is active

Uses queues/shared state for inter-task communication with mutex protection.

### Performance Considerations
- Target: QVGA (320x240) grayscale at 2-3 FPS
- WiFi limitations may require packet delays
- Frame drops acceptable for testing purposes

### Testing Strategy

1. **GVCP Discovery** - Use Wireshark or `arv-tool` to verify device discovery
2. **XML Download** - Confirm GenICam XML is accessible via memory reads
3. **Frame Streaming** - Test image acquisition with dummy/real camera data
4. **Integration** - Validate with Aravis-based applications

### Hardware Support
- Default pin configuration for AI-Thinker ESP32-CAM
- Adjustable camera pins in `src/camera_handler.c`
- WiFi preferred for easy microscope integration

### Compliance Notes
- Implements minimal GenICam feature set for testing
- Uses Aravis as reference for protocol behavior
- Not for commercial distribution (GigE Vision licensing)

## File Structure
```
ESP32GenICam/
├── platformio.ini          # PlatformIO configuration
├── src/                    # Source files
│   ├── main.c             # Main application
│   ├── gvcp_handler.c     # GVCP protocol implementation
│   ├── camera_handler.c   # ESP32-CAM interface
│   ├── wifi_manager.c     # WiFi connection management
│   └── genicam_xml.c      # GenICam XML serving
├── lib/                   # Custom libraries (if needed)
└── README.md              # This file
```

## Expected Behavior

1. **WiFi Connection** - ESP32 connects and gets IP address
2. **Camera Init** - OV2640 sensor initializes for grayscale capture
3. **GVCP Socket** - Binds to port 3956 for control protocol
4. **Discovery Response** - Responds to network scans from vision software
5. **XML Serving** - Provides GenICam feature description
6. **Image Streaming** - Transmits frames on acquisition commands

## Integration with Existing Software

This camera appears as a standard GenICam device to software using:
- Aravis library (Linux)
- Spinnaker SDK (FLIR/Point Grey)
- Vimba SDK (Allied Vision)  
- Generic GenICam clients

Use with go-aravis or other Aravis-based applications by treating as any other GigE Vision camera.

## Troubleshooting

- **Discovery fails**: Check WiFi connection, firewall settings, port 3956
- **XML errors**: Verify GenICam XML syntax and memory addressing
- **No images**: Confirm GVSP port configuration and packet format
- **Frame drops**: Adjust packet delays or reduce resolution/frame rate

Use Wireshark to capture UDP traffic for protocol debugging.

## References

- [GigE Vision Standard Overview](https://en.wikipedia.org/wiki/GigE_Vision)
- [Aravis Project](https://github.com/AravisProject/aravis) - Reference client implementation
- [GenICam Standard](https://www.emva.org/standards-technology/genicam/) - Feature description format
- [PlatformIO ESP32](https://docs.platformio.org/en/latest/platforms/espressif32.html) - Development platform