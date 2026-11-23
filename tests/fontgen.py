#!/usr/bin/env python3
"""Rasterise a font atlas for the headless harness (stands in for GDI on Linux).
usage: fontgen.py face px weight out.bin  -> writes: w h ascent height n, then n lines 'cp x y w h xoff adv', then w*h alpha bytes"""
import sys
from PIL import Image, ImageDraw, ImageFont

face, px, weight, out = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
cps = [int(c) for c in sys.argv[5].split(',')]
mono = face.lower().startswith('consolas')
if mono:
    path = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono%s.ttf' % ('-Bold' if weight >= 600 else '')
else:
    name = {400: 'Regular', 500: 'Medium', 600: 'SemiBold', 700: 'Bold', 800: 'ExtraBold', 900: 'Black'}.get(weight, 'Regular')
    path = '/usr/share/fonts/opentype/inter/Inter-%s.otf' % name
font = ImageFont.truetype(path, int(round(px * (0.92 if mono else 0.95))))
asc, desc = font.getmetrics()
cell_h = asc + desc + 2
W, H = 1024, 512
img = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(img)
x, y = 1, 1
lines = []
for cp in cps:
    ch = chr(cp)
    adv = font.getlength(ch)
    bbox = font.getbbox(ch)
    gw = max(0, bbox[2] - min(0, bbox[0])) + 4 if ch != ' ' else 0
    if x + gw + 2 > W:
        x = 1
        y += cell_h + 2
    if gw:
        d.text((x + 2 - min(0, bbox[0]), y), ch, font=font, fill=255)
    lines.append('%d %d %d %d %d %g %g' % (cp, x, y, gw, cell_h, min(0, bbox[0]) - 2, adv))
    x += gw + 2
with open(out, 'wb') as f:
    f.write(('%d %d %g %g %d\n' % (W, H, asc, cell_h, len(cps))).encode())
    f.write(('\n'.join(lines) + '\n').encode())
    f.write(img.tobytes())
