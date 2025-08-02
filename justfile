# ESP32-CAM GenICam Development Justfile (ESP-IDF Native)
# Usage: just <command>

# Default recipe - show available commands
default:
    @just --list

# Setup - Download dependencies and setup environment
setup:
    @echo "Setting up ESP32-CAM GenICam development environment..."
    @echo "Checking for required tools..."
    @just _check-esp-idf
    @which xmllint > /dev/null || (echo "❌ xmllint not found. Install with: sudo apt install libxml2-utils" && exit 1)
    @which arv-test-0.8 > /dev/null || echo "⚠️  Aravis tools not found. Install with: sudo apt install aravis-tools"
    @echo "Setting up WiFi configuration..."
    @just _setup-wifi-config
    @echo "✅ Development environment ready"

# Internal: Check ESP-IDF environment
_check-esp-idf:
    @if [ -z "$IDF_PATH" ]; then \
        echo "❌ ESP-IDF environment not sourced"; \
        echo "   Please run: source ~/esp/esp-idf/export.sh"; \
        echo "   (adjust path to your ESP-IDF installation)"; \
        exit 1; \
    fi
    @which idf.py > /dev/null || (echo "❌ idf.py not found in PATH" && exit 1)
    @echo "✅ ESP-IDF environment detected: $IDF_PATH"

# Internal: Setup WiFi configuration from template
_setup-wifi-config:
    @echo "Checking WiFi configuration..."
    @if [ ! -f .envrc ]; then \
        echo "📝 WiFi configuration not found. Creating from template..."; \
        if [ -f .envrc.example ]; then \
            cp .envrc.example .envrc; \
            echo "⚠️  Please edit .envrc with your WiFi credentials"; \
            echo "   Then run: direnv allow"; \
        else \
            echo "Creating .envrc template..."; \
            echo "export WIFI_SSID=\"YOUR_WIFI_NETWORK_NAME\"" > .envrc; \
            echo "export WIFI_PASSWORD=\"YOUR_WIFI_PASSWORD\"" >> .envrc; \
            echo "⚠️  Please edit .envrc with your WiFi credentials"; \
            echo "   Then run: direnv allow"; \
        fi; \
    else \
        echo "✅ WiFi configuration file exists"; \
    fi

# Validate GenICam XML against official schema
validate:
    @echo "Validating GenICam XML..."
    @chmod +x scripts/validate_genicam.sh
    @./scripts/validate_genicam.sh

# Set ESP-IDF target to esp32
set-target:
    @echo "Setting IDF target to esp32..."
    @just _check-esp-idf
    @just _validate-wifi-config
    idf.py set-target esp32

# Configure the project (menuconfig)
config:
    @echo "Opening ESP-IDF configuration menu..."
    idf.py menuconfig

# Build the ESP32 project
build:
    @echo "Building ESP32-CAM project with ESP-IDF..."
    @just _check-esp-idf
    @just _validate-wifi-config
    @just _update-wifi-config
    idf.py build
    @echo "✅ Build completed"

# Internal: Validate WiFi configuration before build operations
_validate-wifi-config:
    @if [ -z "${WIFI_SSID}" ] || [ -z "${WIFI_PASSWORD}" ]; then \
        echo "⚠️  WiFi credentials not set in environment"; \
        echo "   Please set WIFI_SSID and WIFI_PASSWORD environment variables"; \
        echo "   Or use direnv with .envrc file (run 'just wifi-config' for help)"; \
    fi

# Internal: Update WiFi configuration in sdkconfig.defaults
_update-wifi-config:
    @if [ -n "${WIFI_SSID}" ] && [ -n "${WIFI_PASSWORD}" ]; then \
        echo "📝 Updating WiFi configuration in sdkconfig.defaults..."; \
        grep -q "CONFIG_ESP_WIFI_SSID" sdkconfig.defaults || echo 'CONFIG_ESP_WIFI_SSID=""' >> sdkconfig.defaults; \
        grep -q "CONFIG_ESP_WIFI_PASSWORD" sdkconfig.defaults || echo 'CONFIG_ESP_WIFI_PASSWORD=""' >> sdkconfig.defaults; \
        sed -i "s/CONFIG_ESP_WIFI_SSID=.*/CONFIG_ESP_WIFI_SSID=\"${WIFI_SSID}\"/" sdkconfig.defaults; \
        sed -i "s/CONFIG_ESP_WIFI_PASSWORD=.*/CONFIG_ESP_WIFI_PASSWORD=\"${WIFI_PASSWORD}\"/" sdkconfig.defaults; \
        echo "✅ WiFi configuration updated"; \
    fi

