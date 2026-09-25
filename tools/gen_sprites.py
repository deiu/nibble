"""Generates sprites.h: the bunny and its icons as shapes, not as pixels.

The panel is a 466 px AMOLED, so the art is drawn rather than pixelated. A
frame is a short list of two primitives in a 32x32 unit space:

    ELL   an ellipse, optionally sheared sideways (that is how an ear leans)
    CONE  a capsule with a radius at each end, so it tapers (a carrot, a
          whisker, a mouth stroke)

Each shape is added to the fur or cut out of it, in order. Both rasterisers,
art.c on the board and mock_screen.py on the Mac, turn the list into an 8 bit
coverage mask with smooth edges at whatever size they need, so the same art is
sharp at 288 px and would be sharp at 2880.

Every part of the bunny is placed from the body ellipse, so a growth stage is
only a body size and an ear size. The STAGES table holds all three.
"""
import math

W = H = 32                      # the bunny's unit square
IW = IH = 16                    # an icon's
FRAMES = ['IDLE_A', 'IDLE_B', 'EAT', 'PLAY', 'SLEEP', 'HUNGRY', 'BORED', 'DIRTY']
STAGES = [
    # name,    body cx, cy,  rx,   ry,  ear scale, ear lean at rest
    ('BABY',    15.5, 22.0,  7.5,  6.5,  0.65,  0.45),   # a kit: small, ears short and out
    ('YOUNG',   15.5, 21.0,  9.0,  7.5,  0.70,  0.15),
    ('ADULT',   15.5, 20.0, 10.5,  8.5,  1.00,  0.00),
]

# --- the two primitives ------------------------------------------------------
def ELL(cx, cy, rx, ry, lean=0.0, cut=0):
    """An ellipse. lean shears it sideways: 0 stands up, 1 flops outward."""
    return ('ELL', cut, cx, cy, rx, ry, lean, 0.0)

def CONE(x0, y0, r0, x1, y1, r1, cut=0):
    """A stroke with a round cap of r0 at one end and r1 at the other."""
    return ('CONE', cut, x0, y0, r0, x1, y1, r1)

def BAR(x0, x1, y, r, cut=0):
    return CONE(x0, y, r, x1, y, r, cut)

def arc(s, p0, p1, p2, r, n=8, cut=1):
    """A bend, as n straight capsules along a quadratic through the pull point
    p1. Eight of them at this size leave no corner an eye can find."""
    pts = []
    for i in range(n + 1):
        t = i / float(n)
        u = 1.0 - t
        pts.append((u * u * p0[0] + 2 * t * u * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * t * u * p1[1] + t * t * p2[1]))
    for a, b in zip(pts, pts[1:]):
        s.append(CONE(a[0], a[1], r, b[0], b[1], r, cut))

# The gauges draw a hairline notch 2 px wide (NOTCH_W in user_app.cpp) and the
# art is drawn at 9 px to the unit (SCALE), so this is that same hairline in
# the art's own units. It is the gap between the two front teeth.
NOTCH = 2.0 / 9.0

# The mouth is the same size at every growth stage, in art units rather than in
# fractions of the body. A kit's body is two thirds of an adult's, and a mouth
# scaled down with it is a line nobody reads from across the room. Young animals
# wear their features large anyway, which is most of what makes them look young.
# One shape serves both faces: the ends go up and the middle down for a smile,
# and the other way round for a frown.
MOUTH_W   = 3.5                                 # half the width of the mouth
MOUTH_END = 1.6                                 # how far the ends sit off the line
MOUTH_MID = 2.4                                 # and the middle, the other way
MOUTH_R   = 0.42                                # the stroke, about 7 px at SCALE 9

