#!/usr/bin/env python3
"""Writes the gunner's sounds: rifle_shot.wav (a gunshot) and mag_click.wav (a magazine
going in or out). Synthesised from noise and tones; mono, 48 kHz, 16-bit WAV. CC0.

Usage: python tools/make_gunner_sounds.py assets/sounds

Plain Python (standard library only).
"""
import math, os, random, struct, sys, wave

RATE = 48000

def write(path, samples):
    peak = max(abs(s) for s in samples) or 1.0
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 0.9 * 32767)) for s in samples))
    print('wrote %s: %.2f s' % (path, len(samples) / RATE))

def lowpass(samples, cutoff):
    a = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)
    out, y = [], 0.0
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out

def shot():
    """A sharp crack, a low thump and a short noisy tail (a rifle outdoors)."""
    rnd = random.Random(7)
    n = int(RATE * 0.6)
    noise = [rnd.uniform(-1, 1) for _ in range(n)]
    crack = [noise[i] * math.exp(-i / (RATE * 0.004)) for i in range(n)]
    body = lowpass([noise[i] * math.exp(-i / (RATE * 0.06)) for i in range(n)], 1800)
    tail = lowpass(lowpass([noise[i] * math.exp(-i / (RATE * 0.18)) for i in range(n)], 600), 600)
    thump = [math.sin(2 * math.pi * (70 - 30 * i / n) * i / RATE) * math.exp(-i / (RATE * 0.05)) for i in range(n)]
    attack = int(RATE * 0.0008)
    return [(0.9 * crack[i] + 2.2 * body[i] + 2.5 * tail[i] + 0.8 * thump[i]) * min(1.0, i / attack) for i in range(n)]

def click():
    """Two quick metallic clicks: a magazine sliding home."""
    rnd = random.Random(3)
    n = int(RATE * 0.12)
    out = [0.0] * n
    for start, level in ((0, 0.7), (int(RATE * 0.035), 1.0)):
        for i in range(n - start):
            env = math.exp(-i / (RATE * 0.0035))
            ring = math.sin(2 * math.pi * 3200 * i / RATE) * math.exp(-i / (RATE * 0.012))
            out[start + i] += level * (rnd.uniform(-1, 1) * env + 0.35 * ring)
    return out

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    write(os.path.join(out, 'rifle_shot.wav'), shot())
    write(os.path.join(out, 'mag_click.wav'), click())
