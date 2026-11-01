#!/usr/bin/env python3
"""Write a UV test texture: 8x8 checker, hue changes with u, brightness with v,
plus thin grid lines, so flipped or rotated texture coordinates are obvious.

usage: tools/make_uv_grid.py <out.png> [size]
"""
import colorsys
import struct
import sys
import zlib


def png_bytes(width, height, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)  # filter type 0 per row
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def main():
    out = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 512
    cells = 8
    cell = size // cells
    rows = []
    for y in range(size):  # PNG rows go top to bottom; v = 1 at the top
        v = 1.0 - (y + 0.5) / size
        row = []
        for x in range(size):
            u = (x + 0.5) / size
            checker = ((x // cell) + (y // cell)) % 2
            r, g, b = colorsys.hsv_to_rgb(u * 0.85, 0.65, 0.45 + 0.45 * v)
            if checker:
                r, g, b = r * 0.75, g * 0.75, b * 0.75
            if x % cell in (0, cell - 1) or y % cell in (0, cell - 1):
                r = g = b = 0.1
            row += [int(r * 255), int(g * 255), int(b * 255)]
        rows.append(row)
    with open(out, "wb") as f:
        f.write(png_bytes(size, size, rows))


if __name__ == "__main__":
    main()
