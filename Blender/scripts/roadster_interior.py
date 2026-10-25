"""Cabin of the 2026 Tesla Roadster: trim, fixed buckets, steer-by-wire yoke, floating screen, forward displays.

Blender frame (same as roadster_body): metres, origin on the ground under the wheelbase midpoint,
+X forward, +Y = car's LEFT (left-hand drive: the driver sits at y = +0.38), +Z up.

build(M) -> dict name -> object (the Unreal pawn loads these names):
  Interior           static cabin, one object, mesh in car coordinates (identity transform)
  Seat_Driver        fixed bucket moulded to the tub, belt, buckle, recline lever (car coordinates)
  Seat_Passenger     mirror of the driver seat (hidden on the 1-seat Rocket trim)
  Yoke               moving: object origin = pivot on the column axis (0.62, 0.38, 0.72). Steering spins it about
                     its LOCAL +X axis (= the column axis, pointing forward-down into the dash). The rim's face is on
                     local -X; the rim centre sits YOKE_SETBACK behind the pivot along that axis.
                     rotation_euler = (0, +24 deg, 0) reproduces the pawn's FRotator(-24, 0, 0) for Blender previews.
  Screen_Bezel       aluminium housing, black border glass and stand of the centre screen (pivot = screen centre)
  Screen_Display     15.0 in 16:10 landscape screen (0.323 x 0.202 m), single slot M_Screen_Main
  Forward_Display_A  0.160 x 0.044 m strip on the dash left of the driver's sight line, single slot M_Screen_Forward
  Forward_Display_B  same, right of the sight line
Screen UVs: one quad spanning 0..1, u from the viewer's left to right, v from bottom to top (as roadster_parts did).

Every mesh carries one UV map at world scale (1 UV unit = 1 m; carbon at 2 units per metre so the 6x tiling
of M_Carbon gives ~5 mm weave cells). Faces are oriented towards the cabin (Unreal materials are one-sided).
References: Research/images/tesla_official_Roadster-03-Carousel-Slide-4-Desktop.jpg, ui_frames/cabin_pov_*.jpg,
Research/roadster_specs.md sections 5-6. Anything not visible in them is marked INFERRED in the comments.
"""
import bpy, bmesh, math, os, random
from mathutils import Vector, Matrix
import roadster_body as RB
from roadster_materials import mat

V = Vector

# ----------------------------------------------------------------------------- package (driver seated, 1.8 m)
Y_DRV = 0.38                              # seat / yoke centreline, left-hand drive
EYE = V((-0.18, 0.38, 1.00))              # = the pawn's cockpit camera (RoadsterPawn.cpp, Cockpit)
FLOOR = 0.150                             # top of the carbon tub floor (body floor skin at z 0.112)
YOKE_PIVOT = V((0.62, 0.38, 0.72))        # = YokeBase in RoadsterPawn.cpp (B2U(0.62, 0.38, 0.72))
YOKE_PITCH = math.radians(24.0)           # column axis points forward and 24 deg down (UE pitch -24)
YOKE_SETBACK = 0.10                       # rim centre 10 cm behind the pivot, towards the driver (Slide-4 fit + reach)
X_HEADER = 0.26                           # windshield header (Slide-4 fit: header ~9 deg above the driver's eye)
X_BULK = -0.64                            # rear bulkhead foot (behind the seat backs)
SCREEN_LOC = V((0.795, 0.0, 0.738))       # centre of the touchscreen surface
SCREEN_ROT = (0.0, math.radians(10.0), math.radians(-5.0))   # leans back 10 deg, turned 5 deg to the driver
SCREEN_W, SCREEN_H = 0.3231, 0.2019       # 15.0 in diagonal, 16:10
FWD_W, FWD_H = 0.160, 0.044               # forward displays (800 x 220 render targets -> 3.64:1)
FWD_POS = {"Forward_Display_A": V((1.005, 0.530, 0.868)), "Forward_Display_B": V((1.005, 0.232, 0.868))}

HERE = os.path.dirname(os.path.abspath(__file__))
TEX_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "Textures"))
PERF_TILE = 0.064                         # metres covered by one tile of T_SeatPerf_* (15.625 tiles per UV unit)


# ----------------------------------------------------------------------------- math helpers
def clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def smooth01(x):
    x = clamp(x)
    return x * x * (3 - 2 * x)


def ramp(x, x0, x1):
    return smooth01((x - x0) / (x1 - x0))


def table(xs, ys, x, smooth=True):
    """Piecewise interpolation (xs ascending, clamped), smoothstepped between keys."""
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    for i in range(len(xs) - 1):
        if xs[i] <= x <= xs[i + 1]:
            t = (x - xs[i]) / (xs[i + 1] - xs[i])
            if smooth:
                t = t * t * (3 - 2 * t)
            return ys[i] + (ys[i + 1] - ys[i]) * t
    return ys[-1]


