#!/usr/bin/env python3
"""PyBoy screen -> PNG without Pillow (zlib + struct). shot(pyboy, path, scale=3)."""
import struct
import zlib


def write_png(path, w, h, rgb_rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgb_rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def shot(pb, path, scale=3):
    a = pb.screen.ndarray            # (144, 160, 4) RGBA
    h, w = a.shape[0], a.shape[1]
    rows = []
    for y in range(h):
        r = bytearray()
        for x in range(w):
            r += bytes(a[y, x, :3]) * scale
        for _ in range(scale):
            rows.append(r)
    write_png(path, w * scale, h * scale, rows)
