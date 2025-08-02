#!/usr/bin/env python3
"""
Source Address Response Test
Test if Aravis accepts discovery responses when source address matches sender interface
"""

import socket
import struct
import threading
import time
import subprocess
from datetime import datetime

def create_fake_response(packet_id, device_ip="192.168.213.40"):
    """Create a fake discovery response packet"""
    
    # GVCP header: ACK, no flags, DISCOVERY_ACK command, 768 bytes payload, packet ID
    header = struct.pack('>BBHHH', 0x00, 0x00, 0x0003, 768, packet_id)
    
    # Simplified bootstrap registers (768 bytes total)
    bootstrap = bytearray(768)
    
    # Version (offset 0x00)
    struct.pack_into('>I', bootstrap, 0x00, 0x00010000)  # Version 1.0
    
    # Device mode (offset 0x04) 
    struct.pack_into('>I', bootstrap, 0x04, 0x80000000)  # Big endian, UTF8
    
    # MAC address (offset 0x08-0x0D)
    mac_bytes = bytes.fromhex('083af2aa64cc')
    bootstrap[0x08:0x08+6] = mac_bytes
    
    # IP configuration (offset 0x14-0x27)
    ip_parts = device_ip.split('.')
    ip_bytes = bytes([int(p) for p in ip_parts])
    bootstrap[0x24:0x24+4] = ip_bytes  # Current IP
    
    # Device info strings (simplified)
    manufacturer = b"ESP32GenICam\x00"
    model = b"ESP32-CAM-GigE\x00" 
    version = b"1.0.0\x00"
    serial = b"ESP32CAM001\x00"
    user_name = b"ESP32Camera\x00"
    
    # Place strings at their offsets
    bootstrap[0x48:0x48+len(manufacturer)] = manufacturer  # Manufacturer
    bootstrap[0x68:0x68+len(model)] = model  # Model  
    bootstrap[0x88:0x88+len(version)] = version  # Version
    bootstrap[0xd8:0xd8+len(serial)] = serial  # Serial
    bootstrap[0xe8:0xe8+len(user_name)] = user_name  # User name
    
    # XML URL at offset 0x200
    xml_url = b"Local:0x10000;0x2000\x00"
    bootstrap[0x200:0x200+len(xml_url)] = xml_url
    
    return header + bytes(bootstrap)

def test_modified_source_response():
    """Test if Aravis accepts responses from modified source addresses"""
    
    print("Source Address Response Test")
    print("===========================")
    print("Testing if Aravis accepts responses from same interface vs different interface")
    print()
    
    interface_ip = "192.168.213.45"
    esp32_ip = "192.168.213.40"
    
    # Test 1: Response from ESP32 IP (current behavior - should fail)
    print("Test 1: Response from ESP32 IP (should FAIL with Aravis)")
    test_response_from_ip(interface_ip, esp32_ip, "ESP32 IP")
    
    print()
    
    # Test 2: Response from same interface IP (should work)
    print("Test 2: Response from same interface IP (should WORK with Aravis)")
    test_response_from_ip(interface_ip, interface_ip, "Same Interface")
    
    print()
    
    # Test 3: Response from different interface on same machine
    other_interface = "192.168.213.28"
    print("Test 3: Response from different interface on same machine")
    test_response_from_ip(interface_ip, other_interface, "Different Interface")

