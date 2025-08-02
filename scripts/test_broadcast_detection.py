#!/usr/bin/env python3
"""
Test script to detect GigE Vision discovery broadcasts from ESP32-CAM
"""

import socket
import time
import struct
import sys

def monitor_broadcasts(duration=30):
    """Monitor for GigE Vision discovery broadcasts on port 3956"""
    
    print(f"Monitoring for GigE Vision discovery broadcasts for {duration} seconds...")
    print("This will detect both solicited and unsolicited discovery packets")
    print("=" * 60)
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    try:
        # Bind to all interfaces on GVCP port
        sock.bind(('', 3956))
        sock.settimeout(1.0)  # 1 second timeout
        
        broadcasts_detected = 0
        start_time = time.time()
        
        while time.time() - start_time < duration:
            try:
                data, addr = sock.recvfrom(1024)
                
                if len(data) >= 8:
                    # Parse GVCP header
                    packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
                    
                    # Check if it's a discovery ACK (0x0003) 
                    if packet_type == 0x00 and command == 0x0003:
                        broadcasts_detected += 1
                        elapsed = time.time() - start_time
                        
                        print(f"[{elapsed:6.1f}s] Discovery broadcast from {addr[0]}:{addr[1]}")
                        print(f"          Packet ID: 0x{packet_id:04x}, Size: {len(data)} bytes")
                        
                        # Parse device info if we have discovery data
                        if len(data) >= 256:
                            device_data = data[8:]  # Skip GVCP header
                            
                            # Extract key device information
                            version = struct.unpack('>I', device_data[0:4])[0]
                            device_mode = struct.unpack('>I', device_data[4:8])[0]
                            mac_high = struct.unpack('>I', device_data[8:12])[0]
                            mac_low = struct.unpack('>I', device_data[12:16])[0]
                            
                            # Extract manufacturer and model strings
                            manufacturer = device_data[0x48:0x48+32].decode('utf-8', errors='ignore').rstrip('\x00')
                            model = device_data[0x68:0x68+32].decode('utf-8', errors='ignore').rstrip('\x00')
                            
                            print(f"          Device: {manufacturer} {model}")
                            print(f"          Version: {version >> 16}.{version & 0xFFFF}")
                            print()
                        
                    elif packet_type == 0x42 and command == 0x0002:
                        # Discovery request - this is what we send, not what we expect to receive
                        print(f"[{elapsed:6.1f}s] Discovery request from {addr[0]}:{addr[1]} (expected)")
                        
            except socket.timeout:
                # Timeout is normal, just continue
                continue
            except Exception as e:
                print(f"Error receiving packet: {e}")
                
        print(f"\nMonitoring complete. Detected {broadcasts_detected} discovery broadcasts.")
        
        if broadcasts_detected == 0:
            print("\n⚠️  No discovery broadcasts detected. This suggests:")
            print("   1. ESP32-CAM is not running updated firmware with broadcast feature")
            print("   2. ESP32-CAM is not connected to network") 
            print("   3. Network configuration is blocking broadcasts")
            print("   4. Broadcast functionality has an issue")
        else:
            print(f"\n✅ Discovery broadcasts are working! Detected {broadcasts_detected} broadcasts.")
            print("   Aravis should be able to discover the camera.")
            
    except Exception as e:
        print(f"Error setting up socket: {e}")
        return False
    finally:
        sock.close()
        
    return broadcasts_detected > 0

if __name__ == "__main__":
    duration = 30
    if len(sys.argv) > 1:
        try:
            duration = int(sys.argv[1])
        except ValueError:
            print("Usage: python3 test_broadcast_detection.py [duration_seconds]")
            sys.exit(1)
            
    success = monitor_broadcasts(duration)
    sys.exit(0 if success else 1)