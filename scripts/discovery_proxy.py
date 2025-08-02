#!/usr/bin/env python3
"""
GigE Vision Discovery Proxy Service

This service solves the ESP32 broadcast reception issue by:
1. Listening for broadcast discovery packets from Aravis/other GigE Vision software
2. Forwarding them as unicast packets to ESP32-CAM devices
3. Forwarding responses back to the original requesters

This enables Aravis to discover ESP32-CAM devices despite the ESP32's broadcast reception limitations.
"""

import socket
import struct
import threading
import time
import sys
import signal
import argparse
from typing import Dict, List, Tuple, Optional

# GVCP Protocol Constants
GVCP_PORT = 3956
GVCP_PACKET_TYPE_CMD = 0x42
GVCP_PACKET_TYPE_ACK = 0x00
GVCP_CMD_DISCOVERY = 0x0002
GVCP_ACK_DISCOVERY = 0x0003

class DiscoveryProxy:
    def __init__(self, esp32_devices: List[str], listen_interfaces: List[str] = None, debug: bool = False):
        """
        Initialize the discovery proxy.
        
        Args:
            esp32_devices: List of ESP32-CAM IP addresses to proxy for
            listen_interfaces: List of interface IPs to listen on (None = all)
            debug: Enable debug logging
        """
        self.esp32_devices = esp32_devices
        self.listen_interfaces = listen_interfaces or self.get_all_interfaces()
        self.debug = debug
        self.running = False
        self.sockets = []
        self.stats = {
            'broadcasts_received': 0,
            'unicast_forwards': 0,
            'responses_received': 0,
            'responses_forwarded': 0,
            'errors': 0
        }
        
        # Track pending requests: packet_id -> (requester_ip, requester_port, timestamp)
        self.pending_requests: Dict[int, Tuple[str, int, float]] = {}
        self.request_timeout = 5.0  # seconds
        
    def get_all_interfaces(self) -> List[str]:
        """Get all non-loopback interface IPs."""
        interfaces = []
        try:
            # Create a socket to enumerate interfaces
            import netifaces
            for interface in netifaces.interfaces():
                addrs = netifaces.ifaddresses(interface)
                if netifaces.AF_INET in addrs:
                    for addr_info in addrs[netifaces.AF_INET]:
                        ip = addr_info.get('addr')
                        if ip and not ip.startswith('127.'):
                            interfaces.append(ip)
        except ImportError:
            # Fallback method if netifaces not available
            interfaces = ['0.0.0.0']  # Listen on all interfaces
        
        return interfaces
    
    def log(self, message: str, level: str = "INFO"):
        """Log a message with timestamp."""
        timestamp = time.strftime("%H:%M:%S")
        print(f"[{timestamp}] {level}: {message}")
    
    def debug_log(self, message: str):
        """Log a debug message if debug mode is enabled."""
        if self.debug:
            self.log(message, "DEBUG")
    
    def is_discovery_packet(self, data: bytes) -> bool:
        """Check if a packet is a GVCP discovery command."""
        if len(data) < 8:
            return False
        
        try:
            packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
            return (packet_type == GVCP_PACKET_TYPE_CMD and 
                   command == GVCP_CMD_DISCOVERY and 
                   size == 0)
        except struct.error:
            return False
    
    def is_discovery_response(self, data: bytes) -> bool:
        """Check if a packet is a GVCP discovery response."""
        if len(data) < 8:
            return False
        
        try:
            packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
            return (packet_type == GVCP_PACKET_TYPE_ACK and 
                   command == GVCP_ACK_DISCOVERY)
        except struct.error:
            return False
    
    def get_packet_id(self, data: bytes) -> Optional[int]:
        """Extract packet ID from GVCP packet."""
        if len(data) < 8:
            return None
        
        try:
            packet_type, packet_flags, command, size, packet_id = struct.unpack('>BBHHH', data[:8])
            return packet_id
        except struct.error:
            return None
    
    def forward_to_esp32(self, discovery_packet: bytes, requester_addr: Tuple[str, int]):
        """Forward discovery packet to ESP32 devices as unicast."""
        requester_ip, requester_port = requester_addr
        packet_id = self.get_packet_id(discovery_packet)
        
        if packet_id is None:
            self.log("Invalid packet ID, cannot track request", "ERROR")
            self.stats['errors'] += 1
            return
        
        # Store requester info for response forwarding
        self.pending_requests[packet_id] = (requester_ip, requester_port, time.time())
        
        self.debug_log(f"Forwarding discovery (ID: 0x{packet_id:04x}) from {requester_ip}:{requester_port} to ESP32 devices")
        
        # Forward to all ESP32 devices
        for esp32_ip in self.esp32_devices:
            try:
                # Create socket for forwarding
                forward_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                forward_sock.settimeout(1.0)
                
                # Send to ESP32
                forward_sock.sendto(discovery_packet, (esp32_ip, GVCP_PORT))
                self.stats['unicast_forwards'] += 1
                
                self.debug_log(f"Forwarded to {esp32_ip}:3956")
                
                # Listen for response (non-blocking)
                try:
                    response_data, resp_addr = forward_sock.recvfrom(4096)
                    if self.is_discovery_response(response_data):
                        self.handle_esp32_response(response_data, packet_id)
                        self.stats['responses_received'] += 1
                    else:
                        self.debug_log(f"Received non-discovery response from {esp32_ip}")
                        
                except socket.timeout:
                    self.debug_log(f"No response from {esp32_ip} within timeout")
                
                forward_sock.close()
                
            except Exception as e:
                self.log(f"Error forwarding to {esp32_ip}: {e}", "ERROR")
                self.stats['errors'] += 1
    
    def handle_esp32_response(self, response_data: bytes, packet_id: int):
        """Forward ESP32 response back to original requester."""
        if packet_id not in self.pending_requests:
            self.debug_log(f"Received response for unknown packet ID: 0x{packet_id:04x}")
            return
        
        requester_ip, requester_port, request_time = self.pending_requests[packet_id]
        
        # Check if request is still valid (not timed out)
        if time.time() - request_time > self.request_timeout:
            self.debug_log(f"Response for packet ID 0x{packet_id:04x} timed out")
            del self.pending_requests[packet_id]
            return
        
        try:
            # Send response back to requester
            response_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            response_sock.sendto(response_data, (requester_ip, requester_port))
            response_sock.close()
            
            self.stats['responses_forwarded'] += 1
            self.debug_log(f"Forwarded response (ID: 0x{packet_id:04x}) back to {requester_ip}:{requester_port}")
            
            # Clean up request tracking
            del self.pending_requests[packet_id]
            
        except Exception as e:
            self.log(f"Error forwarding response to {requester_ip}:{requester_port}: {e}", "ERROR")
            self.stats['errors'] += 1
    
    def cleanup_expired_requests(self):
        """Remove expired pending requests."""
        current_time = time.time()
        expired_ids = [
            packet_id for packet_id, (_, _, timestamp) in self.pending_requests.items()
            if current_time - timestamp > self.request_timeout
        ]
        
        for packet_id in expired_ids:
            del self.pending_requests[packet_id]
            self.debug_log(f"Cleaned up expired request: 0x{packet_id:04x}")
    
    def listen_on_interface(self, interface_ip: str):
        """Listen for broadcast discovery packets on a specific interface."""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(1.0)  # 1 second timeout for responsiveness
            
            # Bind to the interface
            bind_addr = (interface_ip, GVCP_PORT)
            sock.bind(bind_addr)
            
            self.sockets.append(sock)
            self.log(f"Listening on {interface_ip}:3956")
            
            while self.running:
                try:
                    data, addr = sock.recvfrom(1024)
                    
                    if self.is_discovery_packet(data):
                        self.stats['broadcasts_received'] += 1
                        self.debug_log(f"Received discovery broadcast from {addr[0]}:{addr[1]} on interface {interface_ip}")
                        
                        # Forward to ESP32 devices
                        self.forward_to_esp32(data, addr)
                    else:
                        self.debug_log(f"Received non-discovery packet from {addr[0]}:{addr[1]}")
                
                except socket.timeout:
                    # Timeout is normal, use it for housekeeping
                    self.cleanup_expired_requests()
                    continue
                except Exception as e:
                    if self.running:  # Only log errors if we're supposed to be running
                        self.log(f"Error on interface {interface_ip}: {e}", "ERROR")
                        self.stats['errors'] += 1
            
        except Exception as e:
            self.log(f"Failed to listen on interface {interface_ip}: {e}", "ERROR")
            self.stats['errors'] += 1
        finally:
            if 'sock' in locals():
                sock.close()
    
    def start(self):
        """Start the discovery proxy service."""
        self.log("Starting GigE Vision Discovery Proxy")
        self.log(f"ESP32 devices: {', '.join(self.esp32_devices)}")
        self.log(f"Listen interfaces: {', '.join(self.listen_interfaces)}")
        
        self.running = True
        threads = []
        
        # Start listener thread for each interface
        for interface_ip in self.listen_interfaces:
            thread = threading.Thread(
                target=self.listen_on_interface,
                args=(interface_ip,),
                daemon=True
            )
            thread.start()
            threads.append(thread)
        
        self.log("Discovery proxy started successfully")
        self.log("Press Ctrl+C to stop")
        
        try:
            # Main loop - print stats periodically
            while self.running:
                time.sleep(10)
                self.print_stats()
        except KeyboardInterrupt:
            self.log("Shutdown requested")
        finally:
            self.stop()
    
    def stop(self):
        """Stop the discovery proxy service."""
        self.log("Stopping discovery proxy...")
        self.running = False
        
        # Close all sockets
        for sock in self.sockets:
            try:
                sock.close()
            except:
                pass
        
        self.log("Discovery proxy stopped")
        self.print_final_stats()
    
    def print_stats(self):
        """Print current statistics."""
        if self.debug:
            self.log(f"Stats: RX={self.stats['broadcasts_received']}, "
                    f"FWD={self.stats['unicast_forwards']}, "
                    f"RESP_RX={self.stats['responses_received']}, "
                    f"RESP_FWD={self.stats['responses_forwarded']}, "
                    f"ERR={self.stats['errors']}, "
                    f"PENDING={len(self.pending_requests)}")
    
    def print_final_stats(self):
        """Print final statistics."""
        self.log("Final Statistics:")
        self.log(f"  Broadcasts received: {self.stats['broadcasts_received']}")
        self.log(f"  Unicast forwards: {self.stats['unicast_forwards']}")
        self.log(f"  Responses received: {self.stats['responses_received']}")
        self.log(f"  Responses forwarded: {self.stats['responses_forwarded']}")
        self.log(f"  Errors: {self.stats['errors']}")

