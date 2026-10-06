#!/usr/bin/env python3
"""Rasterize the application icon into a Windows .ico (MIT).

Standard library only. Draws the same shapes as packaging/console-observatory.svg
(128x128 design units) with 4x4 supersampling, then writes BMP entries for small
sizes and a PNG entry for 256 px. Output is deterministic.
"""
import argparse
import struct
import zlib
from pathlib import Path

BACKGROUND = (0x11, 0x1B, 0x24)
SCREEN = (0xCA, 0xDC, 0x9F)
STAR = (0x19, 0x3B, 0x31)
ACCENT = (0x81, 0xD9, 0xBC)
STAR_POLYGON = [(64, 47), (52, 47), (52, 59), (40, 59), (40, 71), (52, 71), (52, 83),
                (76, 83), (76, 71), (88, 71), (88, 59), (76, 59), (76, 47)]


def in_rounded_rect(x, y, left, top, right, bottom, radius):
    if not (left <= x <= right and top <= y <= bottom):
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2


def in_polygon(x, y, points):
    inside = False
    for (x1, y1), (x2, y2) in zip(points, points[1:] + points[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            inside = not inside
    return inside


def in_capsule(x, y, x1, x2, cy, half):
    cx = min(max(x, x1), x2)
    return (x - cx) ** 2 + (y - cy) ** 2 <= half ** 2


def sample(x, y):
    """Return an RGBA colour for one design-space point (top-most shape wins)."""
    if in_polygon(x, y, STAR_POLYGON):
        return STAR + (255,)
    if in_rounded_rect(x, y, 23, 23, 105, 93, 8):
        return SCREEN + (255,)
    if in_capsule(x, y, 29, 57, 106, 3) or in_capsule(x, y, 71, 99, 106, 3):
        return ACCENT + (255,)
    if in_rounded_rect(x, y, 4, 4, 124, 124, 24):
        return BACKGROUND + (255,)
    return (0, 0, 0, 0)


def render(size):
    grid = 4
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            total = [0, 0, 0, 0]
            for sy in range(grid):
                for sx in range(grid):
                    x = (px + (sx + 0.5) / grid) * 128 / size
                    y = (py + (sy + 0.5) / grid) * 128 / size
                    r, g, b, a = sample(x, y)
                    total[0] += r * a; total[1] += g * a; total[2] += b * a; total[3] += a
            alpha = total[3] // (grid * grid)
            if total[3]:
                row.append(tuple(round(c / total[3]) for c in total[:3]) + (alpha,))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return rows


def png(rows):
    size = len(rows)
    raw = b''.join(b'\0' + bytes(c for pixel in row for c in pixel) for row in rows)
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def dib(rows):
    size = len(rows)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = b''.join(bytes((b, g, r, a)) for row in reversed(rows) for r, g, b, a in row)
    stride = (size + 31) // 32 * 4
    mask = bytearray()
    for row in reversed(rows):
        bits = bytearray(stride)
        for x, pixel in enumerate(row):
            if pixel[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return header + pixels + bytes(mask)


def build(output):
    sizes = (16, 24, 32, 48, 64, 256)
    images = [png(render(s)) if s == 256 else dib(render(s)) for s in sizes]
    directory = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in zip(sizes, images):
        directory += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    output.write_bytes(directory + b''.join(images))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'packaging/windows/console-observatory.ico')
    build(parser.parse_args().output)
