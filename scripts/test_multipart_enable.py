#!/usr/bin/env python3
"""
Test script for enabling/disabling multipart mode via register 0x0d24
Sets or clears bit 0 of the SCCFG multipart register
"""

import socket
import struct
import sys
import argparse
import time

class GVCPClient:
    """GVCP client with proper socket management and unique request IDs"""
    
    def __init__(self, ip):
        self.ip = ip
        self.sock = None
        self.req_id = 0x1000  # Start with unique base ID
        
    def __enter__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(5)
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.sock:
            self.sock.close()
    
    def _get_next_req_id(self):
        """Get unique request ID for each command"""
        self.req_id += 1
        return self.req_id
    
    def send_gvcp_write(self, address, value):
        """Send GVCP WRITE_MEMORY command to write a register"""
        try:
            # GVCP WRITE_MEMORY command  
            cmd = 0x0082  # WRITE_MEMORY
            length = 0x0003  # 3 words (12 bytes)
            req_id = self._get_next_req_id()
            
            header = struct.pack('>HHHH', cmd, length, req_id, 0)
            payload = struct.pack('>III', address, 4, value)  # address, size, value
            packet = header + payload
            
            self.sock.sendto(packet, (self.ip, 3956))
            response, addr = self.sock.recvfrom(1024)
            
            if len(response) >= 8:
                print(f'✅ Set register 0x{address:04x} = 0x{value:08x}')
                return True
            else:
                print(f'❌ Failed to write register 0x{address:04x} - response too short')
                return False
                
        except socket.timeout:
            print(f'❌ Timeout writing register 0x{address:04x}')
            return False
        except Exception as e:
            print(f'❌ Error writing register 0x{address:04x}: {e}')
            return False

    def send_gvcp_read(self, address):
        """Send GVCP READ_MEMORY command to read a register"""
        try:
            # Longer delay to ensure previous command is processed
            time.sleep(0.5)
            
            cmd = 0x0080  # READ_MEMORY
            length = 0x0002  # 2 words (8 bytes)
            req_id = self._get_next_req_id()
            
            header = struct.pack('>HHHH', cmd, length, req_id, 0)
            payload = struct.pack('>II', address, 4)
            packet = header + payload
            
            self.sock.sendto(packet, (self.ip, 3956))
            response, addr = self.sock.recvfrom(1024)
            
            if len(response) >= 12:
                return struct.unpack('>I', response[8:12])[0]
            return None
        except Exception as e:
            print(f'❌ Error reading register 0x{address:04x}: {e}')
            return None

def send_gvcp_write(ip, address, value):
    """Send GVCP WRITE_MEMORY command to write a register"""
    with GVCPClient(ip) as client:
        return client.send_gvcp_write(address, value)

def send_gvcp_read(ip, address):
    """Send GVCP READ_MEMORY command to read a register"""
    with GVCPClient(ip) as client:
        return client.send_gvcp_read(address)

def main():
    parser = argparse.ArgumentParser(description='Enable/disable multipart mode via register 0x0d24')
    parser.add_argument('ip', help='ESP32-CAM IP address')
    parser.add_argument('--enable', action='store_true', help='Enable multipart mode (default)')
    parser.add_argument('--disable', action='store_true', help='Disable multipart mode')
    parser.add_argument('--status', action='store_true', help='Just check current status')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if args.verbose:
        print(f"Multipart mode control for {args.ip}")
        print("Register 0x0d24 (SCCFG multipart register)")
        print()
    
    # Use single client for all operations to avoid socket confusion
    with GVCPClient(args.ip) as client:
        # Read current status
        current_value = client.send_gvcp_read(0x0d24)
        if current_value is None:
            print("❌ Failed to read current multipart status")
            sys.exit(1)
        
        current_enabled = bool(current_value & 1)
        print(f"Current status: multipart {'enabled' if current_enabled else 'disabled'} (0x{current_value:08x})")
        
        if args.status:
            sys.exit(0)
        
        # Determine action
        if args.disable:
            target_enabled = False
            action = "Disabling"
        else:  # Default to enable
            target_enabled = True  
            action = "Enabling"
        
        if current_enabled == target_enabled:
            status = "enabled" if target_enabled else "disabled"
            print(f"✅ Multipart mode already {status}")
            sys.exit(0)
        
        # Calculate new value
        if target_enabled:
            new_value = current_value | 0x00000001  # Set bit 0
        else:
            new_value = current_value & 0xFFFFFFFE  # Clear bit 0
        
        print(f"{action} multipart mode...")
        
        # Write new value
        if client.send_gvcp_write(0x0d24, new_value):
            # Verify the change
            verify_value = client.send_gvcp_read(0x0d24)
            if verify_value is not None:
                verify_enabled = bool(verify_value & 1)
                print(f"Debug: verify_value=0x{verify_value:08x}, verify_enabled={verify_enabled}, target_enabled={target_enabled}")
                if verify_enabled == target_enabled:
                    status = "enabled" if target_enabled else "disabled"
                    print(f"✅ Multipart mode {status} successfully")
                    sys.exit(0)
                else:
                    print(f"❌ Failed to verify multipart mode change: expected {target_enabled}, got {verify_enabled}")
                    sys.exit(1)
            else:
                print("❌ Failed to verify multipart mode change - read returned None")
                sys.exit(1)
        else:
            print("❌ Failed to change multipart mode")
            sys.exit(1)

if __name__ == '__main__':
    main()