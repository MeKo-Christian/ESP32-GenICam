# ESP32-CAM GenICam Development Justfile
# Usage: just <command>

# Default recipe - show available commands
default:
    @just --list

# Setup - Download dependencies and setup environment
setup:
    @echo "Setting up ESP32-CAM GenICam development environment..."
    @echo "Checking for required tools..."
    @which pio > /dev/null || (echo "❌ PlatformIO not found. Install with: pip install platformio" && exit 1)
    @which xmllint > /dev/null || (echo "❌ xmllint not found. Install with: sudo apt install libxml2-utils" && exit 1)
    @which arv-test-0.8 > /dev/null || echo "⚠️  Aravis tools not found. Install with: sudo apt install aravis-tools"
    @echo "✅ Development environment ready"

# Validate GenICam XML against official schema
validate:
    @echo "Validating GenICam XML..."
    @chmod +x scripts/validate_genicam.sh
    @./scripts/validate_genicam.sh

# Build the ESP32 project
build:
    @echo "Building ESP32-CAM project..."
    pio run

# Clean build artifacts
clean:
    @echo "Cleaning build artifacts..."
    pio run --target clean
    @rm -f esp32_genicam.xml
    @echo "✅ Build artifacts cleaned"

# Flash to ESP32-CAM device (specify port, defaults to /dev/ttyUSB0)
flash port="/dev/ttyUSB0":
    @echo "Flashing to ESP32-CAM on {{port}}..."
    pio run --target upload --upload-port {{port}}

# Monitor serial output (specify port, defaults to /dev/ttyUSB0)
monitor port="/dev/ttyUSB0":
    @echo "Monitoring serial output on {{port}}..."
    @echo "Press Ctrl+C to exit"
    pio device monitor --port {{port}} --baud 115200

# Flash and monitor in sequence (specify port, defaults to /dev/ttyUSB0)
flash-monitor port="/dev/ttyUSB0":
    @echo "Flashing and monitoring ESP32-CAM on {{port}}..."
    pio run --target upload --upload-port {{port}}
    @echo "Switching to monitor mode..."
    @sleep 2
    pio device monitor --port {{port}} --baud 115200

# Test UDP discovery with netcat (specify ESP32 IP address)
test-discovery ip:
    @echo "Testing UDP discovery on {{ip}}:3956..."
    @echo "Sending test packet..."
    @echo "test" | nc -u {{ip}} 3956 || echo "❌ Failed to send discovery packet"
    @echo "Check ESP32 serial output for response"

# Test with Aravis discovery tools
aravis-test:
    @echo "Testing with Aravis tools..."
    @which arv-test-0.8 > /dev/null || (echo "❌ Aravis tools not installed" && exit 1)
    @echo "Running Aravis camera test..."
    @timeout 10 arv-test-0.8 2>/dev/null || echo "⚠️  No cameras discovered or test timeout"
    @echo "Tip: Make sure ESP32-CAM is running and connected to same network"

# View discovered cameras with Aravis viewer
aravis-viewer:
    @echo "Starting Aravis viewer..."
    @which arv-viewer-0.8 > /dev/null || (echo "❌ arv-viewer-0.8 not found" && exit 1)
    arv-viewer-0.8

# Run all validation and tests
test: validate build
    @echo "All tests passed ✅"

# Check project status and network connectivity
status:
    @echo "ESP32-CAM GenICam Project Status"
    @echo "================================"
    @echo "Build status:"
    @if [ -d "build" ]; then echo "✅ Build directory exists"; else echo "❌ No build directory"; fi
    @echo ""
    @echo "Network interfaces:"
    @ip route | grep default | head -1
    @echo ""
    @echo "Available serial ports:"
    @ls /dev/ttyUSB* 2>/dev/null || echo "No USB serial ports found"
    @ls /dev/ttyACM* 2>/dev/null || echo "No ACM serial ports found"

# Show current GenICam XML content
show-xml:
    @echo "Current GenICam XML content:"
    @echo "============================"
    @if [ -f "esp32_genicam.xml" ]; then cat esp32_genicam.xml; else echo "XML file not found. Run 'just validate' first."; fi

# Analyze network traffic with tcpdump (requires sudo)
capture-packets interface="any":
    @echo "Capturing GVCP packets on {{interface}}..."
    @echo "Press Ctrl+C to stop"
    sudo tcpdump -i {{interface}} -nn port 3956

# Quick development cycle: validate, build, flash, monitor
dev port="/dev/ttyUSB0": validate build (flash port) (monitor port)

# Show ESP32 device information
device-info port="/dev/ttyUSB0":
    @echo "ESP32 device information on {{port}}:"
    @echo "======================================"
    @pio device list | grep {{port}} || echo "Device not found on {{port}}"

# Update WiFi credentials in platformio.ini (interactive)
wifi-config:
    @echo "Current WiFi configuration in platformio.ini:"
    @grep -n "CONFIG_ESP\|WIFI" platformio.ini || echo "No WiFi config found"
    @echo ""
    @echo "To update WiFi credentials, edit platformio.ini and uncomment/set:"
    @echo "; -D CONFIG_ESP_WIFI_SSID=\"YourWiFiSSID\""
    @echo "; -D CONFIG_ESP_WIFI_PASSWORD=\"YourWiFiPassword\""

# Generate documentation
docs:
    @echo "Generating documentation..."
    @echo "Project structure:" > docs.txt
    @tree -I 'build|.git' . >> docs.txt 2>/dev/null || ls -la >> docs.txt
    @echo "✅ Documentation generated in docs.txt"

# Development help
help:
    @echo "ESP32-CAM GenICam Development Commands"
    @echo "======================================"
    @echo ""
    @echo "Development workflow:"
    @echo "  just setup              - Setup development environment"
    @echo "  just dev [port]         - Full development cycle (validate, build, flash, monitor)"
    @echo "  just validate           - Validate GenICam XML"
    @echo "  just build              - Build project"
    @echo "  just flash [port]       - Flash to device"
    @echo "  just monitor [port]     - Monitor serial output"
    @echo ""
    @echo "Testing:"
    @echo "  just test-discovery IP  - Test UDP discovery"
    @echo "  just aravis-test        - Test with Aravis tools"
    @echo "  just aravis-viewer      - Launch Aravis camera viewer"
    @echo ""
    @echo "Utilities:"
    @echo "  just status             - Show project status"
    @echo "  just show-xml           - Display current XML"
    @echo "  just wifi-config        - Help with WiFi configuration"
    @echo "  just clean              - Clean build artifacts"
    @echo ""
    @echo "Network debugging:"
    @echo "  just capture-packets    - Capture GVCP network traffic"
    @echo ""
    @echo "Default port: /dev/ttyUSB0 (override with port=/dev/ttyACM0)"