def test_response_from_ip(sender_ip, responder_ip, test_name):
    """Test discovery response from specific IP address"""
    
    print(f"  {test_name}: {sender_ip} -> response from {responder_ip}")
    
    # Create sender socket (simulates Aravis)
    sender_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sender_sock.bind((sender_ip, 0))
    sender_port = sender_sock.getsockname()[1]
    sender_sock.settimeout(3.0)
    
    # Create responder socket (simulates ESP32 response)
    responder_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # Try to bind responder to specific IP
        responder_sock.bind((responder_ip, 0))
        responder_port = responder_sock.getsockname()[1]
        
        print(f"    Sender: {sender_ip}:{sender_port}")
        print(f"    Responder: {responder_ip}:{responder_port}")
        
        # Send discovery packet
        packet_id = 0x7777
        discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, packet_id)
        
        # Send to broadcast (or specific IP for unicast test)
        # For this test, we send directly to our responder to simulate ESP32 response
        sender_sock.sendto(discovery_packet, (responder_ip, responder_port))
        print(f"    Discovery sent")
        
        # Responder receives and sends response back
        try:
            data, addr = responder_sock.recvfrom(1024)
            print(f"    Discovery received from {addr}")
            
            # Create and send response
            response = create_fake_response(packet_id, responder_ip)
            responder_sock.sendto(response, (sender_ip, sender_port))
            print(f"    Response sent from {responder_ip} to {sender_ip}:{sender_port}")
            
            # Sender waits for response
            try:
                response_data, response_addr = sender_sock.recvfrom(2048)
                print(f"    ✅ Response received from {response_addr[0]}:{response_addr[1]}")
                print(f"    📊 Response size: {len(response_data)} bytes")
                
                # Validate response
                if len(response_data) >= 8:
                    magic, status, cmd, length, resp_id = struct.unpack('>BBHHH', response_data[:8])
                    if cmd == 0x0003 and resp_id == packet_id:
                        print(f"    ✅ Valid GVCP discovery response")
                    else:
                        print(f"    ❌ Invalid GVCP response: cmd=0x{cmd:04x}, id=0x{resp_id:04x}")
                
            except socket.timeout:
                print(f"    ❌ No response received within timeout")
                
        except socket.timeout:
            print(f"    ❌ Responder didn't receive discovery")
        
    except OSError as e:
        print(f"    ❌ Cannot bind to {responder_ip}: {e}")
        if "Cannot assign requested address" in str(e):
            print(f"    (This IP might not be available on this machine)")
    
    finally:
        sender_sock.close()
        responder_sock.close()

def test_aravis_with_intercepted_responses():
    """Test Aravis behavior with intercepted and modified responses"""
    
    print("\n" + "="*60)
    print("ARAVIS RESPONSE INTERCEPTION TEST")  
    print("="*60)
    print("This test requires running Aravis manually...")
    print()
    print("1. Start this test")
    print("2. Run 'arv-test-0.8' in another terminal")
    print("3. See if intercepted responses work")
    print()
    
    # Create socket to intercept and modify responses
    interface_ip = "192.168.213.45"
    
    try:
        # Bind to interface to intercept Aravis discoveries
        intercept_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        intercept_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        intercept_sock.bind((interface_ip, 3956))
        intercept_sock.settimeout(2.0)
        
        print(f"Intercepting on {interface_ip}:3956")
        print("Waiting for Aravis discovery packets...")
        
        start_time = time.time()
        while time.time() - start_time < 30:  # Wait 30 seconds
            try:
                data, addr = intercept_sock.recvfrom(1024)
                
                # Check if it's a discovery packet
                if len(data) >= 8:
                    magic, status, cmd, length, packet_id = struct.unpack('>BBHHH', data[:8])
                    if magic == 0x42 and cmd == 0x0002:  # Discovery
                        print(f"Intercepted discovery from {addr[0]}:{addr[1]}, ID: 0x{packet_id:04x}")
                        
                        # Create response from SAME interface (not ESP32)
                        response = create_fake_response(packet_id, interface_ip)
                        
                        # Send response from same interface
                        response_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        response_sock.bind((interface_ip, 0))
                        response_sock.sendto(response, addr)
                        response_sock.close()
                        
                        print(f"Sent response from {interface_ip} back to {addr[0]}:{addr[1]}")
                        print("✅ This should work with Aravis if source address theory is correct!")
                
            except socket.timeout:
                continue
                
        intercept_sock.close()
        
    except OSError as e:
        print(f"Cannot intercept on {interface_ip}:3956: {e}")
        print("Port might be in use by proxy or other process")

def main():
    print("ESP32 Source Address Response Test")
    print("==================================")
    print("Testing the theory that Aravis needs responses from the same interface")
    print()
    
    # Test basic response behavior
    test_modified_source_response()
    
    # Test with actual Aravis (optional)
    print("\nWould you like to test with actual Aravis? (y/n): ", end="")
    # For automated testing, skip the interactive part
    # response = input().lower()
    # if response == 'y':
    #     test_aravis_with_intercepted_responses()

if __name__ == "__main__":
    main()