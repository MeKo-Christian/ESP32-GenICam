#!/usr/bin/env python3
"""
Packet Comparison Tool - Analyze discovery packets with and without proxy
This will help us understand why the proxy works but direct ESP32 doesn't
"""

import socket
import struct
import threading
import time
import subprocess
from datetime import datetime

class PacketCapture:
    def __init__(self):
        self.captured_packets = []
        self.capturing = False
        
    def capture_packet(self, data, addr, direction, context):
        """Capture a packet with metadata"""
        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        
        # Parse GVCP header if possible
        packet_info = {"raw": True}
        if len(data) >= 8:
            try:
                magic, status, cmd, length, req_id = struct.unpack('>BBHHH', data[:8])
                packet_info = {
                    "magic": f"0x{magic:02x}",
                    "status": f"0x{status:02x}", 
                    "cmd": f"0x{cmd:04x}",
                    "length": length,
                    "id": f"0x{req_id:04x}",
                    "type": "DISCOVERY" if cmd == 0x0002 else "DISCOVERY_ACK" if cmd == 0x0003 else "OTHER"
                }
            except:
                packet_info = {"raw": True}
        
        packet = {
            "timestamp": timestamp,
            "direction": direction,
            "context": context,
            "src_ip": addr[0],
            "src_port": addr[1],
            "size": len(data),
            "packet_info": packet_info,
            "data": data[:32].hex()  # First 32 bytes
        }
        
        self.captured_packets.append(packet)
        print(f"[{timestamp}] {direction} {context}: {addr[0]}:{addr[1]} - {len(data)} bytes - {packet_info.get('type', 'RAW')}")

def test_direct_esp32_discovery():
    """Test direct discovery to ESP32 and capture response source"""
    print("=== TESTING DIRECT ESP32 DISCOVERY ===")
    capture = PacketCapture()
    
    # Create socket to send discovery
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    # Bind to specific interface to simulate Aravis behavior
    interface_ip = "192.168.213.45"
    sock.bind((interface_ip, 0))
    local_port = sock.getsockname()[1]
    
    print(f"Sending discovery from {interface_ip}:{local_port}")
    
    # Send discovery packet
    discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x9999)
    esp32_ip = "192.168.213.40"
    
    sock.sendto(discovery_packet, (esp32_ip, 3956))
    capture.capture_packet(discovery_packet, (esp32_ip, 3956), "TX", "Direct to ESP32")
    
    # Listen for response
    sock.settimeout(3.0)
    try:
        response_data, response_addr = sock.recvfrom(2048)
        capture.capture_packet(response_data, response_addr, "RX", "ESP32 Response")
        print(f"✅ Direct ESP32 response: {response_addr[0]}:{response_addr[1]} -> {interface_ip}:{local_port}")
        
        # Key analysis: What IP did the response come from?
        if response_addr[0] == esp32_ip:
            print(f"📍 Response source: ESP32 IP ({esp32_ip}) - DIFFERENT from sender ({interface_ip})")
        elif response_addr[0] == interface_ip:
            print(f"📍 Response source: Same as sender ({interface_ip}) - SAME interface")
        else:
            print(f"📍 Response source: Unknown ({response_addr[0]})")
            
    except socket.timeout:
        print("❌ No response from ESP32")
    
    sock.close()
    return capture

def test_proxy_discovery():
    """Test discovery through proxy and capture packet flow"""
    print("\n=== TESTING PROXY DISCOVERY ===")
    capture = PacketCapture()
    
    # Start discovery proxy in background
    print("Starting discovery proxy...")
    proxy_proc = subprocess.Popen(['python3', 'scripts/discovery_proxy.py', '192.168.213.40'], 
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Give proxy time to start
    time.sleep(2)
    
    # Monitor for proxy packets
    monitor_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    monitor_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    interface_ip = "192.168.213.45"
    
    try:
        # Create discovery socket like Aravis would
        discovery_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        discovery_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        discovery_sock.bind((interface_ip, 0))
        local_port = discovery_sock.getsockname()[1]
        
        print(f"Sending discovery via proxy from {interface_ip}:{local_port}")
        
        # Send broadcast discovery (proxy will intercept)
        discovery_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x8888)
        broadcast_addr = "192.168.213.255"
        
        discovery_sock.sendto(discovery_packet, (broadcast_addr, 3956))
        capture.capture_packet(discovery_packet, (broadcast_addr, 3956), "TX", "Broadcast via Proxy")
        
        # Listen for response from proxy
        discovery_sock.settimeout(5.0)
        try:
            response_data, response_addr = discovery_sock.recvfrom(2048)
            capture.capture_packet(response_data, response_addr, "RX", "Proxy Response")
            print(f"✅ Proxy response: {response_addr[0]}:{response_addr[1]} -> {interface_ip}:{local_port}")
            
            # Key analysis: What IP did the response come from?
            if response_addr[0] == "192.168.213.40":  # ESP32 IP
                print(f"📍 Response source: ESP32 IP (192.168.213.40) - DIFFERENT from sender ({interface_ip})")
            elif response_addr[0] == interface_ip:
                print(f"📍 Response source: Same as sender ({interface_ip}) - SAME interface")
            else:
                print(f"📍 Response source: Other IP ({response_addr[0]}) - proxy host machine")
                
        except socket.timeout:
            print("❌ No response from proxy")
        
        discovery_sock.close()
        
    except Exception as e:
        print(f"Error during proxy test: {e}")
    
    # Stop proxy
    proxy_proc.terminate()
    try:
        proxy_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proxy_proc.kill()
    
    return capture

