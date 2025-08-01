#!/usr/bin/env python3
"""
GVCP Discovery Test Script for ESP32-CAM GenICam
================================================

This script sends a proper GigE Vision Control Protocol (GVCP) discovery packet
to test ESP32-CAM device discovery functionality.

Usage: python3 test_gvcp_discovery.py <ESP32_IP_ADDRESS>
"""

import sys
import socket
import struct
import time
import argparse

# GVCP Protocol Constants
GVCP_PORT = 3956
GVCP_PACKET_TYPE_CMD = 0x42
GVCP_PACKET_TYPE_ACK = 0x00
GVCP_CMD_DISCOVERY = 0x0002
GVCP_ACK_DISCOVERY = 0x0003

def create_gvcp_discovery_packet():
    """Create a proper GVCP discovery command packet."""
    # GVCP header structure (8 bytes total):
    # uint8_t packet_type;    (0x42 for command)
    # uint8_t packet_flags;   (0x00 for discovery)
    # uint16_t command;       (0x0002 for discovery, network byte order)
    # uint16_t size;          (0x0000 for discovery - no payload, network byte order)
    # uint16_t id;            (packet ID, network byte order)
    
    packet_type = GVCP_PACKET_TYPE_CMD
    packet_flags = 0x00
    command = GVCP_CMD_DISCOVERY
    size = 0x0000  # Discovery command has no payload
    packet_id = 0x1234  # Arbitrary packet ID
    
    # Pack as network byte order (big endian)
    packet = struct.pack('>BBHHH', 
                        packet_type, 
                        packet_flags, 
                        command, 
                        size, 
                        packet_id)
    
    return packet, packet_id

def parse_gvcp_response(data, expected_id):
    """Parse GVCP response packet and extract information."""
    if len(data) < 8:
        return None, f"Response too short: {len(data)} bytes (expected at least 8)"
    
    # Unpack GVCP header
    packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
    
    response_info = {
        'packet_type': packet_type,
        'packet_flags': packet_flags, 
        'command': command,
        'size': size,
        'packet_id': packet_id,
        'payload': data[8:] if len(data) > 8 else b''
    }
    
    # Validate response
    if packet_type == GVCP_PACKET_TYPE_ACK and command == GVCP_ACK_DISCOVERY:
        if packet_id != expected_id:
            return response_info, f"Packet ID mismatch: got 0x{packet_id:04x}, expected 0x{expected_id:04x}"
        return response_info, None
    elif packet_type == 0x80:  # Error packet
        error_code = struct.unpack('>H', data[8:10])[0] if len(data) >= 10 else 0
        return response_info, f"Device returned error: 0x{error_code:04x}"
    else:
        return response_info, f"Unexpected response type: packet_type=0x{packet_type:02x}, command=0x{command:04x}"

def extract_device_info(payload):
    """Extract device information from discovery response payload."""
    if len(payload) < 0xf8:  # GVBS_DISCOVERY_DATA_SIZE
        return "Payload too short for complete device info"
    
    try:
        # Extract key device information from bootstrap memory
        # Based on GVBS offsets from gvcp_handler.h
        
        # Version (offset 0x00)
        version_raw = struct.unpack('>I', payload[0x00:0x04])[0]
        version_major = (version_raw >> 16) & 0xFFFF
        version_minor = version_raw & 0xFFFF
        
        # Device mode (offset 0x04)
        device_mode = struct.unpack('>I', payload[0x04:0x08])[0]
        
        # MAC address (offset 0x08, 0x0c)
        mac_high = struct.unpack('>I', payload[0x08:0x0c])[0]
        mac_low = struct.unpack('>I', payload[0x0c:0x10])[0]
        mac_bytes = [
            (mac_high >> 8) & 0xFF, mac_high & 0xFF,
            (mac_low >> 24) & 0xFF, (mac_low >> 16) & 0xFF,
            (mac_low >> 8) & 0xFF, mac_low & 0xFF
        ]
        mac_str = ":".join(f"{b:02x}" for b in mac_bytes)
        
        # Current IP (offset 0x24)
        ip_raw = struct.unpack('<I', payload[0x24:0x28])[0]  # Little endian for IP
        ip_str = f"{ip_raw & 0xFF}.{(ip_raw >> 8) & 0xFF}.{(ip_raw >> 16) & 0xFF}.{(ip_raw >> 24) & 0xFF}"
        
        # Device strings (null-terminated)
        manufacturer = payload[0x48:0x68].split(b'\x00')[0].decode('utf-8', errors='ignore')
        model = payload[0x68:0x88].split(b'\x00')[0].decode('utf-8', errors='ignore')
        device_version = payload[0x88:0xa8].split(b'\x00')[0].decode('utf-8', errors='ignore')
        serial = payload[0xd8:0xe8].split(b'\x00')[0].decode('utf-8', errors='ignore')
        user_name = payload[0xe8:0xf8].split(b'\x00')[0].decode('utf-8', errors='ignore')
        
        info = f"""Device Information:
  Version: {version_major}.{version_minor}
  Device Mode: 0x{device_mode:08x}
  MAC Address: {mac_str}
  IP Address: {ip_str}
  Manufacturer: {manufacturer}
  Model: {model}
  Device Version: {device_version}
  Serial Number: {serial}
  User Name: {user_name}"""
        
        return info
        
    except Exception as e:
        return f"Error parsing device info: {e}"

