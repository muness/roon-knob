#!/usr/bin/env python3
"""Send a UDP fast-state poll to the bridge and print the response."""
import socket, struct, sys

BRIDGE_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.225"
BRIDGE_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8089
ZONE_ID = sys.argv[3] if len(sys.argv) > 3 else ""

MAGIC = 0x524B  # 'RK' LE

# Build request: magic(2) + sha(20) + zone_id(64) = 86 bytes
req = struct.pack("<H", MAGIC)
req += b"\x00" * 20  # empty SHA → always get full response
req += ZONE_ID.encode("utf-8").ljust(64, b"\x00")[:64]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)
sock.sendto(req, (BRIDGE_IP, BRIDGE_PORT))

try:
    data, addr = sock.recvfrom(128)
    print(f"Got {len(data)} bytes from {addr}")
    if len(data) >= 48:
        magic, version, flags = struct.unpack_from("<HBB", data, 0)
        sha = data[4:24]
        vol, vol_min, vol_max, vol_step = struct.unpack_from("<ffff", data, 24)
        seek, length = struct.unpack_from("<iI", data, 40)
        print(f"  magic=0x{magic:04X} ver={version} flags=0x{flags:02X}")
        print(f"  sha={sha[:8]}")
        print(f"  volume={vol:.1f} min={vol_min:.1f} max={vol_max:.1f} step={vol_step:.1f}")
        print(f"  seek_position={seek} length={length}")
        playing = bool(flags & 1)
        print(f"  playing={playing}")
        if length > 0 and seek >= 0:
            print(f"  progress={seek*100//length}%")
        elif length == 0:
            print("  ⚠ length=0 → bridge not sending duration")
        elif seek == -1:
            print("  ⚠ seek=-1 → bridge not sending seek position")
    else:
        print(f"  Unexpected size: {data.hex()}")
except socket.timeout:
    print("No response (timeout)")
finally:
    sock.close()
