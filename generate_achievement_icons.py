#!/usr/bin/env python3
"""Generate the Steam achievement icons (256x256 achieved + locked pairs).

Companion to generate_sounds.py: deterministic, procedural, and styled by
porting the game's own glyph construction so the icons stay on-theme —
  ship        GLShip line loop (glship.cpp), player blue (72,118,255)
  car         GLCar trapezoid (glcar.cpp), player-2 orange (255,69,0)
  enemy       same ship loop in GLEnemy green (glenemy.cpp)
  shield      single 20-segment circle, radius 2x ship, owner's colour
              (glship.cpp genForceShield); god shield is the yellow ring
  station     two concentric 30-segment banded rings (circle r + 0.9r +
              radial spokes), inner assembly at 0.8 scale (glstation.cpp)
  black hole  filled black disc + the minimap ring purple (black_hole.cpp)
  asteroid    9-vertex irregular polygon (asteroid.cpp)
  text        the Typer stroke font (typer.cpp), 1x2 glyph boxes
Output: steam/icons/<id>_achieved.png and <id>_locked.png for every
symbolic ID in ACHIEVEMENTS.md §5, plus preview contact sheets.

Requires Pillow:  pip3 install pillow
"""
import math
import os
import random
from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter

S, SIZE = 4, 256
W = SIZE * S
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "steam", "icons")

# In-game colours
P1BLUE = (72, 118, 255)     # glship.cpp
P2ORANGE = (255, 69, 0)     # glcar.cpp
ENEMYGREEN = (0, 255, 0)    # glenemy.cpp
GODYELLOW = (255, 255, 0)   # glship.cpp genGodShield
BHPURPLE = (153, 76, 255)   # black_hole.cpp minimap ring (0.6,0.3,1.0)
NOVA_BRIGHT = (255, 153, 26)  # glship.cpp draw_shockwaves bright ring (1,0.6,0.1)
NOVA_GLOW = (255, 77, 0)      # ...and its translucent glow ring (1,0.3,0)
WHITE = (235, 235, 245)
GOLD = (255, 240, 170)


class Canvas:
    def __init__(self, seed):
        random.seed(seed)
        self.base = Image.new("RGB", (W, W), (2, 2, 8))
        self.glow = Image.new("RGB", (W, W), (0, 0, 0))
        self.db = ImageDraw.Draw(self.base)
        self.dg = ImageDraw.Draw(self.glow)

    def line(self, pts, color, width, boost=1.6, glow=True):
        if glow:
            self.dg.line(pts, fill=tuple(min(255, int(c * boost)) for c in color),
                         width=width * 3, joint="curve")
        self.db.line(pts, fill=color, width=width, joint="curve")

    def outline(self, pts, color, width):
        self.line(list(pts) + [pts[0]], color, width)

    def disc(self, cx, cy, r, fill):
        self.db.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)

    def ring(self, cx, cy, r, color, width, boost=1.6, glow=True):
        box = [cx - r, cy - r, cx + r, cy + r]
        if glow:
            self.dg.ellipse(box, outline=tuple(min(255, int(c * boost)) for c in color),
                            width=width * 3)
        self.db.ellipse(box, outline=color, width=width)

    def arc(self, cx, cy, r, a0, a1, color, width, boost=1.6):
        box = [cx - r, cy - r, cx + r, cy + r]
        self.dg.arc(box, a0, a1, fill=tuple(min(255, int(c * boost)) for c in color),
                    width=width * 3)
        self.db.arc(box, a0, a1, fill=color, width=width)

    def stars(self, n=80, avoid=None):
        for _ in range(n):
            x, y = random.uniform(0, W), random.uniform(0, W)
            if avoid:
                ax, ay, ar_ = avoid
                if (x - ax) ** 2 + (y - ay) ** 2 < ar_ ** 2:
                    continue
            r = random.uniform(0.6, 1.6) * S
            c = random.choice([(180, 180, 200), (140, 160, 220), (200, 170, 170)])
            b = random.uniform(0.25, 0.7)
            self.db.ellipse([x - r, y - r, x + r, y + r],
                            fill=tuple(int(ch * b) for ch in c))

    def finish(self, name):
        glow = self.glow.filter(ImageFilter.GaussianBlur(radius=5 * S))
        img = ImageChops.screen(self.base, glow)
        achieved = img.resize((SIZE, SIZE), Image.LANCZOS)
        locked = ImageEnhance.Brightness(achieved.convert("L")).enhance(0.55).convert("RGB")
        achieved.save(os.path.join(OUT, f"{name}_achieved.png"))
        locked.save(os.path.join(OUT, f"{name}_locked.png"))
        return achieved, locked


def rot(px, py, a):
    return (px * math.cos(a) - py * math.sin(a),
            px * math.sin(a) + py * math.cos(a))


# ── glyphs ported from the game meshes (game Y-up -> image Y-down) ──────────