# --- rasteriser, the same arithmetic as art.c --------------------------------
def _sd_ell(x, y, cx, cy, rx, ry, lean):
    if rx <= 0.0 or ry <= 0.0:
        return 1e9                                  # no width is nothing to draw
    u = (x - lean * (cy - y) - cx) / rx
    v = (y - cy) / ry
    f = u * u + v * v
    gx = 2.0 * u / rx
    gy = 2.0 * (u * lean / rx + v / ry)
    g = math.sqrt(gx * gx + gy * gy)
    return (f - 1.0) / g if g > 1e-6 else -1e9      # dead centre: solidly inside

def _sd_cone(x, y, ax, ay, r1, bx, by, r2):
    bax, bay = bx - ax, by - ay
    l2 = bax * bax + bay * bay
    if l2 < 1e-9:
        return math.hypot(x - ax, y - ay) - max(r1, r2)
    rr, il2 = r1 - r2, 1.0 / l2
    a2 = l2 - rr * rr
    pax, pay = x - ax, y - ay
    t = pax * bax + pay * bay
    z = t - l2
    qx, qy = pax * l2 - bax * t, pay * l2 - bay * t
    x2 = qx * qx + qy * qy
    k = (rr if rr >= 0.0 else -rr) * rr * x2
    if (a2 if z > 0.0 else -a2) * (z * z * l2) > k:
        return math.sqrt(x2 + z * z * l2) * il2 - r2
    if (a2 if t > 0.0 else -a2) * (t * t * l2) < k:
        return math.sqrt(x2 + t * t * l2) * il2 - r1
    return (math.sqrt(x2 * a2 * il2) + t * rr) * il2 - r1

def _bbox(sh, units_w, units_h, k):
    kind, _cut = sh[0], sh[1]
    if kind == 'ELL':
        cx, cy, rx, ry, lean = sh[2:7]
        ex = rx + abs(lean) * ry
        x0, x1, y0, y1 = cx - ex, cx + ex, cy - ry, cy + ry
    else:
        ax, ay, r1, bx, by, r2 = sh[2:8]
        x0, x1 = min(ax - r1, bx - r2), max(ax + r1, bx + r2)
        y0, y1 = min(ay - r1, by - r2), max(ay + r1, by + r2)
    return (max(0, int(x0 * k) - 1), min(int(units_w * k), int(x1 * k) + 2),
            max(0, int(y0 * k) - 1), min(int(units_h * k), int(y1 * k) + 2))

def coverage(shapes, units_w, units_h, k):
    """Rasterises to a bytearray of (units_w * k) by (units_h * k) alpha."""
    pw, ph = int(units_w * k), int(units_h * k)
    buf = bytearray(pw * ph)
    for sh in shapes:
        cut = sh[1]
        x0, x1, y0, y1 = _bbox(sh, units_w, units_h, k)
        for py in range(y0, y1):
            ay = (py + 0.5) / k
            row = py * pw
            for px in range(x0, x1):
                ax = (px + 0.5) / k
                d = (_sd_ell(ax, ay, *sh[2:7]) if sh[0] == 'ELL'
                     else _sd_cone(ax, ay, *sh[2:8]))
                a = 0.5 - d * k                 # coverage of this pixel, 0 to 1
                if a <= 0.0:
                    continue
                v = 255 if a >= 1.0 else int(a * 255.0)
                i = row + px
                if cut:
                    if 255 - v < buf[i]:
                        buf[i] = 255 - v
                elif v > buf[i]:
                    buf[i] = v
    return buf

