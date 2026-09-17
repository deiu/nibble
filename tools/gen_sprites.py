"""Generates sprites.h: three growth stages of the bunny, plus the want icons.

Every part of the bunny is derived from the body ellipse, so a stage is just a
body size and an ear size. Edit the STAGES table to change how growth looks.
"""
W = H = 32
FRAMES = ['IDLE_A', 'IDLE_B', 'EAT', 'PLAY', 'SLEEP', 'HUNGRY', 'BORED', 'DIRTY']
STAGES = [
    # name,    body cx, cy,  rx,   ry,  ear scale, ear lean at rest
    ('BABY',    15.5, 22.0,  7.5,  6.5,  0.65,  0.45),   # a kit: small, ears short and out
    ('YOUNG',   15.5, 21.0,  9.0,  7.5,  0.70,  0.15),
    ('ADULT',   15.5, 20.0, 10.5,  8.5,  1.00,  0.00),
]

def blank(w=W, h=H): return [['.'] * w for _ in range(h)]
def px(g, x, y, ch):
    x, y = int(round(x)), int(round(y))
    if 0 <= y < len(g) and 0 <= x < len(g[0]): g[y][x] = ch
def ell(g, cx, cy, rx, ry, ch='#'):
    if rx <= 0 or ry <= 0: return
    for y in range(len(g)):
        for x in range(len(g[0])):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0: g[y][x] = ch
def hline(g, x0, x1, y, ch='.'):
    for x in range(int(round(x0)), int(round(x1)) + 1): px(g, x, y, ch)
def ear(g, cx, cy, rx, ry, lean, ch='#'):
    """A vertical ellipse sheared sideways: lean 0 stands up, 1 flops outward."""
    if ry <= 0: return
    for y in range(len(g)):
        t = (y - cy) / ry
        if abs(t) > 1.0: continue
        hw = rx * (1.0 - t * t) ** 0.5
        mid = cx + lean * (cy - y)
        for x in range(len(g[0])):
            if abs(x - mid) <= hw: g[y][x] = ch

class Body:
    """Every feature is placed from the body ellipse, so all stages stay in proportion."""
    def __init__(self, cx, cy, rx, ry, ear_scale, ear_lean):
        self.cx, self.cy, self.rx, self.ry = cx, cy, rx, ry
        self.ear_scale, self.ear_lean = ear_scale, ear_lean
        self.ear_ry = ry * 0.70 * ear_scale
        self.ear_rx = rx * 0.23
        self.ear_cy = cy - ry * 0.75 - self.ear_ry * 0.90
        self.eye_dx, self.eye_y = rx * 0.38, cy - ry * 0.30
        self.eye_rx, self.eye_ry = rx * 0.16, ry * 0.24
        self.nose_y, self.mouth_y = cy + ry * 0.12, cy + ry * 0.35

    def draw(self, dy=0.0, ears='rest'):
        g = blank()
        lean = self.ear_lean if ears == 'rest' else (0.95 if ears == 'down' else 0.0)
        for side in (-1, 1):
            ear(g, self.cx + side * self.rx * 0.33, self.ear_cy + dy,
                self.ear_rx, self.ear_ry, side * lean)
            # The inner ear is carved out of the outer one, so a narrow ear
            # would be left as two single-pixel walls. Judge it on the width
            # in pixels, which is what decides whether it survives.
            if self.ear_rx >= 2.0 and lean < 0.4:
                ell(g, self.cx + side * self.rx * 0.33, self.ear_cy + dy - self.ear_ry * 0.15,
                    self.ear_rx * 0.38, self.ear_ry * 0.47, '.')
        ell(g, self.cx, self.cy + dy, self.rx, self.ry)
        for side in (-1, 1):
            ell(g, self.cx + side * self.rx * 0.52, self.cy + self.ry * 0.92 + dy,
                self.rx * 0.32, self.ry * 0.22)   # flatter than a circle, so no one-pixel tip
        return g

    def eyes(self, g, dy=0.0, shut=False, dx=0.0):
        for side in (-1, 1):
            x = self.cx + side * self.eye_dx + dx
            if shut:
                hline(g, x - self.eye_rx - 0.4, x + self.eye_rx + 0.4, self.eye_y + dy)
            else:
                ell(g, x, self.eye_y + dy, self.eye_rx, self.eye_ry, '.')

    def nose(self, g, dy=0.0):
        hline(g, self.cx - 1.5, self.cx + 1.5, self.nose_y + dy)
        hline(g, self.cx - 0.5, self.cx + 0.5, self.nose_y + 1 + dy)

    def mouth_w(self, g, dy=0.0):                        # the small rabbit "w"
        y = self.mouth_y + dy
        hline(g, self.cx - 0.5, self.cx + 0.5, y)
        hline(g, self.cx - 2.5, self.cx - 1.5, y + 1)
        hline(g, self.cx + 1.5, self.cx + 2.5, y + 1)

    def mouth_flat(self, g, dy=0.0):                     # bored: a straight line
        hline(g, self.cx - 2.5, self.cx + 2.5, self.mouth_y + dy + 1)

    def mouth_open(self, g, dy=0.0):
        y = self.mouth_y + dy + 1.5
        ell(g, self.cx, y, self.rx * 0.29, self.ry * 0.22, '.')
        for side in (-1, 1):                             # two buck teeth
            px(g, self.cx + side * 1.5, y - self.ry * 0.16, '#')
            px(g, self.cx + side * 1.5, y - self.ry * 0.16 + 1, '#')

    def specks(self, g, dy=0.0):                         # dirt on the fur
        for ox, oy in ((-0.62, 0.45), (0.55, 0.62), (-0.2, 0.78), (0.28, 0.30)):
            px(g, self.cx + self.rx * ox, self.cy + self.ry * oy + dy, '.')
            px(g, self.cx + self.rx * ox + 1, self.cy + self.ry * oy + dy + 1, '.')

