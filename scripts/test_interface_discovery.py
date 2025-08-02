#!/usr/bin/env python3
"""
Test GigE Vision discovery on specific network interfaces to isolate networking issues.

This script helps debug why Aravis cannot discover the ESP32-CAM by testing
discovery from each network interface individually.
"""

import socket
import struct
import time
import subprocess
import re
from typing import List, Dict, Optional, Tuple

def get_network_interfaces() -> List[Dict[str, str]]:
    """Get list of network interfaces with their IP addresses and broadcast addresses."""
    interfaces = []
    
    try:
        # Use ip command to get interface information
        result = subprocess.run(['ip', 'addr', 'show'], capture_output=True, text=True)
        
        current_interface = None
        current_ip = None
        current_broadcast = None
        
        for line in result.stdout.split('\n'):
            # Match interface name
            interface_match = re.match(r'^\d+:\s+([^:]+):', line)
            if interface_match:
                # Save previous interface if complete
                if current_interface and current_ip and current_broadcast:
                    interfaces.append({
                        'name': current_interface,
                        'ip': current_ip,
                        'broadcast': current_broadcast
                    })
                
                current_interface = interface_match.group(1)
                current_ip = None
                current_broadcast = None
                continue
            
            # Match IPv4 address with broadcast
            ip_match = re.search(r'inet\s+(\d+\.\d+\.\d+\.\d+)/\d+.*brd\s+(\d+\.\d+\.\d+\.\d+)', line)
            if ip_match and not current_ip:  # Take first IPv4 address
                current_ip = ip_match.group(1)
                current_broadcast = ip_match.group(2)
        
        # Save last interface
        if current_interface and current_ip and current_broadcast:
            interfaces.append({
                'name': current_interface,
                'ip': current_ip,
                'broadcast': current_broadcast
            })
            
    except Exception as e:
        print(f"Error getting network interfaces: {e}")
    
    return interfaces

def create_discovery_packet() -> Tuple[bytes, int]:
    """Create a GVCP discovery packet."""
    packet_type = 0x42  # GVCP_PACKET_TYPE_CMD
    packet_flags = 0x00
    command = 0x0002    # GVCP_CMD_DISCOVERY
    size = 0x0000       # No payload
    packet_id = 0x1234  # Use same ID as working test
    
    packet = struct.pack('>BBHHH', packet_type, packet_flags, command, size, packet_id)
    return packet, packet_id

def test_discovery_from_interface(interface_ip: str, target_ip: str, target_port: int = 3956, timeout: float = 2.0) -> Optional[Dict]:
    """Test discovery from a specific network interface."""
    packet, packet_id = create_discovery_packet()
    
    print(f"  Testing from interface {interface_ip} -> {target_ip}:{target_port}")
    
    try:
        # Create socket and bind to specific interface
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        # Bind to specific interface IP
        sock.bind((interface_ip, 0))
        
        # Send discovery packet
        start_time = time.time()
        bytes_sent = sock.sendto(packet, (target_ip, target_port))
        
        # Wait for response
        try:
            response_data, addr = sock.recvfrom(4096)
            response_time = time.time() - start_time
            
            print(f"    ✓ Response: {len(response_data)} bytes in {response_time*1000:.1f}ms from {addr[0]}:{addr[1]}")
            
            # Parse response header
            if len(response_data) >= 8:
                packet_type, packet_flags, command, size, resp_id = struct.unpack('>BBHHH', response_data[:8])
                
                result = {
                    'success': True,
                    'response_time_ms': response_time * 1000,
                    'response_size': len(response_data),
                    'packet_type': packet_type,
                    'command': command,
                    'packet_id': resp_id,
                    'expected_id': packet_id,
                    'id_match': resp_id == packet_id
                }
                
                print(f"    ✓ Valid GVCP response: type=0x{packet_type:02x}, cmd=0x{command:04x}, id=0x{resp_id:04x}")
                return result
            else:
                print(f"    ❌ Response too short: {len(response_data)} bytes")
                return {'success': False, 'error': 'Response too short'}
                
        except socket.timeout:
            print(f"    ❌ No response within {timeout} seconds")
            return {'success': False, 'error': 'Timeout'}
            
    except Exception as e:
        print(f"    ❌ Error: {e}")
        return {'success': False, 'error': str(e)}
    finally:
        if 'sock' in locals():
            sock.close()

