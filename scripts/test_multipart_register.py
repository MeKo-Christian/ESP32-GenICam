#!/usr/bin/env python3
"""
Test script for multipart register (0x0d24) - SCCFG register access
Tests reading the Stream Channel Configuration multipart register
"""

import socket
import struct
import sys
import argparse

def send_gvcp_read(ip, address):
    """Send GVCP READ_MEMORY command to read a register"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    
    try:
        # GVCP READ_MEMORY command
        cmd = 0x0080  # READ_MEMORY
        length = 0x0002  # 2 words (8 bytes)
        req_id = 0x2000  # Use different ID than enable script
        
        header = struct.pack('>HHHH', cmd, length, req_id, 0)
        payload = struct.pack('>II', address, 4)  # address, size
        packet = header + payload
        
        sock.sendto(packet, (ip, 3956))
        response, addr = sock.recvfrom(1024)
        
        if len(response) >= 12:
            value = struct.unpack('>I', response[8:12])[0]
            multipart_status = "enabled" if value & 1 else "disabled"
            print(f'Register 0x{address:04x} = 0x{value:08x} (multipart {multipart_status})')
            return value
        else:
            print(f'Failed to read register 0x{address:04x} - response too short')
            return None
            
    except socket.timeout:
        print(f'Timeout reading register 0x{address:04x}')
        return None
    except Exception as e:
        print(f'Error reading register 0x{address:04x}: {e}')
        return None
    finally:
        sock.close()

def main():
    parser = argparse.ArgumentParser(description='Test multipart register (0x0d24) access')
    parser.add_argument('ip', help='ESP32-CAM IP address')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if args.verbose:
        print(f"Testing multipart register (0x0d24) on {args.ip}...")
        print("This tests the Stream Channel Configuration (SCCFG) multipart register")
        print("Bit 0 = multipart enable/disable")
        print()
    
    # Test reading the multipart register
    value = send_gvcp_read(args.ip, 0x0d24)
    
    if value is not None:
        if args.verbose:
            print(f"\nRegister breakdown:")
            print(f"  Bit 0 (multipart enable): {value & 1}")
            print(f"  Other bits: 0x{(value >> 1):07x}")
        sys.exit(0)
    else:
        print("❌ Failed to read multipart register")
        sys.exit(1)

if __name__ == '__main__':
    main()