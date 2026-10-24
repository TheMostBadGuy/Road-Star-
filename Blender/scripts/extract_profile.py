"""Extract the Roadster's side silhouette from Tesla's near-orthographic side photo.

Output: Blender/scripts/profile_side.json with the upper (roof/hood/deck) profile in metres,
x measured from the front axle (+ forward), z above ground. Scale from wheelbase = 2.825 m.
Run: python -I extract_profile.py <image> <out.json> <debug.png>
"""
import sys, json
import numpy as np
from PIL import Image

img_path, out_path, dbg_path = sys.argv[1], sys.argv[2], sys.argv[3]
im = np.asarray(Image.open(img_path).convert("RGB")).astype(np.float32)
H, W, _ = im.shape
lum = im.mean(axis=2)

# Background: dark grey studio, brighter towards the right. Estimate a per-column background
# from the top 15% of rows (pure backdrop) and threshold relative to it.
bg = np.median(lum[: int(H * 0.15)], axis=0)
diff = lum - bg[None, :]

# Upper silhouette: first row from the top where the pixel clearly departs from the backdrop.
# Glass is brown (r > b), body is near-white.
r, g, b = im[..., 0], im[..., 1], im[..., 2]
car = (diff > 28) | ((r - b > 12) & (lum > 45))
top = np.full(W, -1)
for x in range(W):
    col = np.where(car[: int(H * 0.92), x])[0]
    if len(col):
        top[x] = col[0]

# Ground line: bottom of the tires. Tires are black against a lit floor, so find the darkest
# horizontal band under the wheel centres.
json.dump({"W": W, "H": H, "top": top.tolist()}, open(out_path, "w"))

dbg = im.copy().astype(np.uint8)
for x in range(W):
    if top[x] >= 0:
        dbg[max(0, top[x] - 2): top[x] + 2, x] = [0, 255, 0]
Image.fromarray(dbg).resize((W // 2, H // 2)).save(dbg_path)
print("columns with silhouette:", int((top >= 0).sum()))
