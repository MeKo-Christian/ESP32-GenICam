#!/usr/bin/env python3
"""
Detailed analysis of ESP32-CAM discovery response to understand Aravis rejection

This tool now includes:
- Discovery response format validation
- Bootstrap register analysis  
- XML URL validation
- GenICam XML content fetching and validation
- Comprehensive issue detection for Aravis compatibility
"""

import socket
import struct
import sys
import xml.etree.ElementTree as ET
from xml.dom import minidom

def test_xml_content(ip_address, xml_address, xml_size):
    """Test fetching and validating XML content from ESP32-CAM"""
    
    print(f"  Fetching XML from address 0x{xml_address:x}, size {xml_size} bytes...")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    try:
        # Create GVCP read memory command for XML
        read_memory_packet = struct.pack('>BBHHHII', 
                                       0x42, 0x00,           # packet type, flags
                                       0x0084, 8,            # read memory command, size
                                       0x5678,               # packet ID
                                       xml_address,          # address
                                       xml_size)             # size
        
        sock.sendto(read_memory_packet, (ip_address, 3956))
        
        xml_response, addr = sock.recvfrom(xml_size + 1024)
        if len(xml_response) < 12:
            print("  ❌ XML response too short")
            return False
            
        # Parse GVCP response header
        packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', xml_response[:8])
        
        if packet_type != 0x00 or command != 0x0085:
            print(f"  ❌ Invalid XML response: type=0x{packet_type:02x}, cmd=0x{command:04x}")
            return False
            
        # Extract XML content (skip 8-byte GVCP header + 4-byte address)
        if len(xml_response) > 12:
            xml_content = xml_response[12:].decode('utf-8', errors='ignore')
            
            # Remove null terminators and whitespace
            xml_content = xml_content.rstrip('\x00').strip()
            
            print(f"  ✅ Received {len(xml_content)} bytes of XML content")
            
            # Debug: Show first few lines to diagnose parsing issues
            lines = xml_content.split('\n')[:8]  # Show more lines to see namespace
            print(f"  📝 First few lines:")
            for i, line in enumerate(lines, 1):
                print(f"     {i}: {repr(line)}")
            
            # Test XML parsing
            try:
                root = ET.fromstring(xml_content)
                print("  ✅ XML is well-formed")
                
                # Check for required GenICam elements
                namespace = root.tag.split('}')[0] + '}' if '}' in root.tag else ''
                
                if root.tag.endswith('RegisterDescription'):
                    print("  ✅ Root element is RegisterDescription")
                else:
                    print(f"  ❌ Root element is '{root.tag}', expected RegisterDescription")
                    return False
                
                # Check for required attributes
                required_attrs = ['ModelName', 'VendorName', 'MajorVersion', 'MinorVersion']
                for attr in required_attrs:
                    if attr in root.attrib:
                        print(f"  ✅ {attr}: '{root.attrib[attr]}'")
                    else:
                        print(f"  ❌ Missing required attribute: {attr}")
                        return False
                
                # Check for namespace - handle different xmlns formats
                xmlns_found = False
                xmlns_value = None
                
                # Check default namespace
                if 'xmlns' in root.attrib:
                    xmlns_value = root.attrib['xmlns']
                    if 'genicam.org' in xmlns_value:
                        xmlns_found = True
                
                # Also check for xmlns in the raw XML content (sometimes attrib parsing misses it)
                if not xmlns_found and 'xmlns="http://www.genicam.org' in xml_content:
                    xmlns_found = True
                    # Extract the namespace value from raw content
                    import re
                    match = re.search(r'xmlns="([^"]*genicam[^"]*)"', xml_content)
                    if match:
                        xmlns_value = match.group(1)
                
                if xmlns_found:
                    print(f"  ✅ GenICam namespace present: {xmlns_value}")
                else:
                    print(f"  ❌ Missing or invalid GenICam namespace")
                    print(f"     Found attributes: {list(root.attrib.keys())}")
                    if xmlns_value:
                        print(f"     xmlns value: '{xmlns_value}'")
                    
                    # Debug: Check for namespace in raw content
                    if 'xmlns=' in xml_content:
                        print(f"     ⚠️  Raw XML contains xmlns but parser didn't detect it")
                        # Show the RegisterDescription opening tag
                        reg_desc_start = xml_content.find('<RegisterDescription')
                        reg_desc_end = xml_content.find('>', reg_desc_start) + 1
                        if reg_desc_start >= 0 and reg_desc_end > reg_desc_start:
                            tag_content = xml_content[reg_desc_start:reg_desc_end]
                            print(f"     RegisterDescription tag: {repr(tag_content[:200])}...")
                    
                    return False
                
                # Count important elements
                categories = root.findall('.//*[@Name][@NameSpace="Standard"]')
                category_count = len([elem for elem in categories if elem.tag.endswith('Category')])
                
                features = root.findall('.//*[@Name][@NameSpace="Standard"]')
                feature_count = len([elem for elem in features if not elem.tag.endswith('Category')])
                
                print(f"  📊 Found {category_count} categories, {feature_count} features")
                
                # Look for critical features
                critical_features = ['DeviceVendorName', 'DeviceModelName', 'Width', 'Height', 'PixelFormat']
                for feature in critical_features:
                    if root.find(f".//*[@Name='{feature}']") is not None:
                        print(f"  ✅ Critical feature '{feature}' present")
                    else:
                        print(f"  ⚠️  Critical feature '{feature}' missing")
                
                return True
                
            except ET.ParseError as e:
                print(f"  ❌ XML parsing error: {e}")
                
                # Check if this might be due to truncated content
                if len(xml_content) < xml_size:
                    print(f"  💡 XML might be truncated: got {len(xml_content)} bytes, expected {xml_size}")
                    print(f"     Try increasing the fetch size or check GVCP read implementation")
                
                # Check for common XML issues
                if not xml_content.startswith('<?xml'):
                    print(f"  💡 XML doesn't start with declaration, begins with: {repr(xml_content[:50])}")
                if not xml_content.strip().endswith('>'):
                    print(f"  💡 XML doesn't end properly, ends with: {repr(xml_content[-50:])}")
                
                return False
            except Exception as e:
                print(f"  ❌ XML validation error: {e}")
                return False
        else:
            print("  ❌ XML response contains no content")
            return False
            
    except socket.timeout:
        print("  ❌ Timeout fetching XML")
        return False
    except Exception as e:
        print(f"  ❌ Error fetching XML: {e}")
        return False
    finally:
        sock.close()

