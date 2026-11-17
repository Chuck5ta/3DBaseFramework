#!/usr/bin/env python3
"""Writes crate_normal.png: a normal map for assets/textures/crate.png (CC0).

Usage: python tools/make_crate_normal.py assets/textures/crate_normal.png

Pure Python (standard library only). It doesn't read crate.png: it rebuilds the crate's
layout (measured from that 128 x 128 texture) as a height map at twice the size, raised
frame and brace, planks a little lower, grooves between them and a fine wood grain, then
turns the slopes into normals.

Normal map convention: tangent space, red = right (+u), green = up (towards the top of
the image), blue = out of the surface. That's OpenGL's, glTF's and Blender's convention,
the one Game3D's MeshRenderer expects. (DirectX-style maps have green pointing down:
invert the green channel to use one.)
"""
import math, struct, sys, zlib

SIZE = 256           # output pixels; the crate texture is 128, so 2 per texel
SCALE = SIZE / 128.0
STRENGTH = 3.0       # how steep a step in height looks


def height_at(ex, ey):
    """Height (0..1) at a point in crate.png's texel units (0..128, y down)."""
    edge = ex < 2 or ex > 126 or ey < 2 or ey > 126
    frame = ex < 10 or ex > 118 or ey < 10 or ey > 118
    # The brace runs corner to corner along x = y, 15 texels wide (measured), its ends cut square.
    brace = abs(ex - ey) < 7.5 and 21.5 <= ex + ey <= 236.5
    if edge:
        return 0.55
    if frame:
        return 1.0
    if brace:
        return 0.9
    if int(ey) % 16 == 0:  # the grooves between planks: rows 16, 32, ... 112
        return 0.2
    return 0.45


def rng(seed):
    """xorshift32: the same numbers on every machine."""
    state = [seed or 1]

    def next01():
        x = state[0]
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        state[0] = x
        return x / 4294967296.0
    return next01


def build_heights():
    h = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            h[y][x] = height_at((x + 0.5) / SCALE, (y + 0.5) / SCALE)
    # Wood grain on the planks and brace: thin streaks along the grain, plus a little noise.
    r = rng(2024)
    streak = [r() for _ in range(SIZE)]          # per row: planks run left to right
    diag = [r() for _ in range(SIZE * 2)]        # per diagonal: the brace runs along x = y
    for y in range(SIZE):
        for x in range(SIZE):
            v = h[y][x]
            if v == 0.45:
                h[y][x] += 0.05 * (streak[y] - 0.5) + 0.02 * (r() - 0.5)
            elif v == 0.9:
                h[y][x] += 0.05 * (diag[x - y + SIZE] - 0.5) + 0.02 * (r() - 0.5)
    return h


def blur(h, passes=2):
    """Box blur (3 x 3), clamped at the borders: rounds the edges into small bevels."""
    for _ in range(passes):
        out = [[0.0] * SIZE for _ in range(SIZE)]
        for y in range(SIZE):
            for x in range(SIZE):
                s = 0.0
                for dy in (-1, 0, 1):
                    yy = min(max(y + dy, 0), SIZE - 1)
                    row = h[yy]
                    for dx in (-1, 0, 1):
                        s += row[min(max(x + dx, 0), SIZE - 1)]
                out[y][x] = s / 9.0
        h = out
    return h


def normals(h):
    pixels = bytearray()
    for y in range(SIZE):
        pixels.append(0)  # PNG filter: none
        for x in range(SIZE):
            dx = (h[y][min(x + 1, SIZE - 1)] - h[y][max(x - 1, 0)]) * 0.5
            dy = (h[min(y + 1, SIZE - 1)][x] - h[max(y - 1, 0)][x]) * 0.5  # down the image
            # Higher to the right tilts the normal left (-x); higher further down the image
            # tilts it up the image (+y, green = up).
            n = (-dx * STRENGTH, dy * STRENGTH, 1.0)
            length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
            for c in n:
                pixels.append(max(0, min(255, int(round((c / length * 0.5 + 0.5) * 255)))))
    return bytes(pixels)


def write_png(path, data):
    def chunk(kind, body):
        c = struct.pack('>I', len(body)) + kind + body
        return c + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 2, 0, 0, 0))  # 8-bit RGB
    png += chunk(b'IDAT', zlib.compress(data, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'crate_normal.png'
    write_png(out, normals(blur(build_heights())))
    print(out, SIZE, 'x', SIZE)