def _place(shape, cx, cy, size, heading):
    # heading: image-space angle the ship nose points toward (0 = +x,
    # increasing clockwise on screen). Game shapes are authored nose-up
    # (+y, game Y-up): the Y-flip puts the nose at image angle -pi/2, so
    # rotating by heading + pi/2 lands it on the requested heading.
    a = heading + math.pi / 2
    return [(cx + rot(px * size, -py * size, a)[0],
             cy + rot(px * size, -py * size, a)[1]) for px, py in shape]


SHIP_LOOP = [(0.0, 1.0), (-0.8, -1.0), (0.0, -0.5), (0.8, -1.0)]   # glship.cpp
CAR_LOOP = [(0.35, 1.0), (-0.35, 1.0), (-0.8, -1.0), (0.8, -1.0)]  # glcar.cpp


def ship(c, cx, cy, size, heading, color=P1BLUE, trail=True):
    c.outline(_place(SHIP_LOOP, cx, cy, size, heading), color, int(2.8 * S))
    if trail:
        for k in range(3):
            o0 = rot((1.7 + k * 0.75) * size, 0, heading + math.pi)
            o1 = rot((2.1 + k * 0.75) * size, 0, heading + math.pi)
            a = 210 - k * 55
            c.line([(cx + o0[0], cy + o0[1]), (cx + o1[0], cy + o1[1])],
                   (a // 2, a // 2, a), int((2.1 - k * 0.5) * S), boost=1.3)


def car(c, cx, cy, size, heading, color=P2ORANGE, trail=True):
    c.outline(_place(CAR_LOOP, cx, cy, size, heading), color, int(2.8 * S))
    if trail:
        # the GLCar thrusts from two jets at the hull corners: twin dash trails
        perp = (math.cos(heading + math.pi / 2), math.sin(heading + math.pi / 2))
        for side in (-1, 1):
            ox, oy = perp[0] * 0.45 * size * side, perp[1] * 0.45 * size * side
            for k in range(3):
                o0 = rot((1.7 + k * 0.75) * size, 0, heading + math.pi)
                o1 = rot((2.1 + k * 0.75) * size, 0, heading + math.pi)
                a = 210 - k * 55
                c.line([(cx + ox + o0[0], cy + oy + o0[1]),
                        (cx + ox + o1[0], cy + oy + o1[1])],
                       (a // 2, a // 4, a // 8), int((1.6 - k * 0.4) * S), boost=1.3)


def shield(c, cx, cy, ship_size, color=P1BLUE):
    # genForceShield: one 20-segment circle at 2x ship scale, owner's colour
    c.ring(cx, cy, ship_size * 2.0, color, int(2.4 * S))


def asteroid(c, cx, cy, r, color=WHITE, width=2.4, shape=None, fill=None):
    shape = shape or [(200, 1.0), (240, 0.86), (275, 1.04), (312, 0.92),
                      (350, 1.08), (25, 0.95), (65, 1.02), (110, 0.88), (155, 0.97)]
    verts = [(cx + r * f * math.cos(math.radians(a)),
              cy + r * f * math.sin(math.radians(a))) for a, f in shape]
    if fill is not None:
        c.db.polygon(verts, fill=fill)
    c.outline(verts, color, int(width * S))
    return verts


def station_band(c, cx, cy, r, rot_deg, skip=None, width=2.0, segments=30,
                 inner=0.9):
    """One glstation.cpp ring assembly: circles at r and inner*r + radial
    spokes every 12 degrees. skip=(a0,a1) leaves a destroyed sector out.
    In-game inner is 0.9; a smaller value mimics the visual mass of the
    two nested assemblies when an icon draws only one band."""
    r2 = r * inner
    seg = 360.0 / segments

    def kept(a):
        if not skip:
            return True
        d = (a - skip[0]) % 360
        return d > (skip[1] - skip[0]) % 360

    for rr in (r, r2):
        a = 0.0
        while a < 360.0:
            if kept(a + rot_deg):
                c.arc(cx, cy, rr, a + rot_deg, a + rot_deg + seg, WHITE,
                      int(width * S), boost=1.4)
            a += seg
    for i in range(segments):
        a = math.radians(i * seg + rot_deg)
        if not kept(math.degrees(a)):
            continue
        c.line([(cx + r * math.cos(a), cy + r * math.sin(a)),
                (cx + r2 * math.cos(a), cy + r2 * math.sin(a))],
               WHITE, int(width * 0.8 * S), boost=1.3)


def station(c, cx, cy, r, skip=None):
    station_band(c, cx, cy, r, 0.0, skip)              # outer assembly
    station_band(c, cx, cy, r * 0.8, 6.0, skip, 1.7)   # inner at 0.8 scale


def sparks(c, ex, ey, heading, n=9, scale=1.0):
    for i in range(n):
        a = heading + (i - n // 2) * 0.26
        l0, l1 = 0.45 * scale, 0.45 * scale + (1.9 - abs(i - n // 2) * 0.3) * scale
        col = GOLD if i % 2 == 0 else (190, 255, 255)
        c.line([(ex + l0 * math.cos(a), ey + l0 * math.sin(a)),
                (ex + l1 * math.cos(a), ey + l1 * math.sin(a))],
               col, int(1.5 * S), boost=1.8)


def mine(c, cx, cy, size, color=WHITE, angle=0.3):
    # mines are spinning square particles in-game
    pts = [(cx + rot(x * size, y * size, angle)[0],
            cy + rot(x * size, y * size, angle)[1])
           for x, y in [(-1, -1), (1, -1), (1, 1), (-1, 1)]]
    c.outline(pts, color, int(2.0 * S))


def missile_pickup_glyph(c, cx, cy, d, color=(51, 204, 255)):
    # missile_pickup.cpp (netplay branch): finned rocket, nose up, exhaust
    # dash below (game Y-up flipped to image Y-down)
    body = [(0, -d), (0.3 * d, -0.45 * d), (0.3 * d, 0.55 * d),
            (-0.3 * d, 0.55 * d), (-0.3 * d, -0.45 * d)]
    c.outline([(cx + x, cy + y) for x, y in body], color, int(1.6 * S))
    for sx in (-1, 1):
        fin = [(sx * 0.3 * d, 0.15 * d), (sx * 0.75 * d, 0.75 * d),
               (sx * 0.3 * d, 0.55 * d)]
        c.line([(cx + x, cy + y) for x, y in fin], color, int(1.6 * S))
    c.line([(cx, cy + 0.65 * d), (cx, cy + 0.95 * d)], color, int(1.6 * S))


def shield_pickup_glyph(c, cx, cy, d, color=(204, 51, 255)):
    # shield_pickup.cpp (netplay branch): three 80-degree bubble arcs every
    # 120 degrees around a small hexagonal core
    for arc in range(3):
        pts = []
        for i in range(7):
            a = math.radians(arc * 120 + i * 80 / 6)
            pts.append((cx + math.cos(a) * 0.95 * d, cy - math.sin(a) * 0.95 * d))
        c.line(pts, color, int(1.6 * S))
    hexpts = [(cx + math.cos(math.radians(60 * i)) * 0.16 * d,
               cy - math.sin(math.radians(60 * i)) * 0.16 * d) for i in range(6)]
    c.outline(hexpts, color, int(1.4 * S))


def mine_pickup_glyph(c, cx, cy, d, color=(255, 128, 0)):
    # mine_pickup.cpp (netplay branch): rotated diamond with a cross inside
    c.outline([(cx, cy - d), (cx + 0.9 * d, cy), (cx, cy + d), (cx - 0.9 * d, cy)],
              color, int(1.6 * S))
    c.line([(cx - 0.45 * d, cy), (cx + 0.45 * d, cy)], color, int(1.4 * S))
    c.line([(cx, cy - 0.5 * d), (cx, cy + 0.5 * d)], color, int(1.4 * S))


def giga_mine_pickup_glyph(c, cx, cy, d, color=(153, 0, 255)):
    # giga_mine_pickup.cpp (netplay branch): the mine glyph ringed by a blast circle
    mine_pickup_glyph(c, cx, cy, d * 0.72, color)
    c.ring(cx, cy, d * 1.05, color, int(1.4 * S))


def weapon_pickup_glyph(c, cx, cy, d, color=(0, 255, 0)):
    # weapon_pickup.cpp (netplay branch): a crosshair — circle with four ticks
    c.ring(cx, cy, 0.55 * d, color, int(1.6 * S))
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        c.line([(cx + dx * 0.3 * d, cy + dy * 0.3 * d),
                (cx + dx * 0.95 * d, cy + dy * 0.95 * d)], color, int(1.6 * S))


def god_mode_pickup_glyph(c, cx, cy, d, color=(255, 230, 0)):
    # god_mode_pickup.cpp: the lightning bolt (game Y-up flipped)
    pts = [(0.2, 1.0), (0.6, 1.0), (0.1, 0.1), (0.5, 0.1),
           (-0.2, -1.0), (-0.6, -1.0), (-0.1, -0.1), (-0.5, -0.1)]
    c.outline([(cx + x * d, cy - y * d) for x, y in pts], color, int(1.6 * S))


def nova_charge_pickup_glyph(c, cx, cy, d, color=NOVA_BRIGHT):
    # nova_charge_pickup.cpp (netplay branch): ring with radiating spokes
    c.ring(cx, cy, 0.7 * d, color, int(1.6 * S))
    for i in range(8):
        a = 2 * math.pi * i / 8
        c.line([(cx + 0.78 * d * math.cos(a), cy + 0.78 * d * math.sin(a)),
                (cx + d * math.cos(a), cy + d * math.sin(a))], color, int(1.4 * S))


def missile(c, cx, cy, size, heading, color=P1BLUE):
    # glship.cpp missile_body: triangle loop (0,1),(-0.5,-1),(0.5,-1)
    c.outline(_place([(0, 1), (-0.5, -1), (0.5, -1)], cx, cy, size, heading),
              color, int(2.2 * S))


# Typer stroke font (typer.cpp): glyph box 1 wide x 2 high, Y-up.
TH, TM, TMW, TC = 2.0, 1.2, 0.67, 0.5
TYPER = {
    '0': [[(0, TH), (1, TH), (1, 0), (0, 0), (0, TH)], [(1, TH), (0, 0)]],
    '1': [[(TMW, TH), (TMW, 0)]],
    '3': [[(0, TH), (1, TH), (1, 0), (0, 0)], [(0, TM), (1, TM)]],
    '5': [[(1, TH), (0, TH), (0, TM), (1, TM), (1, 0), (0, 0)]],
    '7': [[(0, TH), (1, TH), (1, 0)]],
    'K': [[(0, 0), (0, TH)], [(0, TM), (1, TM)], [(1, TM), (1, 0)], [(TMW, TM), (1, TH)]],
    'M': [[(0, 0), (0, TH), (1, TH), (1, 0)], [(TC, TH), (TC, TM)]],
}


def text(c, cx, cy, string, size, color=WHITE, width=2.6, spacing=1.55):
    total = (len(string) - 1) * spacing * size
    x0 = cx - total / 2 - size * 0.5
    for i, ch in enumerate(string):
        strokes = TYPER[ch]
        gx = x0 + i * spacing * size
        for stroke in strokes:
            pts = [(gx + px * size, cy + TH / 2 * size - py * size) for px, py in stroke]
            if len(pts) == 2:
                c.line(pts, color, int(width * S))
            else:
                c.line(pts, color, int(width * S))


# ── scenes, one per §5 symbolic ID ───────────────────────────────────────────

def scene_first_kill():
    c = Canvas(12)
    c.stars()
    acx, acy, ar = W * 0.63, W * 0.37, W * 0.20
    for da, dd, a0, r in [(35, 1.00, 0.2, 0.58), (145, 1.05, 1.0, 0.46),
                          (215, 0.95, 2.1, 0.5), (320, 1.1, 0.6, 0.44)]:
        cx = acx + ar * dd * math.cos(math.radians(da))
        cy = acy + ar * dd * math.sin(math.radians(da))
        piece = [(cx + ar * r * (1.2 if i % 2 else 0.75) * math.cos(2 * math.pi * i / 5 + a0),
                  cy + ar * r * (1.2 if i % 2 else 0.75) * math.sin(2 * math.pi * i / 5 + a0))
                 for i in range(5)]
        c.outline(piece, WHITE, int(2.0 * S))
    sparks(c, acx, acy, math.radians(-45), n=11, scale=W * 0.05)
    scx, scy, ss = W * 0.24, W * 0.76, W * 0.075
    heading = math.atan2(acy - scy, acx - scx)
    ship(c, scx, scy, ss, heading)
    for k in range(4):
        t0 = 2.2 + k * 1.5
        c.line([(scx + t0 * ss * math.cos(heading), scy + t0 * ss * math.sin(heading)),
                (scx + (t0 + 0.55) * ss * math.cos(heading), scy + (t0 + 0.55) * ss * math.sin(heading))],
               GOLD, int(2.0 * S), boost=1.8)
    return c.finish("first_kill")


def scene_clear_level1():
    # Clear skies: nothing left but the ship, centred in clean space,
    # thruster dashes only.
    c = Canvas(41)
    c.stars(n=110)
    ship(c, W * 0.5, W * 0.5, W * 0.10, math.radians(-38))
    return c.finish("clear_level1")


def scene_specials_7():
    # Seven special types in a ring, using the in-game per-type visuals
    # (asteroid_drawer.cpp): reflective/phased/armour cyan (0.3,0.9,1.0),
    # quantum purple, teleport = interior triangle arrow at 0.45r, tough
    # cracks kinked from a rim vertex halfway in (never meeting centre).
    c = Canvas(77)
    c.stars(n=60)
    CYAN = (77, 230, 255)
    QUANTUM = (128, 26, 204)
    cx, cy, R = W * 0.5, W * 0.5, W * 0.335
    small = W * 0.085
    for i in range(7):
        a = 2 * math.pi * i / 7 - math.pi / 2
        px, py = cx + R * math.cos(a), cy + R * math.sin(a)
        if i == 0:    # reflective: cyan outline over teal fill
            asteroid(c, px, py, small, CYAN, 1.8, fill=(0, 82, 102))
        elif i == 1:  # teleporting: black-filled, triangle arrow inside
            asteroid(c, px, py, small, WHITE, 1.8, fill=(4, 4, 10))
            ta = a + 0.6
            ri = small * 0.55
            tip = (px + ri * math.cos(ta), py + ri * math.sin(ta))
            base_r = ri * 0.55
            back, perp = ta + math.pi, ta + math.pi / 2
            bl = (px + base_r * math.cos(back) + base_r * 0.6 * math.cos(perp),
                  py + base_r * math.sin(back) + base_r * 0.6 * math.sin(perp))
            br = (px + base_r * math.cos(back) - base_r * 0.6 * math.cos(perp),
                  py + base_r * math.sin(back) - base_r * 0.6 * math.sin(perp))
            c.outline([tip, bl, br], WHITE, int(1.4 * S))
        elif i == 2:  # invisible: no outline in-game; faint dotted hint
            for j in range(9):
                aa = 2 * math.pi * j / 9
                dx, dy = px + small * math.cos(aa), py + small * math.sin(aa)
                c.db.ellipse([dx - S, dy - S, dx + S, dy + S], fill=(120, 120, 135))
        elif i == 3:  # quantum (observed): purple outline over dark purple fill
            asteroid(c, px, py, small, (166, 26, 255), 1.8, fill=(33, 0, 76))
        elif i == 4:  # tough: black-filled, cracks kinked inward from rim vertices
            v = asteroid(c, px, py, small, WHITE, 1.8, fill=(4, 4, 10))
            for vi, sgn in ((0, 1), (4, -1), (7, 1)):
                vx, vy = v[vi]
                mid = (px + (vx - px) * 0.55 + sgn * (vy - py) * 0.18,
                       py + (vy - py) * 0.55 - sgn * (vx - px) * 0.18)
                end = (px + (vx - px) * 0.45, py + (vy - py) * 0.45)
                c.line([(vx, vy), mid, end], (178, 178, 178), int(1.2 * S), boost=1.2)
        elif i == 5:  # armoured: the asteroid's own edges at 1.18 scale form
                      # the armour plate over the shielded face (asteroid_drawer)
            v = asteroid(c, px, py, small, WHITE, 1.8, fill=(4, 4, 10))
            fa_ = a + math.pi               # armour facing outward from ring
            adx, ady = math.cos(fa_), math.sin(fa_)
            plate = [(px + (vx - px) * 1.18, py + (vy - py) * 1.18) for vx, vy in v]
            n = len(plate)
            for j in range(n):
                k = (j + 1) % n
                ax_, ay_ = plate[j][0] - px, plate[j][1] - py
                bx_, by_ = plate[k][0] - px, plate[k][1] - py
                la = math.hypot(ax_, ay_)
                lb = math.hypot(bx_, by_)
                a_in = (ax_ * adx + ay_ * ady) > -0.5 * la   # within +/-120 deg
                b_in = (bx_ * adx + by_ * ady) > -0.5 * lb
                if a_in and b_in:
                    c.line([plate[j], plate[k]], CYAN, int(2.6 * S))
        else:         # phasing: asteroid outline half solid white, half ghost cyan
            shape = [(200, 1.0), (240, 0.86), (275, 1.04), (312, 0.92),
                     (350, 1.08), (25, 0.95), (65, 1.02), (110, 0.88), (155, 0.97)]
            v = [(px + small * f * math.cos(math.radians(ang)),
                  py + small * f * math.sin(math.radians(ang))) for ang, f in shape]
            c.db.polygon(v, fill=(0, 45, 56))  # ghost teal fill, mid-phase
            n = len(v)
            for j in range(n):
                k = (j + 1) % n
                mx, my = (v[j][0] + v[k][0]) / 2 - px, (v[j][1] + v[k][1]) / 2 - py
                if mx >= 0:   # right half mid-phase ghost, left half solid
                    c.line([v[j], v[k]], (46, 138, 153), int(1.6 * S), boost=1.2)
                else:
                    c.line([v[j], v[k]], WHITE, int(1.8 * S))
    text(c, cx, cy, "7", W * 0.09, GOLD, width=3.0)
    return c.finish("specials_7")


def scene_black_hole_survivor():
    c = Canvas(9)
    cx, cy = W * 0.56, W * 0.44
    c.stars(avoid=(cx, cy, W * 0.30))
    hr = W * 0.17
    c.disc(cx, cy, hr, (0, 0, 0))
    c.ring(cx, cy, hr, BHPURPLE, int(3.0 * S), boost=1.8)  # minimap-ring purple
    for rr, a0, sweep, col in [(hr * 1.7, 300, 150, (120, 110, 220)),
                               (hr * 2.3, 120, 130, (100, 100, 200)),
                               (hr * 3.0, 240, 100, (80, 90, 170))]:
        c.arc(cx, cy, rr, a0, a0 + sweep, col, int(1.6 * S), boost=1.3)
    orbit_r = hr * 2.3
    sa = math.radians(97)    # bottom-middle of the hole's influence radius
    sx, sy = cx + orbit_r * math.cos(sa), cy + orbit_r * math.sin(sa)
    ship(c, sx, sy, W * 0.062, sa - math.pi / 2 - 0.25)
    c.arc(cx, cy, orbit_r, math.degrees(sa) + 12, math.degrees(sa) + 85,
          (70, 110, 200), int(1.8 * S), boost=1.3)
    return c.finish("black_hole_survivor")


def scene_mini_station_kill():
    c = Canvas(50)
    c.stars()
    mx, my, mr = W * 0.60, W * 0.40, W * 0.20
    # single band: reads cleaner at icon size and distinguishes the mini
    # from the big station's two-band icon. The breach faces the incoming
    # shot (ship approaches from lower-left).
    station_band(c, mx, my, mr, 0.0, skip=(107, 167), inner=0.78)
    bang = math.radians(137)
    sparks(c, mx + mr * 0.95 * math.cos(bang), my + mr * 0.95 * math.sin(bang),
           bang, n=11, scale=W * 0.04)
    scx, scy, ss = W * 0.22, W * 0.76, W * 0.07
    heading = math.atan2(my + mr * math.sin(bang) - scy, mx + mr * math.cos(bang) - scx)
    ship(c, scx, scy, ss, heading)
    for k in range(2):  # dashes stop short of the breach
        t0 = 2.4 + k * 1.6
        c.line([(scx + t0 * ss * math.cos(heading), scy + t0 * ss * math.sin(heading)),
                (scx + (t0 + 0.5) * ss * math.cos(heading), scy + (t0 + 0.5) * ss * math.sin(heading))],
               GOLD, int(1.9 * S), boost=1.8)
    return c.finish("mini_station_kill")


def scene_shield_ram():
    c = Canvas(61)
    c.stars()
    # enemy ship intact, knocked tumbling away up-right from the impact
    ex, ey, es = W * 0.66, W * 0.34, W * 0.085
    c.outline(_place(SHIP_LOOP, ex, ey, es, math.radians(20)), ENEMYGREEN,
              int(2.6 * S))
    # shielded player driving in
    scx, scy, ss = W * 0.30, W * 0.70, W * 0.08
    heading = math.atan2(ey - scy, ex - scx)
    ship(c, scx, scy, ss, heading)
    shield(c, scx, scy, ss)
    a_mid = math.degrees(heading)
    c.arc(scx, scy, ss * 2.0, a_mid - 30, a_mid + 30, (230, 240, 255), int(3.2 * S), boost=1.8)
    sparks(c, scx + ss * 2.6 * math.cos(heading), scy + ss * 2.6 * math.sin(heading),
           heading, n=9, scale=ss * 0.7)
    return c.finish("shield_ram")


def scene_shield_ram_asteroid():
    c = Canvas(3)
    c.stars()
    acx, acy, ar = W * 0.615, W * 0.385, W * 0.265
    verts = asteroid(c, acx, acy, ar)
    impact = verts[0]
    main = [impact, (acx - ar * 0.42, acy + ar * 0.18), (acx - ar * 0.05, acy - ar * 0.02),
            (acx + ar * 0.32, acy - ar * 0.30), (acx + ar * 0.58, acy - ar * 0.38)]
    c.line(main, (205, 205, 218), int(1.5 * S), boost=1.2)
    c.line([main[1], (acx - ar * 0.30, acy + ar * 0.52), (acx - ar * 0.12, acy + ar * 0.66)],
           (185, 185, 200), int(1.2 * S), boost=1.2)
    for cx, cy, r, a0 in [(W * 0.335, W * 0.535, W * 0.042, 0.3),
                          (W * 0.245, W * 0.415, W * 0.028, 1.1)]:
        shard = [(cx + r * (1.25 if i % 2 else 0.8) * math.cos(2 * math.pi * i / 5 + a0),
                  cy + r * (1.25 if i % 2 else 0.8) * math.sin(2 * math.pi * i / 5 + a0))
                 for i in range(5)]
        c.outline(shard, (210, 210, 222), int(1.7 * S))
    scx, scy, ss = W * 0.295, W * 0.715, W * 0.075
    heading = math.atan2(impact[1] - scy, impact[0] - scx)
    ship(c, scx, scy, ss, heading)
    shield(c, scx, scy, ss)
    a_mid = math.degrees(heading)
    c.arc(scx, scy, ss * 2.0, a_mid - 38, a_mid + 38, (230, 240, 255), int(4.5 * S), boost=2.0)
    sparks(c, scx + ss * 2 * math.cos(heading), scy + ss * 2 * math.sin(heading),
           heading, scale=ss)
    return c.finish("shield_ram_asteroid")


def scene_station_destroyed():
    c = Canvas(21)
    c.stars()
    cx, cy = W * 0.5, W * 0.47
    station(c, cx, cy, W * 0.30, skip=(-55, 10))
    bang = math.radians(-22)
    br = W * 0.30 * 0.95
    sparks(c, cx + br * math.cos(bang), cy + br * math.sin(bang), bang,
           n=13, scale=W * 0.05)
    for k in range(5):
        a = bang + random.uniform(-0.6, 0.6)
        d = W * 0.30 * random.uniform(1.2, 1.65)
        mine(c, cx + d * math.cos(a), cy + d * math.sin(a),
             W * random.uniform(0.01, 0.02), (215, 215, 225), random.uniform(0, 1.5))
    return c.finish("station_destroyed")


def scene_enemies_10():
    c = Canvas(83)
    c.stars(n=60)
    es = W * 0.055
    for row in range(2):
        for col in range(5):
            px = W * (0.20 + col * 0.15)
            py = W * (0.30 + row * 0.24)
            pts = _place(SHIP_LOOP, px, py, es, math.radians(90))
            c.outline(pts, ENEMYGREEN, int(2.2 * S))
    scx, scy, ss = W * 0.5, W * 0.80, W * 0.075
    ship(c, scx, scy, ss, math.radians(-90))
    return c.finish("enemies_10")


def scene_nova_detonated():
    # The in-game nova shockwave (glship.cpp draw_shockwaves): the bright
    # orange expanding ring with its slightly larger translucent glow ring,
    # ship at the epicentre. Nothing else.
    c = Canvas(30)
    cx, cy = W * 0.5, W * 0.5
    c.stars()
    swr = W * 0.34
    c.ring(cx, cy, swr, NOVA_BRIGHT, int(3.6 * S), boost=1.15)        # bright ring
    c.ring(cx, cy, swr * 1.06, NOVA_GLOW, int(1.8 * S), boost=1.25)   # glow ring
    ship(c, cx, cy, W * 0.06, math.radians(-90), trail=False)
    return c.finish("nova_detonated")


def scene_no_damage_clear():
    c = Canvas(55)
    c.stars()
    # pristine ship threading straight up between two asteroids whose motion
    # streaks pass close on either side — untouched
    scx, scy, ss = W * 0.50, W * 0.60, W * 0.085
    heading = math.radians(-90)
    ship(c, scx, scy, ss, heading)
    # extend the standard thruster dashes into a longer dashed run
    for k in range(3, 6):
        y0 = scy + (1.7 + k * 0.75) * ss
        y1 = scy + (2.1 + k * 0.75) * ss
        a = max(40, 210 - k * 40)
        c.line([(scx, y0), (scx, y1)], (a // 2, a // 2, a), int(1.1 * S), boost=1.3)
    for side in (-1, 1):
        px, py = scx + side * W * 0.235, W * 0.34
        asteroid(c, px, py, W * 0.115, WHITE, 2.0, fill=(4, 4, 10))
    return c.finish("no_damage_clear")


def scene_no_secondary_level10():
    c = Canvas(66)
    c.stars()
    # ship firing plain bullets: short line segments in the ship's colour,
    # exactly as GLShip::draw_particles renders them
    scx, scy, ss = W * 0.32, W * 0.42, W * 0.085
    heading = math.radians(-15)
    ship(c, scx, scy, ss, heading)
    for k in range(4):
        t0 = 2.2 + k * 1.4
        b0 = (scx + t0 * ss * math.cos(heading), scy + t0 * ss * math.sin(heading))
        b1 = (scx + (t0 + 0.45) * ss * math.cos(heading), scy + (t0 + 0.45) * ss * math.sin(heading))
        c.line([b0, b1], P1BLUE, int(2.0 * S), boost=1.4)
    # secondaries struck out below: the in-game pickup glyphs (netplay
    # branch line art) behind glow-free red no-rings — interiors stay black
    for i in range(2):
        px, py = W * (0.38 + i * 0.26), W * 0.76
        pr = W * 0.085
        if i == 0:
            missile_pickup_glyph(c, px, py, pr * 0.62)
        else:
            shield_pickup_glyph(c, px, py, pr * 0.62)
        c.ring(px, py, pr, (255, 80, 80), int(2.6 * S), glow=False)
        d = pr / math.sqrt(2)
        c.line([(px - d, py - d), (px + d, py + d)], (255, 80, 80), int(2.6 * S), glow=False)
    return c.finish("no_secondary_level10")


def scene_weapons_7():
    # The seven weapon kinds as their in-game pickup icons (netplay branch
    # line art), ringed around the ship.
    c = Canvas(70)
    c.stars(n=60)
    cx, cy = W * 0.5, W * 0.54
    ship(c, cx, cy, W * 0.07, math.radians(-90), trail=False)
    R = W * 0.34
    d = W * 0.052
    glyphs = [weapon_pickup_glyph, god_mode_pickup_glyph, mine_pickup_glyph,
              giga_mine_pickup_glyph, missile_pickup_glyph,
              shield_pickup_glyph, nova_charge_pickup_glyph]
    for i, glyph in enumerate(glyphs):
        a = -math.pi / 2 + 2 * math.pi * i / 7
        glyph(c, cx + R * math.cos(a), cy + R * math.sin(a), d)
    return c.finish("weapons_7")


def scene_coop_clear():
    # Two-player mirror of clear_level1: both ships cruising through
    # cleared space in formation, thruster dashes only.
    c = Canvas(90)
    c.stars(n=110)
    heading = math.radians(-38)
    ship(c, W * 0.38, W * 0.42, W * 0.09, heading)
    car(c, W * 0.62, W * 0.62, W * 0.09, heading)
    return c.finish("coop_clear")


def _kill_count_scene(seed, label, label_color):
    c = Canvas(seed)
    c.stars(n=70)
    cx, cy = W * 0.5, W * 0.47
    # ring of shattering asteroid pieces around the number
    for i in range(8):
        a = 2 * math.pi * i / 8 + 0.3
        d = W * random.uniform(0.32, 0.40)
        px, py = cx + d * math.cos(a), cy + d * math.sin(a)
        sz = W * random.uniform(0.03, 0.055)
        shard = [(px + sz * (1.2 if j % 2 else 0.7) * math.cos(2 * math.pi * j / 5 + i),
                  py + sz * (1.2 if j % 2 else 0.7) * math.sin(2 * math.pi * j / 5 + i))
                 for j in range(5)]
        c.outline(shard, (190, 190, 205), int(1.6 * S))
    text(c, cx, cy, label, W * 0.085, label_color, width=3.0)
    return c


def scene_kills_1000():
    c = _kill_count_scene(100, "1000", WHITE)
    return c.finish("kills_1000")


def scene_kills_10000_lifetime():
    c = _kill_count_scene(101, "10K", (255, 180, 10))  # rich gold, not spark-yellow
    return c.finish("kills_10000_lifetime")


def scene_score_3m():
    c = Canvas(102)
    c.stars(n=80)
    cx, cy = W * 0.5, W * 0.47
    text(c, cx, cy, "3M", W * 0.135, GOLD, width=3.4)
    # score sparkle bursts at the corners of the numerals
    for px, py in [(0.30, 0.30), (0.72, 0.62), (0.66, 0.28)]:
        sparks(c, W * px, W * py, math.radians(random.uniform(0, 360)),
               n=7, scale=W * 0.022)
    return c.finish("score_3m")


def scene_reach_level15():
    c = Canvas(103)
    c.stars(n=110)
    # the station small and distant, upper right — the journey's landmark
    station(c, W * 0.74, W * 0.26, W * 0.11)
    # ship far lower-left, long trail toward it
    scx, scy, ss = W * 0.28, W * 0.72, W * 0.07
    heading = math.atan2(W * 0.26 - scy, W * 0.74 - scx)
    trail = [(scx - t * ss * math.cos(heading), scy - t * ss * math.sin(heading))
             for t in range(2, 9)]
    c.line(trail, (60, 70, 140), int(2.2 * S), boost=1.3)
    ship(c, scx, scy, ss, heading)
    text(c, W * 0.24, W * 0.28, "15", W * 0.07, WHITE, width=2.6)
    return c.finish("reach_level15")


SCENES = [
    scene_first_kill, scene_clear_level1, scene_specials_7,
    scene_black_hole_survivor, scene_mini_station_kill, scene_shield_ram,
    scene_shield_ram_asteroid, scene_station_destroyed, scene_enemies_10,
    scene_nova_detonated, scene_no_damage_clear, scene_no_secondary_level10,
    scene_weapons_7, scene_coop_clear, scene_kills_1000,
    scene_kills_10000_lifetime, scene_score_3m, scene_reach_level15,
]


def main():
    os.makedirs(OUT, exist_ok=True)
    pairs = [f() for f in SCENES]
    # contact sheets (achieved + locked, 6 per row) at 256 and at Steam's 64
    for px, name in [(SIZE, "sheet_256.png"), (64, "sheet_64.png")]:
        cols = 6
        rows = math.ceil(len(pairs) * 2 / cols)
        sheet = Image.new("RGB", (px * cols + 8 * (cols + 1),
                                  px * rows + 8 * (rows + 1)), (24, 24, 28))
        cells = [p[0] for p in pairs] + [p[1] for p in pairs]
        for i, im in enumerate(cells):
            r_, c_ = divmod(i, cols)
            sheet.paste(im.resize((px, px), Image.LANCZOS),
                        (8 + c_ * (px + 8), 8 + r_ * (px + 8)))
        sheet.save(os.path.join(OUT, name))
    print(f"{len(pairs)} icon pairs -> {OUT}")


if __name__ == "__main__":
    main()
