"""Generate the simulator's tiling PBR textures (no third-party assets).
Run: python -I make_textures.py <out_dir>
"""
import sys, os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)


def save(arr, name, mode=None):
    arr = np.clip(arr, 0, 255).astype(np.uint8)
    Image.fromarray(arr, mode).save(os.path.join(OUT, name + ".png"))


def tile_noise(n, octaves=6, persistence=0.55, base=4):
    """Tileable fractal value noise in [0,1]."""
    out = np.zeros((n, n))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        f = base * 2 ** o
        g = rng.random((f, f))
        x = np.linspace(0, f, n, endpoint=False)
        xi = np.floor(x).astype(int); xf = x - xi
        xf = xf * xf * (3 - 2 * xf)
        x0, x1 = xi % f, (xi + 1) % f
        a = g[np.ix_(x0, x0)]; b = g[np.ix_(x0, x1)]; c = g[np.ix_(x1, x0)]; d = g[np.ix_(x1, x1)]
        fy = xf[:, None]; fx = xf[None, :]
        layer = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
        out += layer * amp; total += amp; amp *= persistence
    return out / total


def height_to_normal(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.dstack((-dx, -dy, np.ones_like(h)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return (n * 0.5 + 0.5) * 255


# ---------------------------------------------------------------- carbon fibre 2x2 twill
def carbon(n=512, tows=16):
    y, x = np.mgrid[0:n, 0:n] / n * tows
    cell_x, cell_y = np.floor(x).astype(int), np.floor(y).astype(int)
    twill = ((cell_x + cell_y) // 2) % 2           # 2x2 twill pattern
    fx, fy = x - cell_x, y - cell_y
    across = np.where(twill == 0, fx, fy)
    along = np.where(twill == 0, fy, fx)
    bump = np.sin(across * math.pi) ** 0.6 * (0.85 + 0.15 * np.sin(along * math.pi * 6))
    fiber = 0.5 + 0.5 * np.sin(across * math.pi * 26 + rng.random() * 3)
    h = bump * 0.8 + fiber * 0.06
    alb = (0.025 + 0.035 * twill + 0.02 * fiber * bump) * 255
    save(np.dstack([alb, alb, alb * 1.05]), "T_Carbon_D")
    save(height_to_normal(h, 3.0), "T_Carbon_N")


# ---------------------------------------------------------------- asphalt
def asphalt(n=1024):
    base = tile_noise(n, 7, 0.6, 8)
    grit = rng.random((n, n))
    stones = (grit > 0.985).astype(float)
    stones = np.array(Image.fromarray((stones * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))) / 255
    tone = 0.11 + 0.05 * base + 0.06 * stones + 0.03 * (grit - 0.5)
    alb = np.dstack([tone, tone, tone * 1.04]) * 255
    save(alb, "T_Asphalt_D")
    h = base * 0.4 + grit * 0.35 + stones * 0.6
    save(height_to_normal(h, 2.2), "T_Asphalt_N")
    rough = (0.62 + 0.22 * grit - 0.15 * stones) * 255
    save(rough, "T_Asphalt_R", "L")


# ---------------------------------------------------------------- concrete (tunnels / supports)
def concrete(n=1024):
    a = tile_noise(n, 7, 0.55, 4); b = tile_noise(n, 5, 0.5, 16)
    pores = (rng.random((n, n)) > 0.993).astype(float)
    tone = 0.46 + 0.10 * a + 0.05 * b - 0.18 * pores
    save(np.dstack([tone, tone * 0.99, tone * 0.96]) * 255, "T_Concrete_D")
    save(height_to_normal(a * 0.3 + b * 0.3 - pores * 0.8, 1.5), "T_Concrete_N")


# ---------------------------------------------------------------- desert ground
def sand(n=1024):
    a = tile_noise(n, 7, 0.6, 4); b = tile_noise(n, 4, 0.5, 32)
    ripple = 0.5 + 0.5 * np.sin(np.mgrid[0:n, 0:n][0] / n * math.pi * 2 * 48 + a * 9)
    r = 0.62 + 0.10 * a + 0.05 * b; g = 0.48 + 0.08 * a + 0.04 * b; bl = 0.36 + 0.06 * a
    save(np.dstack([r, g, bl]) * 255, "T_Sand_D")
    save(height_to_normal(ripple * 0.15 + b * 0.5, 2.0), "T_Sand_N")


# ---------------------------------------------------------------- tileable noise (smoke / plumes)
def noise(n=512):
    a = tile_noise(n, 6, 0.55, 4)
    a = (a - a.min()) / (a.max() - a.min())
    save(a * 255, "T_Noise", "L")


# ---------------------------------------------------------------- tire sidewall + tread
def font(size, bold=True):
    for name in ("arialbd.ttf" if bold else "arial.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def sidewall(size_text, n_w=4096, n_h=256):
    """Sidewall strip for the revolved tire UVs (u around the circumference, v across the profile).
    Text is a raised rubber mask in R, used to brighten / bump the lettering."""
    img = Image.new("L", (n_w, n_h), 0)
    d = ImageDraw.Draw(img)
    f1, f2 = font(150), font(70)
    texts = [("P ZERO", f1), ("TROFEO RS", f1), (size_text, f2), ("PIRELLI", f1), (size_text, f2)]
    x = 60
    for t, f in texts:
        d.text((x, 40 if f is f1 else 90), t, fill=255, font=f)
        x += int(d.textlength(t, font=f)) + 220
    img = img.filter(ImageFilter.GaussianBlur(1.2))
    return np.array(img)


def tires():
    for nm, txt in (("Front", "295/30 ZR20"), ("Rear", "335/30 ZR21")):
        save(sidewall(txt), "T_TireText_" + nm, "L")
    # tread wear / rubber micro texture
    n = 512
    h = tile_noise(n, 6, 0.6, 16) * 0.5 + rng.random((n, n)) * 0.2
    save(height_to_normal(h, 1.2), "T_Rubber_N")


carbon(); asphalt(); concrete(); sand(); noise(); tires()
print("textures ->", OUT)
