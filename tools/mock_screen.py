"""Draws docs/screens.png: the screen as it looks on the board, without a board.

Nothing here is hand drawn. The art comes out of sprites.h and the geometry out
of user_app.cpp, so a layout change shows up in the picture. Anything moved in
one has to be moved in the other, which is the price of not needing hardware to
see a change.

    python3 tools/mock_screen.py [out.png]

Needs Pillow, and the system python3 rather than the ESP-IDF one.
"""
import math
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = open(os.path.join(HERE, 'components/user_app/sprites.h')).read()
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'docs/screens.png')

# --- the art, one character per pixel, straight out of sprites.h ------------
def grid(block, w):
    return re.findall(r'"([.#]{%d})"' % w, block)

SPR = {}
for s in re.finditer(r'\[STAGE_(\w+)\] = \{(.*?)\n    \},', ART, re.S):
    for f in re.finditer(r'\[SPR_(\w+)\] = \{(.*?)\n        \},', s.group(2), re.S):
        SPR[(s.group(1), f.group(1))] = grid(f.group(2), 32)
ICON = {m.group(1): grid(m.group(2), 16)
        for m in re.finditer(r'\n    \[ICON_(\w+)\] = \{(.*?)\n    \},',
                             ART[ART.index('ICONS['):], re.S)}

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
ZZZ_AT = {'BABY': (23, 11), 'YOUNG': (23, 4), 'ADULT': (25, 5)}
FLY_AT = [(-140, -20), (140, -55), (-150, 60)]
FONT = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'   # stands in for Montserrat

def art(d, rows, cell, cx, cy, colour):
    h, w = len(rows), len(rows[0])
    x0, y0 = cx - w * cell / 2, cy - h * cell / 2
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch == '#':
                d.rectangle([x0 + c * cell, y0 + r * cell,
                             x0 + (c + 1) * cell - 1, y0 + (r + 1) * cell - 1], fill=colour)

def device(stage, frame, food, fun, tidy, buttons=True, paid=None, zzz=False, ball=False):
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

    for at in FLY_AT[:min(3, max(0, int((100 - tidy) / 25)))]:
        art(d, ICON['FLY'], 2 * K, (C + at[0]) * K, (C + at[1]) * K, FLY)

    art(d, SPR[(stage, frame)], SCALE * K, C * K, (C + PET_DY) * K, FUR)

    if zzz:
        x, y = ZZZ_AT[stage]
        d.text(((C - 144 + x * SCALE) * K, (C + PET_DY - 144 + y * SCALE) * K),
               'ZzZz', font=ImageFont.truetype(FONT, 24 * K), fill=FUR, anchor='la')
    if ball:
        art(d, ICON['BALL'], 4 * K, (C + 126) * K, (C + 58) * K, METERS[1][3])
    if buttons:
        for (bx, by, icon, mi) in BUTTONS:
            d.rounded_rectangle([(C + bx - 46) * K, (C + by - 38) * K,
                                 (C + bx + 46) * K, (C + by + 38) * K],
                                radius=38 * K, fill='#232327', outline=METERS[mi][3], width=3 * K)
            art(d, ICON[icon], 4 * K, (C + bx) * K, (C + by) * K, METERS[mi][3])

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
                                                    buttons=False, paid=0, ball=True)),
    ('a nap after thirty taps',             device('YOUNG', 'SLEEP', 46, 52, 22, zzz=True)),
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