# Clean build artifacts
clean:
    @echo "Cleaning build artifacts..."
    idf.py fullclean
    @rm -f esp32_genicam.xml
    @echo "✅ Build artifacts cleaned"

# Flash to ESP32-CAM device (specify port, defaults to /dev/ttyUSB0)
flash port="/dev/ttyUSB0":
    @echo "Flashing to ESP32-CAM on {{port}}..."
    idf.py -p {{port}} flash
    @echo "✅ Flash completed"

# Monitor serial output (specify port, defaults to /dev/ttyUSB0)
monitor port="/dev/ttyUSB0":
    @echo "Monitoring serial output on {{port}}..."
    @echo "Press Ctrl+] to exit"
    idf.py -p {{port}} monitor

# Flash and monitor in sequence (specify port, defaults to /dev/ttyUSB0)
flash-monitor port="/dev/ttyUSB0":
    @echo "Flashing and monitoring ESP32-CAM on {{port}}..."
    idf.py -p {{port}} flash monitor

# Test GVCP discovery with proper protocol (specify ESP32 IP address)
test-discovery ip:
    @echo "Testing GVCP discovery on {{ip}}:3956..."
    @python3 scripts/test_gvcp_discovery.py {{ip}} || echo "❌ GVCP discovery test failed"

# Test GVCP discovery with verbose output for debugging
test-discovery-verbose ip:
    @echo "Testing GVCP discovery on {{ip}}:3956 (verbose mode)..."
    @python3 scripts/test_gvcp_discovery.py {{ip}} --verbose || echo "❌ GVCP discovery test failed"

# Analyze ESP32-CAM discovery response for Aravis compatibility
analyze-discovery ip:
    @echo "Analyzing ESP32-CAM discovery response from {{ip}}..."
    @python3 scripts/analyze_discovery_response.py {{ip}} || echo "❌ Discovery analysis failed"

# Run discovery proxy for ESP32-CAM (enables Aravis discovery)
discovery-proxy esp32_ip="192.168.213.40":
    @echo "Starting GigE Vision Discovery Proxy for ESP32-CAM..."
    @echo "This enables Aravis to discover ESP32-CAM devices"
    @echo "ESP32-CAM IP: {{esp32_ip}}"
    @echo "Press Ctrl+C to stop"
    @echo ""
    python3 scripts/discovery_proxy.py {{esp32_ip}} --debug

# Test with Aravis discovery tools
aravis-test:
    @echo "Testing with Aravis tools..."
    @which arv-test-0.8 > /dev/null || (echo "❌ Aravis tools not installed" && exit 1)
    @echo "Running Aravis camera test..."
    @timeout 10 arv-test-0.8 2>/dev/null || echo "⚠️  No cameras discovered or test timeout"
    @echo "Tip: Make sure ESP32-CAM is running and connected to same network"

# Test with Aravis debug output for troubleshooting
aravis-debug:
    @echo "Running Aravis with debug output..."
    @which arv-test-0.8 > /dev/null || (echo "❌ Aravis tools not installed" && exit 1)
    @echo "This will show detailed discovery process..."
    @timeout 15 arv-test-0.8 --debug=all 2>&1 | head -100

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
    @echo "ESP-IDF environment:"
    @if [ -n "$IDF_PATH" ]; then echo "✅ IDF_PATH: $IDF_PATH"; else echo "❌ IDF_PATH not set"; fi
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
dev port="/dev/ttyUSB0": set-target validate build (flash port) (monitor port)

# Show ESP32 device information
device-info port="/dev/ttyUSB0":
    @echo "ESP32 device information on {{port}}:"
    @echo "======================================"
    @python -m serial.tools.list_ports {{port}} || echo "Device not found on {{port}}"

