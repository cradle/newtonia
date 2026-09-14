#!/usr/bin/env python3
"""Generate the Steam Deck / Steam Machine announcement cover image.

The cover for the "Newtonia on Steam Deck and Steam Machine" Steam Community
post (steam/announcements/steam-deck-steam-machine.md): "NEWTONIA" in the
in-game Typer segment font as glowing green strokes over the dark starfield,
with line-art silhouettes of a Steam Deck and a Steam Machine underneath in
the game's white wireframe vocabulary, each captioned. The Deck's screen
shows a tiny game scene (player-1 ship, asteroid, stars).

The devices are ORIGINAL line-art silhouettes, deliberately not Valve's
trademarked Steam / Steam Deck logos or the Deck Verified badges — those may
only be used from the Steamworks branding assets under Valve's guidelines.

Shares the font table, starfield and glow renderer with
generate_online_announcement.py (the extra glyphs the captions need are
ported here, vertex-for-vertex from typer.cpp, and added to its table).
Requires Pillow. Deterministic (fixed RNG seed).

    python3 generate_deck_announcement.py            # -> newtonia_deck_announcement.png (800x450)
    python3 generate_deck_announcement.py out.png 400 225

Steam's event cover image is 800x450 (16:9); the layout is calibrated in the
400x225 capsule units the online generator uses and scales with the size.
"""

import sys

from PIL import Image, ImageDraw, ImageFilter, ImageChops

import generate_online_announcement as base
from generate_online_announcement import (
    TQ, TH, TMU, TM, TML, TW, TMW, TC, SS, BG, render_stars, render_glow)

# Glyphs the captions need beyond the online title's set, ported from
# typer.cpp init_meshes() in the same font space (y up, 0 = baseline).
base.GLYPHS.update({
    'S': [[(TW, TH), (0, TH), (0, TM), (TW, TM), (TW, 0), (0, 0)]],
    'M': [[(0, 0), (0, TH), (TW, TH), (TW, 0)], [(TC, TH), (TC, TM)]],
    'D': [[(0, 0), (0, TH), (TMW, TH), (TW, TMU), (TW, TM), (TW, 0), (0, 0)]],
    'C': [[(TW, 0), (0, 0), (0, TH), (TW, TH)]],
    'K': [[(0, 0), (0, TH)], [(0, TM), (TW, TM)], [(TW, TM), (TW, 0)],
          [(TMW, TM), (TW, TH)]],
    'H': [[(TW, TM), (0, TM)], [(0, 0), (0, TH)], [(TW, 0), (TW, TH)]],
    '+': [[(0, TM), (TW, TM)], [(TC, 1.75), (TC, 0.25)]],
})

WHITE = (233, 247, 238)     # asteroid / wireframe white (site --ink)
BLUE = (72, 118, 255)       # player-1 ship
GREEN = (0, 255, 0)         # Typer text green


