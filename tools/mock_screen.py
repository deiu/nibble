"""Draws docs/screens.png: the screen as it looks on the board, without a board.

Nothing here is hand drawn. The art comes out of gen_sprites.py, rasterised at
the size the board rasterises it, and the geometry out of user_app.cpp, so a
layout change shows up in the picture. Anything moved in one has to be moved in
the other, which is the price of not needing hardware to see a change.

    python3 tools/mock_screen.py [out.png]

Needs Pillow, and the system python3 rather than the ESP-IDF one.
"""
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, 'tools'))
import gen_sprites as art                       # the shapes, and the same rasteriser
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'docs/screens.png')

SPR = {(st[0], name): shapes for st in art.STAGES
       for name, shapes in art.build(st).items()}
ICON = dict(art.ICONS)

# --- the numbers, all of them from user_app.cpp and pet.h -------------------
K = 3                                       # supersample, thrown away at the end
W, C = 466, 233                             # the panel, and its middle
ARC_BOX, ARC_LEN, ARC_R = 444, 12, (444 - 12) // 2
NOTCH_LEN, NOTCH_W, NEED_LOW = 22, 2, 30
SCALE, PET_DY = 9, -24
FUR, TRACK, MARK, ALERT, FLY, WATER = '#F2E9D8', '#1E1E1E', '#FFFFFF', '#FF3B30', '#8A8A90', '#8FD8FF'
METERS = [   # start, end, reverse, colour, label, label x, label y
    (190, 240, False, '#FF9E3D', 'FOOD', -150, -104),
    (246, 296, False, '#7ED957', 'FUN',     0, -182),
    (302, 352, True,  '#5AC8FA', 'TIDY',  152,  -99),
]
BUTTONS = [(-122, 100, 'CARROT', 0), (0, 152, 'BALL', 1), (122, 100, 'DROP', 2)]
BAT_ARC = (60, 120)                         # the free rim under the bunny
BAT_LOW = 20
BAT_LABEL = (-130, 160)                     # left of the arc, inside the rim
ZZZ_AT = {'BABY': (23, 11), 'YOUNG': (23, 4), 'ADULT': (25, 5)}
FLY_AT = [(-140, -20), (140, -55), (-150, 60)]
FONT = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'   # stands in for Montserrat

# The art is rasterised once per size at the board's own scale, then blown up
# by K with no smoothing, so the picture shows the pixels the panel shows.
_CACHE = {}
def draw_art(img, shapes, units, scale, cx, cy, colour):
    key = (tuple(shapes), scale)
    if key not in _CACHE:
        n = int(units * scale)
        _CACHE[key] = Image.frombytes('L', (n, n), bytes(art.coverage(shapes, units, units, scale)))
    m = _CACHE[key].resize((_CACHE[key].width * K,) * 2, Image.NEAREST)
    patch = Image.new('RGB', m.size, colour)
    img.paste(patch, (int(cx - m.width / 2), int(cy - m.height / 2)), m)