def build(stage):
    name, cx, cy, rx, ry, es, el = stage
    b = Body(cx, cy, rx, ry, es, el)
    f = {}

    g = b.draw();               b.eyes(g);          b.nose(g);       b.mouth_w(g);     f['IDLE_A'] = g
    g = b.draw();               b.eyes(g, shut=True); b.nose(g);     b.mouth_w(g);     f['IDLE_B'] = g
    g = b.draw();               b.eyes(g);          b.nose(g);       b.mouth_open(g);  f['EAT'] = g
    g = b.draw(dy=-2);          b.eyes(g, -2);      b.nose(g, -2);   b.mouth_open(g, -2); f['PLAY'] = g
    # No Z in the art: the screen draws a ZzZz label beside the head, which can
    # follow each stage's ears in a way five fixed pixels cannot.
    g = b.draw(ears='down');    b.eyes(g, shut=True); b.nose(g);     b.mouth_w(g);     f['SLEEP'] = g
    # Hungry: ears forward and up, mouth open, eyes wide.
    g = b.draw(ears='up');      b.eyes(g);          b.nose(g);       b.mouth_open(g);  f['HUNGRY'] = g
    # Bored: ears flopped, a flat mouth, eyes looking to one side.
    g = b.draw(ears='down');    b.eyes(g, dx=1.2);  b.nose(g);       b.mouth_flat(g);  f['BORED'] = g
    # Dirty: normal face, specks of dirt in the fur.
    g = b.draw();               b.eyes(g, shut=True); b.nose(g);     b.mouth_flat(g); b.specks(g); f['DIRTY'] = g
    return f

# --- want icons, 16x16 -------------------------------------------------------
IW = IH = 16
def icon_carrot(tip=14):
    """tip is how far down the root still reaches. Cutting it short leaves a
    blunt end, which is what a bite looks like."""
    g = blank(IW, IH)
    for y in range(5, tip + 1):                          # tapering root
        half = max(0, (14 - y) * 0.46 + 0.6)
        hline(g, 7.5 - half, 7.5 + half, y, '#')
    hline(g, 4, 6, 3, '#'); hline(g, 5, 7, 2, '#')       # leaves
    hline(g, 9, 11, 3, '#'); hline(g, 8, 10, 2, '#')
    px(g, 7, 1, '#'); px(g, 8, 1, '#'); hline(g, 7, 8, 2, '#')
    return g
def icon_ball():
    g = blank(IW, IH)
    ell(g, 7.5, 8.5, 6.0, 6.0)
    ell(g, 7.5, 8.5, 6.0, 1.3, '.')                      # a stripe, so it reads as a ball
    ell(g, 7.5, 8.5, 1.3, 6.0, '.')
    return g
def icon_drop():
    g = blank(IW, IH)
    ell(g, 7.5, 10.0, 5.0, 5.0)
    for y in range(2, 10):                               # the point on top
        half = (y - 2) * 0.62
        hline(g, 7.5 - half, 7.5 + half, y, '#')
    return g
def icon_fly():
    g = blank(IW, IH)
    ell(g, 3.8, 5.4, 3.1, 1.4)                           # wings, wide and flat
    ell(g, 11.2, 5.4, 3.1, 1.4)
    ell(g, 7.5, 9.0, 2.9, 3.2)                           # round body below them
    return g

ICONS = [('CARROT', icon_carrot()), ('CARROT_BITTEN', icon_carrot(10)),
         ('CARROT_STUB', icon_carrot(7)), ('BALL', icon_ball()), ('DROP', icon_drop()),
         ('FLY', icon_fly())]

# --- emit --------------------------------------------------------------------
out = ["""// Bunny sprites. One character per pixel: '#' is fur, '.' is background.
// Generated art, but plain text on purpose: edit any pixel here by hand.
// Three growth stages of the same rabbit, then the icons it shows when it
// wants something.
#pragma once

#include "pet.h"        // for STAGE_COUNT

#define SPRITE_W 32
#define SPRITE_H 32
#define ICON_W   16
#define ICON_H   16
""", "typedef enum {"]
out += ["    SPR_%s," % n for n in FRAMES] + ["    SPR_COUNT,", "} sprite_id_t;", ""]
out.append("typedef enum {")
out += ["    ICON_%s," % n for n, _ in ICONS] + ["    ICON_COUNT,", "} icon_id_t;", ""]

out.append("static const char *const SPRITES[STAGE_COUNT][SPR_COUNT][SPRITE_H] = {")
for stage in STAGES:
    f = build(stage)
    out.append("    [STAGE_%s] = {" % stage[0])
    for n in FRAMES:
        out.append("        [SPR_%s] = {" % n)
        out += ['            "%s",' % ''.join(r) for r in f[n]]
        out.append("        },")
    out.append("    },")
out.append("};\n")

out.append("static const char *const ICONS[ICON_COUNT][ICON_H] = {")
for n, g in ICONS:
    out.append("    [ICON_%s] = {" % n)
    out += ['        "%s",' % ''.join(r) for r in g]
    out.append("    },")
out.append("};")

if __name__ == '__main__':
    import sys
    open('components/user_app/sprites.h', 'w').write('\n'.join(out) + '\n')
    show = sys.argv[1:] or ['ADULT']
    for stage in STAGES:
        if stage[0] not in show: continue
        f = build(stage)
        for n in FRAMES:
            print('==', stage[0], n)
            print('\n'.join(''.join(r).replace('#', '█').replace('.', ' ') for r in f[n]))
    for n, g in ICONS:
        print('==', n)
        print('\n'.join(''.join(r).replace('#', '█').replace('.', ' ') for r in g))
