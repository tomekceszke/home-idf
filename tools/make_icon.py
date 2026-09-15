#!/usr/bin/env python3
"""Writes a 180x180 apple-touch-icon PNG: flat background with a centred dot. Stdlib only.

Usage: make_icon.py OUT.png [--bg 2c333a] [--fg eef1f2] [--radius 0.14]
"""
import argparse
import math
import struct
import zlib

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--bg", default="2c333a")
ap.add_argument("--fg", default="eef1f2")
ap.add_argument("--radius", type=float, default=9 / 64, help="dot radius as a fraction of the size")
ap.add_argument("--size", type=int, default=180)
args = ap.parse_args()

size = args.size
bg = bytes.fromhex(args.bg)
fg = bytes.fromhex(args.fg)
radius = size * args.radius
c = (size - 1) / 2
rows = []
for y in range(size):
    row = bytearray([0])        # filter: none
    for x in range(size):
        a = min(1.0, max(0.0, radius + 0.5 - math.hypot(x - c, y - c)))    # 1 px anti-aliased edge
        row += bytes(round(bg[i] + (fg[i] - bg[i]) * a) for i in range(3))
    rows.append(bytes(row))


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
with open(args.out, "wb") as f:
    f.write(png)
print(args.out, len(png), "bytes")
