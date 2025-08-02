#!/usr/bin/env python3
"""
Test XML fetching from ESP32-CAM to verify GenICam XML is accessible
"""

import socket
import struct
import sys

def test_xml_fetch(ip_address):
    """Test fetching GenICam XML from ESP32-CAM"""
    
    print(f"Testing XML fetch from ESP32-CAM at {ip_address}")
    print("=" * 50)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    try:
        # First, get the XML URL from discovery
        print("1. Getting XML URL from discovery response...")
        discovery_packet = struct.pack('>BBHHH', 0x42, 0x00, 0x0002, 0x0000, 0x1234)
        sock.sendto(discovery_packet, (ip_address, 3956))
        
        data, addr = sock.recvfrom(1024)
        if len(data) < 8 + 0x300:
            print("❌ Discovery response too small")
            return False
            
        bootstrap_data = data[8:]
        xml_url = bootstrap_data[0x200:0x300].decode('utf-8', errors='ignore').rstrip('\x00')
        print(f"   XML URL: {xml_url}")
        
        # Parse XML URL
        if not xml_url.startswith('Local:'):
            print("❌ Invalid XML URL format")
            return False
            
        parts = xml_url.split(';')
        if len(parts) < 2:
            print("❌ XML URL missing size parameter")
            return False
            
        try:
            xml_address = int(parts[0].split(':')[1], 16)
            xml_size = int(parts[1], 16)
            print(f"   XML Address: 0x{xml_address:x}")
            print(f"   XML Size: 0x{xml_size:x} bytes")
        except ValueError:
            print("❌ Cannot parse XML address/size")
            return False
        
        # Now try to read the XML
        print("\n2. Fetching XML content...")
        read_memory_packet = struct.pack('>BBHHHII', 
                                       0x42, 0x00,           # packet type, flags
                                       0x0084, 8,            # read memory command, size
                                       0x5678,               # packet ID
                                       xml_address,          # address
                                       min(xml_size, 1000)) # size (limit for test)
        
        sock.sendto(read_memory_packet, (ip_address, 3956))
        
        xml_response, addr = sock.recvfrom(2048)
        if len(xml_response) < 8:
            print("❌ No XML response received")
            return False
            
        # Parse XML response
        packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', xml_response[:8])
        
        if packet_type != 0x00 or command != 0x0085:
            print(f"❌ Invalid XML response: type=0x{packet_type:02x}, cmd=0x{command:04x}")
            return False
            
        print(f"   ✅ Received XML response: {len(xml_response)} bytes")
        
        # Extract XML content (skip 8-byte GVCP header + 4-byte address)
        if len(xml_response) > 12:
            xml_content = xml_response[12:].decode('utf-8', errors='ignore')
            print(f"   XML content preview:")
            print(f"   {xml_content[:200]}...")
            
            # Basic XML validation
            if '<RegisterDescription' in xml_content and 'xmlns=' in xml_content:
                print("   ✅ XML content appears valid")
                return True
            else:
                print("   ❌ XML content doesn't look like GenICam XML")
                return False
        else:
            print("   ❌ XML response too small")
            return False
            
    except socket.timeout:
        print("❌ Timeout during XML fetch")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 test_xml_fetch.py <ESP32_IP>")
        sys.exit(1)
        
    ip = sys.argv[1]
    success = test_xml_fetch(ip)
    
    if success:
        print("\n✅ XML fetch test passed!")
        print("   The issue with Aravis discovery might be:")
        print("   1. Aravis has additional validation requirements")
        print("   2. Timing or network issues during Aravis validation")
        print("   3. Aravis expects specific XML schema compliance")
    else:
        print("\n❌ XML fetch test failed!")
        print("   This is likely why Aravis cannot discover the camera")
        
    sys.exit(0 if success else 1)