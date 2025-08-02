#!/usr/bin/env python3
"""
Analyze Aravis socket behavior during discovery
"""

import subprocess
import time
import socket
import struct

def analyze_aravis_sockets():
    """Analyze what sockets Aravis creates during discovery"""
    
    print("Aravis Socket Analysis")
    print("=====================")
    print()
    
    print("1. Checking available UDP sockets before Aravis...")
    result = subprocess.run(['ss', '-ulpn'], capture_output=True, text=True)
    udp_before = result.stdout
    
    print("2. Starting Aravis discovery with debug...")
    # Start Aravis in background
    aravis_proc = subprocess.Popen(['arv-test-0.8'], 
                                  stdout=subprocess.PIPE, 
                                  stderr=subprocess.PIPE,
                                  env={'ARV_DEBUG': 'interface,discovery'})
    
    # Give Aravis time to create sockets
    time.sleep(2)
    
    print("3. Checking UDP sockets during Aravis discovery...")
    result = subprocess.run(['ss', '-ulpn'], capture_output=True, text=True)
    udp_during = result.stdout
    
    # Terminate Aravis
    aravis_proc.terminate()
    stdout, stderr = aravis_proc.communicate(timeout=5)
    
    print("4. Aravis discovery output:")
    print(stdout.decode())
    if stderr:
        print("Debug output:")
        print(stderr.decode())
    
    print("\n5. Socket comparison:")
    print("UDP sockets BEFORE Aravis:")
    for line in udp_before.split('\n'):
        if ':3956' in line or 'aravis' in line or 'arv' in line:
            print(f"  {line}")
    
    print("\nUDP sockets DURING Aravis:")
    for line in udp_during.split('\n'):
        if ':3956' in line or 'aravis' in line or 'arv' in line:
            print(f"  {line}")
    
    # Check for Aravis-specific sockets
    print("\nLooking for Aravis discovery sockets...")
    for line in udp_during.split('\n'):
        if 'arv' in line.lower() or any(port in line for port in [':0 ', '*:*']):
            print(f"  Potential Aravis socket: {line}")

def test_interface_specific_discovery():
    """Test discovery from each interface separately like Aravis does"""
    
    print("\n\nInterface-Specific Discovery Test")
    print("=================================")
    
    # Get network interfaces
    interfaces = []
    result = subprocess.run(['ip', 'route', 'show'], capture_output=True, text=True)
    for line in result.stdout.split('\n'):
        if 'src' in line and 'dev' in line:
            parts = line.split()
            for i, part in enumerate(parts):
                if part == 'src' and i + 1 < len(parts):
                    ip = parts[i + 1]
                    if ip not in interfaces and not ip.startswith('127.'):
                        interfaces.append(ip)
    
    print(f"Testing discovery from interfaces: {interfaces}")
    
    discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x5678)
    
    for interface_ip in interfaces:
        print(f"\nTesting from interface {interface_ip}:")
        
        try:
            # Create socket bound to specific interface
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.bind((interface_ip, 0))
            local_port = sock.getsockname()[1]
            sock.settimeout(2.0)
            
            print(f"  Bound to {interface_ip}:{local_port}")
            
            # Send discovery
            sock.sendto(discovery_packet, ('192.168.213.40', 3956))
            print(f"  Sent discovery to ESP32")
            
            # Listen for response
            try:
                data, addr = sock.recvfrom(2048)
                print(f"  ✅ Response received: {len(data)} bytes from {addr}")
            except socket.timeout:
                print(f"  ❌ No response received")
                
            sock.close()
            
        except Exception as e:
            print(f"  ❌ Error: {e}")

def main():
    analyze_aravis_sockets()
    test_interface_specific_discovery()

if __name__ == "__main__":
    main()