def test_gvcp_discovery(target_ip, timeout=5.0, verbose=False):
    """Test GVCP discovery with the specified ESP32-CAM IP address."""
    print(f"Testing GVCP discovery on {target_ip}:{GVCP_PORT}...")
    
    # Create discovery packet
    packet, packet_id = create_gvcp_discovery_packet()
    
    if verbose:
        print(f"Sending {len(packet)} byte GVCP discovery packet:")
        print(f"  Packet Type: 0x{GVCP_PACKET_TYPE_CMD:02x} (CMD)")
        print(f"  Command: 0x{GVCP_CMD_DISCOVERY:04x} (DISCOVERY)")
        print(f"  Packet ID: 0x{packet_id:04x}")
        print(f"  Raw bytes: {packet.hex()}")
        print()
    
    try:
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        # Send discovery packet
        start_time = time.time()
        bytes_sent = sock.sendto(packet, (target_ip, GVCP_PORT))
        print(f"✓ Sent {bytes_sent} bytes to {target_ip}:{GVCP_PORT}")
        
        # Wait for response
        try:
            response_data, addr = sock.recvfrom(4096)
            response_time = time.time() - start_time
            print(f"✓ Received {len(response_data)} bytes from {addr[0]}:{addr[1]} (response time: {response_time*1000:.1f}ms)")
            
            if verbose:
                print(f"Response bytes: {response_data.hex()}")
                print()
            
            # Parse response
            response_info, error = parse_gvcp_response(response_data, packet_id)
            
            if error:
                print(f"❌ Response validation failed: {error}")
                if response_info and verbose:
                    print(f"Response details: {response_info}")
                return False
            else:
                print("✓ Valid GVCP discovery response received")
                
                # Extract and display device information
                if response_info['payload']:
                    print()
                    device_info = extract_device_info(response_info['payload'])
                    print(device_info)
                else:
                    print("⚠️  No device information in response payload")
                
                return True
                
        except socket.timeout:
            print(f"❌ No response received within {timeout} seconds")
            print("   Possible causes:")
            print("   - ESP32-CAM is not running or not connected to network")
            print("   - Incorrect IP address")
            print("   - Firewall blocking UDP traffic")
            print("   - ESP32-CAM GVCP handler not responding properly")
            return False
            
    except Exception as e:
        print(f"❌ Error during discovery test: {e}")
        return False
    finally:
        if 'sock' in locals():
            sock.close()

def main():
    parser = argparse.ArgumentParser(
        description="Test GVCP discovery with ESP32-CAM GenICam device",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 test_gvcp_discovery.py 192.168.1.100
  python3 test_gvcp_discovery.py 192.168.1.100 --timeout 10 --verbose
        """
    )
    parser.add_argument('ip', help='ESP32-CAM IP address')
    parser.add_argument('--timeout', '-t', type=float, default=5.0, 
                       help='Response timeout in seconds (default: 5.0)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose output with packet details')
    
    args = parser.parse_args()
    
    print("ESP32-CAM GVCP Discovery Test")
    print("=" * 30)
    
    success = test_gvcp_discovery(args.ip, args.timeout, args.verbose)
    
    print()
    if success:
        print("🎉 GVCP discovery test completed successfully!")
        print("   ESP32-CAM is responding to GigE Vision discovery requests.")
        sys.exit(0)
    else:
        print("💥 GVCP discovery test failed!")
        print("   Check the ESP32-CAM serial output for error messages.")
        print("   Use 'just monitor' to view ESP32-CAM logs.")
        sys.exit(1)

if __name__ == "__main__":
    main()