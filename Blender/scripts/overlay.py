"""Overlay a transparent render on a reference photo (same pixel size) for shape comparison.
Run: python -I overlay.py <photo> <render.png> <out.png> [alpha]
"""
import sys
from PIL import Image, ImageChops, ImageFilter

photo, rend, out = sys.argv[1:4]
alpha = float(sys.argv[4]) if len(sys.argv) > 4 else 0.55
p = Image.open(photo).convert("RGB")
r = Image.open(rend).convert("RGBA").resize(p.size)
# silhouette edge of the render in green over the photo, plus a translucent fill
a = r.split()[3]
edge = a.filter(ImageFilter.FIND_EDGES).point(lambda v: 255 if v > 40 else 0)
fill = Image.new("RGB", p.size, (255, 60, 60))
comp = Image.composite(Image.blend(p, fill, 0.30), p, a.point(lambda v: int(v * alpha)))
green = Image.new("RGB", p.size, (0, 255, 0))
comp = Image.composite(green, comp, edge)
comp.resize((p.width // 2, p.height // 2)).save(out)