def test_broadcast_discovery_from_interface(interface_ip: str, broadcast_ip: str, target_port: int = 3956, timeout: float = 3.0) -> Optional[Dict]:
    """Test broadcast discovery from a specific network interface."""
    packet, packet_id = create_discovery_packet()
    
    print(f"  Testing broadcast from {interface_ip} -> {broadcast_ip}:{target_port}")
    
    try:
        # Create socket and bind to specific interface
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        
        # Bind to specific interface IP
        sock.bind((interface_ip, 0))
        
        # Send broadcast packet
        start_time = time.time()
        bytes_sent = sock.sendto(packet, (broadcast_ip, target_port))
        
        responses = []
        
        # Collect all responses within timeout
        while True:
            try:
                response_data, addr = sock.recvfrom(4096)
                response_time = time.time() - start_time
                
                print(f"    ✓ Response: {len(response_data)} bytes in {response_time*1000:.1f}ms from {addr[0]}:{addr[1]}")
                
                # Parse response header
                if len(response_data) >= 8:
                    packet_type, packet_flags, command, size, resp_id = struct.unpack('>BBHHH', response_data[:8])
                    
                    response = {
                        'source_ip': addr[0],
                        'source_port': addr[1],
                        'response_time_ms': response_time * 1000,
                        'response_size': len(response_data),
                        'packet_type': packet_type,
                        'command': command,
                        'packet_id': resp_id,
                        'expected_id': packet_id,
                        'id_match': resp_id == packet_id
                    }
                    responses.append(response)
                    
            except socket.timeout:
                break  # No more responses
                
        if responses:
            print(f"    ✓ Received {len(responses)} broadcast responses")
            return {'success': True, 'responses': responses}
        else:
            print(f"    ❌ No broadcast responses within {timeout} seconds")
            return {'success': False, 'error': 'No broadcast responses'}
            
    except Exception as e:
        print(f"    ❌ Error: {e}")
        return {'success': False, 'error': str(e)}
    finally:
        if 'sock' in locals():
            sock.close()

def main():
    print("GigE Vision Discovery Interface Testing")
    print("=" * 50)
    
    # Get target IP (ESP32-CAM)
    esp32_ip = "192.168.213.40"  # Update if needed
    
    print(f"Target ESP32-CAM IP: {esp32_ip}")
    print()
    
    # Get network interfaces
    interfaces = get_network_interfaces()
    
    if not interfaces:
        print("❌ No network interfaces found")
        return
    
    print("Available Network Interfaces:")
    for i, iface in enumerate(interfaces, 1):
        print(f"  {i}. {iface['name']}: {iface['ip']} (broadcast: {iface['broadcast']})")
    print()
    
    # Test unicast discovery from each interface
    print("Testing Unicast Discovery:")
    print("-" * 30)
    
    unicast_results = {}
    for iface in interfaces:
        if iface['ip'].startswith('127.'):  # Skip loopback
            continue
            
        print(f"Interface {iface['name']} ({iface['ip']}):")
        result = test_discovery_from_interface(iface['ip'], esp32_ip)
        unicast_results[iface['name']] = result
        print()
    
    # Test broadcast discovery from each interface
    print("Testing Broadcast Discovery:")
    print("-" * 30)
    
    broadcast_results = {}
    for iface in interfaces:
        if iface['ip'].startswith('127.'):  # Skip loopback
            continue
            
        print(f"Interface {iface['name']} ({iface['ip']}):")
        result = test_broadcast_discovery_from_interface(iface['ip'], iface['broadcast'])
        broadcast_results[iface['name']] = result
        print()
    
    # Summary
    print("Summary:")
    print("=" * 20)
    
    print("Unicast Discovery Results:")
    for iface_name, result in unicast_results.items():
        if result and result.get('success'):
            print(f"  ✓ {iface_name}: Success ({result['response_time_ms']:.1f}ms)")
        else:
            error = result.get('error', 'Unknown error') if result else 'No result'
            print(f"  ❌ {iface_name}: Failed ({error})")
    
    print()
    print("Broadcast Discovery Results:")
    for iface_name, result in broadcast_results.items():
        if result and result.get('success'):
            count = len(result.get('responses', []))
            print(f"  ✓ {iface_name}: {count} responses")
        else:
            error = result.get('error', 'Unknown error') if result else 'No result'
            print(f"  ❌ {iface_name}: Failed ({error})")
    
    # Recommendations
    print()
    print("Recommendations:")
    print("-" * 15)
    
    working_unicast = [name for name, result in unicast_results.items() if result and result.get('success')]
    working_broadcast = [name for name, result in broadcast_results.items() if result and result.get('success')]
    
    if working_unicast:
        print(f"✓ Unicast discovery works from: {', '.join(working_unicast)}")
    else:
        print("❌ Unicast discovery failed from all interfaces")
    
    if working_broadcast:
        print(f"✓ Broadcast discovery works from: {', '.join(working_broadcast)}")
        print("  → Aravis should be able to discover the device")
    else:
        print("❌ Broadcast discovery failed from all interfaces")
        print("  → This explains why Aravis cannot discover the device")
        print("  → ESP32 is not receiving or responding to broadcast packets")
    
    if working_unicast and not working_broadcast:
        print()
        print("💡 Solution suggestions:")
        print("  1. ESP32 broadcast reception issue - check WiFi configuration")
        print("  2. Implement discovery proxy service")
        print("  3. Use multicast discovery instead of broadcast")

if __name__ == "__main__":
    main()