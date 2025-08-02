#!/usr/bin/env python3
"""
Debug script to examine raw GVCP packets for CCP register access.
"""

import socket
import struct
import sys

def hex_dump(data, offset=0):
    """Print hex dump of data."""
    for i in range(0, len(data), 16):
        hex_bytes = ' '.join(f'{b:02x}' for b in data[i:i+16])
        ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in data[i:i+16])
        print(f"{offset+i:04x}: {hex_bytes:<48} {ascii_chars}")

def debug_readreg(target_ip, address):
    """Send READREG and examine raw response."""
    print(f"🔍 Debug READREG for address 0x{address:08x}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    try:
        # Create READREG packet
        packet_type = 0x42    # Command packet
        packet_flags = 0x01   # ACK required
        command = 0x0082      # READREG
        payload = struct.pack('>I', address)  # Address
        payload_size = len(payload)
        packet_id = 0x1234
        
        header = struct.pack('>BBHHH', packet_type, packet_flags, command, payload_size, packet_id)
        packet = header + payload
        
        print(f"📤 Sending packet ({len(packet)} bytes):")
        hex_dump(packet)
        
        sock.sendto(packet, (target_ip, 3956))
        
        # Receive response
        response, addr = sock.recvfrom(1024)
        print(f"\n📥 Received response ({len(response)} bytes) from {addr}:")
        hex_dump(response)
        
        # Parse header
        if len(response) >= 8:
            header = response[:8]
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', header)
            
            print(f"\n📋 Parsed header:")
            print(f"  Packet type: 0x{packet_type:02x}")
            print(f"  Flags: 0x{flags:02x}")
            print(f"  Command: 0x{cmd:04x}")
            print(f"  Size: {size}")
            print(f"  ID: 0x{resp_id:04x}")
            
            if packet_type == 0x00:
                print("  ✅ ACK response")
                if len(response) >= 8 + size:
                    payload = response[8:8+size]
                    print(f"  📦 Payload ({len(payload)} bytes):")
                    hex_dump(payload, 8)
                    
                    if len(payload) >= 8:  # Address (4) + Value (4)
                        resp_addr, value = struct.unpack('>II', payload[:8])
                        print(f"  🎯 Address: 0x{resp_addr:08x}")
                        print(f"  💾 Value: 0x{value:08x} ({value})")
                else:
                    print(f"  ❌ Response too short for payload: {len(response)} < {8+size}")
            elif packet_type == 0x80:
                print("  ❌ NACK response")
                if len(response) >= 10:
                    error_code = struct.unpack('>H', response[8:10])[0]
                    print(f"  🚫 Error code: 0x{error_code:04x}")
            else:
                print(f"  ❓ Unknown packet type: 0x{packet_type:02x}")
        else:
            print("❌ Response too short for header")
            
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

def debug_writereg(target_ip, address, value):
    """Send WRITEREG and examine raw response."""
    print(f"\n🔍 Debug WRITEREG for address 0x{address:08x} = 0x{value:08x}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    try:
        # Create WRITEREG packet
        packet_type = 0x42    # Command packet
        packet_flags = 0x01   # ACK required
        command = 0x0086      # WRITEREG
        payload = struct.pack('>II', address, value)  # Address + Value
        payload_size = len(payload)
        packet_id = 0x1235
        
        header = struct.pack('>BBHHH', packet_type, packet_flags, command, payload_size, packet_id)
        packet = header + payload
        
        print(f"📤 Sending packet ({len(packet)} bytes):")
        hex_dump(packet)
        
        sock.sendto(packet, (target_ip, 3956))
        
        # Receive response
        response, addr = sock.recvfrom(1024)
        print(f"\n📥 Received response ({len(response)} bytes) from {addr}:")
        hex_dump(response)
        
        # Parse header
        if len(response) >= 8:
            header = response[:8]
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', header)
            
            print(f"\n📋 Parsed header:")
            print(f"  Packet type: 0x{packet_type:02x}")
            print(f"  Flags: 0x{flags:02x}")
            print(f"  Command: 0x{cmd:04x}")
            print(f"  Size: {size}")
            print(f"  ID: 0x{resp_id:04x}")
            
            if packet_type == 0x00:
                print("  ✅ ACK response")
                if len(response) >= 8 + size:
                    payload = response[8:8+size]
                    print(f"  📦 Payload ({len(payload)} bytes):")
                    hex_dump(payload, 8)
                    
                    if len(payload) >= 4:  # Address (4)
                        resp_addr = struct.unpack('>I', payload[:4])[0]
                        print(f"  🎯 Address: 0x{resp_addr:08x}")
                else:
                    print(f"  ❌ Response too short for payload: {len(response)} < {8+size}")
            elif packet_type == 0x80:
                print("  ❌ NACK response")
                if len(response) >= 10:
                    error_code = struct.unpack('>H', response[8:10])[0]
                    print(f"  🚫 Error code: 0x{error_code:04x}")
                    if error_code == 0x800e:
                        print("      GVCP_ERROR_INVALID_HEADER")
            else:
                print(f"  ❓ Unknown packet type: 0x{packet_type:02x}")
        else:
            print("❌ Response too short for header")
            
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 debug_ccp_packets.py <ESP32_IP_ADDRESS>")
        sys.exit(1)
    
    target_ip = sys.argv[1]
    print(f"🐛 Debug CCP Packets for {target_ip}")
    print("=" * 60)
    
    # Test READREG 0x200
    debug_readreg(target_ip, 0x200)
    
    # Test WRITEREG 0x200 = 0x200
    debug_writereg(target_ip, 0x200, 0x200)