#!/usr/bin/env python3
"""Monitor GVCP discovery traffic to understand Aravis vs ESP32 communication"""

import socket
import struct
import threading
import time
from datetime import datetime

def log_packet(direction, addr, data):
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] {direction} {addr[0]}:{addr[1]} - {len(data)} bytes")
    
    if len(data) >= 8:
        # Parse GVCP header
        magic, status, cmd, length, req_id = struct.unpack('>BBHHH', data[:8])
        print(f"  GVCP: magic=0x{magic:02x}, status=0x{status:02x}, cmd=0x{cmd:04x}, len={length}, id=0x{req_id:04x}")
        
        # Show first 16 bytes in hex
        hex_data = ' '.join(f'{b:02x}' for b in data[:16])
        print(f"  Data: {hex_data}")
    print()

def monitor_port(port, interface_ip="0.0.0.0"):
    """Monitor GVCP traffic on specified port and interface"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    try:
        sock.bind((interface_ip, port))
        print(f"Monitoring {interface_ip}:{port}")
        
        while True:
            try:
                data, addr = sock.recvfrom(2048)
                log_packet("RX", addr, data)
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                break
                
    except Exception as e:
        print(f"Error monitoring {interface_ip}:{port}: {e}")
    finally:
        sock.close()

def main():
    print("GVCP Discovery Monitor")
    print("=====================")
    print("Monitoring UDP port 3956 for discovery traffic...")
    print("Press Ctrl+C to stop\n")
    
    # Monitor on all interfaces
    monitor_port(3956)

if __name__ == "__main__":
    main()