#!/usr/bin/env python3
"""
Test script to verify bootstrap register access and packet type fixes.
Tests the key registers that Aravis would access.
"""

import socket
import struct
import sys

def read_memory(sock, target_ip, address, size, packet_id=0x1234):
    """Send READ_MEMORY command and return response."""
    
    # Create READ_MEMORY packet
    command = 0x0080  # READ_MEMORY
    payload = struct.pack('>II', address, size)  # address, size
    
    # GVCP header: type, flags, command, size, id
    header = struct.pack('>BBHHH', 0x42, 0x01, command, len(payload), packet_id)
    packet = header + payload
    
    print(f"📤 READ_MEMORY addr=0x{address:08x}, size={size}")
    sock.sendto(packet, (target_ip, 3956))
    
    try:
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes")
        
        if len(response) >= 8:
            packet_type, flags, cmd, size_resp, resp_id = struct.unpack('>BBHHH', response[:8])
            print(f"   Header: type=0x{packet_type:02x}, cmd=0x{cmd:04x}, size={size_resp}")
            
            if packet_type == 0x43:  # ACK
                if len(response) >= 12:  # Header + address
                    addr_resp = struct.unpack('>I', response[8:12])[0]
                    payload = response[12:]
                    print(f"   ✅ ACK: addr=0x{addr_resp:08x}, payload={len(payload)} bytes")
                    
                    if len(payload) >= 4:
                        value = struct.unpack('>I', payload[:4])[0]
                        print(f"   💾 Value: 0x{value:08x} ({value})")
                        return value
                    else:
                        print(f"   ⚠️  Payload too short: {len(payload)} bytes")
                else:
                    print(f"   ❌ Response too short for address: {len(response)} bytes")
            elif packet_type == 0x80:  # NACK
                if len(response) >= 10:
                    error_code = struct.unpack('>H', response[8:10])[0]
                    print(f"   ❌ NACK: error code 0x{error_code:04x}")
                else:
                    print(f"   ❌ NACK without error code")
            else:
                print(f"   ❓ Unknown packet type: 0x{packet_type:02x}")
        else:
            print(f"   ❌ Response too short: {len(response)} bytes")
            
    except socket.timeout:
        print("   ⏰ Timeout")
        
    return None

def test_bootstrap_registers(target_ip):
    """Test key bootstrap registers that Aravis accesses."""
    print(f"🧪 Testing Bootstrap Registers on {target_ip}")
    print("=" * 60)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    test_cases = [
        (0x00000000, "Version register"),
        (0x00000048, "Manufacturer name"),
        (0x00000064, "XML URL pointer (NEW)"),
        (0x00000068, "Model name"),
        (0x00000200, "Control Channel Privilege"),
        (0x00000220, "XML URL string"),
    ]
    
    try:
        for address, description in test_cases:
            print(f"\n📋 Testing {description} (0x{address:08x})")
            value = read_memory(sock, target_ip, address, 4)
            
            if address == 0x00000064:  # XML URL pointer
                if value is not None:
                    print(f"   🔗 XML URL pointer points to: 0x{value:08x}")
                    if value == 0x220:
                        print("   ✅ Correct! Points to XML URL string location")
                    else:
                        print("   ⚠️  Unexpected pointer value")
                        
        # Test actual XML URL reading
        print(f"\n📋 Testing XML URL string reading")
        value = read_memory(sock, target_ip, 0x220, 32)  # Read first 32 bytes of URL
        
        # Test XML data reading 
        print(f"\n📋 Testing XML data reading (from 0x10000)")
        xml_value = read_memory(sock, target_ip, 0x10000, 64)  # Read first 64 bytes of XML
        
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()
    
    print("\n" + "=" * 60)
    print("🏁 Bootstrap register test completed")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 test_bootstrap_registers.py <ESP32_IP_ADDRESS>")
        sys.exit(1)
    
    target_ip = sys.argv[1]
    test_bootstrap_registers(target_ip)