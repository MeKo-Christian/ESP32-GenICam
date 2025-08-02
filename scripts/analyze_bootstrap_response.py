#!/usr/bin/env python3
"""
Analyze ESP32 bootstrap register values in discovery response
This will help us understand what values Aravis is seeing and why it chooses the wrong interface.
"""

import socket
import struct
from datetime import datetime

def parse_ip(ip_bytes):
    """Parse 4 bytes as IP address in both byte orders"""
    if len(ip_bytes) != 4:
        return "Invalid"
    
    # Try both byte orders
    little_endian = socket.inet_ntoa(ip_bytes)
    big_endian = socket.inet_ntoa(ip_bytes[::-1])
    
    return f"LE:{little_endian} BE:{big_endian}"

def analyze_discovery_response():
    """Capture and analyze ESP32 discovery response"""
    print("ESP32 Bootstrap Register Analysis")
    print("=================================")
    print()
    
    # Send discovery to ESP32
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    esp32_ip = "192.168.213.40"
    
    # Create discovery packet
    discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x1234)
    
    print(f"Sending discovery to {esp32_ip}:3956...")
    sock.sendto(discovery_packet, (esp32_ip, 3956))
    
    # Receive response
    sock.settimeout(3.0)
    try:
        response_data, response_addr = sock.recvfrom(2048)
        print(f"Received {len(response_data)} bytes from {response_addr[0]}:{response_addr[1]}")
        print()
        
        if len(response_data) < 8:
            print("Response too short for GVCP header")
            return
            
        # Parse GVCP header
        packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', response_data[:8])
        print(f"GVCP Header:")
        print(f"  Type: 0x{packet_type:02x}, Flags: 0x{packet_flags:02x}")
        print(f"  Command: 0x{command:04x}, Size: {size}, ID: 0x{packet_id:04x}")
        print()
        
        # Extract bootstrap data (skip 8-byte GVCP header)
        bootstrap_data = response_data[8:]
        
        if len(bootstrap_data) < 0x50:  # Need at least 0x50 bytes for IP info
            print("Bootstrap data too short")
            return
            
        print("Bootstrap Register Analysis:")
        print("============================")
        
        # Parse key registers
        offsets = {
            'Version': 0x00,
            'Device Mode': 0x04,
            'MAC High': 0x08,
            'MAC Low': 0x0c,
            'Device Capabilities': 0x10,
            'Subnet Mask': 0x14,
            'Default Gateway': 0x18,
            'Current IP Config': 0x1c,
            'Supported IP Config': 0x20,
            'Current IP Address': 0x24,
            'Link Speed': 0x2c,
        }
        
        for name, offset in offsets.items():
            if offset + 4 <= len(bootstrap_data):
                value_bytes = bootstrap_data[offset:offset+4]
                value_le = struct.unpack('<I', value_bytes)[0]  # Little endian
                value_be = struct.unpack('>I', value_bytes)[0]  # Big endian
                
                if 'IP' in name or 'Gateway' in name or 'Mask' in name:
                    print(f"  {name:20} (0x{offset:02x}): {parse_ip(value_bytes)}")
                else:
                    print(f"  {name:20} (0x{offset:02x}): LE:0x{value_le:08x} BE:0x{value_be:08x}")
        
        print()
        print("Expected Values:")
        print("================")
        print("  Current IP Address should be: 192.168.213.40")
        print("  This corresponds to:")
        expected_ip = socket.inet_aton("192.168.213.40")
        expected_le = struct.unpack('<I', expected_ip)[0]
        expected_be = struct.unpack('>I', expected_ip)[0]
        print(f"    Little Endian: 0x{expected_le:08x}")
        print(f"    Big Endian:    0x{expected_be:08x}")
        print()
        
        # Check current IP field specifically
        current_ip_offset = 0x24
        if current_ip_offset + 4 <= len(bootstrap_data):
            current_ip_bytes = bootstrap_data[current_ip_offset:current_ip_offset+4]
            current_ip_le = struct.unpack('<I', current_ip_bytes)[0]
            current_ip_be = struct.unpack('>I', current_ip_bytes)[0]
            
            print("Analysis:")
            print("=========")
            if current_ip_le == expected_le:
                print("✅ IP address is stored in LITTLE ENDIAN format")
                print("   This might be causing Aravis interface selection issues!")
            elif current_ip_be == expected_be:
                print("✅ IP address is stored in BIG ENDIAN format")
                print("   Byte order appears correct for GigE Vision")
            else:
                print("❌ IP address doesn't match expected value in either byte order")
                print(f"   Expected: 0x{expected_le:08x} (LE) or 0x{expected_be:08x} (BE)")
                print(f"   Found:    0x{current_ip_le:08x} (LE) or 0x{current_ip_be:08x} (BE)")
        
    except socket.timeout:
        print("❌ No response from ESP32")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    analyze_discovery_response()