def test_aravis_with_monitoring():
    """Run Aravis discovery while monitoring packet flow"""
    print("\n=== TESTING ARAVIS DISCOVERY (No Proxy) ===")
    capture = PacketCapture()
    
    # Create monitoring socket
    try:
        monitor_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        monitor_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        monitor_sock.bind(('0.0.0.0', 3956))
        monitor_sock.settimeout(1.0)
        
        print("Monitoring GVCP port 3956...")
        print("Run 'arv-test-0.10' in another terminal NOW")
        
        # Monitor for 15 seconds
        start_time = time.time()
        while time.time() - start_time < 15:
            try:
                data, addr = monitor_sock.recvfrom(2048)
                capture.capture_packet(data, addr, "RX", "Aravis Traffic")
            except socket.timeout:
                continue
                
        monitor_sock.close()
        
    except OSError as e:
        print(f"Cannot monitor port 3956: {e}")
        print("Port might be in use by proxy or other process")
    
    return capture

def analyze_captures(direct_capture, proxy_capture):
    """Analyze and compare captured packets"""
    print("\n" + "="*60)
    print("PACKET ANALYSIS COMPARISON")
    print("="*60)
    
    print("\n🔍 DIRECT ESP32 PACKETS:")
    for packet in direct_capture.captured_packets:
        pinfo = packet['packet_info']
        print(f"  {packet['timestamp']} {packet['direction']} {packet['context']}")
        print(f"    {packet['src_ip']}:{packet['src_port']} - {packet['size']} bytes - {pinfo.get('type', 'RAW')}")
        if 'id' in pinfo:
            print(f"    ID: {pinfo['id']}, CMD: {pinfo['cmd']}")
    
    print("\n🔍 PROXY PACKETS:")
    for packet in proxy_capture.captured_packets:
        pinfo = packet['packet_info']
        print(f"  {packet['timestamp']} {packet['direction']} {packet['context']}")
        print(f"    {packet['src_ip']}:{packet['src_port']} - {packet['size']} bytes - {pinfo.get('type', 'RAW')}")
        if 'id' in pinfo:
            print(f"    ID: {pinfo['id']}, CMD: {pinfo['cmd']}")
    
    # Key analysis
    print("\n📊 KEY DIFFERENCES:")
    
    # Find response packets
    direct_responses = [p for p in direct_capture.captured_packets if p['direction'] == 'RX']
    proxy_responses = [p for p in proxy_capture.captured_packets if p['direction'] == 'RX']
    
    if direct_responses:
        dr = direct_responses[0]
        print(f"  Direct ESP32 response source: {dr['src_ip']}:{dr['src_port']}")
    
    if proxy_responses:
        pr = proxy_responses[0]
        print(f"  Proxy response source: {pr['src_ip']}:{pr['src_port']}")
    
    if direct_responses and proxy_responses:
        if direct_responses[0]['src_ip'] != proxy_responses[0]['src_ip']:
            print("  ⚠️  CRITICAL: Response source IPs are DIFFERENT!")
            print("     This confirms the source address theory!")
        else:
            print("  ✅ Response source IPs are the same")

def main():
    print("ESP32 Discovery Packet Comparison Tool")
    print("=====================================")
    print("This tool compares packet flows to understand why proxy works but direct ESP32 doesn't")
    print()
    
    # Test 1: Direct ESP32
    direct_capture = test_direct_esp32_discovery()
    
    # Small delay between tests
    time.sleep(2)
    
    # Test 2: Proxy operation  
    proxy_capture = test_proxy_discovery()
    
    # Analysis
    analyze_captures(direct_capture, proxy_capture)
    
    print("\n" + "="*60)
    print("CONCLUSION")
    print("="*60)
    print("If response source IPs are different, this confirms that Aravis")
    print("expects responses from the same interface/IP that sent the discovery.")
    print("The proxy works by 'spoofing' the source address to match expectations.")

if __name__ == "__main__":
    main()