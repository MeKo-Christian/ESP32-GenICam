#!/usr/bin/env python3
"""
GVCP Register Test Script for ESP32-CAM GenICam
===============================================

This script tests the newly implemented GVCP registers for Aravis compatibility:
- 0x0A00 = TLParamsLocked
- 0x0A04 = GevSCPSPacketSize  
- 0x0A08 = GevSCPD (Packet Delay)
- 0x0A10 = GevSCDA (Destination Address)

Usage: python3 test_gvcp_registers.py <ESP32_IP_ADDRESS>
"""

import sys
import socket
import struct
import time

# GVCP Protocol Constants
GVCP_PORT = 3956
GVCP_PACKET_TYPE_CMD = 0x42
GVCP_PACKET_TYPE_ACK = 0x00
GVCP_CMD_READ_MEMORY = 0x0084
GVCP_CMD_WRITE_MEMORY = 0x0086
GVCP_ACK_READ_MEMORY = 0x0084
GVCP_ACK_WRITE_MEMORY = 0x0086

# Register addresses to test
REGISTERS = {
    "TLParamsLocked": 0x0A00,
    "GevSCPSPacketSize": 0x0A04, 
    "GevSCPD": 0x0A08,
    "GevSCDA": 0x0A10
}

def create_gvcp_header(packet_type, command, size_words, packet_id):
    """Create GVCP packet header"""
    header = struct.pack(">BBHHH",
                        packet_type,    # Packet type
                        0x01,           # Flags  
                        command,        # Command
                        size_words,     # Size in 32-bit words
                        packet_id)      # Packet ID
    return header

def send_read_memory(sock, address, size=4, packet_id=1):
    """Send GVCP READ_MEMORY command"""
    # Payload: address (4 bytes) + size (4 bytes) = 8 bytes = 2 words
    payload = struct.pack(">II", address, size)
    header = create_gvcp_header(GVCP_PACKET_TYPE_CMD, GVCP_CMD_READ_MEMORY, 2, packet_id)
    packet = header + payload
    return sock.send(packet)

def send_write_memory(sock, address, value, packet_id=1):
    """Send GVCP WRITE_MEMORY command"""
    # Payload: address (4 bytes) + data (4 bytes) = 8 bytes = 2 words
    payload = struct.pack(">II", address, value)
    header = create_gvcp_header(GVCP_PACKET_TYPE_CMD, GVCP_CMD_WRITE_MEMORY, 2, packet_id)
    packet = header + payload
    return sock.send(packet)

def parse_read_response(data):
    """Parse GVCP READ_MEMORY response"""
    if len(data) < 8:
        return None, None
        
    # Parse header
    packet_type, flags, command, size_words, packet_id = struct.unpack(">BBHHH", data[:8])
    
    if packet_type != GVCP_PACKET_TYPE_ACK or command != GVCP_ACK_READ_MEMORY:
        return None, None
    
    # Parse payload: address + data
    if len(data) >= 16:
        address, value = struct.unpack(">II", data[8:16])
        return address, value
    
    return None, None

def parse_write_response(data):
    """Parse GVCP WRITE_MEMORY response"""
    if len(data) < 8:
        return None
        
    # Parse header
    packet_type, flags, command, size_words, packet_id = struct.unpack(">BBHHH", data[:8])
    
    if packet_type != GVCP_PACKET_TYPE_ACK or command != GVCP_ACK_WRITE_MEMORY:
        return None
    
    # Parse payload: address
    if len(data) >= 12:
        address = struct.unpack(">I", data[8:12])[0]
        return address
    
    return None

def test_register_access(esp32_ip):
    """Test reading and writing GVCP registers"""
    print(f"Testing GVCP registers on ESP32-CAM at {esp32_ip}...")
    
    try:
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(5.0)  # 5 second timeout
        
        packet_id = 1
        
        for reg_name, reg_addr in REGISTERS.items():
            print(f"\n--- Testing {reg_name} (0x{reg_addr:04X}) ---")
            
            # Test read
            print(f"Reading register 0x{reg_addr:04X}...")
            send_read_memory(sock, reg_addr, 4, packet_id)
            sock.sendto(b'', (esp32_ip, GVCP_PORT))  # Send the packet
            
            try:
                data, addr = sock.recvfrom(1024)
                address, value = parse_read_response(data)
                if address == reg_addr:
                    print(f"  ✅ Read successful: 0x{value:08X} ({value})")
                    original_value = value
                else:
                    print(f"  ❌ Read failed or wrong address")
                    continue
            except socket.timeout:
                print(f"  ❌ Read timeout")
                continue
            
            packet_id += 1
            
            # Test write (except for read-only registers if any)
            if reg_name == "TLParamsLocked":
                test_value = 1 if original_value == 0 else 0
            elif reg_name == "GevSCPSPacketSize":
                test_value = 1400  # Valid packet size
            elif reg_name == "GevSCPD":
                test_value = 2000  # 2ms delay
            elif reg_name == "GevSCDA":
                # Test with a dummy IP address (192.168.1.100)
                test_value = struct.unpack(">I", socket.inet_aton("192.168.1.100"))[0]
            
            print(f"Writing test value 0x{test_value:08X} to register 0x{reg_addr:04X}...")
            send_write_memory(sock, reg_addr, test_value, packet_id)
            sock.sendto(b'', (esp32_ip, GVCP_PORT))  # Send the packet
            
            try:
                data, addr = sock.recvfrom(1024)
                address = parse_write_response(data)
                if address == reg_addr:
                    print(f"  ✅ Write successful")
                    
                    # Read back to verify
                    packet_id += 1
                    send_read_memory(sock, reg_addr, 4, packet_id)
                    sock.sendto(b'', (esp32_ip, GVCP_PORT))
                    
                    try:
                        data, addr = sock.recvfrom(1024)
                        address, readback_value = parse_read_response(data)
                        if address == reg_addr and readback_value == test_value:
                            print(f"  ✅ Readback verified: 0x{readback_value:08X}")
                        else:
                            print(f"  ❌ Readback mismatch: expected 0x{test_value:08X}, got 0x{readback_value:08X}")
                    except socket.timeout:
                        print(f"  ❌ Readback timeout")
                        
                else:
                    print(f"  ❌ Write failed or wrong address")
            except socket.timeout:
                print(f"  ❌ Write timeout")
                
            packet_id += 1
            time.sleep(0.1)  # Small delay between tests
            
    except Exception as e:
        print(f"Error during testing: {e}")
    finally:
        sock.close()

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 test_gvcp_registers.py <ESP32_IP_ADDRESS>")
        print("Example: python3 test_gvcp_registers.py 192.168.1.100")
        sys.exit(1)
    
    esp32_ip = sys.argv[1]
    test_register_access(esp32_ip)

if __name__ == "__main__":
    main()