def device(stage, frame, food, fun, tidy, buttons=True, paid=None, zzz=False, ball=False,
           bat=70):
    img = Image.new('RGB', (W * K, W * K), '#000000')
    d = ImageDraw.Draw(img)
    box = [(C - ARC_BOX / 2) * K] * 2 + [(C + ARC_BOX / 2) * K] * 2
    label_font = ImageFont.truetype(FONT, 13 * K)

    for i, (a0, a1, rev, col, text, lx, ly) in enumerate(METERS):
        value = (food, fun, tidy)[i]
        d.arc(box, a0, a1, fill=TRACK, width=ARC_LEN * K)
        span = (a1 - a0) * value / 100.0
        if span > 0:
            colour = ALERT if value < NEED_LOW else (MARK if paid == i else col)
            d.arc(box, *((a1 - span, a1) if rev else (a0, a0 + span)),
                  fill=colour, width=ARC_LEN * K)
        # the notch: where the gauge turns red and the bunny asks for help
        at = a1 - (a1 - a0) * NEED_LOW / 100.0 if rev else a0 + (a1 - a0) * NEED_LOW / 100.0
        rad = math.radians(at)
        ends = [((C + r * math.cos(rad)) * K, (C + r * math.sin(rad)) * K)
                for r in (ARC_R - NOTCH_LEN / 2, ARC_R + NOTCH_LEN / 2)]
        d.line(ends, fill=MARK, width=NOTCH_W * K)
        d.text(((C + lx) * K, (C + ly) * K), text, font=label_font, fill=col, anchor='mm')

    # The board's own gauge, on the rim the three needs leave empty. It fills
    # from the left, like a battery, and it wears no colour of theirs.
    a0, a1 = BAT_ARC
    d.arc(box, a0, a1, fill=TRACK, width=ARC_LEN * K)
    span = (a1 - a0) * bat / 100.0
    if span > 0:
        d.arc(box, a1 - span, a1, fill=ALERT if bat < BAT_LOW else FUR, width=ARC_LEN * K)
    d.text(((C + BAT_LABEL[0]) * K, (C + BAT_LABEL[1]) * K), 'BAT',
           font=label_font, fill=FUR, anchor='mm')

    for at in FLY_AT[:min(3, max(0, int((100 - tidy) / 25)))]:
        draw_art(img, ICON['FLY'], 16, 2, (C + at[0]) * K, (C + at[1]) * K, FLY)

    draw_art(img, SPR[(stage, frame)], 32, SCALE, C * K, (C + PET_DY) * K, FUR)

    if zzz:
        x, y = ZZZ_AT[stage]
        d.text(((C - 144 + x * SCALE) * K, (C + PET_DY - 144 + y * SCALE) * K),
               'ZzZz', font=ImageFont.truetype(FONT, 24 * K), fill=FUR, anchor='la')
    if ball:
        draw_art(img, ICON['BALL'], 16, 4, (C + 126) * K, (C + 58) * K, METERS[1][3])
    if buttons:
        for (bx, by, icon, mi) in BUTTONS:
            d.rounded_rectangle([(C + bx - 46) * K, (C + by - 38) * K,
                                 (C + bx + 46) * K, (C + by + 38) * K],
                                radius=38 * K, fill='#232327', outline=METERS[mi][3], width=3 * K)
            draw_art(img, ICON[icon], 16, 4, (C + bx) * K, (C + by) * K, METERS[mi][3])

    # the glass is round, so the corners go, and the case goes around it
    mask = Image.new('L', img.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, W * K - 1, W * K - 1], fill=255)
    img.putalpha(mask)
    case = Image.new('RGBA', ((W + 40) * K,) * 2, (0, 0, 0, 0))
    ImageDraw.Draw(case).ellipse([0, 0, (W + 40) * K - 1, (W + 40) * K - 1],
                                 fill='#17171A', outline='#3A3A40', width=2 * K)
    case.alpha_composite(img, (20 * K, 20 * K))
    return case.resize((W + 40, W + 40), Image.LANCZOS)

PANELS = [
    ('a new kit, fed and content',          device('BABY', 'IDLE_A', 82, 74, 88)),
    ('playing: FUN rises, FOOD pays for it', device('ADULT', 'PLAY', 61, 96, 44,
                                                    buttons=False, paid=0, ball=True,
                                                    bat=44)),
    ('a nap after thirty taps',             device('YOUNG', 'SLEEP', 46, 52, 22, zzz=True, bat=14)),
]
PAD, CAP, SIDE = 30, 46, 506
sheet = Image.new('RGB', (len(PANELS) * (SIDE + PAD) + PAD, SIDE + 2 * PAD + CAP), '#0B0B0D')
sd = ImageDraw.Draw(sheet)
caption = ImageFont.truetype(FONT, 17)
for i, (text, panel) in enumerate(PANELS):
    x = PAD + i * (SIDE + PAD)
    sheet.paste(panel.convert('RGB'), (x, PAD))
    sd.text((x + SIDE // 2, PAD + SIDE + 22), text, font=caption, fill='#9A9AA2', anchor='mm')
os.makedirs(os.path.dirname(OUT), exist_ok=True)
sheet.save(OUT)
print(OUT, sheet.size)