# --- the bunny ---------------------------------------------------------------
class Body:
    """Every feature is placed from the body ellipse, so all stages stay in
    proportion. Nothing here is rounded to a pixel: the units are the art's."""

    def __init__(self, cx, cy, rx, ry, ear_scale, ear_lean):
        self.cx, self.cy, self.rx, self.ry = cx, cy, rx, ry
        self.ear_scale, self.ear_lean = ear_scale, ear_lean
        self.ear_ry = ry * 0.70 * ear_scale
        self.ear_rx = rx * 0.23
        self.ear_cy = cy - ry * 0.75 - self.ear_ry * 0.90
        self.eye_dx, self.eye_y = rx * 0.38, cy - ry * 0.30
        self.eye_rx, self.eye_ry = rx * 0.19, ry * 0.26
        self.nose_y, self.mouth_y = cy + ry * 0.12, cy + ry * 0.35

    def draw(self, dy=0.0, ears='rest'):
        s = []
        lean = self.ear_lean if ears == 'rest' else (0.95 if ears == 'down' else 0.0)
        for side in (-1, 1):
            x = self.cx + side * self.rx * 0.33
            s.append(ELL(x, self.ear_cy + dy, self.ear_rx, self.ear_ry, side * lean))
            # The inner ear is cut out of the outer one. At 32 pixels it had to
            # be skipped on a narrow ear, which left two one-pixel walls; drawn
            # as a shape there is no width below which it stops working.
            s.append(ELL(x, self.ear_cy + dy - self.ear_ry * 0.12,
                         self.ear_rx * 0.46, self.ear_ry * 0.62, side * lean, cut=1))
        s.append(ELL(self.cx, self.cy + dy, self.rx, self.ry))
        for side in (-1, 1):                     # feet, flatter than a circle
            s.append(ELL(self.cx + side * self.rx * 0.52, self.cy + self.ry * 0.92 + dy,
                         self.rx * 0.32, self.ry * 0.24))
        return s

    def eyes(self, s, dy=0.0, shut=False, dx=0.0):
        for side in (-1, 1):
            x = self.cx + side * self.eye_dx + dx
            y = self.eye_y + dy
            if shut:
                s.append(BAR(x - self.eye_rx - 0.4, x + self.eye_rx + 0.4, y,
                             self.ry * 0.045, cut=1))
            else:
                s.append(ELL(x, y, self.eye_rx, self.eye_ry, cut=1))
                # A shine, which is what stops a cut eye reading as a hole.
                s.append(ELL(x - self.eye_rx * 0.34, y - self.eye_ry * 0.40,
                             self.eye_rx * 0.38, self.eye_ry * 0.32))

    def nose(self, s, dy=0.0):
        y = self.nose_y + dy
        s.append(CONE(self.cx, y - self.ry * 0.06, self.rx * 0.085,
                      self.cx, y + self.ry * 0.05, self.rx * 0.035, cut=1))
        s.append(CONE(self.cx, y + self.ry * 0.04, self.rx * 0.022,      # philtrum
                      self.cx, self.mouth_y + dy, self.rx * 0.022, cut=1))

    def whiskers(self, s, dy=0.0):
        """Three strokes a side, kept well inside the silhouette: they are cut
        out of the fur, so one reaching the edge would break the outline."""
        y = self.nose_y + dy
        for side in (-1, 1):
            for n, (spread, tilt) in enumerate(((0.52, -0.16), (0.58, 0.0), (0.52, 0.18))):
                x0 = self.cx + side * self.rx * 0.13
                s.append(CONE(x0, y + self.ry * tilt * 0.5, self.rx * 0.018,
                              x0 + side * self.rx * spread, y + self.ry * tilt,
                              self.rx * 0.012, cut=1))

    def mouth_smile(self, s, dy=0.0):
        """The resting face, and it smiles. One arc with both ends above the
        middle: the two small lobes of a rabbit "w" are the prettier mouth, but
        at arm's length they read as a squiggle, and a child has to see at a
        glance that the bunny is content. The philtrum drops onto the middle of
        it, which is what keeps the face a rabbit's."""
        y = self.mouth_y + dy
        arc(s, (self.cx - MOUTH_W, y - MOUTH_END),      # left end, up
               (self.cx,           y + MOUTH_MID),      # pulls the middle down
               (self.cx + MOUTH_W, y - MOUTH_END), MOUTH_R)

    def mouth_frown(self, s, dy=0.0):
        """Every face that wants something. The same arc turned over: a mouth
        that is merely open reads as surprise, and surprise is not what a bunny
        with an empty gauge is asking for."""
        y = self.mouth_y + dy
        arc(s, (self.cx - MOUTH_W, y + MOUTH_END),      # left end, down
               (self.cx,           y - MOUTH_MID),      # and the middle lifted
               (self.cx + MOUTH_W, y + MOUTH_END), MOUTH_R)

    def mouth_open(self, s, dy=0.0):
        y = self.mouth_y + dy + self.ry * 0.16
        s.append(ELL(self.cx, y, self.rx * 0.29, self.ry * 0.24, cut=1))
        half = self.rx * 0.055                   # two buck teeth, a notch apart
        for side in (-1, 1):
            x = self.cx + side * (NOTCH * 0.5 + half)
            s.append(CONE(x, y - self.ry * 0.23, half,
                          x, y - self.ry * 0.03, half))

    def specks(self, s, dy=0.0):                 # dirt in the fur
        for ox, oy, r in ((-0.62, 0.45, 0.10), (0.55, 0.62, 0.08),
                          (-0.20, 0.78, 0.07), (0.28, 0.30, 0.09)):
            s.append(ELL(self.cx + self.rx * ox, self.cy + self.ry * oy + dy,
                         self.rx * r, self.rx * r * 0.85, cut=1))

