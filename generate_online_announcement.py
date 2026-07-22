#!/usr/bin/env python3
"""Generate the "NEWTONIA ONLINE" Steam-announcement header image.

Reproduces the 400x225 Steam event capsule: the in-game Typer segment font
(ported vertex-for-vertex from typer.cpp) rendered as glowing green strokes
over a dark starfield, with "NEWTONIA" large and "ONLINE" right-aligned below.

Requires Pillow (`pip install Pillow`). Deterministic (fixed RNG seed) so the
starfield is identical on every run.

    python3 generate_online_announcement.py            # -> newtonia_online_announcement.png (400x225)
    python3 generate_online_announcement.py out.png 800 450

Rerun after any change to the title text, colours, or the Typer glyph table.
"""

import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageChops

# --- Typer font metrics (mirror typer.cpp lines 196-203) --------------------
TQ = 0.25
TH = 2.0
TMU = TH - TQ      # 1.75
TM = TH * 0.6      # 1.2
TML = TM - TQ      # 0.95
TW = 1.0
TMW = TW * 0.67    # 0.67
TC = TW / 2.0      # 0.5

# Glyph geometry, ported from typer.cpp init_meshes(). Each glyph is a list of
# polylines in font space: x in [0,TW], y in [0,TH] with y pointing UP
# (0 = baseline/bottom, TH = cap top). Only the letters used by the title are
# defined; add more straight from the C++ table if the text changes.
GLYPHS = {
    'N': [[(0, 0), (0, TH)], [(0, TH), (TW, TM)], [(TW, 0), (TW, TH)]],
    'E': [[(TW, TH), (0, TH), (0, 0), (TW, 0)], [(0, TM), (TMW, TM)]],
    'W': [[(0, TH), (0, 0), (TW, 0), (TW, TH)], [(TC, TM), (TC, 0)]],
    'T': [[(TC, TH), (TC, 0)], [(0, TH), (TW, TH)]],
    'O': [[(0, 0), (0, TH), (TW, TH), (TW, 0), (0, 0)]],
    'I': [[(TC, TH), (TC, 0)]],
    'A': [[(0, 0), (0, TH), (TW, TH), (TW, 0)], [(0, TM), (TW, TM)]],
    'L': [[(0, TH), (0, 0), (TW, 0)]],
}

GREEN = (0, 255, 0)
BG = (2, 2, 8)

# Supersample factor for smooth thick strokes; final image is downscaled.
SS = 4


def draw_text(draw, text, x0, adv, size, base_y, width):
    """Stroke `text` (uppercase, spaces allowed) onto an ImageDraw surface.

    x0     : screen x of each glyph's local x=0 (stroke centre), first column
    adv    : per-column horizontal advance (px)
    size   : uniform glyph scale (glyph is `size` wide, 2*size tall)
    base_y : screen y of the glyph baseline (font y=0)
    width  : stroke width in px
    """
    for col, ch in enumerate(text):
        if ch == ' ':
            continue
        glyph = GLYPHS[ch]
        ox = x0 + col * adv
        for poly in glyph:
            pts = [(ox + gx * size, base_y - gy * size) for (gx, gy) in poly]
            draw.line(pts, fill=255, width=width, joint='curve')
            # Round the endpoints so corners read as welded, like the game's
            # thick-line joints.
            r = width / 2.0
            for px, py in (pts[0], pts[-1]):
                draw.ellipse([px - r, py - r, px + r, py + r], fill=255)


def render_glow(size_px, passes):
    """Render title strokes into an additive green glow layer.

    `passes` is a list of (text, x0, adv, size, base_y, width) tuples in final
    (non-supersampled) coordinates; each is drawn onto a high-res ink mask,
    then bloomed with layered Gaussian blur and coloured pure green.
    """
    w, h = size_px[0] * SS, size_px[1] * SS
    ink = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(ink)
    for text, x0, adv, size, base_y, width in passes:
        draw_text(d, text, x0 * SS, adv * SS, size * SS, base_y * SS, int(round(width * SS)))

    # Sharp core + two blur radii, scaled to the stroke size, summed. Radii are
    # tuned so the falloff (peak -> background over ~8px at big size) matches the
    # reference cross-section.
    core = ink
    glow_inner = ink.filter(ImageFilter.GaussianBlur(2.4 * SS))
    glow_outer = ink.filter(ImageFilter.GaussianBlur(5.5 * SS))

    def scaled(img, k):
        return img.point(lambda v: int(min(255, v * k)))

    intensity = ImageChops.add(
        ImageChops.add(scaled(core, 1.0), scaled(glow_inner, 0.85)),
        scaled(glow_outer, 0.55))

    # Colour the intensity as pure green (R,B stay ~0, as measured).
    zero = Image.new('L', (w, h), 0)
    green = Image.merge('RGB', (zero, intensity, zero))
    return green.resize(size_px, Image.LANCZOS)


def render_stars(size_px, seed=1977):
    """Dark starfield: scattered white/slightly-blue points of varied brightness."""
    w, h = size_px
    img = Image.new('RGB', (w, h), BG)
    d = ImageDraw.Draw(img)
    rng = random.Random(seed)
    n = int(w * h / 900)            # ~100 stars at 400x225
    for _ in range(n):
        x = rng.randint(0, w - 1)
        y = rng.randint(0, h - 1)
        b = rng.random()
        val = int(60 + b * b * 195)          # bias toward dim stars
        blue = min(255, val + rng.randint(0, 20))
        col = (val, val, blue)
        d.point((x, y), fill=col)
        if b > 0.85:                         # a few brighter stars get a +
            d.point((x + 1, y), fill=(val // 2,) * 2 + (blue // 2,))
            d.point((x - 1, y), fill=(val // 2,) * 2 + (blue // 2,))
            d.point((x, y + 1), fill=(val // 2,) * 2 + (blue // 2,))
            d.point((x, y - 1), fill=(val // 2,) * 2 + (blue // 2,))
    return img


def generate(out_path, w=400, h=225):
    sx, sy = w / 400.0, h / 225.0
    s = min(sx, sy)

    # Layout (calibrated against the reference 400x225 capsule).
    big = ("NEWTONIA", 27 * sx, 45.3 * sx, 29.5 * s, 124 * sy, 3.4 * s)
    small = ("ONLINE", 266 * sx, 19.0 * sx, 10.5 * s, 152 * sy, 1.8 * s)

    stars = render_stars((w, h))
    glow = render_glow((w, h), [big, small])
    out = ImageChops.add(stars, glow)      # additive: glow sits over the field
    out.save(out_path)
    print("wrote %s (%dx%d)" % (out_path, w, h))


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'newtonia_online_announcement.png'
    W = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    H = int(sys.argv[3]) if len(sys.argv) > 3 else 225
    generate(out, W, H)