class Ink(object):
    """Stroke collector drawing onto a supersampled 8-bit mask.

    Coordinates are capsule units (400x225 reference); `sx`/`sy` scale them
    to output pixels and SS supersamples for smooth strokes.
    """

    def __init__(self, size_px, sx, sy):
        self.w, self.h = size_px[0] * SS, size_px[1] * SS
        self.sx, self.sy = sx * SS, sy * SS
        self.mask = Image.new('L', (self.w, self.h), 0)
        self.d = ImageDraw.Draw(self.mask)

    def px(self, x, y):
        return (x * self.sx, y * self.sy)

    def line(self, pts, width, closed=False):
        p = [self.px(x, y) for (x, y) in pts]
        if closed:
            p.append(p[0])
        wpx = max(1, int(round(width * min(self.sx, self.sy))))
        self.d.line(p, fill=255, width=wpx, joint='curve')
        r = wpx / 2.0
        for x, y in (p[0], p[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=255)

    def circle(self, cx, cy, r, width, fill=False):
        x0, y0 = self.px(cx - r, cy - r)
        x1, y1 = self.px(cx + r, cy + r)
        if fill:
            self.d.ellipse([x0, y0, x1, y1], fill=255)
        else:
            wpx = max(1, int(round(width * min(self.sx, self.sy))))
            self.d.ellipse([x0, y0, x1, y1], outline=255, width=wpx)

    def rrect(self, x0, y0, x1, y1, radius, width):
        p0 = self.px(x0, y0)
        p1 = self.px(x1, y1)
        wpx = max(1, int(round(width * min(self.sx, self.sy))))
        self.d.rounded_rectangle([p0, p1], radius=radius * min(self.sx, self.sy),
                                 outline=255, width=wpx)

    def dot(self, x, y, r):
        self.circle(x, y, r, 0, fill=True)

    def layer(self, colour, size_px, glow=1.0):
        """Colour the mask: sharp core plus a soft bloom, like the title."""
        core = self.mask
        inner = core.filter(ImageFilter.GaussianBlur(1.6 * SS))
        outer = core.filter(ImageFilter.GaussianBlur(4.0 * SS))

        def scaled(img, k):
            return img.point(lambda v: int(min(255, v * k)))

        intensity = ImageChops.add(
            ImageChops.add(scaled(core, 1.0), scaled(inner, 0.55 * glow)),
            scaled(outer, 0.35 * glow))
        r, g, b = colour
        rgb = Image.merge('RGB', (
            intensity.point(lambda v: v * r // 255),
            intensity.point(lambda v: v * g // 255),
            intensity.point(lambda v: v * b // 255)))
        return rgb.resize(size_px, Image.LANCZOS)


def draw_deck(ink, cx, cy, w, width):
    """Steam Deck silhouette centred at (cx, cy), `w` wide, in capsule units."""
    h = w * 0.39                      # the Deck's 298 x 117 mm footprint
    x0, x1 = cx - w / 2, cx + w / 2
    y0, y1 = cy - h / 2, cy + h / 2
    # Body: rounded slab. The grips are the bulges at the bottom corners.
    ink.rrect(x0, y0, x1, y1, h * 0.28, width)
    # Screen (16:10, about half the body width).
    sw = w * 0.50
    sh = sw * 0.625
    ink.rrect(cx - sw / 2, cy - sh / 2, cx + sw / 2, cy + sh / 2, 1.2, width)
    # Left cluster: d-pad at the edge, stick near the screen, trackpad under it.
    stick_r = h * 0.11
    lx = cx - sw / 2 - w * 0.06           # stick column, hugging the screen
    ex = x0 + w * 0.072                   # edge column
    ty = cy - h * 0.20                    # top row
    ink.circle(lx, ty, stick_r, width)
    ink.circle(lx, ty, stick_r * 0.45, width * 0.8)
    pad = h * 0.16
    ink.rrect(lx - pad / 2, cy + h * 0.06, lx + pad / 2, cy + h * 0.06 + pad, 1.0, width * 0.8)
    # d-pad: a plus of two bars
    dp = h * 0.085
    ink.line([(ex - dp, ty), (ex + dp, ty)], width * 1.4)
    ink.line([(ex, ty - dp), (ex, ty + dp)], width * 1.4)
    # Right cluster: mirror — stick + trackpad, face buttons at the edge.
    rx = cx + sw / 2 + w * 0.06
    fx = x1 - w * 0.072
    ink.circle(rx, ty, stick_r, width)
    ink.circle(rx, ty, stick_r * 0.45, width * 0.8)
    ink.rrect(rx - pad / 2, cy + h * 0.06, rx + pad / 2, cy + h * 0.06 + pad, 1.0, width * 0.8)
    br = h * 0.045
    for dx, dy in ((0, -dp), (dp, 0), (0, dp), (-dp, 0)):
        ink.circle(fx + dx, ty + dy, br, width * 0.8)
    # Menu buttons either side of the screen, and the speaker slits.
    ink.dot(cx - sw / 2 - w * 0.03, cy + h * 0.30, 0.55)
    ink.dot(cx + sw / 2 + w * 0.03, cy + h * 0.30, 0.55)
    return (cx - sw / 2, cy - sh / 2, cx + sw / 2, cy + sh / 2)


def draw_scene(ink_blue, ink_white, screen):
    """A tiny Newtonia moment on the Deck's screen: ship, asteroid, stars."""
    x0, y0, x1, y1 = screen
    w, h = x1 - x0, y1 - y0
    # Player-1 ship (glship.cpp body outline), pointing up-right.
    import math
    cx, cy = x0 + w * 0.36, y0 + h * 0.58
    s = h * 0.13
    ang = math.radians(-35)
    body = [(0, -10), (-8, 10), (0, 5), (8, 10)]

    def rot(px, py):
        return (cx + (px * math.cos(ang) - py * math.sin(ang)) * s / 10.0,
                cy + (px * math.sin(ang) + py * math.cos(ang)) * s / 10.0)
    ink_blue.line([rot(*p) for p in body], 0.9, closed=True)
    # Asteroid: the 9-vertex irregular polygon from the site hero.
    ax, ay = x0 + w * 0.72, y0 + h * 0.36
    a = h * 0.17
    poly = [(10, 0), (6.5, 5.5), (1.9, 10.8), (-4.5, 7.8), (-9.9, 3.6),
            (-7.5, -2.7), (-4.75, -8.2), (1.9, -10.8), (6.9, -5.8)]
    ink_white.line([(ax + px * a / 10.0, ay + py * a / 10.0) for px, py in poly],
                   0.7, closed=True)
    # A few stars.
    for fx, fy in ((0.12, 0.2), (0.25, 0.8), (0.55, 0.15), (0.88, 0.75), (0.6, 0.62), (0.9, 0.3)):
        ink_white.dot(x0 + w * fx, y0 + h * fy, 0.35)


def draw_machine(ink, ink_green, cx, cy, size, width):
    """Steam Machine silhouette: a small cube seen from front-left-above."""
    f = size                              # front face edge
    x0, y0 = cx - f / 2, cy - f / 2 + f * 0.12
    x1, y1 = x0 + f, y0 + f
    dx, dy = f * 0.22, -f * 0.22          # depth direction (up-right)
    r = f * 0.06
    ink.rrect(x0, y0, x1, y1, r, width)
    # Top face and right face, as open parallelograms, joined at the
    # rounded top-right corner.
    ink.line([(x0 + r, y0), (x0 + r + dx, y0 + dy), (x1 - r + dx, y0 + dy),
              (x1 + dx, y0 + r + dy), (x1 + dx, y1 - r + dy), (x1, y1 - r)], width)
    ink.line([(x1, y0 + r), (x1 + dx, y0 + r + dy)], width)
    # Front: power button top-right, status light bar low across the face.
    ink.circle(x1 - f * 0.14, y0 + f * 0.14, f * 0.05, width * 0.8)
    ink_green.line([(x0 + f * 0.12, y1 - f * 0.16), (x1 - f * 0.12, y1 - f * 0.16)], width * 1.1)
    return (x0, y0, x1, y1)


def generate(out_path, w=800, h=450):
    sx, sy = w / 400.0, h / 225.0
    s = min(sx, sy)

    # Title: the online capsule's NEWTONIA, lifted to the top third.
    title = ("NEWTONIA", 27 * sx, 45.3 * sx, 29.5 * s, 84 * sy, 3.4 * s)
    # Captions under each device, in the small ONLINE weight.
    cap_size, cap_adv = 6.6 * s, 12.2 * sx
    deck_cx, mach_cx = 128.0, 312.0
    deck_cap = "STEAM DECK"
    mach_cap = "STEAM MACHINE"

    def caption(text, cx):
        width = (len(text) - 1) * 12.2 + 6.6
        return (text, (cx - width / 2) * sx, cap_adv, cap_size, 208 * sy, 1.6 * s)

    stars = render_stars((w, h), seed=2026)
    green = render_glow((w, h), [title, caption(deck_cap, deck_cx),
                                 caption(mach_cap, mach_cx)])

    white = Ink((w, h), sx, sy)
    blue = Ink((w, h), sx, sy)
    led = Ink((w, h), sx, sy)
    screen = draw_deck(white, deck_cx, 146.0, 196.0, 1.25)
    draw_scene(blue, white, screen)
    draw_machine(white, led, mach_cx, 146.0, 62.0, 1.25)

    out = ImageChops.add(stars, green)
    out = ImageChops.add(out, white.layer(WHITE, (w, h), glow=0.8))
    out = ImageChops.add(out, blue.layer(BLUE, (w, h), glow=1.2))
    out = ImageChops.add(out, led.layer(GREEN, (w, h), glow=1.6))
    out.save(out_path)
    print("wrote %s (%dx%d)" % (out_path, w, h))


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'newtonia_deck_announcement.png'
    W = int(sys.argv[2]) if len(sys.argv) > 2 else 800
    H = int(sys.argv[3]) if len(sys.argv) > 3 else 450
    generate(out, W, H)
