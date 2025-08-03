#!/usr/bin/env python3
"""
Test script to verify heartbeat register 0x934 is accessible.
This should fix the "Unexpected answer (0x80)" errors from Aravis.
"""

import socket
import struct
import sys

def create_gvcp_header(packet_type, flags, command, size_words, packet_id):
    """Create GVCP header with proper byte order"""
    return struct.pack('>BBHHH', packet_type, flags, command, size_words, packet_id)

def parse_gvcp_header(data):
    """Parse GVCP header and return components"""
    if len(data) < 8:
        return None
    packet_type, flags, command, size_words, packet_id = struct.unpack('>BBHHH', data[:8])
    return {
        'packet_type': packet_type,
        'flags': flags, 
        'command': command,
        'size_words': size_words,
        'size_bytes': size_words * 4,
        'packet_id': packet_id
    }

def test_heartbeat_register(esp32_ip):
    """Test reading the heartbeat register at 0x934"""
    print(f"🧪 Testing heartbeat register 0x934 with {esp32_ip}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)
    
    try:
        # Test reading heartbeat register 0x934
        register_address = 0x00000934  # Heartbeat timeout register
        
        # Create READREG packet: header + 1 register address (4 bytes)
        header = create_gvcp_header(0x42, 0x01, 0x0084, 2, 0x1111)  # 2 words: address + size
        payload = struct.pack('>II', register_address, 4)  # address + size
        packet = header + payload
        
        print(f"📤 Sending READ_MEMORY for heartbeat register 0x{register_address:08X}")
        print(f"   Packet size: {len(packet)} bytes (header: 8, payload: {len(payload)})")
        
        sock.sendto(packet, (esp32_ip, 3956))
        
        # Receive response
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        # Parse response header
        resp_header = parse_gvcp_header(response)
        if not resp_header:
            print("❌ Failed to parse response header")
            return False
            
        print(f"   Response header:")
        print(f"     Type: 0x{resp_header['packet_type']:02X} ({'ACK' if resp_header['packet_type'] == 0x00 else 'NACK' if resp_header['packet_type'] == 0x80 else 'Unknown'})")
        print(f"     Command: 0x{resp_header['command']:04X}")
        print(f"     Size (words): {resp_header['size_words']} (= {resp_header['size_bytes']} bytes)")
        print(f"     Packet ID: 0x{resp_header['packet_id']:04X}")
        
        if resp_header['packet_type'] == 0x00:  # ACK
            if len(response) >= 16:  # Header (8) + Address (4) + Value (4)
                returned_addr = struct.unpack('>I', response[8:12])[0]
                heartbeat_value = struct.unpack('>I', response[12:16])[0]
                
                print(f"   ✅ SUCCESS! Heartbeat register accessible:")
                print(f"     Returned address: 0x{returned_addr:08X}")
                print(f"     Heartbeat timeout: {heartbeat_value} ms")
                
                if returned_addr == register_address:
                    print(f"   ✅ Address match confirmed")
                    return True
                else:
                    print(f"   ❌ Address mismatch (expected 0x{register_address:08X})")
                    return False
            else:
                print(f"   ❌ Response too short: {len(response)} bytes")
                return False
        else:
            print(f"   ❌ Received NACK (error code may be in payload)")
            if len(response) > 8:
                error_code = struct.unpack('>H', response[8:10])[0]
                print(f"     Error code: 0x{error_code:04X}")
            return False
            
    except Exception as e:
        print(f"❌ Test failed: {e}")
        return False
    finally:
        sock.close()

def test_readreg_heartbeat(esp32_ip):
    """Test reading heartbeat register using READREG command"""
    print(f"\n🧪 Testing READREG command for heartbeat register 0x934")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)
    
    try:
        # Test reading heartbeat register using READREG
        register_address = 0x00000934  # Heartbeat timeout register
        
        # Create READREG packet: header + 1 register address (4 bytes)
        header = create_gvcp_header(0x42, 0x01, 0x0080, 1, 0x2222)  # 1 word payload
        payload = struct.pack('>I', register_address)
        packet = header + payload
        
        print(f"📤 Sending READREG for heartbeat register 0x{register_address:08X}")
        
        sock.sendto(packet, (esp32_ip, 3956))
        
        # Receive response
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        # Parse response header
        resp_header = parse_gvcp_header(response)
        if not resp_header:
            print("❌ Failed to parse response header")
            return False
            
        print(f"   Response type: 0x{resp_header['packet_type']:02X} ({'ACK' if resp_header['packet_type'] == 0x00 else 'NACK' if resp_header['packet_type'] == 0x80 else 'Unknown'})")
        
        if resp_header['packet_type'] == 0x00:  # ACK
            if len(response) >= 12:  # Header (8) + Value (4)
                heartbeat_value = struct.unpack('>I', response[8:12])[0]
                print(f"   ✅ READREG SUCCESS! Heartbeat timeout: {heartbeat_value} ms")
                return True
            else:
                print(f"   ❌ Response too short: {len(response)} bytes")
                return False
        else:
            print(f"   ❌ Received NACK - register still not accessible via READREG")
            return False
            
    except Exception as e:
        print(f"❌ READREG test failed: {e}")
        return False
    finally:
        sock.close()

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 test_heartbeat_register.py <ESP32_IP_ADDRESS>")
        sys.exit(1)
        
    esp32_ip = sys.argv[1]
    print(f"🔧 Testing heartbeat register 0x934 with ESP32 at {esp32_ip}")
    print("   This should fix Aravis 'Unexpected answer (0x80)' errors")
    
    # Test both READ_MEMORY and READREG approaches
    memory_ok = test_heartbeat_register(esp32_ip)
    readreg_ok = test_readreg_heartbeat(esp32_ip)
    
    # Summary
    print(f"\n📊 Test Summary:")
    print(f"   READ_MEMORY for 0x934: {'✅ PASS' if memory_ok else '❌ FAIL'}")
    print(f"   READREG for 0x934: {'✅ PASS' if readreg_ok else '❌ FAIL'}")
    
    if memory_ok or readreg_ok:
        print("\n🎉 Heartbeat register is accessible! This should fix Aravis compatibility.")
    else:
        print("\n❌ Heartbeat register still not accessible. Bootstrap memory may need adjustment.")
        
    return 0 if (memory_ok or readreg_ok) else 1

if __name__ == "__main__":
    sys.exit(main())