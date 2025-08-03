#!/usr/bin/env python3
"""
Test script to verify GVCP ACK response size field fixes.
This addresses the "Unexpected answer (0x80)" errors from Aravis.
"""

import socket
import struct
import sys
import time

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

def test_readreg_ack_size(esp32_ip):
    """Test READREG command and verify ACK response size field"""
    print(f"\n🧪 Testing READREG ACK size field with {esp32_ip}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)
    
    try:
        # Test reading one standard GVCP register (TLParamsLocked)
        register_address = 0x00000A00  # TLParamsLocked register
        
        # Create READREG packet: header + 1 register address (4 bytes)
        header = create_gvcp_header(0x42, 0x01, 0x0084, 1, 0x1234)  # 1 word payload
        payload = struct.pack('>I', register_address)
        packet = header + payload
        
        print(f"📤 Sending READREG for address 0x{register_address:08X}")
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
        
        # Verify response
        expected_payload_bytes = 4  # 1 register value (4 bytes)
        expected_size_words = 1     # 4 bytes = 1 word
        actual_payload_bytes = len(response) - 8
        
        print(f"   Payload verification:")
        print(f"     Expected payload: {expected_payload_bytes} bytes ({expected_size_words} words)")
        print(f"     Actual payload: {actual_payload_bytes} bytes")
        print(f"     Header claims: {resp_header['size_bytes']} bytes ({resp_header['size_words']} words)")
        
        if resp_header['packet_type'] == 0x00:  # ACK
            if resp_header['size_words'] == expected_size_words and actual_payload_bytes == expected_payload_bytes:
                print("✅ READREG ACK size field is correct!")
                return True
            else:
                print("❌ READREG ACK size field mismatch")
                return False
        else:
            print(f"❌ Received NACK or unexpected response (type 0x{resp_header['packet_type']:02X})")
            return False
            
    except Exception as e:
        print(f"❌ Test failed: {e}")
        return False
    finally:
        sock.close()

def test_writereg_ack_size(esp32_ip):
    """Test WRITEREG command and verify ACK response size field"""
    print(f"\n🧪 Testing WRITEREG ACK size field with {esp32_ip}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)
    
    try:
        # Test writing to TLParamsLocked register
        register_address = 0x00000A00  # TLParamsLocked register
        register_value = 0x00000001    # Lock the parameters
        
        # Create WRITEREG packet: header + address (4 bytes) + value (4 bytes)
        header = create_gvcp_header(0x42, 0x01, 0x0082, 2, 0x5678)  # 2 words payload
        payload = struct.pack('>II', register_address, register_value)
        packet = header + payload
        
        print(f"📤 Sending WRITEREG: addr=0x{register_address:08X}, value=0x{register_value:08X}")
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
        
        # Verify response
        expected_payload_bytes = 4  # 1 register address echoed back (4 bytes)
        expected_size_words = 1     # 4 bytes = 1 word
        actual_payload_bytes = len(response) - 8
        
        print(f"   Payload verification:")
        print(f"     Expected payload: {expected_payload_bytes} bytes ({expected_size_words} words)")
        print(f"     Actual payload: {actual_payload_bytes} bytes")
        print(f"     Header claims: {resp_header['size_bytes']} bytes ({resp_header['size_words']} words)")
        
        if resp_header['packet_type'] == 0x00:  # ACK
            if resp_header['size_words'] == expected_size_words and actual_payload_bytes == expected_payload_bytes:
                print("✅ WRITEREG ACK size field is correct!")
                
                # Parse and verify echoed address
                if len(response) >= 12:
                    echoed_addr = struct.unpack('>I', response[8:12])[0]
                    print(f"   Echoed address: 0x{echoed_addr:08X} ({'✅ correct' if echoed_addr == register_address else '❌ wrong'})")
                
                return True
            else:
                print("❌ WRITEREG ACK size field mismatch")
                return False
        else:
            print(f"❌ Received NACK or unexpected response (type 0x{resp_header['packet_type']:02X})")
            return False
            
    except Exception as e:
        print(f"❌ Test failed: {e}")
        return False
    finally:
        sock.close()

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 test_ack_fix.py <ESP32_IP_ADDRESS>")
        sys.exit(1)
        
    esp32_ip = sys.argv[1]
    print(f"🔧 Testing GVCP ACK size field fixes with ESP32 at {esp32_ip}")
    print("   This verifies the fix for Aravis 'Unexpected answer (0x80)' errors")
    
    # Run tests
    readreg_ok = test_readreg_ack_size(esp32_ip)
    time.sleep(0.5)  # Brief delay between tests
    writereg_ok = test_writereg_ack_size(esp32_ip)
    
    # Summary
    print(f"\n📊 Test Summary:")
    print(f"   READREG ACK size field: {'✅ PASS' if readreg_ok else '❌ FAIL'}")
    print(f"   WRITEREG ACK size field: {'✅ PASS' if writereg_ok else '❌ FAIL'}")
    
    if readreg_ok and writereg_ok:
        print("\n🎉 All tests passed! ACK size fields should now be compatible with Aravis.")
        print("   The 'Unexpected answer (0x80)' errors should be resolved.")
    else:
        print("\n❌ Some tests failed. ACK size field issues may persist.")
        
    return 0 if (readreg_ok and writereg_ok) else 1

if __name__ == "__main__":
    sys.exit(main())