def cr(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def spline(pts, n=6, closed=False):
    """Catmull-Rom through all points, n samples per span."""
    P = [V(p) for p in pts]
    N = len(P)
    out = []
    for i in range(N if closed else N - 1):
        if closed:
            p0, p1, p2, p3 = P[(i - 1) % N], P[i], P[(i + 1) % N], P[(i + 2) % N]
        else:
            p1, p2 = P[i], P[i + 1]
            p0 = P[i - 1] if i > 0 else 2 * p1 - p2
            p3 = P[i + 2] if i + 2 < N else 2 * p2 - p1
        for k in range(n):
            out.append(cr(p0, p1, p2, p3, k / n))
    if not closed:
        out.append(P[-1].copy())
    return out


def cumlen(P, closed=False):
    acc = [0.0]
    for i in range(1, len(P)):
        acc.append(acc[-1] + (V(P[i]) - V(P[i - 1])).length)
    if closed:
        acc.append(acc[-1] + (V(P[0]) - V(P[-1])).length)
    return acc


def resample(P, n, closed=False):
    """n points evenly spaced by arc length along the polyline P."""
    P = [V(p) for p in P]
    Q = P + [P[0]] if closed else P
    acc = cumlen(Q)
    L = acc[-1]
    out = []
    j = 0
    for k in range(n):
        s = L * k / (n if closed else (n - 1))
        while j < len(acc) - 2 and acc[j + 1] < s:
            j += 1
        seg = acc[j + 1] - acc[j]
        t = 0.0 if seg < 1e-12 else (s - acc[j]) / seg
        out.append(Q[j].lerp(Q[j + 1], clamp(t)))
    return out


def rounded_poly(corners, radii, seg=6):
    """Convex 2D polygon with filleted corners -> dense outline (same winding as the corners)."""
    out = []
    N = len(corners)
    for i in range(N):
        p, a, b, r = V(corners[i]), V(corners[i - 1]), V(corners[(i + 1) % N]), radii[i]
        da, db = (a - p).normalized(), (b - p).normalized()
        ang = math.acos(clamp(da.dot(db), -1.0, 1.0))
        d = r / math.tan(ang / 2)
        p1, p2 = p + da * d, p + db * d
        c = p + (da + db).normalized() * (r / math.sin(ang / 2))
        a1 = math.atan2(p1.y - c.y, p1.x - c.x)
        a2 = math.atan2(p2.y - c.y, p2.x - c.x)
        dd = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
        for k in range(seg + 1):
            t = a1 + dd * k / seg
            out.append(V((c.x + r * math.cos(t), c.y + r * math.sin(t))))
    return out


def rrect(w, h, r, seg=4):
    return rounded_poly([(w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2), (-w / 2, -h / 2)], [r] * 4, seg)


def offset2d(poly, d):
    """Offset a closed CCW 2D polygon outward by d (negative = inward)."""
    N = len(poly)
    out = []
    for i in range(N):
        e = (V(poly[(i + 1) % N]) - V(poly[i - 1]))
        n = V((e.y, -e.x)).normalized()
        out.append(V(poly[i]) + n * d)
    return out


def area2d(poly):
    return 0.5 * sum(poly[i].x * poly[(i + 1) % len(poly)].y - poly[(i + 1) % len(poly)].x * poly[i].y for i in range(len(poly)))


# ----------------------------------------------------------------------------- body envelope (contract: RB.ROWS)
def body_half_section(x):
    """(y, z) of design rows 0..9 at station x: floor centre -> side -> roof centre (outer skin)."""
    return [RB.row_at(r, x) for r in range(len(RB.ROWS))]


def skin_y(x, z):
    """Outer skin half-width at height z (max over the facets of the half section)."""
    pts = body_half_section(x)
    best = 0.0
    for (y0, z0), (y1, z1) in zip(pts, pts[1:]):
        if (z0 - z) * (z1 - z) <= 0 and abs(z1 - z0) > 1e-9:
            best = max(best, y0 + (y1 - y0) * (z - z0) / (z1 - z0))
    return best


def roof_z(x, y):
    """Roof skin height above (x, |y|) from rows 7..9."""
    y = abs(y)
    y7, z7 = RB.row_at(7, x)
    y8, z8 = RB.row_at(8, x)
    z9 = RB.row_at(9, x)[1]
    if y <= y8:
        return z9 + (z8 - z9) * (y / y8)
    return z8 + (z7 - z8) * clamp((y - y8) / max(1e-6, y7 - y8))


def inside_body(p):
    """Signed clearance (m) of point p inside the body section at p.x (negative = outside the skin)."""
    pts = body_half_section(p.x)
    poly = pts + [(0.0, pts[-1][1])][:0]
    y, z = abs(p.y), p.z
    # winding test on the half section closed along the centreline (y = 0)
    ring = poly + [(0.0, pts[0][1])]
    inside = False
    for (y0, z0), (y1, z1) in zip(ring, ring[1:] + ring[:1]):
        if (z0 > z) != (z1 > z):
            yc = y0 + (y1 - y0) * (z - z0) / (z1 - z0)
            if y < yc:
                inside = not inside
    d = 1e9
    for (y0, z0), (y1, z1) in zip(pts, pts[1:]):
        a, b, q = V((y0, z0)), V((y1, z1)), V((y, z))
        ab = b - a
        t = clamp((q - a).dot(ab) / max(1e-12, ab.dot(ab)))
        d = min(d, (a + ab * t - q).length)
    return d if inside else -d


# ----------------------------------------------------------------------------- materials
def interior_materials(M):
    """Shared keys from roadster_materials plus the module's own M_* materials."""
    X = dict(M)
    X["Stitch"] = mat("M_Interior_Stitch", (0.36, 0.36, 0.37), rough=0.8)
    X["PerfDot"] = mat("M_Interior_PerfDot", (0.16, 0.16, 0.165), rough=0.85)
    X["Perf"] = perf_material()
    X["Webbing"] = mat("M_Interior_Webbing", (0.018, 0.018, 0.02), rough=0.75)
    X["Glyph"] = mat("M_Interior_Glyph", (0.85, 0.85, 0.85), rough=0.4, emit=((1.0, 1.0, 1.0), 0.6))
    for k in ("Leather", "Suede", "InteriorDark", "Carbon", "RedAmbient", "Aluminum", "Screen", "ScreenSmall", "BlackGloss", "Titanium"):
        if k not in X:
            raise KeyError("roadster_materials.materials() lacks " + k)
    return X


def perf_material():
    """Ultrawhite leather with the fine 'Braille line' perforation of the seat cushions (T_SeatPerf_D/N)."""
    m = mat("M_Interior_UltrawhitePerf", (0.86, 0.86, 0.84), rough=0.55)
    make_textures()
    nt = m.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    if nt.nodes.get("PerfD"):
        return m
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    s = 1.0 / PERF_TILE
    mp.inputs["Scale"].default_value = (s, s, s)
    nt.links.new(tc.outputs["UV"], mp.inputs["Vector"])
    td = nt.nodes.new("ShaderNodeTexImage"); td.name = "PerfD"
    td.image = bpy.data.images.load(os.path.join(TEX_DIR, "T_SeatPerf_D.png"), check_existing=True)
    tn = nt.nodes.new("ShaderNodeTexImage"); tn.name = "PerfN"
    tn.image = bpy.data.images.load(os.path.join(TEX_DIR, "T_SeatPerf_N.png"), check_existing=True)
    tn.image.colorspace_settings.name = "Non-Color"
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.inputs["Strength"].default_value = 0.8
    for t in (td, tn):
        nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
    nt.links.new(td.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def make_textures(force=False):
    """Tileable seat perforation: rows of 1.1 mm holes at 2 mm pitch, 4 mm row spacing, broken into 'words'
    like Braille text (Slide-4 cushions). T_SeatPerf_D (albedo, sRGB) and T_SeatPerf_N (tangent normal)."""
    pd, pn = os.path.join(TEX_DIR, "T_SeatPerf_D.png"), os.path.join(TEX_DIR, "T_SeatPerf_N.png")
    if not force and os.path.exists(pd) and os.path.exists(pn):
        return pd, pn
    import numpy as np
    N = 512
    px = N / PERF_TILE                       # pixels per metre (8 px/mm)
    rng = random.Random(11)
    cols, rows = 32, 16                      # 2.0 mm x 4.0 mm cells over the 64 mm tile
    h = np.zeros((N, N), dtype=np.float32)
    r = 0.55e-3 * px
    R = int(math.ceil(r)) + 1
    oy, ox = np.mgrid[-R:R + 1, -R:R + 1].astype(np.float32)
    for rr in range(rows):
        # words: runs of 2..7 holes separated by 1..2 blanks; the last cell is blank so the row tiles
        on = []
        while len(on) < cols:
            on += [1] * rng.randint(2, 7) + [0] * rng.randint(1, 2)
        on = on[:cols]
        on[-1] = 0
        for c in range(cols):
            if not on[c]:
                continue
            cx = (c + 0.5 + (0.5 if rr % 2 else 0.0)) * (N / cols)
            cy = (rr + 0.5) * (N / rows)
            ix, iy = int(round(cx)), int(round(cy))
            d = np.sqrt((ox + ix - cx) ** 2 + (oy + iy - cy) ** 2)
            dot = np.clip(1.0 - (d / r) ** 2, 0, 1)
            yi = (np.arange(iy - R, iy + R + 1) % N)[:, None]
            xi = (np.arange(ix - R, ix + R + 1) % N)[None, :]
            h[yi, xi] = np.maximum(h[yi, xi], dot)
    alb = 0.86 - 0.62 * h
    rgb = np.dstack([alb, alb, alb * 0.985, np.ones_like(alb)])
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 2.0
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 2.0
    n = np.dstack((dx, dy, np.ones_like(h)))      # holes are depressions: normal tilts towards the hole centre
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    nrgb = np.dstack([n * 0.5 + 0.5, np.ones_like(h)])
    os.makedirs(TEX_DIR, exist_ok=True)
    for path, arr, cs in ((pd, rgb, "sRGB"), (pn, nrgb, "Non-Color")):
        img = bpy.data.images.new(os.path.basename(path)[:-4], N, N, alpha=True)
        img.colorspace_settings.name = cs
        img.pixels.foreach_set(np.flipud(arr).astype(np.float32).ravel())
        img.filepath_raw = path
        img.file_format = 'PNG'
        img.save()
        bpy.data.images.remove(img)
    return pd, pn


# ----------------------------------------------------------------------------- mesh builder
class MB:
    """bmesh accumulator: faces with material slots, a world-scale UV map, orientation groups."""

    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mats = []
        self.groups = []

    def mi(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def vert(self, co):
        return self.bm.verts.new(V(co))

    def face(self, verts, m, uvs=None):
        vv, uu = [], []
        for k, v in enumerate(verts):
            if v not in vv:
                vv.append(v)
                uu.append(uvs[k] if uvs else None)
        if len(vv) < 3:
            return None
        try:
            f = self.bm.faces.new(vv)
        except ValueError:
            return None
        f.material_index = self.mi(m)
        if uvs:
            for loop, uv in zip(f.loops, uu):
                loop[self.uv].uv = uv
        return f

    def group(self, faces, orient):
        faces = [f for f in faces if f is not None]
        if faces:
            self.groups.append((faces, orient))
        return faces

    # -- surfaces
    def grid(self, P, mats, closed_u=False, closed_v=False, uvs=1.0, orient=None, flip=False, verts=None):
        """P[i][j]: rows i (v direction) of points j (u direction). mats: one material, a list per u-segment,
        or f(i, j). UVs = arc length along rows / columns times uvs (or uvs(m) per material)."""
        nv, nu = len(P), len(P[0])
        Vt = verts or [[self.vert(p) for p in row] for row in P]
        U = [cumlen(row, closed_u) for row in P]
        cols = [[P[i][j] for i in range(nv)] for j in range(nu)]
        Vc = [cumlen(c, closed_v) for c in cols]
        faces = []
        for i in range(nv if closed_v else nv - 1):
            i1 = (i + 1) % nv
            for j in range(nu if closed_u else nu - 1):
                j1 = (j + 1) % nu
                m = mats(i, j) if callable(mats) else (mats[j] if isinstance(mats, (list, tuple)) else mats)
                if m is None:
                    continue
                s = uvs(m) if callable(uvs) else uvs
                q = [Vt[i][j], Vt[i][j1], Vt[i1][j1], Vt[i1][j]]
                t = [(U[i][j] * s, Vc[j][i] * s), (U[i][j + 1] * s, Vc[j1][i] * s),
                     (U[i1][j + 1] * s, Vc[j1][i + 1] * s), (U[i1][j] * s, Vc[j][i + 1] * s)]
                if flip:
                    q, t = q[::-1], t[::-1]
                faces.append(self.face(q, m, t))
        self.group(faces, orient)
        return Vt

    def cap(self, ring, m, orient=None, uvs=1.0, tri=False):
        """Fill a closed ring of BMVerts with one n-gon (planar projection UVs)."""
        pts = [v.co.copy() for v in ring]
        c = sum(pts, V()) / len(pts)
        n = V()
        for a, b in zip(pts, pts[1:] + pts[:1]):
            n += (a - c).cross(b - c)
        n = n.normalized() if n.length > 1e-12 else V((0, 0, 1))
        e1 = (pts[0] - c)
        e1 = (e1 - n * e1.dot(n)).normalized()
        e2 = n.cross(e1)
        s = uvs(m) if callable(uvs) else uvs
        uv = [((p - c).dot(e1) * s, (p - c).dot(e2) * s) for p in pts]
        f = self.face(list(ring), m, uv)
        out = [f]
        if tri and f is not None:
            out = bmesh.ops.triangulate(self.bm, faces=[f], quad_method='BEAUTY', ngon_method='EAR_CLIP')["faces"]
        self.group(out, orient)
        return out

    def skirt(self, chain, z, m, orient, uvs=1.0):
        """Wall from a chain of existing BMVerts straight down to height z."""
        low = [self.vert((v.co.x, v.co.y, z)) for v in chain]
        P = [[v.co.copy() for v in chain], [v.co.copy() for v in low]]
        return self.grid(P, m, uvs=uvs, orient=orient, verts=[list(chain), low])

    def box(self, center, size, m, r=0.0, seg=2, rot=None, uvs=1.0):
        """Closed (bevelled) box, box-projected UVs, outward normals."""
        bm = self.bm
        mt = Matrix.Translation(V(center)) @ (rot.to_4x4() if rot is not None else Matrix.Identity(4))
        res = bmesh.ops.create_cube(bm, size=1.0, matrix=mt @ Matrix.Diagonal((size[0], size[1], size[2], 1.0)))
        vs = res["verts"]
        cube_faces = list({f for v in vs for f in v.link_faces})
        seeds = list(cube_faces)
        if r > 0:
            es = list({e for v in vs for e in v.link_edges})
            rr = min(r, 0.49 * min(size))
            bv = bmesh.ops.bevel(bm, geom=vs + es, offset=rr, offset_type='OFFSET', segments=seg, profile=0.5,
                                 affect='EDGES', clamp_overlap=True)
            seeds = [f for f in cube_faces if f.is_valid] + list(bv["faces"])
        faces = connected_faces(next(f for f in seeds if f.is_valid))
        idx = self.mi(m)
        s = uvs(m) if callable(uvs) else uvs
        inv = mt.inverted()
        for f in faces:
            f.material_index = idx
            f.normal_update()
            nl = (inv.to_3x3() @ f.normal)
            ax = max(range(3), key=lambda k: abs(nl[k]))
            for loop in f.loops:
                p = inv @ loop.vert.co
                c = [p[k] for k in range(3) if k != ax]
                loop[self.uv].uv = (c[0] * s, c[1] * s)
        self.group(faces, 'closed')
        return faces

    def tube(self, path, profile, m, up, caps=True, uvs=1.0, closed_path=False):
        """Sweep a closed 2D profile (list of (a, b)) along a 3D path; b along the up hint, a along T x N."""
        P = []
        for i, p in enumerate(path):
            if closed_path:
                t = (path[(i + 1) % len(path)] - path[i - 1]).normalized()
            else:
                t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
            u = up(p, t) if callable(up) else V(up)
            N = (u - t * u.dot(t)).normalized()
            B = t.cross(N)
            P.append([p + B * a + N * b for (a, b) in profile])
        Vt = self.grid(P, m, closed_u=True, closed_v=closed_path, uvs=uvs, orient=None)
        faces = [f for f in self.groups.pop()[0]]
        if caps and not closed_path:
            faces += self.cap(Vt[0], m, uvs=uvs)
            faces += self.cap(Vt[-1], m, uvs=uvs)
            self.groups.pop(); self.groups.pop()
        self.group(faces, 'closed')
        return Vt

    def disc(self, center, n, e1, r, m, segs=8, lift=0.0, uvs=1.0):
        """Flat polygon facing n (used for perforation dots, grille holes, buttons)."""
        e1 = (e1 - n * e1.dot(n)).normalized()
        e2 = n.cross(e1)
        c = V(center) + n * lift
        vs = [self.vert(c + (e1 * math.cos(2 * math.pi * k / segs) + e2 * math.sin(2 * math.pi * k / segs)) * r) for k in range(segs)]
        uv = [(0.5 + 0.5 * math.cos(2 * math.pi * k / segs), 0.5 + 0.5 * math.sin(2 * math.pi * k / segs)) for k in range(segs)]
        f = self.face(vs, m, uv)
        return f

    def text(self, s, size, m, m4, extrude=0.00025, align='CENTER'):
        """Engraved/raised lettering: font curve -> mesh, placed by m4 (text faces +Z of its own frame)."""
        cu = bpy.data.curves.new("tmp_text", 'FONT')
        cu.body = s
        cu.size = size
        cu.align_x = align
        cu.align_y = 'CENTER'
        cu.extrude = extrude
        cu.resolution_u = 3
        ob = bpy.data.objects.new("tmp_text", cu)
        bpy.context.scene.collection.objects.link(ob)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        vmap = [self.vert(m4 @ v.co) for v in me.vertices]
        faces = []
        for p in me.polygons:
            uv = [((me.vertices[i].co.x) * 4, (me.vertices[i].co.y) * 4) for i in p.vertices]
            faces.append(self.face([vmap[i] for i in p.vertices], m, uv))
        ev.to_mesh_clear()
        bpy.data.objects.remove(ob, do_unlink=True)
        bpy.data.curves.remove(cu)
        self.group(faces, 'closed')
        return faces

    # -- finish
    def finish(self, name, location=None, rotation=None, smooth_angle=38.0, local=None):
        bm = self.bm
        for faces, orient in self.groups:
            faces = [f for f in faces if f.is_valid]
            if not faces or orient is None:
                continue
            if orient == 'closed':
                bmesh.ops.recalc_face_normals(bm, faces=faces)
                continue
            kind, ref = orient
            score = 0.0
            for f in faces:
                f.normal_update()
                c = f.calc_center_median()
                a = f.calc_area()
                if kind == 'toward':
                    score += f.normal.dot(V(ref(c) if callable(ref) else ref) - c) * a
                elif kind == 'away':
                    score -= f.normal.dot(V(ref(c) if callable(ref) else ref) - c) * a
                else:   # 'dir'
                    score += f.normal.dot(V(ref)) * a
            if score < 0:
                bmesh.ops.reverse_faces(bm, faces=faces)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=bm.edges[:])
        bm.normal_update()
        # sharp edges by angle, smooth faces (same result as shade_smooth_by_angle)
        lim = math.radians(smooth_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) == 2:
                a, b = e.link_faces
                e.smooth = a.normal.angle(b.normal, 0.0) < lim and a.material_index == b.material_index
            else:
                e.smooth = True
        if local is not None:
            bmesh.ops.transform(bm, matrix=local, verts=bm.verts[:])
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        for m in self.mats:
            me.materials.append(m)
        if location is not None:
            ob.location = location
        if rotation is not None:
            ob.rotation_euler = rotation
        return ob


def connected_faces(seed):
    seen = {seed}
    stack = [seed]
    while stack:
        f = stack.pop()
        for e in f.edges:
            for g in e.link_faces:
                if g not in seen:
                    seen.add(g)
                    stack.append(g)
    return list(seen)


def grid_eval(G, fi, fj):
    """Bilinear point / normal / tangents on a point grid at fractional indices."""
    i0 = int(clamp(math.floor(fi), 0, len(G) - 2)); ti = fi - i0
    j0 = int(clamp(math.floor(fj), 0, len(G[0]) - 2)); tj = fj - j0
    p00, p01, p10, p11 = G[i0][j0], G[i0][j0 + 1], G[i0 + 1][j0], G[i0 + 1][j0 + 1]
    p = (p00 * (1 - tj) + p01 * tj) * (1 - ti) + (p10 * (1 - tj) + p11 * tj) * ti
    du = (p01 - p00) * (1 - ti) + (p11 - p10) * ti
    dv = (p10 - p00) * (1 - tj) + (p11 - p01) * tj
    return p, du.cross(dv).normalized(), du.normalized(), dv.normalized()


def frac_index(values, x):
    """Fractional index of x in a monotonic list."""
    inc = values[-1] >= values[0]
    for i in range(len(values) - 1):
        a, b = values[i], values[i + 1]
        if (a <= x <= b) if inc else (b <= x <= a):
            return i + (x - a) / (b - a) if b != a else i
    return 0.0 if (x < values[0]) == inc else len(values) - 1.0


def carbon_uv(m):
    return 2.0 if m.name.startswith("M_Carbon") else 1.0


def toward_cabin(c):
    """Reference point for 'faces look into the cabin'."""
    return V((c.x, 0.0, 0.55))


# ============================================================================= SEATS
CUSH_C = [(-0.175, 0.200), (-0.10, 0.212), (0.0, 0.232), (0.10, 0.262), (0.20, 0.300), (0.29, 0.342), (0.36, 0.377),
          (0.405, 0.396), (0.437, 0.392), (0.455, 0.372), (0.463, 0.338), (0.464, 0.288), (0.460, 0.230), (0.454, 0.182)]
# (w lateral [outer +], h scaled by the bolster height, h absolute, material) ; G = seam groove
CUSH_PROF = [
    (-0.236, 0.00, -0.055, 'L'), (-0.238, 0.00, -0.022, 'L'), (-0.236, 0.32, 0.0, 'L'), (-0.229, 0.63, 0.0, 'L'),
    (-0.217, 0.86, 0.0, 'L'), (-0.203, 0.97, 0.0, 'L'), (-0.189, 0.90, 0.0, 'S'), (-0.176, 0.64, 0.0, 'S'),
    (-0.164, 0.34, 0.0, 'S'), (-0.156, 0.10, 0.002, 'S'), (-0.151, 0.0, -0.003, 'G'), (-0.146, 0.0, -0.003, 'G'),
    (-0.141, 0.0, 0.006, 'P'), (-0.115, 0.0, 0.011, 'P'), (-0.075, 0.0, 0.013, 'P'), (-0.035, 0.0, 0.014, 'P'),
    (0.0, 0.0, 0.014, 'P'), (0.035, 0.0, 0.014, 'P'), (0.075, 0.0, 0.013, 'P'), (0.115, 0.0, 0.011, 'P'),
    (0.141, 0.0, 0.006, 'G'), (0.146, 0.0, -0.003, 'G'), (0.151, 0.0, -0.003, 'S'), (0.157, 0.08, 0.002, 'S'),
    (0.166, 0.25, 0.0, 'S'), (0.178, 0.48, 0.0, 'S'), (0.192, 0.71, 0.0, 'S'), (0.206, 0.88, 0.0, 'L'),
    (0.220, 0.97, 0.0, 'L'), (0.235, 0.96, 0.0, 'L'), (0.247, 0.80, 0.0, 'L'), (0.255, 0.52, 0.0, 'L'),
    (0.259, 0.16, 0.0, 'L'), (0.258, 0.00, -0.040, 'L'),
]
BACK_C = [(-0.078, 0.110), (-0.103, 0.170), (-0.129, 0.235), (-0.157, 0.302), (-0.187, 0.372), (-0.218, 0.443),
          (-0.249, 0.514), (-0.280, 0.585), (-0.310, 0.655), (-0.340, 0.726), (-0.368, 0.796), (-0.392, 0.858),
          (-0.410, 0.905), (-0.420, 0.935)]
BACK_FRONT = [
    (-1.000, 0.20, 0.0, 'L'), (-0.985, 0.55, 0.0, 'L'), (-0.952, 0.85, 0.0, 'L'), (-0.905, 1.00, 0.0, 'L'),
    (-0.855, 0.96, 0.0, 'S'), (-0.795, 0.80, 0.0, 'S'), (-0.735, 0.55, 0.0, 'S'), (-0.678, 0.30, 0.0, 'S'),
    (-0.628, 0.10, 0.002, 'S'), (-0.602, 0.0, -0.003, 'G'), (-0.586, 0.0, -0.003, 'G'), (-0.570, 0.0, 0.006, 'L'),
    (-0.46, 0.0, 0.010, 'L'), (-0.34, 0.0, 0.012, 'L'), (-0.22, 0.0, 0.013, 'L'), (-0.11, 0.0, 0.014, 'L'),
    (0.0, 0.0, 0.014, 'L'), (0.11, 0.0, 0.014, 'L'), (0.22, 0.0, 0.013, 'L'), (0.34, 0.0, 0.012, 'L'),
    (0.46, 0.0, 0.010, 'L'), (0.570, 0.0, 0.006, 'G'), (0.586, 0.0, -0.003, 'G'), (0.602, 0.0, -0.003, 'S'),
    (0.628, 0.10, 0.002, 'S'), (0.678, 0.30, 0.0, 'S'), (0.735, 0.55, 0.0, 'S'), (0.795, 0.80, 0.0, 'S'),
    (0.855, 0.96, 0.0, 'L'), (0.905, 1.00, 0.0, 'L'), (0.952, 0.85, 0.0, 'L'), (0.985, 0.55, 0.0, 'L'),
    (1.000, 0.20, 0.0, 'L'),
]


def braille_cells(rng, n):
    """n random Braille-like cells: lists of (col 0..1, row 0..2) dots, 3..6 dots each."""
    out = []
    for _ in range(n):
        dots = [(c, r) for c in range(2) for r in range(3) if rng.random() < 0.55]
        while len(dots) < 3:
            d = (rng.randint(0, 1), rng.randint(0, 2))
            if d not in dots:
                dots.append(d)
        out.append(dots)
    return out


def build_seat(X, rng_seed=5):
    """Driver bucket in car coordinates (y = +0.38). Returns the MB (the passenger seat mirrors it)."""
    mb = MB()
    keys = {'L': X["Leather"], 'S': X["Suede"], 'P': X["Perf"], 'G': X["Leather"]}
    yc = Y_DRV
    rng = random.Random(rng_seed)

    # ---------------- cushion: sweep of the cross-section along the centreline (rear -> front -> down the nose)
    C = resample(spline([V((x, 0, z)) for x, z in CUSH_C], 6), 46)
    L = cumlen(C)
    S = [l / L[-1] for l in L]
    rows = []
    for i, c in enumerate(C):
        t = (C[min(i + 1, len(C) - 1)] - C[max(i - 1, 0)]).normalized()
        n = V((-t.z, 0, t.x))                       # up for a forward-running curve
        k = table([0.0, 0.18, 0.62, 0.76, 0.86, 1.0], [0.78, 1.0, 1.0, 0.82, 0.45, 0.32], S[i])
        row = []
        for (w, hs, ha, _) in CUSH_PROF:
            H = 0.042 if w < 0 else 0.102
            row.append(V((c.x, yc + w, c.z)) + n * (hs * H * k + ha))
        rows.append(row)
    mats = [keys[p[3]] for p in CUSH_PROF[:-1]]
    G = mb.grid(rows, mats, orient=('dir', V((0, 0, 1))))
    # skirts: inner side, front, outer side down to the tub
    mb.skirt([r[0] for r in G], FLOOR - 0.006, keys['L'], orient=('toward', V((0.1, yc, 0.3))))
    mb.skirt([r[-1] for r in G], FLOOR - 0.006, keys['L'], orient=('toward', V((0.1, yc, 0.3))))
    mb.skirt(G[-1], FLOOR - 0.006, keys['L'], orient=('dir', V((1, 0, 0))))
    # stitches along the cushion seams (double row either side of each groove)
    seat_cushion_stitches(mb, X, rows)
    # Braille-like perforation clusters on the bolster tops (the white humps in Slide-4)
    w_cols = [p[0] for p in CUSH_PROF]
    cells = braille_cells(rng, 200)
    ci = 0
    for (w0, w1, sp) in ((0.200, 0.246, 0.034), (-0.226, -0.206, 0.034)):
        rowk = 0
        sm = 0.10 * L[-1]
        while sm < 0.74 * L[-1]:
            w = w0 + (0.5 * sp if rowk % 2 else 0.0) * (1 if w1 - w0 > sp else 0)
            while w <= w1 + 1e-6:
                for (cc, rr) in cells[ci % len(cells)]:
                    fi = frac_index(L, sm + (rr - 1) * 0.0052)
                    fj = frac_index(w_cols, w + (cc - 0.5) * 0.0052)
                    p, nn, du, dv = grid_eval(rows, fi, fj)
                    if nn.z < 0:
                        nn = -nn
                    f = mb.disc(p, nn, dv, 0.00135, X["PerfDot"], segs=8, lift=0.0005)
                    mb.group([f], ('dir', nn))
                ci += 1
                w += sp
            sm += sp * 0.9
            rowk += 1

    # ---------------- back: closed-loop sweep (front profile + rear shell), rounded top cap
    B = resample(spline([V((x, 0, z)) for x, z in BACK_C], 6), 44)
    LB = cumlen(B)
    SB = [l / LB[-1] for l in LB]
    Wf = lambda s: table([0, 0.12, 0.36, 0.62, 0.74, 0.83, 0.91, 1.0], [0.250, 0.250, 0.240, 0.262, 0.262, 0.212, 0.165, 0.150], s)
    Hw = lambda s: table([0, 0.10, 0.25, 0.55, 0.72, 0.83, 0.92, 1.0], [0.050, 0.078, 0.106, 0.100, 0.088, 0.052, 0.034, 0.028], s)
    Tk = lambda s: table([0, 1.0], [0.120, 0.072], s, smooth=False)
    lumbar = lambda s: 0.016 * math.exp(-((s - 0.27) / 0.10) ** 2)
    F = len(BACK_FRONT)
    loops, frames = [], []
    for i, c in enumerate(B):
        s = SB[i]
        t = (B[min(i + 1, len(B) - 1)] - B[max(i - 1, 0)]).normalized()
        n = V((t.z, 0, -t.x))                       # towards the occupant (forward-up)
        W, H, T = Wf(s), Hw(s), Tk(s)
        loop = []
        for (u, hs, ha, _) in BACK_FRONT:
            ha2 = ha + (lumbar(s) if abs(u) < 0.58 else 0.0)
            loop.append(V((c.x, yc + u * W, c.z)) + n * (hs * H + ha2))
        for (u, hs, ha, _) in reversed(BACK_FRONT):
            g = (1 - min(1.0, abs(u)) ** 8) ** 0.3
            loop.append(V((c.x, yc + u * W * (1 + 0.035 * abs(u) ** 4), c.z)) + n * (-T * g))
        loops.append(loop)
        frames.append((c, t, n, W, H, T))
    # rounded top: the loop flattens towards its mid-thickness while rising along the back
    c, t, n, W, H, T = frames[-1]
    last = loops[-1]
    mid = c + n * (-0.5 * T)
    for k in (1, 2, 3, 4):
        a = math.radians(22.0 * k)
        lift = t * (0.55 * T * math.sin(a))
        L2 = []
        for p in last:                  # keep the lateral extent, fold the thickness over the top
            d = p - mid
            dn = n * d.dot(n)
            L2.append(mid + (d - dn) * (1.0 - 0.05 * k / 4) + dn * math.cos(a) + lift)
        loops.append(L2)
    mats_back = [keys[p[3]] for p in BACK_FRONT[:-1]] + [keys['L']] * (F + 1)
    GB = mb.grid(loops, mats_back, closed_u=True, orient=None)
    back_faces = mb.groups.pop()[0]
    top = GB[-1]
    zip_faces = []
    for j in range(F - 1):
        zip_faces.append(mb.face([top[j], top[j + 1], top[2 * F - 2 - j], top[2 * F - 1 - j]], keys['L'],
                                 [(j * 0.01, 0), ((j + 1) * 0.01, 0), ((j + 1) * 0.01, 0.01), (j * 0.01, 0.01)]))
    mb.group(back_faces + zip_faces, 'closed')

    # perforation clusters (geometry): front centre panel, head panel, upper rear shell
    front_grid = [l[:F] for l in loops[:len(B)]]
    rear_grid = [l[F:] for l in loops[:len(B)]]
    us = [p[0] for p in BACK_FRONT]
    cells = braille_cells(rng, 400)
    ci = 0
    pitch = 0.0052
    for (s0, s1, w0, w1, grid, sign, sp) in ((0.20, 0.80, -0.125, 0.125, front_grid, 1, 0.040),
                                               (0.84, 0.97, -0.090, 0.090, front_grid, 1, 0.036),
                                               (0.58, 0.96, -0.170, 0.170, rear_grid, -1, 0.046)):
        rowk = 0
        sm = s0 * LB[-1]
        while sm <= s1 * LB[-1]:
            stag = 0.5 * sp if rowk % 2 else 0.0
            w = w0 + stag
            while w <= w1:
                dots = cells[ci % len(cells)]; ci += 1
                for (cc, rr) in dots:
                    s_dot = sm + (1 - rr) * pitch
                    w_dot = w + (cc - 0.5) * pitch
                    fi = frac_index(LB, s_dot)
                    s_n = s_dot / LB[-1]
                    u = w_dot / Wf(s_n)
                    if grid is rear_grid:
                        fj = frac_index(list(reversed(us)), u)
                    else:
                        fj = frac_index(us, u)
                    p, nn, du, dv = grid_eval(grid, fi, fj)
                    ref = frames[min(int(fi), len(frames) - 1)][2] * sign
                    if nn.dot(ref) < 0:
                        nn = -nn
                    f = mb.disc(p, nn, dv, 0.00135, X["PerfDot"], segs=8, lift=0.0005)
                    mb.group([f], ('dir', nn))
                w += sp
            sm += sp * 0.92
            rowk += 1

    # stitches along the back seams
    for side in (-1, 1):
        for off in (-0.0045, 0.0045):
            pts = []
            for i in range(2, len(B) - 2):
                s = SB[i]
                u = (side * 0.594) + off / Wf(s)
                p, nn, du, dv = grid_eval(front_grid, i, frac_index(us, u))
                pts.append((p, nn))
            stitch_line_pn(mb, X["Stitch"], pts)

    # ---------------- belt (stowed 3-point), shoulder guide, buckle, recline lever
    belt(mb, X, frames, Wf, Hw, front_grid, us, yc)
    return mb


def stitch_line_pn(mb, m, pn, pitch=0.0056, length=0.0034, width=0.0009, lift=0.0004):
    """Dashes along a polyline of (point, normal): raised thread stitches."""
    pts = [p for p, _ in pn]
    acc = cumlen(pts)
    s = pitch * 0.5
    j = 0
    faces = []
    while s < acc[-1] - pitch * 0.5:
        while j < len(acc) - 2 and acc[j + 1] < s:
            j += 1
        t = (s - acc[j]) / max(1e-9, acc[j + 1] - acc[j])
        p = pts[j].lerp(pts[j + 1], t)
        n = pn[j][1].lerp(pn[j + 1][1], t).normalized()
        d = (pts[j + 1] - pts[j])
        d = (d - n * d.dot(n)).normalized()
        e = n.cross(d)
        c = p + n * lift
        q = [c - d * length / 2 - e * width / 2, c + d * length / 2 - e * width / 2, c + d * length / 2 + e * width / 2, c - d * length / 2 + e * width / 2]
        f = mb.face([mb.vert(x) for x in q], m, [(0, 0), (1, 0), (1, 1), (0, 1)])
        if f:
            mb.group([f], ('dir', n))
        s += pitch
    return faces


def belt(mb, X, frames, Wf, Hw, front_grid, us, yc):
    """Stowed 3-point belt on the outer side of the back, shoulder guide, inner buckle, recline lever. INFERRED
    (belts are not visible in the official photos; Tesla-style black webbing, slot guide in the shoulder wing)."""
    # shoulder guide on the outer wing at s ~ 0.80
    i_g = int(0.80 * (len(frames) - 1))
    c, t, n, W, H, T = frames[i_g]
    g0 = V((c.x, yc + W * 1.0, c.z)) + n * (0.45 * H) + t * 0.0
    mb.box(g0 + n * 0.004, (0.022, 0.016, 0.060), X["InteriorDark"], r=0.005, seg=2,
           rot=Matrix.Rotation(math.atan2(t.x, t.z), 3, 'Y'))
    # webbing hangs from the guide down the outer flank to the lower anchor on the sill
    path = [g0 + V((0, 0.010, 0)) + n * 0.008]
    for s in (0.70, 0.55, 0.40, 0.26, 0.14):
        i = int(s * (len(frames) - 1))
        c, t, n2, W2, H2, T2 = frames[i]
        path.append(V((c.x, yc + W2 * 1.02 + 0.010, c.z)) + n2 * (0.10 * H2 - 0.012))
    anchor = V((-0.185, yc + 0.272, 0.205))
    path.append(anchor + V((0, 0, 0.03)))
    path.append(anchor)
    path = spline(path, 4)
    prof = [(-0.024, -0.0008), (0.024, -0.0008), (0.024, 0.0008), (-0.024, 0.0008)]
    mb.tube(path, prof, X["Webbing"], up=V((0, 1, 0)), uvs=1.0)
    # anchor bolt cover + latch plate on the belt
    mb.box(anchor + V((0, 0.004, -0.004)), (0.035, 0.012, 0.030), X["Titanium"], r=0.004)
    k = int(len(path) * 0.62)
    p, q = path[k], path[k + 1]
    ang = math.atan2((q - p).x, (q - p).z)
    mb.box(p + V((0, 0.006, 0)), (0.030, 0.006, 0.050), X["Aluminum"], r=0.002, rot=Matrix.Rotation(ang, 3, 'Y'))
    mb.box(p + V((0, 0.007, 0.012)), (0.036, 0.009, 0.024), X["InteriorDark"], r=0.003, rot=Matrix.Rotation(ang, 3, 'Y'))
    # buckle stalk between seat and spine
    base = V((-0.075, yc - 0.252, FLOOR))
    top = V((-0.040, yc - 0.252, 0.300))
    stalk = spline([base, base + V((0.01, 0, 0.08)), top], 4)
    mb.tube(stalk, [(-0.012, -0.0016), (0.012, -0.0016), (0.012, 0.0016), (-0.012, 0.0016)], X["Webbing"], up=V((0, 1, 0)))
    ang = math.atan2((top - stalk[-2]).x, (top - stalk[-2]).z)
    mb.box(top + V((0.006, 0, 0.022)), (0.022, 0.030, 0.060), X["BlackGloss"], r=0.006, rot=Matrix.Rotation(ang, 3, 'Y'))
    mb.box(top + V((0.012, 0, 0.046)), (0.012, 0.024, 0.010), X["Aluminum"], r=0.002, rot=Matrix.Rotation(ang, 3, 'Y'))
    # recline lever (the seatbacks are the only adjustable part) on the outer flank
    lev = [V((-0.115, yc + 0.266, 0.262)), V((-0.05, yc + 0.270, 0.258)), V((0.03, yc + 0.271, 0.250))]
    mb.tube(spline(lev, 3), [(a * 0.011, b * 0.006) for a, b in rrect(2, 2, 0.6, 2)], X["Carbon"], up=V((0, 1, 0)), uvs=carbon_uv)


def seat_cushion_stitches(mb, X, rows):
    """Stitches either side of the two cushion seams, sampled on the swept rows."""
    w_cols = [p[0] for p in CUSH_PROF]
    for gw in (-0.1485, 0.1485):
        for off in (-0.0045, 0.0045):
            fj = frac_index(w_cols, gw + off)
            pn = []
            for i in range(2, len(rows) - 9):
                p, n, du, dv = grid_eval(rows, i, fj)
                if n.z < 0:
                    n = -n
                pn.append((p, n))
            stitch_line_pn(mb, X["Stitch"], pn)


def mirror_mb(src_obj, name):
    """Passenger seat: mirror the driver seat mesh across y = 0 (normals re-oriented)."""
    me = src_obj.data.copy()
    me.name = name
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((1, -1, 1, 1)), verts=bm.verts[:])
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


# ============================================================================= YOKE (local frame: +X column axis)
def yoke_outline(a, b, n, K, cz=0.0):
    pts = [V((a * math.copysign(abs(math.cos(t)) ** (2 / n), math.cos(t)), b * math.copysign(abs(math.sin(t)) ** (2 / n), math.sin(t)) + cz))
           for t in [2 * math.pi * k / 2048 for k in range(2048)]]
    return pts


def build_yoke(X):
    """Squircle yoke: Ultrawhite outer band, gray microsuede face, dark inner edge, pentagon airbag hub with the
    T emblem, two spokes with button clusters (scroll wheels, signal / voice buttons), aluminium paddles behind the
    spokes: upper pair (regen / shift) and lower pair (left DRS, right BOOST - the BOOST paddle is on the RIGHT
    spoke in Sawyer Merritt's frame cabin_touchscreen_bezel_boost_button)."""
    mb = MB()
    xr = -YOKE_SETBACK                         # rim section centre plane (local x)
    K = 132
    A, Bh = 0.165, 0.086                       # outer half width / half height (0.33 x 0.172 m, Slide-4 aspect)
    Ai, Bi = 0.119, 0.0655                     # inner opening: 46 mm grips, ~20 mm top / bottom bars
    NO, NI = 4.6, 4.0                          # superellipse exponents (rounded-rectangle squircle)
    outer_d = yoke_outline(A, Bh, NO, 2048)
    acc = cumlen(outer_d, closed=True)
    th = []
    j = 0
    for k in range(K):
        s = acc[-1] * k / K
        while acc[j + 1] < s:
            j += 1
        th.append(2 * math.pi * (j + (s - acc[j]) / (acc[j + 1] - acc[j])) / 2048)
    sec = [  # (t radial 0 inner..1 outer, extra radial m, x as fraction of depth, material)
        (0.0, -0.0025, -0.22, 'D'), (0.0, -0.0032, 0.0, 'D'), (0.0, -0.0025, 0.22, 'D'), (0.0, 0.0010, 0.42, 'L'),
        (0.12, 0.0, 0.50, 'L'), (0.45, 0.0, 0.52, 'L'), (0.80, 0.0, 0.50, 'L'), (1.0, -0.0050, 0.45, 'L'),
        (1.0, -0.0005, 0.30, 'L'), (1.0, 0.0012, 0.05, 'L'), (1.0, 0.0002, -0.20, 'L'), (1.0, -0.0030, -0.38, 'L'),
        (1.0, -0.0075, -0.47, 'S'), (0.82, 0.0, -0.53, 'S'), (0.55, 0.0, -0.555, 'S'), (0.28, 0.0, -0.545, 'S'),
        (0.08, 0.0, -0.51, 'S'), (0.0, 0.0018, -0.44, 'D'), (0.0, -0.0008, -0.36, 'D'),
    ]
    keys = {'L': X["Leather"], 'S': X["Suede"], 'D': X["InteriorDark"]}
    rings = []
    for t in th:
        c, s = math.cos(t), math.sin(t)
        po = V((A * math.copysign(abs(c) ** (2 / NO), c), Bh * math.copysign(abs(s) ** (2 / NO), s)))
        pi = V((Ai * math.copysign(abs(c) ** (2 / NI), c), Bi * math.copysign(abs(s) ** (2 / NI), s) + 0.0012))
        r = (po - pi)
        w = r.length
        rh = r.normalized()
        D = 0.028 + 0.014 * abs(c) ** 2
        ring = []
        for (tt, e, xf, _) in sec:
            q = pi + rh * (tt * w + e)
            ring.append(V((xr + xf * D, q.x, q.y)))
        rings.append(ring)
    mb.grid(rings, [keys[s[3]] for s in sec], closed_u=True, closed_v=True, orient=None)
    mb.groups[-1] = (mb.groups[-1][0], 'closed')

    # --- hub: rounded pentagon (flat top, point-down with a short flat), domed front, soft edge
    corners = [(0.062, 0.050), (-0.062, 0.050), (-0.065, -0.006), (-0.024, -0.068), (0.024, -0.068), (0.065, -0.006)]
    outline = resample(rounded_poly(corners, [0.017, 0.017, 0.020, 0.016, 0.016, 0.020], 8), 72, closed=True)
    if area2d(outline) < 0:
        outline.reverse()
    cz = sum(p.y for p in outline) / len(outline)
    rings2 = []
    spec = [(0.88, 0.0, xr + 0.034), (1.0, 0.0, xr + 0.016), (1.0, 0.0, xr - 0.022), (1.0, -0.003, xr - 0.033),
            (1.0, -0.008, xr - 0.0395), (0.82, -0.008, xr - 0.0438), (0.58, -0.008, xr - 0.0462), (0.30, -0.008, xr - 0.0476)]
    for (sc, inset, x) in spec:
        o = offset2d(outline, inset) if inset else outline
        o = [V((p.x * sc, (p.y - cz) * sc + cz)) for p in o]
        rings2.append([V((x, p.x, p.y)) for p in o])
    Gh = mb.grid(rings2, X["Suede"], closed_u=True, orient=None)
    hub_faces = mb.groups.pop()[0]
    hub_faces += mb.cap(Gh[-1], X["Suede"]); mb.groups.pop()
    hub_faces += mb.cap(Gh[0], X["InteriorDark"]); mb.groups.pop()
    mb.group(hub_faces, 'closed')
    # column boss behind the hub (rotates with the yoke; the static shroud continues from here)
    boss = [V((xr + 0.030 + 0.012 * k, 0, cz)) for k in range(4)]
    mb.tube(boss, [(0.030 * math.cos(2 * math.pi * k / 24), 0.030 * math.sin(2 * math.pi * k / 24)) for k in range(24)],
            X["InteriorDark"], up=V((0, 0, 1)))
    # T emblem on the dome (raised, gloss black on the matte suede)
    xe = xr - 0.0478
    T = [(-0.0125, 0.020), (0.0125, 0.020), (0.0125, 0.0155), (0.0028, 0.0130), (0.0028, -0.0150), (0.0, -0.0175),
         (-0.0028, -0.0150), (-0.0028, 0.0130), (-0.0125, 0.0155)]
    TT = [(-0.0125, 0.0245), (0.0125, 0.0245), (0.0100, 0.0222), (-0.0100, 0.0222)]
    for poly in (T, TT):
        front = [mb.vert((xe - 0.0007, -px, pz + 0.004)) for (px, pz) in poly]
        backv = [mb.vert((xe + 0.0010, -px, pz + 0.004)) for (px, pz) in poly]
        fs = mb.cap(front, X["BlackGloss"]); mb.groups.pop()
        fs += mb.cap(backv, X["BlackGloss"]); mb.groups.pop()
        n = len(poly)
        for k in range(n):
            fs.append(mb.face([front[k], front[(k + 1) % n], backv[(k + 1) % n], backv[k]], X["BlackGloss"],
                              [(0, 0), (0.01, 0), (0.01, 0.01), (0, 0.01)]))
        mb.group(fs, 'closed')

    # --- spokes: housings from the hub flanks into the grips, button clusters on their faces
    for side in (1, -1):
        yc = side * 0.090
        ZS = 0.010                             # spokes sit ~1 cm above the rim centre (Slide-4)
        mb.box(V((xr - 0.004, yc, ZS)), (0.034, 0.066, 0.031), X["InteriorDark"], r=0.009, seg=3)
        mb.box(V((xr - 0.0215, yc + side * 0.004, ZS + 0.0125)), (0.003, 0.052, 0.0030), X["Aluminum"], r=0.0010, seg=1)
        # scroll wheel (axis along the spoke), recessed half way into the housing face
        wc = V((xr - 0.020, side * 0.072, ZS))
        wheel = [wc + V((0, side * (-0.006 + 0.004 * k), 0)) for k in range(4)]
        prof = [(0.0092 * math.cos(2 * math.pi * k / 20) * (0.92 if k % 2 else 1.0), 0.0092 * math.sin(2 * math.pi * k / 20) * (0.92 if k % 2 else 1.0)) for k in range(20)]
        mb.tube(wheel, prof, X["BlackGloss"], up=V((1, 0, 0)))
        # two buttons with glyphs (signal arrows / voice), towards the grip
        for (dz, glyph) in ((0.0055, 'voice'), (-0.0085, 'arrows')):
            bc = V((xr - 0.0205, side * 0.103, ZS + dz))
            mb.box(bc, (0.004, 0.015, 0.0115), X["BlackGloss"], r=0.0018, seg=2)
            gx = bc.x - 0.0023
            if glyph == 'arrows':
                for sgn in (1, -1):
                    tri = [V((gx, bc.y + sgn * 0.0045, bc.z + 0.0018 * sgn)), V((gx, bc.y - sgn * 0.0005, bc.z + 0.0018 * sgn + 0.0022)),
                           V((gx, bc.y - sgn * 0.0005, bc.z + 0.0018 * sgn - 0.0022))]
                    f = mb.face([mb.vert(p) for p in tri], X["Glyph"], [(0, 0), (1, 0), (0, 1)])
                    mb.group([f], ('dir', V((-1, 0, 0))))
            else:
                f = mb.disc(V((gx, bc.y, bc.z)), V((-1, 0, 0)), V((0, 1, 0)), 0.0022, X["Glyph"], segs=10)
                mb.group([f], ('dir', V((-1, 0, 0))))
        # paddles: upper (regen) and lower (left DRS / right BOOST), brushed aluminium, mounted behind the spoke
        for (dz, label) in ((0.042, None), (-0.030, "BOOST" if side < 0 else "DRS")):
            pc = V((xr + 0.021, side * 0.101, dz))
            tilt = Matrix.Rotation(math.radians(-6 * side), 3, 'X')
            mb.box(pc, (0.005, 0.032, 0.025), X["Aluminum"], r=0.0018, seg=2, rot=tilt)
            mb.box(pc + V((-0.008, -side * 0.010, -0.45 * dz)), (0.014, 0.012, 0.016), X["InteriorDark"], r=0.003)
            if label:
                m4 = Matrix.Translation(pc + V((-0.0027, 0, -0.004))) @ tilt.to_4x4() @ Matrix(((0, 0, -1, 0), (-1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
                mb.text(label, 0.0058, X["InteriorDark"], m4, extrude=0.00012)
    ob = mb.finish("Yoke", smooth_angle=40)
    ob.location = YOKE_PIVOT
    ob.rotation_euler = (0.0, YOKE_PITCH, 0.0)
    return ob


def yoke_matrix():
    return Matrix.Translation(YOKE_PIVOT) @ Matrix.Rotation(YOKE_PITCH, 4, 'Y')


# ============================================================================= SCREENS
def screen_matrix():
    return Matrix.Translation(SCREEN_LOC) @ Matrix.Rotation(SCREEN_ROT[2], 4, 'Z') @ Matrix.Rotation(SCREEN_ROT[1], 4, 'Y')


def build_screen(X):
    """Screen_Display (one quad, UV 0..1) and Screen_Bezel (thin aluminium housing, black border glass, stand)."""
    w, h = SCREEN_W, SCREEN_H
    mb = MB()
    q = [mb.vert((-0.0062, w / 2, -h / 2)), mb.vert((-0.0062, -w / 2, -h / 2)), mb.vert((-0.0062, -w / 2, h / 2)), mb.vert((-0.0062, w / 2, h / 2))]
    f = mb.face(q, X["Screen"], [(0, 0), (1, 0), (1, 1), (0, 1)])
    mb.group([f], ('dir', V((-1, 0, 0))))
    disp = mb.finish("Screen_Display", smooth_angle=10)
    disp.location = SCREEN_LOC
    disp.rotation_euler = (0, SCREEN_ROT[1], SCREEN_ROT[2])

    mb = MB()
    bw, bh = w + 0.0110, h + 0.0110              # 5.5 mm border all round: 3 mm black glass + 2.5 mm aluminium
    out = resample(rrect(bw, bh, 0.0085, 6), 96, closed=True)
    inner = resample(rrect(bw - 0.0050, bh - 0.0050, 0.0062, 6), 96, closed=True)
    hole = resample(rrect(w, h, 0.0015, 2), 96, closed=True)
    # match starting points (all start at the right-bottom corner region by construction)
    def ring(poly, x):
        return [V((x, p.x, p.y)) for p in poly]
    A0 = ring(hole, 0.0030)
    A = ring(hole, -0.0059)
    Bv = ring(inner, -0.0059)
    Cc = ring(offset2d(inner, 0.0012), -0.0056)
    Dd = ring(offset2d(out, -0.0008), -0.0050)
    Ee = ring(out, -0.0035)
    Ff = ring(out, 0.0050)
    Gg = ring(offset2d(out, -0.0015), 0.0078)
    Hh = ring(offset2d(out, -0.0060), 0.0088)
    P = [A0, A, Bv, Cc, Dd, Ee, Ff, Gg, Hh]
    mats = lambda i, j: X["BlackGloss"] if i <= 1 else X["Aluminum"]
    Gs = mb.grid(P, mats, closed_u=True, orient=None)
    faces = mb.groups.pop()[0]
    faces += mb.cap(Gs[-1], X["Aluminum"]); mb.groups.pop()
    # glass backing behind the display (closes the housing; not visible through the opaque display)
    faces += mb.cap(Gs[0], X["BlackGloss"]); mb.groups.pop()
    for f in faces:
        pass
    mb.group(faces, 'closed')
    # stand: VESA boss on the back, a short tapered neck down to the console top (world z 0.600)
    inv = screen_matrix().inverted()
    foot_w = V((0.835, 0.0, 0.600))
    foot = inv @ foot_w
    neck = spline([V((0.009, 0, -0.035)), V((0.030, 0, -0.050)), V((0.050, 0, -0.075)), foot + V((0, 0, 0.012))], 5)
    mb.tube(neck, [(a, b) for a, b in rrect(0.050, 0.016, 0.006, 3)], X["Aluminum"], up=lambda p, t: V((1, 0, 0)) if abs(t.x) < 0.9 else V((0, 0, 1)))
    mb.box(V((0.012, 0, -0.025)), (0.008, 0.090, 0.070), X["Aluminum"], r=0.004)
    mb.box(foot + V((0, 0, 0.004)), (0.075, 0.090, 0.008), X["Aluminum"], r=0.003)
    bez = mb.finish("Screen_Bezel", smooth_angle=40)
    bez.location = SCREEN_LOC
    bez.rotation_euler = (0, SCREEN_ROT[1], SCREEN_ROT[2])
    return bez, disp


def fwd_matrix(pos):
    """Forward display frame: local -X faces the driver's eye, local Z up."""
    d = (EYE - pos).normalized()           # = local -X
    xa = -d
    za = V((0, 0, 1))
    ya = za.cross(xa).normalized()
    za = xa.cross(ya).normalized()
    R = Matrix((xa, ya, za)).transposed()
    return Matrix.Translation(pos) @ R.to_4x4()


def build_forward_displays(X):
    out = {}
    for nm, pos in FWD_POS.items():
        mb = MB()
        w, h = FWD_W, FWD_H
        q = [mb.vert((-0.0004, w / 2, -h / 2)), mb.vert((-0.0004, -w / 2, -h / 2)), mb.vert((-0.0004, -w / 2, h / 2)), mb.vert((-0.0004, w / 2, h / 2))]
        f = mb.face(q, X["ScreenSmall"], [(0, 0), (1, 0), (1, 1), (0, 1)])
        mb.group([f], ('dir', V((-1, 0, 0))))
        ob = mb.finish(nm, smooth_angle=10)
        M4 = fwd_matrix(pos)
        ob.location = pos
        ob.rotation_euler = M4.to_euler('XYZ')
        out[nm] = ob
    return out


def forward_display_housings(mb, X):
    """Slim gloss-black pods on the dash top (INFERRED placement: either side of the driver's sight line,
    just above the dash edge so they read over the yoke)."""
    for nm, pos in FWD_POS.items():
        M4 = fwd_matrix(pos)
        w, h = FWD_W, FWD_H
        hole = resample(rrect(w, h, 0.001, 2), 64, closed=True)
        rim = resample(rrect(w + 0.010, h + 0.010, 0.004, 4), 64, closed=True)
        back = offset2d(rim, -0.002)
        rings = [[M4 @ V((x, p.x, p.y)) for p in poly] for (poly, x) in ((hole, -0.0002), (offset2d(hole, 0.002), -0.0002),
                                                                        (offset2d(rim, -0.0015), -0.0010), (rim, 0.0005), (back, 0.012))]
        G = mb.grid(rings, lambda i, j: X["BlackGloss"] if i < 2 else X["Aluminum"] if i == 2 else X["BlackGloss"], closed_u=True)
        faces = mb.groups.pop()[0]
        faces += mb.cap(G[-1], X["BlackGloss"]); mb.groups.pop()
        faces += mb.cap(G[0], X["BlackGloss"]); mb.groups.pop()
        mb.group(faces, 'closed')
        # foot onto the dash top
        base = M4 @ V((0.010, 0, -h / 2 - 0.004))
        zt = dash_top_z(base.x) + 0.002
        foot = V((base.x + 0.004, base.y, (base.z + zt) / 2))
        mb.box(foot, (0.030, 0.050, abs(base.z - zt) + 0.008), X["BlackGloss"], r=0.004)


# ============================================================================= INTERIOR (static)
DASH_PROF = [  # (x, z, material of the segment that starts here)
    (1.345, 0.768, 'S'), (1.28, 0.778, 'S'), (1.18, 0.795, 'S'), (1.08, 0.812, 'S'), (0.99, 0.826, 'S'),
    (0.935, 0.834, 'S'), (0.910, 0.836, 'S'), (0.894, 0.832, 'S'), (0.885, 0.822, 'S'), (0.881, 0.806, 'S'),
    (0.881, 0.785, 'S'), (0.884, 0.765, 'S'), (0.889, 0.754, 'S'), (0.896, 0.749, 'D'), (0.912, 0.748, 'D'),
    (0.918, 0.742, 'D'), (0.916, 0.735, 'D'), (0.900, 0.732, 'C'), (0.880, 0.731, 'C'), (0.871, 0.726, 'C'),
    (0.868, 0.714, 'C'), (0.868, 0.693, 'C'), (0.868, 0.674, 'C'), (0.871, 0.665, 'C'), (0.880, 0.661, 'D'),
    (0.896, 0.658, 'D'), (0.897, 0.651, 'D'), (0.886, 0.648, 'C'), (0.885, 0.625, 'C'), (0.900, 0.590, 'C'),
    (0.935, 0.548, 'C'), (0.985, 0.515, 'C'), (1.050, 0.490, 'C'), (1.120, 0.474, 'C'), (1.185, 0.466, 'C'),
]


def dash_top_z(x):
    xs = [p[0] for p in DASH_PROF[:7]][::-1]
    zs = [p[1] for p in DASH_PROF[:7]][::-1]
    return table(xs, zs, x, smooth=False)


def build_interior(X):
    mb = MB()
    K = {'S': X["Suede"], 'C': X["Carbon"], 'D': X["InteriorDark"], 'L': X["Leather"], 'R': X["RedAmbient"],
         'A': X["Aluminum"], 'B': X["BlackGloss"], 'T': X["Titanium"]}
    uvc = carbon_uv

    # ---------------- tub floor (carbon), heel pads
    xs = [X_BULK - 0.04 + k * 0.1 for k in range(19)] + [1.12]
    ys = [-0.800 + k * 0.08 for k in range(21)]
    P = [[V((x, y, FLOOR)) for y in ys] for x in sorted(set(round(v, 4) for v in xs))]
    mb.grid(P, K['C'], uvs=uvc, orient=('dir', V((0, 0, 1))))
    for yc in (Y_DRV, -Y_DRV):
        mb.box(V((0.93, yc - 0.03, FLOOR + 0.002)), (0.20, 0.26, 0.006), K['D'], r=0.003, seg=1)

    # ---------------- firewall / toe board and the front wheel-arch walls of the footwells
    zs_fw = [FLOOR - 0.004, 0.22, 0.30, 0.38, 0.47]
    P = [[V((1.10 + (z - FLOOR) * 0.30, y, z)) for y in [-0.64 + k * 0.064 for k in range(21)]] for z in zs_fw]
    mb.grid(P, K['C'], uvs=uvc, orient=('dir', V((-1, 0, 0.3))))
    for s in (1, -1):
        P = []
        for z in (FLOOR - 0.004, 0.25, 0.35, 0.45, 0.53):
            P.append([V((x, s * (0.632 - 0.05 * ramp(z, 0.35, 0.56)), z)) for x in (0.90, 0.96, 1.02, 1.08, 1.14, 1.20, 1.26)])
        mb.grid(P, K['C'], uvs=uvc, orient=('dir', V((0, -s, 0))))

    # ---------------- dash: profile swept across the car, end caps behind the doors
    ys = [-0.858, -0.852, -0.84, -0.80, -0.76, -0.72] + [round(-0.66 + 0.04 * k, 3) for k in range(34)] + [0.72, 0.76, 0.80, 0.84, 0.852, 0.858]
    P = []
    for y in ys:
        # outer ends: the upper dash (above z 0.70) is compressed so its top drops to the low door belt
        # (DLO z ~0.78 at x 0.9-1.0) and stays under the side glass / fender top at the windshield corners
        f = 1.0 - 0.56 * ramp(abs(y), 0.56, 0.84)
        row = []
        for (x, z, _) in DASH_PROF:
            zz = 0.70 + (z - 0.70) * f if z > 0.70 else z
            row.append(V((x + 0.012 * ramp(abs(y), 0.80, 0.858), y, zz)))
        P.append(row)
    G = mb.grid(P, [K[p[2]] for p in DASH_PROF[:-1]], uvs=uvc, orient=('toward', lambda c: V((0.2, c.y * 0.5, 0.75))))
    for gi in (0, -1):
        mb.cap(G[gi], K['S'], orient=('dir', V((0, -1 if gi == 0 else 1, 0))), tri=True)
    # air-vent vanes in the full-width slot and the dash-end vents
    for s in (1, -1):
        for k in range(22):
            y = s * (0.13 + 0.026 * k)
            mb.box(V((0.905, y, 0.7405)), (0.022, 0.0025, 0.0085), K['D'], r=0.0008, seg=1)
        vc = V((0.879, s * 0.765, 0.790))
        ring_o = resample(rrect(0.095, 0.042, 0.012, 4), 40, closed=True)
        ring_i = offset2d(ring_o, -0.004)
        Rr = [[V((vc.x + dx, vc.y + p.x, vc.z + p.y)) for p in poly] for (poly, dx) in ((ring_i, 0.012), (ring_i, -0.001), (ring_o, -0.003), (ring_o, 0.004))]
        Gv = mb.grid(Rr, lambda i, j: K['D'] if i == 0 else K['A'], closed_u=True, orient=None)
        f = mb.groups.pop()[0]
        f += mb.cap(Gv[0], K['D']); mb.groups.pop()
        mb.group(f, ('dir', V((-1, 0, 0))))
        for k in range(3):
            mb.box(vc + V((0.002, 0, -0.010 + 0.010 * k)), (0.010, 0.082, 0.0025), K['A'], r=0.0008, seg=1)
    # defroster grille along the windshield base and tweeter grilles in the dash-top corners
    for k in range(60):
        y = -0.59 + k * 0.02
        mb.box(V((1.300, y, dash_top_z(1.300) + 0.0015)), (0.030, 0.012, 0.004), K['D'], r=0.001, seg=1)
    for s in (1, -1):
        c = V((1.215, s * 0.700, dash_top_z(1.215) + 0.0012))
        f = mb.disc(c, V((0, 0, 1)), V((1, 0, 0)), 0.034, K['D'], segs=32)
        mb.group([f], ('dir', V((0, 0, 1))))
        for k in range(6):
            mb.box(c + V((-0.025 + 0.01 * k, 0, 0.0008)), (0.0035, 0.060 * math.sqrt(max(0.0, 1 - ((-0.025 + 0.01 * k) / 0.034) ** 2)), 0.0016), K['A'], r=0.0006, seg=1)

    # ---------------- steering column shroud (static, along the yoke axis)
    Y = yoke_matrix()
    path = [Y @ V((x, 0, 0)) for x in (-YOKE_SETBACK + 0.066, -0.04, 0.06, 0.18, 0.34)]
    shroud = lambda s: [(a, b) for a, b in rrect(0.074 + 0.03 * s, 0.062 + 0.026 * s, 0.018, 3)]
    rows = []
    for i, p in enumerate(path):
        t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
        N = (V((0, 0, 1)) - t * t.z).normalized()
        Bv = t.cross(N)
        rows.append([p + Bv * a + N * b for a, b in shroud(i / (len(path) - 1))])
    Gs = mb.grid(rows, K['C'], closed_u=True, uvs=uvc)
    f = mb.groups.pop()[0]
    f += mb.cap(Gs[0], K['D']); mb.groups.pop()
    mb.group(f, 'closed')

    # ---------------- door / quarter inner panels (both sides)
    for s in (1, -1):
        door_panel(mb, K, s)

    # ---------------- headliner, windshield frame (A-pillars + header), braces, rear-view display
    headliner(mb, K)
    frame_and_braces(mb, K)
    rear_view(mb, K)

    # ---------------- centre spine and console
    spine_console(mb, K)

    # ---------------- rear bulkhead, speakers, harness anchors
    bulkhead(mb, K)

    # ---------------- pedal box (driver), footrests
    pedals(mb, K)

    # ---------------- forward display pods
    forward_display_housings(mb, X)
    # safety: nothing of the cabin may pierce the body shell (RB.ROWS envelope); report what had to move
    moved = clamp_inside(mb.bm, 0.005)
    print("INTERIOR clamp: %d vertices pushed inside the shell" % moved)
    return mb.finish("Interior", smooth_angle=36)


def fit_inside(p, margin=0.005):
    """Move p towards the cabin axis until it clears the body skin by `margin` (no-op when already inside)."""
    if inside_body(p) >= margin:
        return p
    d = V((0.0, -p.y, 0.50 - p.z))
    if d.length < 1e-9:
        return p
    d.normalize()
    q = p.copy()
    for _ in range(120):
        q += d * 0.001
        if inside_body(q) >= margin:
            break
    return q


def clamp_inside(bm, margin):
    n = 0
    for v in bm.verts:
        if inside_body(v.co) < margin:
            v.co = fit_inside(v.co, margin)
            n += 1
    return n


def clearance_report(objects, margin=0.003):
    """Worst skin clearance of every object's evaluated vertices (world space). Negative = pierces the shell."""
    out = {}
    for ob in objects:
        mw = ob.matrix_world
        worst, bad, wp = 1e9, 0, None
        for v in ob.data.vertices:
            p = mw @ v.co
            c = inside_body(p)
            if c < margin:
                bad += 1
            if c < worst:
                worst, wp = c, p
        out[ob.name] = (worst, bad, tuple(round(x, 3) for x in wp) if wp else None)
    return out


def door_section(x, s):
    """Inner door/quarter trim cross-section at station x on side s: list of (y, z, mat) top (glass) -> floor."""
    y6, z6 = RB.row_at(6, x)
    y6 -= 0.010
    zt = z6
    ypan = lambda z: min(0.800 + (z - 0.32) * 0.085, skin_y(x, z) - 0.085)
    t = clamp((x + 0.62) / 1.40)
    zsw_t = 0.585 + 0.085 * t
    zsw_b = 0.537 + 0.072 * t
    zpk_b = min(0.448 + 0.150 * t ** 1.6, zsw_b - 0.012)
    p_sw = 0.046 * ramp(x, -0.74, -0.58) * (1 - smooth01((x - 0.66) / 0.14) ** 1.5 if x > 0.66 else 1.0)
    p_pk = 0.032 * ramp(x, -0.74, -0.62) * (1 - ramp(x, 0.40, 0.66))
    yz = []
    S, C, D, R = 'S', 'C', 'D', 'R'
    # belt rail (suede), tucked against the glass
    for (dy, dz, m) in ((0.004, -0.008, S), (0.012, 0.004, S), (0.030, 0.009, S), (0.050, 0.004, S), (0.066, -0.008, S),
                        (0.075, -0.026, S), (0.073, -0.046, S), (0.064, -0.058, D)):
        yz.append((y6 - dy, zt + dz, m))
    zc0 = zt - 0.064
    yz.append((ypan(zc0) - 0.002, zc0, C))
    # carbon upper panel down to the swoosh
    for k in range(1, 5):
        z = zc0 + (zsw_t + 0.012 - zc0) * k / 5
        yz.append((ypan(z), z, C))
    # swoosh (suede armrest bulge)
    z_top = zsw_t + 0.012
    for (f, fp, m) in ((0.0, 0.00, S), (0.10, 0.42, S), (0.22, 0.78, S), (0.36, 0.95, S), (0.52, 1.0, S),
                       (0.70, 1.0, S), (0.86, 0.97, S), (1.0, 0.90, R)):
        z = z_top + (zsw_b - z_top) * f
        yz.append((ypan(z) - p_sw * fp, z, m))
    # red strip under the lip, then the pocket (dark), red line on the bottom lip
    zl = zsw_b
    yz.append((ypan(zl) - p_sw * 0.70, zl - 0.002, R))
    yz.append((ypan(zl) - p_sw * 0.25, zl - 0.004, D))
    for k, f in enumerate((0.0, 0.33, 0.66, 1.0)):
        z = zl - 0.006 - (zl - 0.006 - zpk_b - 0.006) * f
        yz.append((ypan(z) + p_pk * (1.0 if 0 < f < 1 else 0.85), z, D))
    yz.append((ypan(zpk_b + 0.004) + p_pk * 0.2, zpk_b + 0.003, R))
    yz.append((ypan(zpk_b) - 0.006, zpk_b - 0.001, R))
    yz.append((ypan(zpk_b - 0.006) - 0.008, zpk_b - 0.008, C))
    # lower carbon panel to the sill
    zs = 0.318
    for k in range(1, 4):
        z = zpk_b - 0.008 - (zpk_b - 0.008 - zs - 0.012) * k / 3
        yz.append((ypan(z) - 0.004, z, C))
    yz.append((ypan(zs) - 0.010, zs + 0.004, C))
    yz.append((ypan(zs) - 0.030, zs, C))
    yz.append((0.700, zs, C))
    yz.append((0.676, zs - 0.006, C))
    yz.append((0.666, zs - 0.024, C))
    yz.append((0.664, FLOOR - 0.004, C))
    return [(s * y, z, m) for (y, z, m) in yz]


def door_panel(mb, K, s):
    xs = [round(1.00 - 0.02 * k, 3) for k in range(int((1.00 - (X_BULK - 0.10)) / 0.02) + 1)]
    rows, mats = [], None
    for x in xs:
        sec = door_section(x, s)
        rows.append([V((x, y, z)) for (y, z, m) in sec])
        mats = [K[m] for (_, _, m) in sec[:-1]]
    mb.grid(rows, mats, uvs=carbon_uv, orient=('toward', toward_cabin))
    # door speaker (lower panel, ahead of the pocket) and inner release handle in the pocket front
    for (x, z, r) in ((0.52, 0.405, 0.058),):
        y = s * (0.800 + (z - 0.32) * 0.085 - 0.006)
        n = V((0, -s, 0))
        c = V((x, y, z))
        outer = [c + V((0, 0, 0)) + (V((1, 0, 0)) * math.cos(a) + V((0, 0, 1)) * math.sin(a)) * r for a in [2 * math.pi * k / 40 for k in range(40)]]
        ring = [mb.vert(p + n * 0.002) for p in outer]
        ring2 = [mb.vert(c + n * 0.006 + (p - c) * 0.92) for p in outer]
        Gr = mb.grid([[v.co for v in ring], [v.co for v in ring2]], K['A'], closed_u=True, verts=[ring, ring2], orient=None)
        f = mb.groups.pop()[0]
        f += mb.cap(ring2, K['D']); mb.groups.pop()
        mb.group(f, ('dir', n))
        for k in range(9):
            zz = z - r * 0.8 + k * r * 0.2
            half = r * 0.88 * math.sqrt(max(0.0, 1 - ((zz - z) / (r * 0.92)) ** 2))
            mb.box(c + n * 0.0075 + V((0, 0, zz - z)), (2 * half, 0.003, 0.004), K['A'], r=0.0012, seg=1)
    hx, hz = 0.50, 0.548
    y = s * (0.800 + (hz - 0.32) * 0.085 - 0.012)
    mb.box(V((hx, y, hz)), (0.090, 0.016, 0.014), K['A'], r=0.005, seg=2)


def roof_section(x, inset, n=10):
    """Inner roof line (y, z) from the left rail (row 7) over the centre to the right rail, offset inwards."""
    y7, z7 = RB.row_at(7, x)
    y8, z8 = RB.row_at(8, x)
    z9 = RB.row_at(9, x)[1]
    pts = [V((-y7, z7)), V((-y8, z8)), V((0.0, z9)), V((y8, z8)), V((y7, z7))]
    dense = spline(pts, n)
    out = []
    for i, p in enumerate(dense):
        a, b = dense[max(i - 1, 0)], dense[min(i + 1, len(dense) - 1)]
        t = (b - a).normalized()
        nn = V((t.y, -t.x))
        if nn.dot(V((0.0, 0.55)) - p) < 0:
            nn = -nn
        out.append(p + nn * inset)
    return out


def headliner(mb, K):
    """Gray microsuede headliner under the glass roof: raised centre spine, side panels, rolled edges at the
    roof rails (side-glass top). Front edge meets the windshield header."""
    xs = [X_HEADER + 0.02 - 0.025 * k for k in range(int((X_HEADER + 0.02 - (X_BULK - 0.20)) / 0.025) + 1)]
    rows = []
    for x in xs:
        sec = roof_section(x, 0.030, 12)
        sec = resample(sec, 41)
        row = []
        for p in sec:
            y = p.x
            drop = 0.016 * (1 - ramp(abs(y), 0.098, 0.125))      # centre spine band
            row.append(V((x, y, p.y - drop)))
        # rolled lips at the rails
        l0, r0 = row[0], row[-1]
        row = [l0 + V((0, 0.010, -0.032)), l0 + V((0, 0.002, -0.014))] + row + [r0 + V((0, -0.002, -0.014)), r0 + V((0, -0.010, -0.032))]
        rows.append(row)
    mb.grid(rows, K['S'], orient=('dir', V((0, 0, -1))))


def frame_path():
    """Windshield frame centre line: left A-pillar (inside row 7) -> header -> right A-pillar."""
    # Slide-4: the padded A-pillar trim stands on the dash's rear outer corner (door front edge, x ~0.92) and
    # sweeps up and inboard across the front of the side glass into the header corner, a "U" around the view.
    # Pillar centre line: z rises from the dash corner to the header corner; y follows the side glass 4.5 cm inboard.
    left = []
    for (x, z) in ((0.935, 0.786), (0.880, 0.845), (0.800, 0.905), (0.700, 0.958), (0.590, 0.998), (0.480, 1.026),
                   (0.385, 1.042), (X_HEADER + 0.045, 1.050)):
        left.append(V((x, skin_y(x, z) - 0.050 - 0.020 * ramp(x, 0.70, 0.40), z)))
    head = []
    sec = roof_section(X_HEADER, 0.044, 10)
    for p in sec:
        if abs(p.x) < 0.44:
            head.append(V((X_HEADER, p.x, p.y)))
    head.sort(key=lambda p: -p.y)
    right = [V((p.x, -p.y, p.z)) for p in reversed(left)]
    pts = left + head[::max(1, len(head) // 8)] + right
    return resample(spline(pts, 6), 170)


def frame_and_braces(mb, K):
    path = frame_path()
    inward = lambda p, t: V((0, -p.y, 0.50 - p.z))
    prof = [(a, b) for a, b in rrect(0.078, 0.042, 0.014, 4)]
    prof = [(a, b + 0.010) for (a, b) in prof]
    mb.tube(path, prof, K['S'], up=inward)
    # A-pillar braces: from the dash top inboard, rising back/outboard into the pillars (Slide-4)
    for s in (1, -1):
        x0 = 1.10
        p0 = V((x0, s * 0.600, dash_top_z(x0) - 0.010))
        p1 = V((X_HEADER + 0.060, s * 0.455, roof_z(X_HEADER + 0.06, 0.455) - 0.075))
        mid = p0.lerp(p1, 0.5) + V((0, s * 0.006, 0.004))
        bp = resample(spline([p0 + V((0.03, 0, -0.025)), p0, mid, p1, p1 + (p1 - mid).normalized() * 0.03], 8), 40)
        mb.tube(bp, [(a, b) for a, b in rrect(0.052, 0.034, 0.014, 4)], K['S'], up=lambda p, t, s=s: V((0, -s, 0.5)))


def rear_view(mb, K):
    """Rear-view camera display hanging from the roof spine on an inverted triangular mount."""
    x0 = X_HEADER - 0.006
    zt = roof_section(x0, 0.030, 8)
    ztop = max(p.y for p in zt) - 0.016
    mb.box(V((x0 - 0.010, 0, ztop - 0.012)), (0.040, 0.044, 0.024), K['S'], r=0.006)
    mb.box(V((x0 + 0.004, 0, ztop - 0.034)), (0.012, 0.012, 0.030), K['B'], r=0.003)
    apex = V((x0 + 0.006, 0, ztop - 0.046))
    base_z = ztop - 0.118
    tri = [apex, V((x0 + 0.020, -0.066, base_z)), V((x0 + 0.020, 0.066, base_z))]
    for k in range(3):
        a, b = tri[k], tri[(k + 1) % 3]
        mid = (a + b) / 2
        d = b - a
        rot = d.to_track_quat('Z', 'X').to_matrix()
        mb.box(mid, (0.010, 0.009, d.length + 0.008), K['A'] if k != 1 else K['A'], r=0.003, rot=rot)
    # dark infill of the triangle
    f = mb.face([mb.vert(p + V((0.003, 0, 0))) for p in tri], K['B'], [(0.5, 1), (0, 0), (1, 0)])
    f2 = mb.face([mb.vert(p + V((0.006, 0, 0))) for p in reversed(tri)], K['B'], [(0.5, 1), (1, 0), (0, 0)])
    mb.group([f], ('dir', V((-1, 0, 0))))
    mb.group([f2], ('dir', V((1, 0, 0))))
    # the display body + screen face
    dc = V((x0 - 0.012, 0, ztop - 0.084))
    mb.box(dc, (0.014, 0.128, 0.038), K['D'], r=0.005, seg=2)
    q = [V((dc.x - 0.0075, 0.059, dc.z - 0.015)), V((dc.x - 0.0075, -0.059, dc.z - 0.015)), V((dc.x - 0.0075, -0.059, dc.z + 0.015)), V((dc.x - 0.0075, 0.059, dc.z + 0.015))]
    f = mb.face([mb.vert(p) for p in q], K['B'], [(0, 0), (1, 0), (1, 1), (0, 1)])
    mb.group([f], ('dir', V((-1, 0, 0))))


SPINE_TOP = 0.500
SPINE_HW = 0.112


def spine_x_front(y):
    return 0.300 - 0.55 * abs(y)


def spine_console(mb, K):
    """Tall Ultrawhite spine between the seats (chevron nose), carbon console with the wireless charger
    (dark pad + red accent line) under the floating screen."""
    prof = [(SPINE_HW + 0.008, FLOOR - 0.004), (SPINE_HW + 0.006, 0.28), (SPINE_HW + 0.002, 0.40), (SPINE_HW, SPINE_TOP - 0.030),
            (SPINE_HW - 0.006, SPINE_TOP - 0.010), (SPINE_HW - 0.018, SPINE_TOP - 0.002), (0.06, SPINE_TOP + 0.002), (0.0, SPINE_TOP + 0.003)]
    prof = prof + [(-y, z) for (y, z) in reversed(prof[:-1])]
    nst = 34
    rows = []
    for i in range(nst):
        t = i / (nst - 1)
        row = []
        for (y, z) in prof:
            xf = spine_x_front(y if z > SPINE_TOP - 0.04 else SPINE_HW)
            x0 = X_BULK - 0.02
            row.append(V((x0 + (xf - x0) * t, y, z)))
        rows.append(row)
    G = mb.grid(rows, K['L'], orient=('toward', lambda c: V((c.x, 0.0, 0.30))))
    # nose: from the chevron edge sloping down/forward to the floor
    top = rows[-1]
    nose = [top]
    for k, (dx, dz) in enumerate(((0.030, -0.030), (0.060, -0.090), (0.085, -0.170), (0.100, -0.260), (0.105, -0.40))):
        nose.append([V((p.x + dx * (1 if p.z > FLOOR + 0.05 else 0.0), p.y * (1 - 0.15 * k / 5), max(FLOOR - 0.004, p.z + dz))) for p in top])
    mb.grid(nose, lambda i, j: K['L'] if i < 3 else K['C'], uvs=carbon_uv, orient=('dir', V((1, 0, 0.3))), verts=[G[-1]] + [[mb.vert(p) for p in r] for r in nose[1:]])

    # console: side path (x, z) swept across y with rounded top corners, then side skirts to the floor
    cpath = resample(spline([V((0.395, 0, FLOOR)), V((0.402, 0, 0.30)), V((0.412, 0, 0.440)), V((0.438, 0, 0.478)), V((0.62, 0, 0.583)),
                              V((0.665, 0, 0.598)), V((0.75, 0, 0.601)), V((0.905, 0, 0.602))], 6), 40)
    hw = 0.106
    cprof = [(hw, -0.022), (hw - 0.001, -0.010), (hw - 0.005, -0.003), (hw - 0.014, 0.0), (0.04, 0.0), (0.0, 0.0)]
    cprof = cprof + [(-y, h) for (y, h) in reversed(cprof[:-1])]
    rows = []
    for i, p in enumerate(cpath):
        t = (cpath[min(i + 1, len(cpath) - 1)] - cpath[max(i - 1, 0)]).normalized()
        n = V((-t.z, 0, t.x))
        rows.append([V((p.x, y, p.z)) + n * h for (y, h) in cprof])
    Gc = mb.grid(rows, K['C'], uvs=carbon_uv, orient=('dir', V((-0.3, 0, 1))))
    mb.skirt([r[0] for r in Gc], FLOOR - 0.004, K['C'], orient=('dir', V((0, 1, 0))), uvs=carbon_uv)
    mb.skirt([r[-1] for r in Gc], FLOOR - 0.004, K['C'], orient=('dir', V((0, -1, 0))), uvs=carbon_uv)
    # wireless charger pad on the incline + red accent line + phone ledge
    a, b = V((0.452, 0, 0.486)), V((0.600, 0, 0.571))
    t = (b - a).normalized()
    n = V((-t.z, 0, t.x))
    c = (a + b) / 2 + n * 0.0015
    pad = resample(rrect(0.150, 0.088, 0.016, 5), 48, closed=True)
    ring = [mb.vert(c + t * p.x + V((0, p.y, 0))) for p in pad]
    inner = [mb.vert(c + n * 0.0012 + t * q.x + V((0, q.y, 0))) for q in offset2d(pad, -0.004)]
    mb.grid([[v.co for v in ring], [v.co for v in inner]], K['D'], closed_u=True, verts=[ring, inner], orient=('dir', n))
    mb.cap(inner, K['S'], orient=('dir', n))
    q = [c + n * 0.0016 + t * (-0.060) + V((0, 0.0013, 0)), c + n * 0.0016 + t * (-0.060) + V((0, -0.0013, 0)),
         c + n * 0.0016 + t * 0.060 + V((0, -0.0013, 0)), c + n * 0.0016 + t * 0.060 + V((0, 0.0013, 0))]
    f = mb.face([mb.vert(p) for p in q], K['R'], [(0, 0), (0.0026, 0), (0.0026, 0.12), (0, 0.12)])
    mb.group([f], ('dir', n))
    mb.box(a + t * 0.002 + n * 0.006, (0.012, 0.094, 0.010), K['C'], r=0.003, rot=Matrix.Rotation(-math.atan2(t.z, t.x), 3, 'Y'), uvs=carbon_uv)
    # ribbed carbon edge strips along the console top (pov3 frame)
    for sy in (1, -1):
        for k in range(14):
            x = 0.64 + k * 0.018
            mb.box(V((x, sy * (hw - 0.010), 0.6025)), (0.010, 0.012, 0.003), K['D'], r=0.001, seg=1)


def bulkhead(mb, K):
    """Rear bulkhead behind the seats (leaning back with the seat backs), carbon below, suede above."""
    zs = [FLOOR - 0.004] + [0.20 + 0.06 * k for k in range(15)]
    rows = []
    for z in zs:
        x = X_BULK - (z - FLOOR) * 0.24
        zr = roof_z(x, 0.0) - 0.030
        if z > zr:
            break
        hw = min(0.80, skin_y(x, z) - 0.060)
        rows.append([V((x, y * hw, z)) for y in [-1 + 2 * k / 24 for k in range(25)]])
    # top row follows the headliner
    x = X_BULK - (rows[-1][0].z - FLOOR) * 0.24 - 0.02
    sec = resample([p for p in roof_section(x, 0.030, 10)], 25)
    rows.append([V((x, p.x, p.y - 0.004)) for p in sec])
    mb.grid(rows, lambda i, j: K['C'] if rows[i][0].z < 0.42 else K['S'], uvs=carbon_uv, orient=('dir', V((1, 0, 0))))
    # subwoofer grille between the seat backs above the spine, harness eye-bolts behind each seat
    for (y, z, r) in ((0.0, 0.64, 0.075),):
        x = X_BULK - (z - FLOOR) * 0.24 + 0.004
        n = V((1, 0, 0.24)).normalized()
        c = V((x, y, z))
        e1 = V((0, 1, 0)); e2 = n.cross(e1)
        outer = [c + (e1 * math.cos(a) + e2 * math.sin(a)) * r for a in [2 * math.pi * k / 48 for k in range(48)]]
        ring = [mb.vert(p) for p in outer]
        ring2 = [mb.vert(c + n * 0.006 + (p - c) * 0.93) for p in outer]
        mb.grid([[v.co for v in ring], [v.co for v in ring2]], K['A'], closed_u=True, verts=[ring, ring2], orient=('dir', n))
        mb.cap(ring2, K['D'], orient=('dir', n))
        for k in range(-6, 7):
            for j in range(-6, 7):
                if (k * k + j * j) * 0.011 ** 2 < (r * 0.85) ** 2:
                    f = mb.disc(c + n * 0.0065 + e1 * (k * 0.011 + (0.0055 if j % 2 else 0)) + e2 * (j * 0.0095), n, e1, 0.0032, K['B'], segs=6)
                    mb.group([f], ('dir', n))
    for yc in (Y_DRV, -Y_DRV):
        for dy in (-0.10, 0.10):
            z = 0.235
            x = X_BULK - (z - FLOOR) * 0.24 + 0.012
            c = V((x, yc + dy, z))
            mb.box(c, (0.016, 0.034, 0.034), K['T'], r=0.006)
            ring_pts = [c + V((0.020, 0, 0)) + V((0.0, math.cos(a) * 0.014, math.sin(a) * 0.014)) for a in [2 * math.pi * k / 16 for k in range(16)]]
            mb.tube(ring_pts, [(0.0035 * math.cos(2 * math.pi * k / 8), 0.0035 * math.sin(2 * math.pi * k / 8)) for k in range(8)], K['T'],
                    up=lambda p, t, c=c: (p - c - V((0.02, 0, 0))), caps=False, closed_path=True)


def pedals(mb, K):
    """Floor-hinged pedal box on aluminium rails (the box slides towards the driver), throttle right of the brake,
    dead pedal left; passenger footrest plate. INFERRED (pedals not shown in official images)."""
    yc = Y_DRV
    for dy in (-0.11, 0.10):
        mb.box(V((1.005, yc + dy, FLOOR + 0.008)), (0.230, 0.020, 0.016), K['A'], r=0.004)
    mb.box(V((1.005, yc - 0.005, FLOOR + 0.018)), (0.190, 0.200, 0.006), K['C'], r=0.002, uvs=carbon_uv)
    for (dy, w, h, name) in ((-0.090, 0.060, 0.115, "throttle"), (0.040, 0.090, 0.080, "brake")):
        hinge = V((1.085, yc + dy, FLOOR + 0.030))
        padc = V((1.025, yc + dy, FLOOR + 0.165 if name == "throttle" else FLOOR + 0.200))
        arm = [hinge, hinge.lerp(padc, 0.5) + V((0.012, 0, 0)), padc + V((0.012, 0, 0))]
        mb.tube(spline(arm, 4), [(a, b) for a, b in rrect(0.022, 0.012, 0.004, 2)], K['A'], up=V((1, 0, 0.3)))
        ang = math.radians(-58)
        rot = Matrix.Rotation(ang, 3, 'Y')
        mb.box(padc, (0.010, w, h), K['A'], r=0.004, seg=2, rot=rot)
        for k in range(4):
            off = (-0.35 + 0.233 * k) * h
            mb.box(padc + rot @ V((-0.0062, 0, off)), (0.003, w * 0.84, 0.007), K['D'], r=0.001, seg=1, rot=rot)
    # dead pedal on the outboard footwell wall
    mb.box(V((1.000, yc + 0.205, FLOOR + 0.120)), (0.130, 0.012, 0.075), K['A'], r=0.004, rot=Matrix.Rotation(math.radians(-55), 3, 'Y') @ Matrix.Rotation(math.radians(-25), 3, 'X'))
    # passenger footrest plate
    mb.box(V((1.020, -yc, FLOOR + 0.110)), (0.010, 0.300, 0.150), K['A'], r=0.004, rot=Matrix.Rotation(math.radians(-52), 3, 'Y'))
    for k in range(5):
        mb.box(V((1.020 - 0.0045, -yc, FLOOR + 0.110)) + Matrix.Rotation(math.radians(-52), 3, 'Y') @ V((0, 0, -0.06 + 0.03 * k)),
               (0.003, 0.27, 0.008), K['D'], r=0.001, seg=1, rot=Matrix.Rotation(math.radians(-52), 3, 'Y'))


# ============================================================================= entry point
def build(M):
    """Build the interior into the current scene. M = roadster_materials.materials(). Returns {name: object}."""
    X = interior_materials(M)
    parts = {}
    parts["Interior"] = build_interior(X)
    mbs = build_seat(X)
    for v in mbs.bm.verts:                    # the shell base sits on the tub floor (never below the underbody)
        if v.co.z < FLOOR - 0.008:
            v.co.z = FLOOR - 0.008
    drv = mbs.finish("Seat_Driver", smooth_angle=40)
    parts["Seat_Driver"] = drv
    parts["Seat_Passenger"] = mirror_mb(drv, "Seat_Passenger")
    parts["Yoke"] = build_yoke(X)
    bez, disp = build_screen(X)
    parts["Screen_Bezel"] = bez
    parts["Screen_Display"] = disp
    parts.update(build_forward_displays(X))
    return parts
