#!/usr/bin/env python3
"""
Debug script for multipart register - tests multiple reads and writes to isolate the issue
"""

import socket
import struct
import sys
import time

def send_gvcp_command(ip, cmd, payload):
    """Send a GVCP command and return the response"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    
    try:
        # Create GVCP header
        length = len(payload) // 4  # Length in words
        req_id = int(time.time() * 1000) & 0xFFFF  # Unique request ID
        
        header = struct.pack('>HHHH', cmd, length, req_id, 0)
        packet = header + payload
        
        print(f"Sending: cmd=0x{cmd:04x}, length={length} words, req_id=0x{req_id:04x}")
        print(f"Payload: {payload.hex()}")
        
        sock.sendto(packet, (ip, 3956))
        response, addr = sock.recvfrom(1024)
        
        print(f"Response: {response.hex()}")
        return response
        
    except Exception as e:
        print(f"Error: {e}")
        return None
    finally:
        sock.close()

def read_register(ip, address):
    """Read a register using READ_MEMORY command"""
    payload = struct.pack('>II', address, 4)  # address, size
    response = send_gvcp_command(ip, 0x0080, payload)  # READ_MEMORY
    
    if response and len(response) >= 12:
        value = struct.unpack('>I', response[8:12])[0]
        print(f"Read 0x{address:04x} = 0x{value:08x}")
        return value
    else:
        print(f"Failed to read 0x{address:04x}")
        return None

def write_register(ip, address, value):
    """Write a register using WRITE_MEMORY command"""
    payload = struct.pack('>III', address, 4, value)  # address, size, value
    response = send_gvcp_command(ip, 0x0082, payload)  # WRITE_MEMORY
    
    if response and len(response) >= 8:
        print(f"Write 0x{address:04x} = 0x{value:08x} - SUCCESS")
        return True
    else:
        print(f"Write 0x{address:04x} = 0x{value:08x} - FAILED")
        return False

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 debug_multipart_register.py <ESP32_IP>")
        sys.exit(1)
    
    ip = sys.argv[1]
    reg_addr = 0x0d24
    
    print(f"Debugging multipart register 0x{reg_addr:04x} on {ip}")
    print("=" * 60)
    
    # Test 1: Read initial value
    print("\n1. Reading initial value...")
    initial_value = read_register(ip, reg_addr)
    if initial_value is None:
        print("❌ Failed to read initial value")
        sys.exit(1)
    
    initial_enabled = bool(initial_value & 1)
    print(f"   Initial: multipart {'enabled' if initial_enabled else 'disabled'}")
    
    # Test 2: Write new value (toggle bit 0)
    print("\n2. Writing new value...")
    new_value = initial_value ^ 0x00000001  # Toggle bit 0
    if write_register(ip, reg_addr, new_value):
        print(f"   Written: 0x{new_value:08x}")
    else:
        print("❌ Write failed")
        sys.exit(1)
    
    # Test 3: Read back immediately
    print("\n3. Reading back immediately...")
    readback1 = read_register(ip, reg_addr)
    if readback1 is not None:
        readback1_enabled = bool(readback1 & 1)
        print(f"   Readback: multipart {'enabled' if readback1_enabled else 'disabled'}")
        if readback1 == new_value:
            print("   ✅ Value persisted correctly")
        else:
            print(f"   ❌ Value mismatch: expected 0x{new_value:08x}, got 0x{readback1:08x}")
    
    # Test 4: Wait and read again
    print("\n4. Waiting 2 seconds and reading again...")
    time.sleep(2)
    readback2 = read_register(ip, reg_addr)
    if readback2 is not None:
        readback2_enabled = bool(readback2 & 1)
        print(f"   After delay: multipart {'enabled' if readback2_enabled else 'disabled'}")
        if readback2 == new_value:
            print("   ✅ Value still persisted")
        else:
            print(f"   ❌ Value changed: expected 0x{new_value:08x}, got 0x{readback2:08x}")
    
    # Test 5: Multiple rapid reads
    print("\n5. Multiple rapid reads...")
    for i in range(3):
        time.sleep(0.1)
        rapid_read = read_register(ip, reg_addr)
        if rapid_read is not None:
            print(f"   Read {i+1}: 0x{rapid_read:08x}")
    
    print("\n" + "=" * 60)
    print("Debug test complete")

if __name__ == '__main__':
    main()