def analyze_discovery_response(ip_address):
    """Analyze the discovery response in detail"""
    
    print(f"Analyzing ESP32-CAM discovery response from {ip_address}")
    print("=" * 60)
    
    # Create discovery request packet
    discovery_packet = struct.pack('>BBHHH', 0x42, 0x00, 0x0002, 0x0000, 0x1234)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    
    try:
        # Send discovery request
        sock.sendto(discovery_packet, (ip_address, 3956))
        
        # Receive response
        data, addr = sock.recvfrom(1024)
        
        print(f"Received {len(data)} bytes from {addr}")
        print(f"Raw response: {data.hex()}")
        print()
        
        if len(data) < 8:
            print("❌ Response too short for GVCP header")
            return False
            
        # Parse GVCP header
        packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
        
        print("GVCP Header Analysis:")
        print(f"  Packet Type: 0x{packet_type:02x} ({'ACK' if packet_type == 0x00 else 'UNKNOWN'})")
        print(f"  Packet Flags: 0x{packet_flags:02x}")
        print(f"  Command: 0x{command:04x} ({'DISCOVERY_ACK' if command == 0x0003 else 'UNKNOWN'})")
        print(f"  Size: {size} bytes")
        print(f"  Packet ID: 0x{packet_id:04x}")
        print()
        
        if command != 0x0003:
            print("❌ Invalid command in response")
            return False
            
        if len(data) < 8 + size:
            print("❌ Response shorter than declared size")
            return False
            
        # Parse bootstrap data
        bootstrap_data = data[8:]
        
        print("Bootstrap Register Analysis:")
        print("=" * 30)
        
        # Initialize variables
        version = device_mode = 0
        issues = []
        
        # Check if we have enough data for standard bootstrap registers
        if len(bootstrap_data) >= 0x10:
            # Parse key bootstrap registers
            version = struct.unpack('>I', bootstrap_data[0x00:0x04])[0]
            device_mode = struct.unpack('>I', bootstrap_data[0x04:0x08])[0]
            mac_high = struct.unpack('>I', bootstrap_data[0x08:0x0c])[0]
            mac_low = struct.unpack('>I', bootstrap_data[0x0c:0x10])[0]
            
            print(f"Version (0x00): 0x{version:08x} ({version >> 16}.{version & 0xFFFF})")
            print(f"Device Mode (0x04): 0x{device_mode:08x}")
            
            # MAC address
            mac_bytes = [(mac_high >> 8) & 0xFF, mac_high & 0xFF,
                        (mac_low >> 24) & 0xFF, (mac_low >> 16) & 0xFF,
                        (mac_low >> 8) & 0xFF, mac_low & 0xFF]
            mac_str = ':'.join(f'{b:02x}' for b in mac_bytes)
            print(f"MAC Address (0x08-0x0f): {mac_str}")
            
            # Check additional registers
            if len(bootstrap_data) >= 0x28:
                subnet_mask = struct.unpack('>I', bootstrap_data[0x14:0x18])[0]
                gateway = struct.unpack('>I', bootstrap_data[0x18:0x1c])[0]
                ip_config = struct.unpack('>I', bootstrap_data[0x20:0x24])[0]
                current_ip = struct.unpack('<I', bootstrap_data[0x24:0x28])[0]  # Little endian for IP
                
                print(f"Subnet Mask (0x14): {socket.inet_ntoa(struct.pack('<I', subnet_mask))}")
                print(f"Gateway (0x18): {socket.inet_ntoa(struct.pack('<I', gateway))}")
                print(f"IP Config (0x20): 0x{ip_config:08x}")
                print(f"Current IP (0x24): {socket.inet_ntoa(struct.pack('<I', current_ip))}")
            
            # Device strings
            manufacturer = bootstrap_data[0x48:0x68].decode('utf-8', errors='ignore').rstrip('\x00')
            model = bootstrap_data[0x68:0x88].decode('utf-8', errors='ignore').rstrip('\x00')
            device_version = bootstrap_data[0x88:0xa8].decode('utf-8', errors='ignore').rstrip('\x00')
            
            print(f"Manufacturer (0x48): '{manufacturer}'")
            print(f"Model (0x68): '{model}'")  
            print(f"Device Version (0x88): '{device_version}'")
            
            # Check for serial number and user name
            if len(bootstrap_data) >= 0xf8:
                serial = bootstrap_data[0xd8:0xe8].decode('utf-8', errors='ignore').rstrip('\x00')
                user_name = bootstrap_data[0xe8:0xf8].decode('utf-8', errors='ignore').rstrip('\x00')
                
                print(f"Serial Number (0xd8): '{serial}'")
                print(f"User Name (0xe8): '{user_name}'")
            
            # XML URL
            if len(bootstrap_data) >= 0x300:
                xml_url = bootstrap_data[0x200:0x300].decode('utf-8', errors='ignore').rstrip('\x00')
                print(f"XML URL (0x200): '{xml_url}'")
                
                # Validate XML URL format
                if xml_url.startswith('Local:'):
                    parts = xml_url.split(';')
                    if len(parts) >= 2:
                        try:
                            address = int(parts[0].split(':')[1], 16)
                            size = int(parts[1], 16)
                            print(f"  ✅ XML URL format valid: address=0x{address:x}, size=0x{size:x}")
                        except ValueError:
                            print(f"  ❌ XML URL format invalid: cannot parse address/size")
                    else:
                        print(f"  ❌ XML URL format invalid: missing size parameter")
                else:
                    print(f"  ❌ XML URL format invalid: should start with 'Local:'")
        
        # Test XML fetching and validation
        print()
        print("XML Content Analysis:")
        print("=" * 30)
        
        if len(bootstrap_data) >= 0x300:
            xml_url = bootstrap_data[0x200:0x300].decode('utf-8', errors='ignore').rstrip('\x00')
            if xml_url.startswith('Local:'):
                parts = xml_url.split(';')
                if len(parts) >= 2:
                    try:
                        xml_address = int(parts[0].split(':')[1], 16)
                        xml_size = int(parts[1], 16)
                        
                        # Try to fetch XML content
                        xml_valid = test_xml_content(ip_address, xml_address, min(xml_size, 8192))
                        if not xml_valid:
                            issues.append("XML content validation failed")
                    except ValueError:
                        issues.append("Cannot parse XML URL parameters")
                else:
                    issues.append("XML URL missing required parameters")
            else:
                issues.append("XML URL format is invalid")
        else:
            issues.append("Discovery response too small to contain XML URL")
        
        print()
        print("Potential Issues for Aravis:")
        print("=" * 30)
        
        # Check for common validation issues
        if device_mode != 0x80000000:
            issues.append(f"Device mode 0x{device_mode:08x} might not indicate BigEndian+CharacterSet correctly")
            
        if version == 0:
            issues.append("Version register is zero")
            
        if not manufacturer or not model:
            issues.append("Missing manufacturer or model name")
            
        # Check string encoding (should be UTF-8, null-terminated)
        try:
            bootstrap_data[0x48:0x68].decode('utf-8')
            bootstrap_data[0x68:0x88].decode('utf-8')
        except UnicodeDecodeError:
            issues.append("Device strings contain invalid UTF-8")
            
        if not issues:
            print("✅ No obvious format issues detected")
            print("   The issue might be:")
            print("   1. Aravis expects additional validation beyond bootstrap registers")
            print("   2. XML content/format issues when Aravis tries to fetch it")
            print("   3. Specific register values that don't match Aravis expectations")
            print("   4. Network/timing issues during Aravis validation")
        else:
            for issue in issues:
                print(f"⚠️  {issue}")
                
        return True
        
    except socket.timeout:
        print("❌ Timeout waiting for discovery response")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 analyze_discovery_response.py <ESP32_IP>")
        sys.exit(1)
        
    ip = sys.argv[1]
    success = analyze_discovery_response(ip)
    sys.exit(0 if success else 1)