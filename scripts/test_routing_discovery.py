#!/usr/bin/env python3
"""
Test discovery routing to understand why interface-bound sockets fail.

This script compares default routing vs interface-specific binding.
"""

import socket
import struct
import time

def create_discovery_packet():
    """Create a GVCP discovery packet."""
    packet_type = 0x42  # GVCP_PACKET_TYPE_CMD
    packet_flags = 0x00
    command = 0x0002    # GVCP_CMD_DISCOVERY
    size = 0x0000       # No payload
    packet_id = 0x1234  # Same as working test
    
    packet = struct.pack('>BBHHH', packet_type, packet_flags, command, size, packet_id)
    return packet, packet_id

def test_default_route(target_ip, timeout=2.0):
    """Test discovery using default routing (like the working manual test)."""
    packet, packet_id = create_discovery_packet()
    
    print(f"Testing default route to {target_ip}:")
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        # Get the local IP that would be used
        sock.connect((target_ip, 3956))
        local_ip = sock.getsockname()[0]
        sock.close()
        
        # Create new socket for actual test
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        print(f"  Local IP (auto-selected): {local_ip}")
        
        start_time = time.time()
        bytes_sent = sock.sendto(packet, (target_ip, 3956))
        print(f"  Sent: {bytes_sent} bytes")
        
        try:
            response_data, addr = sock.recvfrom(4096)
            response_time = time.time() - start_time
            print(f"  ✓ Response: {len(response_data)} bytes in {response_time*1000:.1f}ms from {addr[0]}:{addr[1]}")
            return True, local_ip, response_time
        except socket.timeout:
            print(f"  ❌ No response within {timeout} seconds")
            return False, local_ip, None
            
    except Exception as e:
        print(f"  ❌ Error: {e}")
        return False, None, None
    finally:
        if 'sock' in locals():
            sock.close()

def test_bound_interface(target_ip, source_ip, timeout=2.0):
    """Test discovery bound to specific interface."""
    packet, packet_id = create_discovery_packet()
    
    print(f"Testing bound interface {source_ip} -> {target_ip}:")
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        # Bind to specific interface
        sock.bind((source_ip, 0))
        local_port = sock.getsockname()[1]
        print(f"  Bound to: {source_ip}:{local_port}")
        
        start_time = time.time()
        bytes_sent = sock.sendto(packet, (target_ip, 3956))
        print(f"  Sent: {bytes_sent} bytes")
        
        try:
            response_data, addr = sock.recvfrom(4096)
            response_time = time.time() - start_time
            print(f"  ✓ Response: {len(response_data)} bytes in {response_time*1000:.1f}ms from {addr[0]}:{addr[1]}")
            return True, response_time
        except socket.timeout:
            print(f"  ❌ No response within {timeout} seconds")
            return False, None
            
    except Exception as e:
        print(f"  ❌ Error: {e}")
        return False, None
    finally:
        if 'sock' in locals():
            sock.close()

def main():
    target_ip = "192.168.213.40"
    
    print("Discovery Routing Analysis")
    print("=" * 30)
    print()
    
    # Test 1: Default routing (working method)
    success1, auto_ip, time1 = test_default_route(target_ip)
    print()
    
    # Test 2: Bound to the same IP that auto-selection chose
    if auto_ip:
        success2, time2 = test_bound_interface(target_ip, auto_ip)
        print()
        
        # Test 3: Bound to other interface IPs
        other_ips = ["192.168.213.45", "192.168.213.28"]
        for ip in other_ips:
            if ip != auto_ip:
                print(f"Testing alternate interface {ip}:")
                success3, time3 = test_bound_interface(target_ip, ip)
                print()
    
    print("Analysis:")
    print("-" * 10)
    if success1:
        print(f"✓ Default routing works (auto-selected IP: {auto_ip})")
    else:
        print("❌ Default routing failed")
    
    print()
    print("This test helps identify if the issue is:")
    print("1. Interface binding vs default routing")
    print("2. Specific source IP rejection by ESP32")
    print("3. Network routing/firewall issues")

if __name__ == "__main__":
    main()