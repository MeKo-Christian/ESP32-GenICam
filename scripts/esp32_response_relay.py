#!/usr/bin/env python3
"""
ESP32 Response Relay - Lightweight alternative to full discovery proxy
This captures ESP32 responses and relays them from the correct source interface
"""

import socket
import struct
import threading
import time
import signal
import sys
from typing import Dict, Tuple

class ESP32ResponseRelay:
    def __init__(self, esp32_ip: str, debug: bool = False):
        self.esp32_ip = esp32_ip
        self.debug = debug
        self.running = False
        self.interfaces = self.get_interfaces()
        self.pending_discoveries: Dict[int, Tuple[str, int, float]] = {}  # packet_id -> (interface, port, timestamp)
        
    def get_interfaces(self):
        """Get available network interfaces"""
        interfaces = []
        # Try to detect interfaces that can reach ESP32
        import subprocess
        result = subprocess.run(['ip', 'route', 'get', self.esp32_ip], capture_output=True, text=True)
        if result.returncode == 0:
            # Extract source interface from route
            for line in result.stdout.split('\n'):
                if 'src' in line:
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if part == 'src' and i + 1 < len(parts):
                            interfaces.append(parts[i + 1])
        
        # Fallback interfaces
        fallback_interfaces = ['192.168.213.45', '192.168.213.28']
        for iface in fallback_interfaces:
            if iface not in interfaces:
                interfaces.append(iface)
                
        return interfaces
    
    def log(self, message: str, level: str = "INFO"):
        """Log message with timestamp"""
        if self.debug or level in ["INFO", "ERROR"]:
            timestamp = time.strftime("%H:%M:%S")
            print(f"[{timestamp}] {level}: {message}")
    
    def is_discovery_packet(self, data: bytes) -> bool:
        """Check if packet is GVCP discovery"""
        if len(data) < 8:
            return False
        try:
            magic, status, cmd, length, packet_id = struct.unpack('>BBHHH', data[:8])
            return magic == 0x42 and cmd == 0x0002
        except:
            return False
    
    def is_discovery_response(self, data: bytes) -> bool:
        """Check if packet is GVCP discovery response"""
        if len(data) < 8:
            return False
        try:
            magic, status, cmd, length, packet_id = struct.unpack('>BBHHH', data[:8])
            return magic == 0x00 and cmd == 0x0003
        except:
            return False
    
    def get_packet_id(self, data: bytes) -> int:
        """Extract packet ID from GVCP packet"""
        if len(data) < 8:
            return 0
        try:
            return struct.unpack('>BBHHH', data[:8])[4]
        except:
            return 0
    
    def monitor_interface_discoveries(self, interface_ip: str):
        """Monitor discovery packets on specific interface"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(1.0)
            
            # Bind to interface
            sock.bind((interface_ip, 3956))
            self.log(f"Monitoring discoveries on {interface_ip}:3956")
            
            while self.running:
                try:
                    data, addr = sock.recvfrom(1024)
                    
                    if self.is_discovery_packet(data):
                        packet_id = self.get_packet_id(data)
                        self.log(f"Discovery from {addr[0]}:{addr[1]} on {interface_ip}, ID=0x{packet_id:04x}", "DEBUG")
                        
                        # Store the interface and port for response relay
                        self.pending_discoveries[packet_id] = (interface_ip, addr[1], time.time())
                        
                        # Forward discovery to ESP32
                        esp32_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        esp32_sock.sendto(data, (self.esp32_ip, 3956))
                        esp32_sock.close()
                        
                        self.log(f"Forwarded discovery to ESP32: {self.esp32_ip}")
                
                except socket.timeout:
                    self.cleanup_expired_discoveries()
                    continue
                except Exception as e:
                    if self.running:
                        self.log(f"Error on {interface_ip}: {e}", "ERROR")
            
        except Exception as e:
            self.log(f"Failed to monitor {interface_ip}: {e}", "ERROR")
        finally:
            if 'sock' in locals():
                sock.close()
    
    def monitor_esp32_responses(self):
        """Monitor responses from ESP32 and relay them"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(('0.0.0.0', 0))  # Any available port
            sock.settimeout(1.0)
            
            # Send a test packet to establish connection tracking
            test_packet = struct.pack('>BBHHH', 0x42, 0x01, 0x0002, 0x0000, 0x0000)
            sock.sendto(test_packet, (self.esp32_ip, 3956))
            
            self.log(f"Monitoring ESP32 responses from {self.esp32_ip}")
            
            while self.running:
                try:
                    data, addr = sock.recvfrom(2048)
                    
                    if addr[0] == self.esp32_ip and self.is_discovery_response(data):
                        packet_id = self.get_packet_id(data)
                        self.log(f"ESP32 response received, ID=0x{packet_id:04x}", "DEBUG")
                        
                        # Find corresponding discovery request
                        if packet_id in self.pending_discoveries:
                            interface_ip, requester_port, timestamp = self.pending_discoveries[packet_id]
                            
                            # Relay response from correct interface
                            relay_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            relay_sock.bind((interface_ip, 0))
                            relay_sock.sendto(data, (interface_ip, requester_port))
                            relay_sock.close()
                            
                            self.log(f"Relayed response from {interface_ip} to {interface_ip}:{requester_port}")
                            del self.pending_discoveries[packet_id]
                        else:
                            self.log(f"No pending discovery for packet ID 0x{packet_id:04x}", "DEBUG")
                
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        self.log(f"Error monitoring ESP32: {e}", "ERROR")
            
        except Exception as e:
            self.log(f"Failed to monitor ESP32 responses: {e}", "ERROR")
        finally:
            if 'sock' in locals():
                sock.close()
    
    def cleanup_expired_discoveries(self):
        """Clean up expired discovery requests"""
        current_time = time.time()
        expired = [pid for pid, (_, _, ts) in self.pending_discoveries.items() 
                  if current_time - ts > 5.0]
        for pid in expired:
            del self.pending_discoveries[pid]
    
    def start(self):
        """Start the response relay"""
        self.log(f"Starting ESP32 Response Relay for {self.esp32_ip}")
        self.log(f"Monitoring interfaces: {self.interfaces}")
        
        self.running = True
        threads = []
        
        # Start interface monitors
        for interface_ip in self.interfaces:
            thread = threading.Thread(
                target=self.monitor_interface_discoveries,
                args=(interface_ip,),
                daemon=True
            )
            thread.start()
            threads.append(thread)
        
        # Start ESP32 response monitor
        esp32_thread = threading.Thread(
            target=self.monitor_esp32_responses,
            daemon=True
        )
        esp32_thread.start()
        threads.append(esp32_thread)
        
        self.log("Response relay started - Press Ctrl+C to stop")
        
        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            self.log("Shutdown requested")
        finally:
            self.stop()
    
    def stop(self):
        """Stop the response relay"""
        self.log("Stopping response relay...")
        self.running = False

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="ESP32 Response Relay - Lightweight discovery fix")
    parser.add_argument('esp32_ip', help='ESP32-CAM IP address')
    parser.add_argument('--debug', action='store_true', help='Enable debug logging')
    
    args = parser.parse_args()
    
    relay = ESP32ResponseRelay(args.esp32_ip, args.debug)
    
    def signal_handler(signum, frame):
        relay.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        relay.start()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()