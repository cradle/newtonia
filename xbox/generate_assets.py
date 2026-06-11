#!/usr/bin/env python3
"""Generate placeholder Xbox store/shell image assets into xbox/Assets/.

Green-on-black vector look matching the game's aesthetic. Pure stdlib
(minimal PNG writer via zlib), same convention as generate_sounds.py.
These are placeholders — replace with real art before store submission.

Outputs (sizes required by MicrosoftGame.config ShellVisuals):
  StoreLogo.png     100 x 100
  Logo150x150.png   150 x 150
  Logo480x480.png   480 x 480
  SplashScreen.png  1920 x 1080
"""
import math
import os
import random
import struct
import zlib

GREEN = (0, 255, 68)
DIM_GREEN = (0, 140, 60)
STAR = (180, 200, 190)


def write_png(path, w, h, buf):
    raw = b''.join(b'\x00' + bytes(buf[y * w * 3:(y + 1) * w * 3])
                   for y in range(h))

    def chunk(tag, data):
        out = struct.pack('>I', len(data)) + tag + data
        return out + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)
    print('wrote %s (%dx%d)' % (path, w, h))


class Canvas:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.buf = bytearray(w * h * 3)

    def set(self, x, y, color):
        x = int(x)
        y = int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.buf[i:i + 3] = bytes(color)

    def disc(self, cx, cy, r, color):
        for dy in range(-int(r), int(r) + 1):
            for dx in range(-int(r), int(r) + 1):
                if dx * dx + dy * dy <= r * r:
                    self.set(cx + dx, cy + dy, color)

    def line(self, x0, y0, x1, y1, thick, color):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(steps + 1):
            t = i / float(steps)
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            if thick <= 1:
                self.set(x, y, color)
            else:
                self.disc(x, y, thick / 2.0, color)

    def polyline(self, pts, thick, color, close=False):
        for a, b in zip(pts, pts[1:]):
            self.line(a[0], a[1], b[0], b[1], thick, color)
        if close:
            self.line(pts[-1][0], pts[-1][1], pts[0][0], pts[0][1], thick, color)


# Minimal stroke font for the letters in "NEWTONIA".
# Coordinates: x in 0..1, y in 0..1 with y=0 at the TOP of the glyph.
FONT = {
    'N': [[(0, 1), (0, 0)], [(0, 0), (1, 1)], [(1, 1), (1, 0)]],
    'E': [[(1, 0), (0, 0)], [(0, 0), (0, 1)], [(0, 1), (1, 1)],
          [(0, 0.5), (0.7, 0.5)]],
    'W': [[(0, 0), (0.25, 1)], [(0.25, 1), (0.5, 0.4)],
          [(0.5, 0.4), (0.75, 1)], [(0.75, 1), (1, 0)]],
    'T': [[(0, 0), (1, 0)], [(0.5, 0), (0.5, 1)]],
    'O': [[(0, 0), (1, 0)], [(1, 0), (1, 1)], [(1, 1), (0, 1)],
          [(0, 1), (0, 0)]],
    'I': [[(0.2, 0), (0.8, 0)], [(0.5, 0), (0.5, 1)], [(0.2, 1), (0.8, 1)]],
    'A': [[(0, 1), (0.5, 0)], [(0.5, 0), (1, 1)], [(0.2, 0.55), (0.8, 0.55)]],
}


def draw_text(c, cx, cy, text, letter_h, thick, color):
    letter_w = letter_h * 0.62
    spacing = letter_w * 0.55
    total = len(text) * letter_w + (len(text) - 1) * spacing
    x = cx - total / 2.0
    for ch in text:
        for stroke in FONT[ch]:
            (ax, ay), (bx, by) = stroke
            c.line(x + ax * letter_w, cy - letter_h / 2.0 + ay * letter_h,
                   x + bx * letter_w, cy - letter_h / 2.0 + by * letter_h,
                   thick, color)
        x += letter_w + spacing


def asteroid_points(cx, cy, r, seed):
    # 9-vertex irregular polygon, like the in-game asteroids.
    rng = random.Random(seed)
    pts = []
    for i in range(9):
        a = i * 2.0 * math.pi / 9.0
        rr = r * (0.72 + rng.random() * 0.4)
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    return pts


def draw_ship(c, cx, cy, size, thick, color):
    # Simple player-ship triangle with a thruster line, nose up.
    nose = (cx, cy - size)
    left = (cx - size * 0.7, cy + size * 0.8)
    right = (cx + size * 0.7, cy + size * 0.8)
    c.polyline([nose, left, (cx, cy + size * 0.45), right], thick, color,
               close=True)
    c.line(cx, cy + size * 0.8, cx, cy + size * 1.4, thick, DIM_GREEN)


def draw_tile(path, size):
    c = Canvas(size, size)
    thick = max(1, size // 60)
    rng = random.Random(11)
    for _ in range(size // 6):
        c.set(rng.randrange(size), rng.randrange(size), STAR)
    c.polyline(asteroid_points(size * 0.5, size * 0.5, size * 0.42, 3),
               thick, GREEN, close=True)
    draw_ship(c, size * 0.5, size * 0.52, size * 0.16, thick, GREEN)
    write_png(path, size, size, c.buf)


def draw_splash(path, w=1920, h=1080):
    c = Canvas(w, h)
    rng = random.Random(7)
    for _ in range(700):
        x = rng.randrange(w)
        y = rng.randrange(h)
        if rng.random() < 0.15:
            c.disc(x, y, 1, STAR)
        else:
            c.set(x, y, STAR)
    # Asteroids flanking the title.
    c.polyline(asteroid_points(w * 0.80, h * 0.30, 170, 5), 3, GREEN,
               close=True)
    c.polyline(asteroid_points(w * 0.16, h * 0.72, 110, 9), 3, DIM_GREEN,
               close=True)
    c.polyline(asteroid_points(w * 0.68, h * 0.80, 60, 13), 2, DIM_GREEN,
               close=True)
    draw_ship(c, w * 0.30, h * 0.34, 55, 4, GREEN)
    draw_text(c, w / 2.0, h * 0.58, 'NEWTONIA', 130, 6, GREEN)
    write_png(path, w, h, c.buf)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'Assets')
    os.makedirs(out, exist_ok=True)
    draw_tile(os.path.join(out, 'StoreLogo.png'), 100)
    draw_tile(os.path.join(out, 'Logo150x150.png'), 150)
    draw_tile(os.path.join(out, 'Logo480x480.png'), 480)
    draw_splash(os.path.join(out, 'SplashScreen.png'))


if __name__ == '__main__':
    main()
