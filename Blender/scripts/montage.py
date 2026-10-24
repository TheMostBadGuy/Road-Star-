"""Side-by-side montage of reference photos and matching renders.
Run: python -I montage.py <out.png> <photo1> <render1> [<photo2> <render2> ...]
"""
import sys
from PIL import Image

out = sys.argv[1]
pairs = list(zip(sys.argv[2::2], sys.argv[3::2]))
H = 420
rows = []
for photo, rend in pairs:
    p = Image.open(photo).convert("RGB")
    r = Image.open(rend).convert("RGBA")
    bg = Image.new("RGB", r.size, (70, 72, 78))
    bg.paste(r, mask=r.split()[3])
    p = p.resize((int(p.width * H / p.height), H))
    bg = bg.resize((int(bg.width * H / bg.height), H))
    row = Image.new("RGB", (p.width + bg.width + 10, H), (0, 0, 0))
    row.paste(p, (0, 0)); row.paste(bg, (p.width + 10, 0))
    rows.append(row)
W = max(r.width for r in rows)
m = Image.new("RGB", (W, H * len(rows) + 10 * (len(rows) - 1)), (0, 0, 0))
y = 0
for r in rows:
    m.paste(r, (0, y)); y += H + 10
m.save(out)
