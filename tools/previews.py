#!/usr/bin/env python3
"""Export the actual UI renderer to PNG and GIF with the Python standard library."""
from pathlib import Path
import argparse
import struct
import subprocess
import zlib


def load_pgm(path):
    kind, size, maximum, pixels = path.read_bytes().split(b"\n", 3)
    assert kind == b"P5" and size == b"128 64" and maximum == b"255" and len(pixels) == 8192
    return pixels


def enlarge(pixels, scale):
    rows = []
    for y in range(64):
        row = b"".join(bytes([value]) * scale for value in pixels[y * 128:(y + 1) * 128])
        rows.extend([row] * scale)
    return rows


def png(path, rows):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    width, height = len(rows[0]), len(rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    body = zlib.compress(b"".join(b"\0" + row for row in rows), 9)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", body) + chunk(b"IEND", b""))


def gif(path, frames, scale=2):
    width, height = 128 * scale, 64 * scale
    data = bytearray(b"GIF89a" + struct.pack("<HHBBB", width, height, 0x80, 0, 0))
    data.extend(b"\0\0\0\xff\xff\xff")
    data.extend(b"\x21\xff\x0bNETSCAPE2.0\x03\x01\0\0\0")
    for frame in frames:
        data.extend(b"\x21\xf9\x04\x08\x01\0\0\0")
        data.extend(b"," + struct.pack("<HHHHB", 0, 0, width, height, 0))
        # Clear between literals to keep valid fixed-width 3-bit LZW codes.
        bits = 0
        count = 0
        encoded = bytearray()
        def emit(code):
            nonlocal bits, count
            bits |= code << count
            count += 3
            while count >= 8:
                encoded.append(bits & 255)
                bits >>= 8
                count -= 8
        for row in enlarge(frame, scale):
            for value in row:
                emit(4)
                emit(1 if value else 0)
        emit(5)
        if count:
            encoded.append(bits & 255)
        data.append(2)
        for offset in range(0, len(encoded), 255):
            block = encoded[offset:offset + 255]
            data.append(len(block))
            data.extend(block)
        data.append(0)
    data.append(0x3B)
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", default="build/host/render_previews")
    parser.add_argument("--output", default="docs/previews")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    raw = root / "build/previews"
    subprocess.run([str(root / args.renderer), str(raw)], check=True, cwd=root)
    output = root / args.output
    output.mkdir(parents=True, exist_ok=True)
    files = [f"home-{i}.pgm" for i in range(4)] + ["1.pgm", "4.pgm", "3.pgm", "2.pgm", "9.pgm"]
    tiles = [enlarge(load_pgm(raw / file), 3) for file in files]
    rows = []
    gap = bytes([48]) * (384 * 3 + 8 * 2)
    for row in range(3):
        if row:
            rows.extend([gap] * 8)
        for y in range(192):
            rows.append(bytes([48] * 8).join(tiles[row * 3 + col][y] for col in range(3)))
    png(output / "menu.png", rows)
    gif(output / "menu.gif", [load_pgm(file) for file in sorted(raw.glob("animation-*.pgm"))])
    print(f"Preview: {output / 'menu.png'}")
    print(f"Animation: {output / 'menu.gif'}")


if __name__ == "__main__":
    main()