def build(stage):
    name, cx, cy, rx, ry, es, el = stage
    b = Body(cx, cy, rx, ry, es, el)
    f = {}

    s = b.draw();            b.eyes(s);            b.nose(s);    b.whiskers(s); b.mouth_smile(s);       f['IDLE_A'] = s
    s = b.draw();            b.eyes(s, shut=True); b.nose(s);    b.whiskers(s); b.mouth_smile(s);       f['IDLE_B'] = s
    s = b.draw();            b.eyes(s);            b.nose(s);    b.whiskers(s); b.mouth_open(s);    f['EAT'] = s
    s = b.draw(dy=-2);       b.eyes(s, -2);        b.nose(s, -2); b.whiskers(s, -2); b.mouth_open(s, -2); f['PLAY'] = s
    # No Z in the art: the screen draws a ZzZz label beside the head, which can
    # follow each stage's ears in a way five fixed pixels cannot.
    s = b.draw(ears='down'); b.eyes(s, shut=True); b.nose(s);    b.whiskers(s); b.mouth_smile(s);       f['SLEEP'] = s
    # Hungry: ears forward and up, eyes wide, and unhappy about it.
    s = b.draw(ears='up');   b.eyes(s);            b.nose(s);    b.whiskers(s); b.mouth_frown(s);   f['HUNGRY'] = s
    # Bored: ears flopped, eyes looking to one side.
    s = b.draw(ears='down'); b.eyes(s, dx=1.2);    b.nose(s);    b.whiskers(s); b.mouth_frown(s);   f['BORED'] = s
    # Dirty: eyes screwed shut, specks of dirt in the fur.
    s = b.draw();            b.eyes(s, shut=True); b.nose(s);    b.whiskers(s); b.mouth_frown(s); b.specks(s); f['DIRTY'] = s
    return f

# --- want icons, in a 16x16 square -------------------------------------------
def icon_carrot(tip=14.5):
    """tip is how far down the root still reaches. Cutting it short leaves a
    blunt end, which is what a bite looks like."""
    half = max(0.35, (14.0 - tip) * 0.46 + 0.5)
    s = [CONE(7.5, 5.2, 3.0, 7.5, tip, half)]
    for dx, dy in ((-3.6, 3.0), (0.0, 4.0), (3.6, 3.0)):      # a leaf each way, and
        s.append(CONE(7.5, 4.4, 1.15, 7.5 + dx, 4.4 - dy, 0.35))   # clear of the root
    return s

def icon_ball():
    return [ELL(7.5, 8.0, 6.2, 6.2),
            ELL(7.5, 8.0, 6.2, 0.85, cut=1),                  # stripes, so it reads
            ELL(7.5, 8.0, 0.85, 6.2, cut=1)]                  # as a ball and not a dot

