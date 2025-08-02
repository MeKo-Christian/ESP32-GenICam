# ESP32-CAM GenICam Compatible Camera

🎥 **A complete GenICam/GigE Vision implementation for ESP32-CAM that works with existing industrial vision software**

Transform your ESP32-CAM into a network-discoverable camera compatible with Aravis, Spinnaker, Vimba, and go-aravis applications.

## ⚡ Quick Start

### 1. Setup Development Environment
```bash
# Install dependencies and setup project
just setup
```

### 2. Configure WiFi
Set your WiFi credentials using environment variables:
```bash
# Edit .envrc with your WiFi credentials
export WIFI_SSID="YOUR_WIFI_NETWORK_NAME"  
export WIFI_PASSWORD="YOUR_WIFI_PASSWORD"

# If using direnv:
direnv allow
```

### 3. Build, Flash & Run
```bash
# Complete development cycle: validate, build, flash, monitor
just dev [/dev/ttyUSB0]

# Or step by step:
just build                    # Build project
just flash [/dev/ttyUSB0]     # Flash to ESP32-CAM  
just monitor [/dev/ttyUSB0]   # Monitor serial output
```

### 4. Test Camera Discovery
```bash
# Test with Aravis tools
just aravis-test              # Discover ESP32-CAM on network
just aravis-viewer            # Launch camera viewer GUI

# Manual testing
just test-discovery <ESP32_IP>    # Test UDP discovery

# If discovery fails (WiFi broadcast limitation):
just discovery-proxy <ESP32_IP>   # Start discovery proxy (failsafe)
```

## ✅ What's Implemented

This is a **complete, working implementation** with:

- **🔍 Device Discovery** - Appears on network scans via GigE Vision protocol
- **📡 GVCP Control** - Full bootstrap registers and memory access  
- **📊 GenICam XML** - Standards-compliant camera feature description
- **🎬 GVSP Streaming** - Real-time image transmission from ESP32-CAM
- **📷 Real Camera** - Actual frame capture from OV2640 sensor (320x240 Mono8)
- **🛠️ Complete Toolchain** - Build, flash, test, and debug workflow

## 🏗️ Architecture Overview

Multi-task ESP-IDF implementation with these core components:

- **GVCP Protocol** - Control and discovery on UDP port 3956
- **GVSP Streaming** - Image transmission via UDP packets  
- **GenICam XML** - Camera feature description served from memory
- **Real Hardware** - ESP32-CAM OV2640 sensor integration

## 🔧 Development Commands

The project uses **justfile** for streamlined development workflow:

### Core Operations
```bash
just setup                   # Install dependencies & validate environment
just dev [port]              # Complete cycle: validate→build→flash→monitor
just build                   # Build project only
just flash [port]            # Flash to device  
just monitor [port]          # Serial output monitoring
just clean                   # Clean artifacts
```

### Testing & Debugging  
```bash
just test-discovery <ip>     # Test UDP discovery manually
just aravis-test            # Discover with Aravis tools
just aravis-viewer          # Launch GUI camera viewer
just discovery-proxy <ip>   # Start discovery proxy (WiFi broadcast failsafe)
just capture-packets        # Network protocol debugging
just status                 # Project and network status
just show-xml               # Display current GenICam XML
```

### Configuration
```bash
just wifi-config            # Help with WiFi credentials setup
just validate               # Validate GenICam XML compliance
```

Use `just help` for complete command reference.

## 📁 Project Structure

```
ESP32GenICam/
├── justfile               # Development workflow commands
├── .envrc                 # WiFi credentials (environment variables)
├── src/                   # Core implementation
│   ├── main.c            # Application entry point
│   ├── wifi_manager.*    # Network connectivity  
│   ├── gvcp_handler.*    # Control protocol (port 3956)
│   ├── gvsp_handler.*    # Streaming protocol
│   ├── camera_handler.*  # ESP32-CAM hardware interface
│   └── genicam_xml.*     # XML feature description
├── components/           # ESP32-camera driver integration
├── tools/schema/         # GenICam XML validation
└── CLAUDE.md            # Developer architecture guide
```

## 🎯 Expected Behavior

1. **📶 WiFi Connection** - ESP32 connects and gets IP address
2. **📷 Camera Init** - OV2640 sensor initializes for 320x240 grayscale
3. **🔍 Network Discovery** - Responds to GigE Vision discovery scans
4. **📋 XML Serving** - Provides GenICam feature description via GVCP
5. **🎬 Image Streaming** - Transmits real camera frames via GVSP

## 💻 Compatible Software

Works with existing GenICam/GigE Vision applications:

- **Aravis Library** (Linux) - `arv-tool`, `arv-viewer`
- **go-aravis** - Go language bindings  
- **Spinnaker SDK** (FLIR/Point Grey)
- **Vimba SDK** (Allied Vision)
- **Custom Applications** - Any GenICam-compatible software

## 🚨 Troubleshooting

| Issue | Check | Solution |
|-------|-------|----------|
| Discovery fails | WiFi connection, port 3956 | `just status`, firewall settings |
| Aravis can't discover | ESP32 WiFi broadcast limits | `just discovery-proxy <ESP32_IP>` |
| XML errors | GenICam compliance | `just validate` |
| No images | GVSP streaming | `just capture-packets`, check acquisition |
| Build issues | Dependencies, WiFi config | `just setup`, `just wifi-config` |

### Discovery Proxy (WiFi Broadcast Failsafe)

If Aravis cannot discover the ESP32-CAM due to WiFi broadcast limitations:

```bash
# Terminal 1: Start discovery proxy
just discovery-proxy 192.168.1.100

# Terminal 2: Test discovery  
just aravis-test              # Should now find device
```

**How it works**: The proxy forwards broadcast discovery packets as unicast to ESP32, enabling Aravis integration on WiFi networks with broadcast filtering.

### Network Protocol Debugging

Use `just capture-packets` to debug with Wireshark, or apply this filter manually:
```
udp.port == 3956 || udp.port == 50010
```

**Protocol Flow:**
```
PC → ESP32: Discovery (port 3956)
PC ← ESP32: Discovery ACK + device info
PC → ESP32: Read bootstrap registers (port 3956)  
PC ← ESP32: Device details + XML pointer
PC → ESP32: Download GenICam XML (port 3956)
PC ← ESP32: XML feature description
PC → ESP32: AcquisitionStart command (port 3956)
PC ← ESP32: Image stream via GVSP (port 50010)
```

### Integration Testing

Automated test script for full protocol validation:
```bash
# Test complete discovery → streaming pipeline
just aravis-test                    # Should discover ESP32-CAM
arv-tool-0.8 -n "ESP32-CAM" --features    # Download and display XML features
arv-tool-0.8 -n "ESP32-CAM" acquisition   # Test acquisition commands
```

## 📖 References

- **[GigE Vision Standard](https://en.wikipedia.org/wiki/GigE_Vision)** - Protocol specification
- **[Aravis Project](https://github.com/AravisProject/aravis)** - Reference implementation  
- **[GenICam Standard](https://www.emva.org/standards-technology/genicam/)** - Camera description format
- **[ESP32 Camera Component](https://github.com/espressif/esp32-camera)** - Hardware driver