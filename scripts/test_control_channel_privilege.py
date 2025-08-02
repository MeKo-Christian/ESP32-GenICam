#!/usr/bin/env python3
"""
Test script for Control Channel Privilege Register implementation.
Tests READREG and WRITEREG commands for addresses 0x200 and 0x204.
"""

import socket
import struct
import time
import sys

# GVCP constants
GVCP_PORT = 3956
GVCP_CMD_READREG = 0x0082
GVCP_CMD_WRITEREG = 0x0086

# Control Channel Privilege registers
CONTROL_CHANNEL_PRIVILEGE_OFFSET = 0x200
CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET = 0x204

def create_gvcp_packet(command, packet_id, data):
    """Create a GVCP packet with the given command and data."""
    # GVCP header format: packet_type, packet_flags, command, size, id
    packet_type = 0x42  # Command packet type
    packet_flags = 0x01  # ACK required flag
    header = struct.pack('>BBHHH', packet_type, packet_flags, command, len(data), packet_id)
    return header + data

def send_readreg(sock, target_ip, address, packet_id=0x1234):
    """Send a READREG command and return the response."""
    data = struct.pack('>I', address)
    packet = create_gvcp_packet(GVCP_CMD_READREG, packet_id, data)
    
    sock.sendto(packet, (target_ip, GVCP_PORT))
    print(f"📤 Sent READREG for address 0x{address:08x}")
    
    try:
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        if len(response) >= 12:  # Header (8) + address (4)
            header = response[:8]
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', header)
            
            if len(response) >= 12:
                resp_address = struct.unpack('>I', response[8:12])[0]
                if len(response) >= 16:
                    value = struct.unpack('>I', response[12:16])[0]
                    print(f"✅ READREG 0x{resp_address:08x} = 0x{value:08x} ({value})")
                    return value
                else:
                    print(f"❌ Response too short for data: {len(response)} bytes")
            else:
                print(f"❌ Response too short for address: {len(response)} bytes")
        else:
            print(f"❌ Response too short: {len(response)} bytes")
            
    except socket.timeout:
        print("⏰ Timeout waiting for response")
        
    return None

def send_writereg(sock, target_ip, address, value, packet_id=0x1235):
    """Send a WRITEREG command and return success status."""
    data = struct.pack('>II', address, value)
    packet = create_gvcp_packet(GVCP_CMD_WRITEREG, packet_id, data)
    
    sock.sendto(packet, (target_ip, GVCP_PORT))
    print(f"📤 Sent WRITEREG for address 0x{address:08x} = 0x{value:08x} ({value})")
    
    try:
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        if len(response) >= 8:
            header = response[:8]
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', header)
            
            if packet_type == 0x00:  # ACK (should be 0x00 for ACK, not 0x81)
                print(f"✅ WRITEREG acknowledged")
                return True
            elif packet_type == 0x80:  # NACK
                if len(response) >= 10:
                    error_code = struct.unpack('>H', response[8:10])[0]
                    print(f"❌ WRITEREG NACK: error code 0x{error_code:04x}")
                else:
                    print(f"❌ WRITEREG NACK: no error code")
                return False
        else:
            print(f"❌ Response too short: {len(response)} bytes")
            
    except socket.timeout:
        print("⏰ Timeout waiting for response")
        
    return False

def test_control_channel_privilege(target_ip):
    """Test the Control Channel Privilege Register implementation."""
    print(f"🧪 Testing Control Channel Privilege Register on {target_ip}")
    print("=" * 60)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    
    try:
        # Test 1: Read initial privilege value (should be 0)
        print("\n📋 Test 1: Read initial Control Channel Privilege (0x200)")
        initial_value = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET)
        if initial_value is not None:
            if initial_value == 0:
                print("✅ Initial privilege value is 0 (No access) - correct!")
            else:
                print(f"⚠️  Initial privilege value is {initial_value}, expected 0")
        
        time.sleep(0.5)
        
        # Test 2: Write privilege value 0x200 (Primary control - Aravis standard)
        print("\n📋 Test 2: Write Control Channel Privilege = 0x200 (Primary control - Aravis)")
        if send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0x200):
            print("✅ Write acknowledged")
            
            # Verify the write
            time.sleep(0.2)
            print("\n📋 Test 2b: Verify Control Channel Privilege value")
            read_value = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET)
            if read_value == 0x200:
                print("✅ Privilege value correctly set to 0x200 (Primary control)")
            else:
                print(f"❌ Privilege value is 0x{read_value:x}, expected 0x200")
        
        time.sleep(0.5)
        
        # Test 3: Write privilege value 0x1 (Exclusive control)
        print("\n📋 Test 3: Write Control Channel Privilege = 0x1 (Exclusive control)")
        if send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0x1):
            print("✅ Write acknowledged")
            
            # Verify the write
            time.sleep(0.2)
            print("\n📋 Test 3b: Verify Control Channel Privilege value")
            read_value = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET)
            if read_value == 0x1:
                print("✅ Privilege value correctly set to 0x1 (Exclusive control)")
            else:
                print(f"❌ Privilege value is 0x{read_value:x}, expected 0x1")
        
        time.sleep(0.5)
        
        # Test 4: Write privilege value 0x201 (Both bits)
        print("\n📋 Test 4: Write Control Channel Privilege = 0x201 (Both exclusive and primary)")
        if send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0x201):
            print("✅ Write acknowledged")
            
            # Verify the write
            time.sleep(0.2)
            print("\n📋 Test 4b: Verify Control Channel Privilege value")
            read_value = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET)
            if read_value == 0x201:
                print("✅ Privilege value correctly set to 0x201 (Both bits)")
            else:
                print(f"❌ Privilege value is 0x{read_value:x}, expected 0x201")
        
        time.sleep(0.5)
        
        # Test 5: Try to write invalid privilege value (should fail)
        print("\n📋 Test 5: Write invalid Control Channel Privilege = 0x100 (should fail)")
        if not send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0x100):
            print("✅ Invalid privilege value correctly rejected")
        else:
            print("❌ Invalid privilege value was accepted (should be rejected)")
        
        time.sleep(0.5)
        
        # Test 6: Test Control Channel Privilege Key register (0x204)
        print("\n📋 Test 6: Test Control Channel Privilege Key register (0x204)")
        print("\n📋 Test 6a: Read initial key value")
        key_value = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET)
        if key_value is not None:
            print(f"✅ Initial key value: 0x{key_value:08x}")
        
        time.sleep(0.2)
        
        print("\n📋 Test 6b: Write key value 0x12345678")
        if send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET, 0x12345678):
            print("✅ Key write acknowledged")
            
            # Verify the write
            time.sleep(0.2)
            print("\n📋 Test 6c: Verify key value")
            read_key = send_readreg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET)
            if read_key == 0x12345678:
                print("✅ Key value correctly set to 0x12345678")
            else:
                print(f"❌ Key value is 0x{read_key:08x}, expected 0x12345678")
        
        # Test 7: Reset privilege to 0
        print("\n📋 Test 7: Reset Control Channel Privilege to 0 (No access)")
        if send_writereg(sock, target_ip, CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0):
            print("✅ Reset acknowledged")
        
    except Exception as e:
        print(f"❌ Error during testing: {e}")
    finally:
        sock.close()
    
    print("\n" + "=" * 60)
    print("🏁 Control Channel Privilege Register test completed")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 test_control_channel_privilege.py <ESP32_IP_ADDRESS>")
        print("Example: python3 test_control_channel_privilege.py 192.168.1.100")
        sys.exit(1)
    
    target_ip = sys.argv[1]
    test_control_channel_privilege(target_ip)