def icon_drop():
    return [CONE(7.5, 1.8, 0.30, 7.5, 9.8, 5.0)]              # one tapered capsule

def icon_fly():
    return [ELL(4.3, 5.4, 3.0, 1.1, -0.55),                   # wings, swept back
            ELL(10.7, 5.4, 3.0, 1.1, 0.55),
            ELL(7.5, 6.4, 1.7, 1.7),                          # head between them
            ELL(7.5, 9.8, 2.3, 3.3)]                          # and a long body below

ICONS = [('CARROT', icon_carrot()), ('CARROT_BITTEN', icon_carrot(10.0)),
         ('CARROT_STUB', icon_carrot(7.0)), ('BALL', icon_ball()),
         ('DROP', icon_drop()), ('FLY', icon_fly())]

# --- emit --------------------------------------------------------------------
def emit_shape(sh):
    return "    { %-7s %d, { %s } }," % (
        'SH_ELL,' if sh[0] == 'ELL' else 'SH_CONE,', sh[1],
        ', '.join('%8.3ff' % v for v in sh[2:8]))

def header():
    out = ["""// Bunny art. Shapes, not pixels: each frame is a list of ellipses and
// tapered capsules in a %d by %d unit square, added to the fur or cut out of
// it, in order. art.c rasterises a list into an anti-aliased mask at any size,
// so the art has no resolution of its own and nothing on screen is blocky.
//
// Generated by tools/gen_sprites.py. Edit the geometry there, not here.
#pragma once

#include "art.h"
#include "pet.h"        // for STAGE_COUNT

#define SPRITE_W %d
#define SPRITE_H %d
#define ICON_W   %d
#define ICON_H   %d
""" % (W, H, W, H, IW, IH), "typedef enum {"]
    out += ["    SPR_%s," % n for n in FRAMES] + ["    SPR_COUNT,", "} sprite_id_t;", ""]
    out.append("typedef enum {")
    out += ["    ICON_%s," % n for n, _ in ICONS] + ["    ICON_COUNT,", "} icon_id_t;", ""]

    for stage in STAGES:
        for n, shapes in build(stage).items():
            out.append("static const art_shape_t A_%s_%s[] = {" % (stage[0], n))
            out += [emit_shape(sh) for sh in shapes]
            out.append("};")
    out.append("")
    for n, shapes in ICONS:
        out.append("static const art_shape_t A_ICON_%s[] = {" % n)
        out += [emit_shape(sh) for sh in shapes]
        out.append("};")
    out.append("")

    out.append("static const art_t SPRITES[STAGE_COUNT][SPR_COUNT] = {")
    for stage in STAGES:
        out.append("    [STAGE_%s] = {" % stage[0])
        out += ["        [SPR_%s] = { A_%s_%s, ART_N(A_%s_%s) },"
                % (n, stage[0], n, stage[0], n) for n in FRAMES]
        out.append("    },")
    out.append("};\n")

    out.append("static const art_t ICONS[ICON_COUNT] = {")
    out += ["    [ICON_%s] = { A_ICON_%s, ART_N(A_ICON_%s) }," % (n, n, n) for n, _ in ICONS]
    out.append("};")
    return '\n'.join(out) + '\n'

def preview(shapes, units, k=1.0):
    ramp = ' .:-=+*#%@'
    cov = coverage(shapes, units, units, k)
    w = int(units * k)
    return '\n'.join(''.join(ramp[cov[y * w + x] * (len(ramp) - 1) // 255]
                             for x in range(w)) for y in range(int(units * k)))

if __name__ == '__main__':
    import sys
    open('components/user_app/sprites.h', 'w').write(header())
    show = sys.argv[1:] or ['ADULT']
    for stage in STAGES:
        if stage[0] not in show:
            continue
        f = build(stage)
        for n in FRAMES:
            print('==', stage[0], n)
            print(preview(f[n], W))
    for n, shapes in ICONS:
        print('==', n)
        print(preview(shapes, IW, 2.0))