def main():
    parser = argparse.ArgumentParser(
        description="GigE Vision Discovery Proxy for ESP32-CAM",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Proxy for single ESP32-CAM
  python3 discovery_proxy.py 192.168.213.40
  
  # Proxy for multiple ESP32-CAM devices
  python3 discovery_proxy.py 192.168.1.100 192.168.1.101
  
  # With debug output
  python3 discovery_proxy.py 192.168.213.40 --debug
  
  # Listen on specific interfaces only
  python3 discovery_proxy.py 192.168.213.40 --interfaces 192.168.213.45 192.168.213.28
        """
    )
    parser.add_argument('esp32_devices', nargs='+', 
                       help='IP addresses of ESP32-CAM devices to proxy for')
    parser.add_argument('--interfaces', nargs='*',
                       help='Interface IPs to listen on (default: all non-loopback)')
    parser.add_argument('--debug', action='store_true',
                       help='Enable debug logging')
    
    args = parser.parse_args()
    
    # Validate ESP32 IP addresses
    for ip in args.esp32_devices:
        try:
            socket.inet_aton(ip)
        except socket.error:
            print(f"Error: Invalid IP address '{ip}'")
            sys.exit(1)
    
    # Create and start proxy
    proxy = DiscoveryProxy(
        esp32_devices=args.esp32_devices,
        listen_interfaces=args.interfaces,
        debug=args.debug
    )
    
    # Set up signal handlers
    def signal_handler(signum, frame):
        proxy.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        proxy.start()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()