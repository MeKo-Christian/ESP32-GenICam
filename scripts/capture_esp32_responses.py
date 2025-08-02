#!/usr/bin/env python3
"""
Capture ESP32 discovery responses to understand why Aravis doesn't receive them
"""

import socket
import struct
import threading
import time
from datetime import datetime

def monitor_discovery_responses():
    """Monitor for ESP32 discovery responses on all interfaces"""
    
    # Create socket to capture responses from ESP32
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Bind to any available port to receive responses
    sock.bind(('0.0.0.0', 0))
    local_port = sock.getsockname()[1]
    
    print(f"ESP32 Response Monitor")
    print(f"=====================")
    print(f"Listening on port {local_port}")
    print(f"Sending discovery to ESP32 and monitoring response...")
    print()
    
    # Send discovery packet to ESP32
    esp32_ip = "192.168.213.40"
    discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x1234)
    
    print(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] Sending discovery to {esp32_ip}:3956")
    sock.sendto(discovery_packet, (esp32_ip, 3956))
    
    # Listen for response
    sock.settimeout(3.0)
    try:
        data, addr = sock.recvfrom(2048)
        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        print(f"[{timestamp}] ✅ Response received from {addr[0]}:{addr[1]} - {len(data)} bytes")
        
        if len(data) >= 8:
            magic, status, cmd, length, req_id = struct.unpack('>BBHHH', data[:8])
            print(f"  GVCP Header: magic=0x{magic:02x}, status=0x{status:02x}, cmd=0x{cmd:04x}, len={length}, id=0x{req_id:04x}")
            
        # Show first 32 bytes
        hex_data = ' '.join(f'{b:02x}' for b in data[:32])
        print(f"  Data: {hex_data}...")
        print()
        print("✅ ESP32 is responding correctly!")
        print("❌ Problem: Aravis is not receiving these responses")
        
    except socket.timeout:
        print(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] ❌ No response received within 3 seconds")
        print("❌ ESP32 is not responding to discovery")
        
    except Exception as e:
        print(f"Error: {e}")
        
    finally:
        sock.close()

def test_aravis_concurrent():
    """Test what happens when Aravis and our monitor run simultaneously"""
    
    print("\nTesting concurrent discovery...")
    print("==============================")
    print("Run 'arv-test-0.10' in another terminal NOW")
    print("This will show if our socket interferes with Aravis...")
    
    # Monitor for any traffic on GVCP port
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        sock.bind(('0.0.0.0', 3956))
        print("✅ Bound to GVCP port 3956 for monitoring")
        sock.settimeout(10.0)
        
        while True:
            try:
                data, addr = sock.recvfrom(2048)
                timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                print(f"[{timestamp}] Traffic from {addr[0]}:{addr[1]} - {len(data)} bytes")
                
                if len(data) >= 8:
                    magic, status, cmd, length, req_id = struct.unpack('>BBHHH', data[:8])
                    print(f"  GVCP: magic=0x{magic:02x}, status=0x{status:02x}, cmd=0x{cmd:04x}, id=0x{req_id:04x}")
                    
            except socket.timeout:
                print("No traffic received in 10 seconds")
                break
                
    except OSError as e:
        print(f"❌ Cannot bind to port 3956: {e}")
        print("This means another process (like discovery proxy) is using the port")
        
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user")
        
    finally:
        sock.close()

def main():
    print("ESP32 Discovery Response Analysis")
    print("=================================")
    print()
    
    # Test 1: Direct response monitoring
    monitor_discovery_responses()
    
    # Test 2: Concurrent monitoring
    test_aravis_concurrent()

if __name__ == "__main__":
    main()