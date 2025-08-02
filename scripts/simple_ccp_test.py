#!/usr/bin/env python3
"""
Simple Control Channel Privilege test using UDP read/write commands.
This uses the same format as successful discovery packets.
"""

import socket
import struct
import sys

def test_ccp_via_read_memory(target_ip):
    """Test CCP using READ_MEMORY commands instead of READREG."""
    print(f"🧪 Testing CCP via READ_MEMORY/WRITE_MEMORY commands")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    try:
        # Test 1: Read CCP register via READ_MEMORY
        print("\n📋 Test 1: Read CCP register (0x200) via READ_MEMORY")
        
        # READ_MEMORY packet: address=0x200, size=4
        packet_id = 0x1234
        command = 0x0080  # READ_MEMORY
        payload = struct.pack('>II', 0x200, 4)  # address, size
        
        # GVCP header: type, flags, command, size, id
        header = struct.pack('>BBHHH', 0x42, 0x01, command, len(payload), packet_id)
        packet = header + payload
        
        print(f"📤 Sending READ_MEMORY packet ({len(packet)} bytes)")
        sock.sendto(packet, (target_ip, 3956))
        
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        if len(response) >= 8:
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', response[:8])
            print(f"Header: type=0x{packet_type:02x}, cmd=0x{cmd:04x}, size={size}")
            
            if packet_type == 0x43:  # ACK
                if len(response) >= 16:  # Header + address + value
                    addr_resp, value = struct.unpack('>II', response[8:16])
                    print(f"✅ CCP register 0x{addr_resp:08x} = 0x{value:08x}")
                else:
                    print(f"❌ Response too short: {len(response)} bytes")
            else:
                print(f"❌ Non-ACK response: 0x{packet_type:02x}")
        
        # Test 2: Write CCP register via WRITE_MEMORY
        print("\n📋 Test 2: Write CCP register (0x200) = 0x200 via WRITE_MEMORY")
        
        command = 0x0084  # WRITE_MEMORY
        payload = struct.pack('>III', 0x200, 0x200, 0)  # address, value (high), value (low)
        payload = struct.pack('>II', 0x200, 0x200)  # Just address + value
        
        header = struct.pack('>BBHHH', 0x42, 0x01, command, len(payload), packet_id + 1)
        packet = header + payload
        
        print(f"📤 Sending WRITE_MEMORY packet ({len(packet)} bytes)")
        sock.sendto(packet, (target_ip, 3956))
        
        response, addr = sock.recvfrom(1024)
        print(f"📥 Received {len(response)} bytes from {addr}")
        
        if len(response) >= 8:
            packet_type, flags, cmd, size, resp_id = struct.unpack('>BBHHH', response[:8])
            print(f"Header: type=0x{packet_type:02x}, cmd=0x{cmd:04x}, size={size}")
            
            if packet_type == 0x43:  # ACK
                print(f"✅ WRITE_MEMORY acknowledged")
            elif packet_type == 0x80:  # NACK
                if len(response) >= 10:
                    error_code = struct.unpack('>H', response[8:10])[0]
                    print(f"❌ NACK: error code 0x{error_code:04x}")
            else:
                print(f"❌ Unknown response: 0x{packet_type:02x}")
        
        # Test 3: Read again to verify write
        print("\n📋 Test 3: Read CCP register again to verify write")
        
        command = 0x0080  # READ_MEMORY
        payload = struct.pack('>II', 0x200, 4)  # address, size
        
        header = struct.pack('>BBHHH', 0x42, 0x01, command, len(payload), packet_id + 2)
        packet = header + payload
        
        sock.sendto(packet, (target_ip, 3956))
        response, addr = sock.recvfrom(1024)
        
        if len(response) >= 16:
            packet_type = response[0]
            if packet_type == 0x43:  # ACK
                addr_resp, value = struct.unpack('>II', response[8:16])
                print(f"✅ CCP register 0x{addr_resp:08x} = 0x{value:08x}")
                if value == 0x200:
                    print("🎉 SUCCESS: CCP register accepts 0x200 (Aravis value)!")
                else:
                    print(f"⚠️  Unexpected value: got 0x{value:08x}, expected 0x200")
            else:
                print(f"❌ Non-ACK response: 0x{packet_type:02x}")
        
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 simple_ccp_test.py <ESP32_IP_ADDRESS>")
        sys.exit(1)
    
    target_ip = sys.argv[1]
    test_ccp_via_read_memory(target_ip)