# WiFi configuration management
wifi-config:
    @echo "ESP32-CAM WiFi Configuration"
    @echo "============================"
    @echo ""
    @if [ -f .envrc ]; then \
        echo "✅ WiFi configuration file exists: .envrc"; \
        echo "Current settings:"; \
        grep "WIFI_" .envrc || echo "No WIFI_ variables found"; \
    else \
        echo "❌ WiFi configuration file not found"; \
        echo "Creating .envrc template..."; \
        echo "export WIFI_SSID=\"YOUR_WIFI_NETWORK_NAME\"" > .envrc; \
        echo "export WIFI_PASSWORD=\"YOUR_WIFI_PASSWORD\"" >> .envrc; \
        echo "✅ Created .envrc template"; \
    fi
    @echo ""
    @echo "Setup instructions:"
    @echo "1. Edit .envrc with your WiFi credentials"
    @echo "2. If using direnv: run 'direnv allow'"
    @echo "3. If not using direnv: export WIFI_SSID and WIFI_PASSWORD manually"
    @echo "4. Environment variables will be used during build"
    @echo ""
    @echo "Current environment variables:"
    @echo "WIFI_SSID: ${WIFI_SSID:-<not set>}"
    @echo "WIFI_PASSWORD: ${WIFI_PASSWORD:-<not set>}"

# Generate documentation
docs:
    @echo "Generating documentation..."
    @echo "Project structure:" > docs.txt
    @tree -I 'build|.git' . >> docs.txt 2>/dev/null || ls -la >> docs.txt
    @echo "✅ Documentation generated in docs.txt"

# Run integration test suite (discovery + XML + streaming)
integration-test:
    @echo "Running ESP32-CAM GenICam integration test suite..."
    @echo "=============================================="
    @echo "1. Testing device discovery..."
    @timeout 10 arv-test-0.8 2>/dev/null || echo "⚠️  Discovery test timeout or no devices found"
    @echo ""
    @echo "2. Testing feature access..."
    @arv-tool-0.8 -n "ESP32-CAM" --features 2>/dev/null || echo "⚠️  Feature access failed - check device connection"
    @echo ""
    @echo "3. Testing acquisition commands..."
    @arv-tool-0.8 -n "ESP32-CAM" acquisition 2>/dev/null || echo "⚠️  Acquisition test failed"
    @echo ""
    @echo "✅ Integration test complete. Check output above for any warnings."
    @echo "💡 Use 'just capture-packets' to debug protocol issues"

# Show build size information
size:
    @echo "ESP32 application size information:"
    @if [ -f "build/ESP32GenICam.elf" ]; then \
        idf.py size; \
    else \
        echo "❌ Build first with 'just build'"; \
    fi

# Erase flash memory
erase-flash port="/dev/ttyUSB0":
    @echo "Erasing flash memory on {{port}}..."
    idf.py -p {{port}} erase_flash

# Development help
help:
    @echo "ESP32-CAM GenICam Development Commands (ESP-IDF Native)"
    @echo "======================================================="
    @echo ""
    @echo "Setup & Configuration:"
    @echo "  just setup              - Setup development environment"
    @echo "  just set-target         - Set ESP-IDF target to esp32"
    @echo "  just config             - Open menuconfig"
    @echo "  just wifi-config        - Help with WiFi configuration"
    @echo ""
    @echo "Development workflow:"
    @echo "  just dev [port]         - Full development cycle (set-target, validate, build, flash, monitor)"
    @echo "  just validate           - Validate GenICam XML"
    @echo "  just build              - Build project"
    @echo "  just flash [port]       - Flash to device"
    @echo "  just monitor [port]     - Monitor serial output"
    @echo "  just flash-monitor      - Flash and monitor in one command"
    @echo ""
    @echo "Testing:"
    @echo "  just test-discovery IP  - Test GVCP discovery with proper protocol"
    @echo "  just test-discovery-verbose IP - Test GVCP discovery with verbose output"
    @echo "  just aravis-test        - Test with Aravis tools"
    @echo "  just aravis-viewer      - Launch Aravis camera viewer"
    @echo "  just integration-test   - Full protocol test suite"
    @echo ""
    @echo "Utilities:"
    @echo "  just status             - Show project status"
    @echo "  just show-xml           - Display current XML"
    @echo "  just size               - Show build size information"
    @echo "  just clean              - Clean build artifacts"
    @echo "  just erase-flash        - Erase ESP32 flash memory"
    @echo ""
    @echo "Network debugging:"
    @echo "  just capture-packets    - Capture GVCP network traffic"
    @echo ""
    @echo "Default port: /dev/ttyUSB0 (override with port=/dev/ttyACM0)"