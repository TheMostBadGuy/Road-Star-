"""Crop a region of a reference photo and overlay a labelled pixel grid for measurement.
Run: python -I grid_crop.py <image> <x0> <y0> <x1> <y1> <step> <out.png> [scale]
"""
import sys
from PIL import Image, ImageDraw, ImageEnhance

path, x0, y0, x1, y1, step, out = sys.argv[1], *map(int, sys.argv[2:7]), sys.argv[7]
scale = float(sys.argv[8]) if len(sys.argv) > 8 else 1.0
im = Image.open(path).convert("RGB").crop((x0, y0, x1, y1))
im = ImageEnhance.Brightness(im).enhance(1.8)
if scale != 1.0:
    im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
d = ImageDraw.Draw(im)
for gx in range((x0 // step + 1) * step, x1, step):
    X = (gx - x0) * scale
    d.line([(X, 0), (X, im.height)], fill=(0, 160, 255), width=1)
    d.text((X + 2, 2), str(gx), fill=(255, 255, 0))
for gy in range((y0 // step + 1) * step, y1, step):
    Y = (gy - y0) * scale
    d.line([(0, Y), (im.width, Y)], fill=(0, 160, 255), width=1)
    d.text((2, Y + 2), str(gy), fill=(255, 255, 0))
im.save(out)
