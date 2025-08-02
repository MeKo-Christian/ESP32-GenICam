#!/usr/bin/env python3

import socket
import struct
import argparse
import time

GVCP_PORT = 3956
READMEM_CMD = 0x0080
READMEM_ACK = 0x0081

def read_mem(ip, address, size, packet_id=0x4321):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)

    # Build READMEM command
    header = struct.pack('>BBHHH', 0x42, 0x00, READMEM_CMD, 8, packet_id)
    payload = struct.pack('>II', address, size)
    packet = header + payload

    print(f"Sending READMEM_CMD to {ip}:{GVCP_PORT} (addr=0x{address:08x}, size={size})")

    sock.sendto(packet, (ip, GVCP_PORT))

    try:
        data, _ = sock.recvfrom(4096)
        if len(data) < 8:
            raise Exception("Response too short")

        pt, pf, cmd, size_words, resp_id = struct.unpack('>BBHHH', data[:8])
        payload = data[8:]

        if pt != 0x43 or cmd != READMEM_ACK or resp_id != packet_id:
            raise Exception(f"Unexpected response: type=0x{pt:02x}, cmd=0x{cmd:04x}, id=0x{resp_id:04x}")

        print(f"✓ READMEM_ACK received ({len(payload)} bytes)")
        return payload

    except Exception as e:
        print(f"❌ Error: {e}")
        return None
    finally:
        sock.close()

def main():
    parser = argparse.ArgumentParser(description="Read GenICam XML from GVCP device")
    parser.add_argument('ip', help="Device IP")
    parser.add_argument('--address', type=lambda x: int(x, 0), default=0x10000, help="Start address (default: 0x10000)")
    parser.add_argument('--size', type=int, default=1024, help="Bytes to read (default: 1024)")
    args = parser.parse_args()

    data = read_mem(args.ip, args.address, args.size)
    if data:
        print("\n--- Dump ---\n")
        try:
            text = data.decode('utf-8', errors='replace')
            print(text)
        except Exception as e:
            print(f"Cannot decode data: {e}")

if __name__ == "__main__":
    main()
