"""Wheels, tyres, brakes and suspension of the 2026 Tesla Roadster (hero detail, fully procedural).

    build(M) -> {object name: bpy object}

Frame (Blender, metres): origin on the ground under the wheelbase midpoint, +X forward, +Y = the car's left, +Z up.

Wheel-type objects (Wheel_Front/Rear = rim + tyre in one mesh, Brake_*_Disc, Brake_*_Caliper) are built for the
LEFT side, centred at the origin (= wheel centre, tyre mid-plane), spin axis along Y, outer face towards +Y.
The disc and caliper already contain their true inboard offset from the wheel centre, so they are placed at
the wheel centre exactly like the wheel (no extra inboard shift).

Suspension objects (Susp_<FL|FR|RL|RR>_*) are built in place at the four corners at static ride height
(wheel centre z = unloaded tyre radius), both sides explicitly (right = mirror of left). Each object's origin is
its pivot; export/suspension_rig.json lists pivots, hinge axes, aim targets and solved kinematic tables.

Sources (Research/images): 02-Pilot side profile (2.251 mm/px) for the wheel face and tyre sidewall,
08-End and 04-World for spoke depth and the gold brake hardware seen through the spoke slots,
03-Carousel-Slide-5 for the transverse gold coil-over / rocker / pushrod and the Trofeo tread pattern.
"""
import bpy, bmesh, math, os, json
from mathutils import Vector, Matrix, Quaternion
from roadster_materials import mat

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXPORT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "export"))
RIG_JSON = os.path.join(EXPORT_DIR, "suspension_rig.json")
FONT_DIR = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")

WB = 2.825
FA, RA = WB / 2, -WB / 2
IN = 0.0254
D2R = math.pi / 180.0
CORNER_Y = {"F": 0.85, "R": 0.84}

# ----------------------------------------------------------------------------------------------------------------
# Specification. Tyre sizes are the observed ones (295/30 R20, 335/30 R21). Rim widths are the standard fitments
# for those sections (10.5J / 12J). Brakes: 410 x 38 mm front / 390 x 32 mm rear carbon-ceramic, the sizes used by
# cars of this class on exactly these wheel diameters (e.g. 911 GT3 RS: 410 mm 6-piston front in 20 in,
# 390 mm 4-piston rear in 21 in with the same 335/30 R21 rear tyre); 410 mm is the largest rotor whose 6-piston
# caliper clears the barrel of a 20 in rim with ~10 mm to spare.
SPEC = {
    "Front": dict(tire_w=0.295, aspect=0.30, rim_d=20, rim_w=10.5, et=0.045, n_pitch=54,
                  disc_d=0.410, disc_t=0.038, disc_gap=0.012, ring_w=0.068, vanes=36, hole_pat=15,
                  pistons=(0.0165, 0.0185, 0.0205), cal_len=0.300, cal_r0=0.190, cal_phi=0.0,
                  size_text="295/30 ZR20"),
    "Rear": dict(tire_w=0.335, aspect=0.30, rim_d=21, rim_w=12.0, et=0.050, n_pitch=58,
                 disc_d=0.390, disc_t=0.032, disc_gap=0.010, ring_w=0.062, vanes=32, hole_pat=15,
                 pistons=(0.0185, 0.0205), cal_len=0.236, cal_r0=0.180, cal_phi=20.0,
                 size_text="335/30 ZR21"),
}


def geom(key):
    g = dict(SPEC[key])
    g["key"] = key
    g["Rr"] = g["rim_d"] * IN / 2                     # bead-seat radius
    g["R"] = g["Rr"] + g["tire_w"] * g["aspect"]      # unloaded tyre radius (0.3425 / 0.3672)
    g["hw"] = g["tire_w"] / 2
    g["hwr"] = g["rim_w"] * IN / 2
    g["k"] = g["Rr"] / 0.254                          # face design scale relative to the 20 in front
    g["ht"] = 0.425 * g["tire_w"]                     # tread half width
    g["rs"] = g["hw"] - 0.0015 - g["ht"]              # shoulder radius
    k = g["k"]
    g["r_io"] = g["Rr"] + 0.014 * k                   # aero insert outer edge (measured 268-270 mm on the 20 in)
    g["r_lo"] = g["Rr"] + 0.022 * k                   # rim lip outer radius
    g["y_lip"] = g["hwr"] + 0.0125                    # outer face of the rim lip
    g["y_f"] = g["y_lip"] - 0.016                     # aero insert face (16 mm recess behind the lip)
    g["y_d"] = g["et"] - 0.040 + (0.0 if key == "Front" else 0.002)   # disc mid-plane
    g["r_do"] = g["disc_d"] / 2
    g["r_di"] = g["r_do"] - g["ring_w"]
    return g


# ================================================================================================ small helpers
def W(r, phi, y):
    """Wheel frame point. phi = angle as seen from OUTSIDE the left wheel (looking -Y), measured counter-clockwise
    from the rearward direction (picture right in Tesla's side photo). y = axial, + outward."""
    return Vector((-r * math.cos(phi), y, r * math.sin(phi)))


def lerp_table(tab, x):
    if x <= tab[0][0]:
        return tab[0][1]
    for (x0, y0), (x1, y1) in zip(tab, tab[1:]):
        if x <= x1:
            t = (x - x0) / (x1 - x0)
            return y0 + (y1 - y0) * t
    return tab[-1][1]


def bisect(f, a, b, it=60):
    fa, fb = f(a), f(b)
    if fa == 0:
        return a
    if fb == 0:
        return b
    if fa * fb > 0:
        return None
    for _ in range(it):
        m = 0.5 * (a + b)
        fm = f(m)
        if fm is None:
            return None
        if fa * fm <= 0:
            b, fb = m, fm
        else:
            a, fa = m, fm
    return 0.5 * (a + b)


def root_near(f, lo, hi, n=80, near=0.0):
    """All sign changes of f on [lo, hi]; returns the root closest to `near`."""
    xs = [lo + (hi - lo) * i / n for i in range(n + 1)]
    best = None
    prev = f(xs[0])
    for a, b in zip(xs, xs[1:]):
        fb = f(b)
        if prev is not None and fb is not None and prev * fb <= 0:
            r = bisect(f, a, b)
            if r is not None and (best is None or abs(r - near) < abs(best - near)):
                best = r
        prev = fb
    return best


def basis(d, up=None):
    z = Vector(d).normalized()
    up = Vector((0, 0, 1)) if up is None else Vector(up)
    if abs(up.normalized().dot(z)) > 0.95:
        up = Vector((1, 0, 0)) if abs(z.x) < 0.9 else Vector((0, 1, 0))
    x = up.cross(z).normalized()
    y = z.cross(x)
    return x, y, z


def new_mesh_object(name, bm, mats, smooth=35.0, pivot=None):
    bm.loops.layers.uv.verify()
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(smooth))
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if pivot is not None:
        p = Vector(pivot)
        me.transform(Matrix.Translation(-p))
        ob.location = p
    return ob


def recalc(bm, faces):
    faces = [f for f in faces if f.is_valid]
    if faces:
        bmesh.ops.recalc_face_normals(bm, faces=faces)


def set_mat(faces, idx):
    for f in faces:
        f.material_index = idx


def planar_uv(bm, faces, uvl, R):
    """Face-on projection for wheel-face parts: u to the right as seen from outside, 0..1 over the rim diameter."""
    for f in faces:
        for l in f.loops:
            co = l.vert.co
            l[uvl].uv = (0.5 - co.x / (2 * R), 0.5 + co.z / (2 * R))


def box_uv(bm, faces, uvl, scale=5.0):
    """Tri-planar box projection (texel density 1/scale m per UV unit) for mechanical parts."""
    for f in faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        a, b = [(1, 2), (0, 2), (0, 1)][ax]
        for l in f.loops:
            co = l.vert.co
            l[uvl].uv = (co[a] * scale, co[b] * scale)


class Track:
    """Collect the faces created between two points of the build (bmesh index trick-free)."""
    def __init__(self, bm):
        self.bm = bm
        self.before = set(bm.faces)

    def faces(self):
        return [f for f in self.bm.faces if f not in self.before]


# ------------------------------------------------------------------------------------------------ revolve / lofts
def revolve_y(bm, prof, segs, mat_fn=None, uvl=None, uv_mode="cyl", uv_R=0.35, phi0=0.0, closed=None):
    """Revolve (r, y) profile about the wheel axis. Face (P_i[k], P_i[k+1], P_i+1[k+1], P_i+1[k]) has its normal
    on the (dy, -dr) side of the profile direction. r == 0 points become poles. A profile whose last point
    repeats the first is treated as a closed loop (welded)."""
    prof = list(prof)
    if closed is None:
        closed = len(prof) > 2 and abs(prof[0][0] - prof[-1][0]) < 1e-9 and abs(prof[0][1] - prof[-1][1]) < 1e-9
    if closed and abs(prof[0][0] - prof[-1][0]) < 1e-9 and abs(prof[0][1] - prof[-1][1]) < 1e-9:
        prof = prof[:-1]
    n = len(prof)
    L = [0.0]
    for i in range(1, n + (1 if closed else 0)):
        a, b = prof[i - 1], prof[i % n]
        L.append(L[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    tot = L[-1] or 1.0
    poles = {i: bm.verts.new((0.0, y, 0.0)) for i, (r, y) in enumerate(prof) if r < 1e-7}
    rings = []
    for k in range(segs):
        phi = phi0 + 2 * math.pi * k / segs
        rings.append([poles[i] if i in poles else bm.verts.new(W(r, phi, y)) for i, (r, y) in enumerate(prof)])
    faces = []
    for k in range(segs):
        A, B = rings[k], rings[(k + 1) % segs]
        for i in range(n if closed else n - 1):
            i1 = (i + 1) % n
            corners = [(A[i], k, i), (B[i], k + 1, i), (B[i1], k + 1, i + 1), (A[i1], k, i + 1)]
            seen, uniq = set(), []
            for c in corners:
                if c[0] not in seen:
                    seen.add(c[0])
                    uniq.append(c)
            if len(uniq) < 3:
                continue
            f = bm.faces.new([c[0] for c in uniq])
            f.material_index = mat_fn(i) if mat_fn else 0
            if uvl is not None:
                for l, c in zip(f.loops, uniq):
                    if uv_mode == "cyl":
                        l[uvl].uv = (c[1] / segs, L[c[2]] / tot)
                    else:
                        co = l.vert.co
                        l[uvl].uv = (0.5 - co.x / (2 * uv_R), 0.5 + co.z / (2 * uv_R))
            faces.append(f)
    return faces, rings


def revolve_axis(bm, prof, c, axis, segs, mat_fn=None, up=None, rot0=0.0):
    """Revolve (r, t) profile about an arbitrary axis through c (t along the axis)."""
    x, y, z = basis(axis, up)
    c = Vector(c)
    poles = {i: bm.verts.new(c + z * t) for i, (r, t) in enumerate(prof) if r < 1e-7}
    rings = []
    for k in range(segs):
        a = rot0 + 2 * math.pi * k / segs
        d = x * math.cos(a) + y * math.sin(a)
        rings.append([poles[i] if i in poles else bm.verts.new(c + z * t + d * r) for i, (r, t) in enumerate(prof)])
    faces = []
    for k in range(segs):
        A, B = rings[k], rings[(k + 1) % segs]
        for i in range(len(prof) - 1):
            vs = []
            for v in (A[i], A[i + 1], B[i + 1], B[i]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = mat_fn(i) if mat_fn else 0
            faces.append(f)
    return faces


def cylinder(bm, p0, p1, r, segs=16, mat=0, bevel=0.0, r1=None, up=None):
    p0, p1 = Vector(p0), Vector(p1)
    ax = p1 - p0
    L = ax.length
    r1 = r if r1 is None else r1
    b = min(bevel, 0.45 * L, 0.45 * min(r, r1))
    if b > 0:
        prof = [(0, 0), (r - b, 0), (r, b), (r1, L - b), (r1 - b, L), (0, L)]
    else:
        prof = [(0, 0), (r, 0), (r1, L), (0, L)]
    return revolve_axis(bm, prof, p0, ax, segs, mat_fn=lambda i: mat, up=up)


def superellipse_ring(c, x, y, rx, ry, n, p=2.0):
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        ca, sa = math.cos(a), math.sin(a)
        e = 2.0 / p
        pts.append(c + x * (rx * math.copysign(abs(ca) ** e, ca)) + y * (ry * math.copysign(abs(sa) ** e, sa)))
    return pts


def loft(bm, centers, sizes, n=12, mat=0, up=None, p=2.0, caps=True):
    """Tube through `centers` with per-station (rx, ry) cross-sections (superellipse exponent p)."""
    rings = []
    m = len(centers)
    for i, c in enumerate(centers):
        c = Vector(c)
        t = (Vector(centers[min(i + 1, m - 1)]) - Vector(centers[max(i - 1, 0)])).normalized()
        x, y, _ = basis(t, up)
        rx, ry = sizes[i]
        rings.append([bm.verts.new(q) for q in superellipse_ring(c, x, y, rx, ry, n, p)])
    faces = []
    for i in range(m - 1):
        A, B = rings[i], rings[i + 1]
        for k in range(n):
            faces.append(bm.faces.new((A[k], A[(k + 1) % n], B[(k + 1) % n], B[k])))
    if caps:
        faces.append(bm.faces.new(list(reversed(rings[0]))))
        faces.append(bm.faces.new(rings[-1]))
    set_mat(faces, mat)
    return faces


def sphere(bm, c, r, mat=0, u=12, v=8):
    ret = bmesh.ops.create_uvsphere(bm, u_segments=u, v_segments=v, radius=r, matrix=Matrix.Translation(Vector(c)))
    fs = list({f for vv in ret["verts"] for f in vv.link_faces})
    set_mat(fs, mat)
    return fs


def rbox_coords(h, r, nmid, seg=4):
    core = max(h - r, 0.0)
    inner = [-core + 2 * core * i / (nmid + 1) for i in range(1, nmid + 1)] if core > 0 else []
    if seg >= 4:
        t = math.tan(math.radians(22.5))
        vals = [-h, -core - r * t, -core] + inner + [core, core + r * t, h]
    else:
        vals = [-h, -core] + inner + [core, h]
    out = []
    for v in vals:
        if not out or abs(v - out[-1]) > 1e-9:
            out.append(v)
    return out


def rounded_box(bm, half, r, xform=None, nmid=(1, 1, 1), mat=0, seg=4):
    """Box with all edges rounded (radius r, `seg` segments per 90 deg: 4 or 2), all quads.
    xform maps local -> world."""
    half = [float(h) for h in half]
    r = min(r, *[h * 0.999 for h in half])
    core = [max(h - r, 0.0) for h in half]
    coords = [rbox_coords(half[i], r, nmid[i], seg) for i in range(3)]
    cache = {}

    def V(p):
        key = tuple(round(c, 9) for c in p)
        v = cache.get(key)
        if v is None:
            c = Vector([max(-core[i], min(core[i], p[i])) for i in range(3)])
            d = Vector(p) - c
            q = c + d.normalized() * r if d.length > 1e-12 else c
            v = bm.verts.new(xform(q) if xform else q)
            cache[key] = v
        return v

    faces = []
    for ax in range(3):
        a, b = [(1, 2), (0, 2), (0, 1)][ax]
        for sgn in (-1, 1):
            ca, cb = coords[a], coords[b]
            for i in range(len(ca) - 1):
                for j in range(len(cb) - 1):
                    pts = []
                    for (ii, jj) in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)):
                        p = [0.0, 0.0, 0.0]
                        p[ax] = sgn * half[ax]
                        p[a] = ca[ii]
                        p[b] = cb[jj]
                        pts.append(V(p))
                    if len(set(pts)) < 4:
                        continue
                    if sgn < 0:
                        pts.reverse()
                    try:
                        faces.append(bm.faces.new(pts))
                    except ValueError:
                        pass
    set_mat(faces, mat)
    return faces


def rbox_at(bm, center, half, r, axes=None, mat=0, nmid=(1, 1, 1), seg=2):
    """Rounded box with local axes (3 column vectors) at `center` (2 segments per rounded edge by default)."""
    if axes is None:
        axes = (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
    c = Vector(center)
    X, Y, Z = [Vector(a).normalized() for a in axes]
    return rounded_box(bm, half, r, xform=lambda q: c + X * q.x + Y * q.y + Z * q.z, nmid=nmid, mat=mat, seg=seg)


def extrude_polygon(bm, pts2d, frame, z0, z1, mat=0):
    """Prism from a 2D polygon (list of (u, v)) placed with frame(u, v, z) -> world point."""
    bot = [bm.verts.new(frame(u, v, z0)) for (u, v) in pts2d]
    top = [bm.verts.new(frame(u, v, z1)) for (u, v) in pts2d]
    faces = [bm.faces.new(top), bm.faces.new(list(reversed(bot)))]
    n = len(pts2d)
    for i in range(n):
        j = (i + 1) % n
        faces.append(bm.faces.new((bot[i], bot[j], top[j], top[i])))
    set_mat(faces, mat)
    return faces


def hull2d(points):
    pts = sorted(set((round(p[0], 7), round(p[1], 7)) for p in points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


# ================================================================================================ materials
def extra_materials(M):
    """New materials (added to M under these keys). Unreal maps them by name."""
    new = {
        "TireLettering": mat("M_Tire_Lettering", (0.62, 0.62, 0.61), rough=0.75),
        "CaliperGold": mat("M_Caliper_Gold", (0.80, 0.56, 0.26), rough=0.32, metal=0.85, coat=0.3),
        "SuspForged": mat("M_Susp_Forged", (0.055, 0.055, 0.06), rough=0.45, metal=0.85),
        "Chrome": mat("M_Chrome", (0.92, 0.92, 0.93), rough=0.06, metal=1.0),
    }
    M.update(new)
    return new


# ================================================================================================ TYRE
TV = [0.0, 0.045, 0.09, 0.135, 0.18, 0.225, 0.27, 0.31, 0.345, 0.375, 0.41, 0.44, 0.47, 0.50, 0.545,
      0.585, 0.615, 0.665, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.0]
SH_A = (18, 36, 54, 72, 90)
GROOVE_DEPTH = 0.0055


def tire_outer_half(g):
    """Outer half of the section from the tread edge (excluded) to the bead: [(r, y, kind, dist_from_edge)]."""
    R, ht, rs, hw, hwr, Rr, k = g["R"], g["ht"], g["rs"], g["hw"], g["hwr"], g["Rr"], g["k"]
    r_edge = R - 0.004
    pts = []
    for a in SH_A:
        ar = a * D2R
        pts.append((r_edge - rs + rs * math.cos(ar), ht + rs * math.sin(ar), "sh", rs * ar))
    rA, rB = r_edge - rs, Rr + 0.034 * k
    for f in (0.14, 0.34, 0.56, 0.78, 1.0):
        y = hw - (0.0015 * ((0.34 - f) / 0.34) ** 2 if f < 0.34 else 0.0035 * ((f - 0.34) / 0.66) ** 2)
        pts.append((rA - (rA - rB) * f, y, "sw", 0.0))
    for dr, dy in ((0.0300, 0.0012), (0.0262, 0.0038), (0.0228, 0.0036), (0.0202, 0.0008)):      # rim protector
        pts.append((Rr + dr * k, hw + dy, "pr", 0.0))
    for r, y in ((Rr + 0.0185 * k, hwr + 0.0105), (Rr + 0.0150 * k, hwr + 0.0060),
                 (Rr + 0.0100, hwr - 0.0005), (Rr + 0.0010, hwr - 0.0020)):                        # bead (hidden)
        pts.append((r, y, "bd", 0.0))
    return pts


def tire_profile(g):
    """Full section from the inner bead to the outer bead. Each point: dict(r, y, fine, v, side)."""
    out = tire_outer_half(g)
    R, ht = g["R"], g["ht"]
    T = 2 * ht
    prof = []
    for (r, y, kind, d) in reversed(out):
        prof.append(dict(r=r, y=-y, fine=(kind == "sh"), v=1.0 + d / T if kind == "sh" else None, side=-1))
    for v in reversed(TV):
        y = ht - v * T
        prof.append(dict(r=R - 0.004 * (y / ht) ** 2, y=y, fine=True, v=v, side=0))
    for (r, y, kind, d) in out:
        prof.append(dict(r=r, y=y, fine=(kind == "sh"), v=-d / T if kind == "sh" else None, side=1))
    # 2D outward normals (dy, -dr) of the profile direction
    n = len(prof)
    for i, p in enumerate(prof):
        a, b = prof[max(i - 1, 0)], prof[min(i + 1, n - 1)]
        dr, dy = b["r"] - a["r"], b["y"] - a["y"]
        L = math.hypot(dr, dy) or 1.0
        p["n"] = (dy / L, -dr / L)
    return prof


def shear_fn(T):
    """Circumferential offset (m) of tread column v: angles the lateral slots (Trofeo R pattern, Slide 5)."""
    a1 = 0.345 * T * math.tan(12 * D2R)
    a2 = a1 + 0.065 * T * math.tan(40 * D2R)
    a3 = a2 + 0.060 * T * math.tan(15 * D2R)
    a4 = a3 - 0.385 * T * math.tan(10 * D2R)
    knots = [(0.0, 0.0), (0.345, a1), (0.375, a1), (0.44, a2), (0.50, a3), (0.615, a3), (1.0, a4)]

    def f(v):
        if v < 0:
            return v * (a1 / 0.345)
        if v > 1:
            return a4 + (v - 1.0) * (a4 - a3) / 0.385
        return lerp_table(knots, v)
    return f


def groove_at(vm, kk):
    """Trofeo-R style pattern (from the official Slide 5 render): two circumferential grooves, angled outer
    shoulder slots running over the shoulder, kinked centre-rib sipes every other pitch, inner dashes + slots."""
    p, c = divmod(kk, 6)
    if 0.345 < vm < 0.375 or 0.585 < vm < 0.615:
        return True
    if c == 0 and -0.055 < vm < 0.27:
        return True
    if c == 3 and p % 2 == 0 and 0.375 < vm < 0.50:
        return True
    if c == 3 and 0.615 < vm < 0.665:
        return True
    if c == 3 and 0.70 < vm < 1.085:
        return True
    return False


def build_tire(bm, g, uvl, mat_tire=0):
    trk = Track(bm)
    prof = tire_profile(g)
    N = g["n_pitch"] * 6
    Nc = N // 2
    T = 2 * g["ht"]
    shear = shear_fn(T)
    lay_u = bm.verts.layers.float.new("tu")
    lay_v = bm.verts.layers.float.new("tv")
    lay_c = bm.verts.layers.int.new("tc")
    # arc length for v
    L = [0.0]
    for i in range(1, len(prof)):
        L.append(L[-1] + math.hypot(prof[i]["r"] - prof[i - 1]["r"], prof[i]["y"] - prof[i - 1]["y"]))
    tot = L[-1]
    v_in_end = max(p["v"] for p in prof if p["fine"])
    v_out_end = min(p["v"] for p in prof if p["fine"])
    rings = []
    for i, p in enumerate(prof):
        n = N if p["fine"] else Nc
        if p["fine"]:
            sh = shear(p["v"])
        else:
            sh = shear(v_in_end if p["side"] < 0 else v_out_end)
        ring = []
        for kk in range(n):
            phi = 2 * math.pi * kk / n + sh / p["r"]
            v = bm.verts.new(W(p["r"], phi, p["y"]))
            v[lay_u] = (phi / (2 * math.pi)) % 1.0
            v[lay_v] = L[i] / tot
            v[lay_c] = i
            ring.append(v)
        rings.append(ring)
    grooves = []
    for i in range(len(prof) - 1):
        P, Q = rings[i], rings[i + 1]
        if len(P) == len(Q):
            n = len(P)
            both_fine = prof[i]["fine"] and prof[i + 1]["fine"]
            vm = 0.5 * (prof[i]["v"] + prof[i + 1]["v"]) if both_fine else None
            for kk in range(n):
                f = bm.faces.new((P[kk], P[(kk + 1) % n], Q[(kk + 1) % n], Q[kk]))
                f.material_index = mat_tire
                if both_fine and groove_at(vm, kk):
                    grooves.append(f)
        elif len(P) == 2 * len(Q):
            m = len(Q)
            for j in range(m):
                bm.faces.new((P[2 * j], P[2 * j + 1], Q[j])).material_index = mat_tire
                bm.faces.new((P[2 * j + 1], P[(2 * j + 2) % (2 * m)], Q[(j + 1) % m], Q[j])).material_index = mat_tire
        else:
            m = len(P)
            for j in range(m):
                bm.faces.new((P[j], Q[2 * j + 1], Q[2 * j])).material_index = mat_tire
                bm.faces.new((P[j], P[(j + 1) % m], Q[(2 * j + 2) % (2 * m)], Q[2 * j + 1])).material_index = mat_tire
    # sink the pattern: extrude groove regions inward along the section normal
    ret = bmesh.ops.extrude_face_region(bm, geom=grooves)
    bmesh.ops.delete(bm, geom=grooves, context='FACES')        # the op keeps the original (top) faces
    newv = {e for e in ret["geom"] if isinstance(e, bmesh.types.BMVert)}
    for v in newv:
        nr, ny = prof[v[lay_c]]["n"]
        phi = math.atan2(v.co.z, -v.co.x)
        v.co -= Vector((-nr * math.cos(phi), ny, nr * math.sin(phi))) * GROOVE_DEPTH
    ret_faces = {e for e in ret["geom"] if isinstance(e, bmesh.types.BMFace)}
    walls = {f for f in ret_faces if not all(v in newv for v in f.verts)}
    # UVs: u around (0..1), v along the section (0 inner bead .. 1 outer bead); groove walls get a small UV width
    faces = trk.faces()
    for f in faces:
        us = [l.vert[lay_u] for l in f.loops]
        wrap = max(us) - min(us) > 0.5
        for l in f.loops:
            u = l.vert[lay_u]
            if wrap and u < 0.5:
                u += 1.0
            l[uvl].uv = (u, l.vert[lay_v])
        if f in walls:
            tops = [l for l in f.loops if l.vert not in newv]
            if len(tops) == 2:
                a, b = tops[0][uvl].uv.copy(), tops[1][uvl].uv.copy()
                e = b - a
                perp = Vector((-e.y, e.x))
                if perp.length > 1e-12:
                    perp.normalize()
                    for l in f.loops:
                        if l.vert in newv:
                            l[uvl].uv = l[uvl].uv + perp * 0.0025
    bm.verts.layers.float.remove(lay_u)
    bm.verts.layers.float.remove(lay_v)
    bm.verts.layers.int.remove(lay_c)
    return faces, prof


# ------------------------------------------------------------------------------------------------ sidewall text
_FONTS = {}


def load_font(fname):
    if fname not in _FONTS:
        path = os.path.join(FONT_DIR, fname)
        try:
            _FONTS[fname] = bpy.data.fonts.load(path, check_existing=True)
        except Exception:
            _FONTS[fname] = None
    return _FONTS[fname]


def text_mesh(text, font, size, shear=0.0, spacing=1.0, res=3, extrude=0.0005):
    cu = bpy.data.curves.new("tmp_text", 'FONT')
    cu.body = text
    f = load_font(font)
    if f:
        cu.font = f
    cu.size = size
    cu.shear = shear
    cu.space_character = spacing
    cu.resolution_u = res
    cu.extrude = extrude
    cu.fill_mode = 'FRONT'
    cu.align_x = 'CENTER'
    ob = bpy.data.objects.new("tmp_text", cu)
    bpy.context.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    verts = [v.co.copy() for v in me.vertices]
    faces = [list(p.vertices) for p in me.polygons]
    normals = [p.normal.copy() for p in me.polygons]
    bpy.data.objects.remove(ob, do_unlink=True)
    bpy.data.curves.remove(cu)
    bpy.data.meshes.remove(me)
    return verts, faces, normals


def sidewall_surface(g):
    """y of the outer sidewall surface as a function of r (visible band)."""
    pts = [(r, y) for (r, y, kind, d) in tire_outer_half(g) if kind in ("sw", "pr")]
    pts.append((g["R"] - 0.004 - g["rs"], g["ht"] + g["rs"]))
    pts.sort()
    return lambda r: lerp_table(pts, r)


def sidewall_text(bm, g, text, font, size, phi_c_deg, r_base, mat_idx, sx=1.0, shear=0.0, spacing=1.0,
                  res=3, height=0.0009):
    side = sidewall_surface(g)
    verts, faces, normals = text_mesh(text, font, size, shear, spacing, res)
    if not verts:
        return []
    xs = [v.x for v in verts]
    xm = 0.5 * (min(xs) + max(xs))
    z0, z1 = min(v.z for v in verts), max(v.z for v in verts)
    phi_c = phi_c_deg * D2R
    new = []
    for v in verts:
        x = (v.x - xm) * sx
        r = r_base + v.y
        phi = phi_c - x / r_base
        h = -0.0004 + (v.z - z0) / max(z1 - z0, 1e-9) * (height + 0.0004)
        new.append(bm.verts.new(W(r, phi, side(r) + h)))
    out = []
    for fi, f in enumerate(faces):
        try:
            nf = bm.faces.new([new[i] for i in f])
        except ValueError:
            continue
        nf.material_index = mat_idx
        out.append(nf)
    return out


def tire_lettering(bm, g, uvl, mat_black, mat_white):
    """Raised sidewall lettering on the outer sidewall (two sets, 180 deg apart, as on the real tyre). Positions
    follow the official side photo: TROFEO just above the rim at 10 o'clock, P ZERO before it."""
    trk = Track(bm)
    k = g["k"]
    r0 = g["r_lo"] + 0.0005                       # inner edge of the visible sidewall
    band = (g["R"] - 0.004 - g["rs"]) - r0        # visible sidewall height
    for rot in (0.0, 180.0):
        sidewall_text(bm, g, "TROFEO R", "arialbi.ttf", 0.0305 * k, 157 + rot, r0 + 0.25 * band, mat_white,
                      sx=1.05, spacing=1.10)
        sidewall_text(bm, g, "P ZERO", "arialbi.ttf", 0.0285 * k, 196 + rot, r0 + 0.22 * band, mat_black,
                      sx=1.1, spacing=1.05)
        sidewall_text(bm, g, "PIRELLI", "arialbd.ttf", 0.0265 * k, 72 + rot, r0 + 0.36 * band, mat_black,
                      sx=1.35, spacing=1.0)
        sidewall_text(bm, g, g["size_text"], "bahnschrift.ttf", 0.0115 * k, 118 + rot, r0 + 0.10 * band,
                      mat_black, spacing=1.05, res=2)
        sidewall_text(bm, g, "DOT  M+S  TWI", "bahnschrift.ttf", 0.0080 * k, 250 + rot, r0 + 0.12 * band,
                      mat_black, spacing=1.05, res=2)
    faces = trk.faces()
    planar_uv(bm, faces, uvl, g["R"])
    return faces


# ================================================================================================ RIM
SPOKE_PHI = [30 + 60 * i for i in range(6)]      # spoke axes as in the official side photo (degrees)


def build_barrel(bm, g, uvl, mat):
    Rr, hwr, k, yl, r_io, r_lo = g["Rr"], g["hwr"], g["k"], g["y_lip"], g["r_io"], g["r_lo"]
    prof = [
        (r_io - 0.002, yl - 0.022), (r_io - 0.002, yl - 0.006), (r_io, yl - 0.0015), (r_io + 0.0018, yl),
        (r_lo - 0.0035, yl), (r_lo - 0.0006, yl - 0.0018), (r_lo, yl - 0.005), (r_lo - 0.003, yl - 0.0105),
        (Rr + 0.0135, hwr + 0.0005), (Rr + 0.001, hwr), (Rr, hwr - 0.022),
        (Rr - 0.020 * k, hwr - 0.030), (Rr - 0.020 * k, hwr - 0.048), (Rr - 0.002, hwr - 0.060),
        (Rr, -hwr + 0.022), (Rr + 0.001, -hwr), (Rr + 0.0135, -hwr - 0.0005), (r_lo - 0.003, -hwr - 0.005),
        (r_lo, -hwr - 0.009), (r_lo - 0.003, -hwr - 0.0125), (Rr + 0.004, -hwr - 0.0125),
        (Rr - 0.009, -hwr - 0.006), (Rr - 0.0095, -hwr + 0.010), (Rr - 0.0095, hwr - 0.052),
        (Rr - 0.026 * k, hwr - 0.040), (Rr - 0.026 * k, hwr - 0.030), (Rr - 0.012, hwr - 0.018),
        (r_io - 0.020, yl - 0.022), (r_io - 0.002, yl - 0.022)]
    faces, _ = revolve_y(bm, prof, 120, mat_fn=lambda i: mat, uvl=uvl, uv_mode="cyl")
    recalc(bm, faces)
    return faces


SPOKE_HW = [(0.034, 0.0215), (0.070, 0.0250), (0.100, 0.0268), (0.150, 0.0272), (0.240, 0.0270),
            (0.262, 0.0285), (0.272, 0.0300)]          # spoke half width vs distance along the axis (x k)


def insert_delta_fn(g):
    """Half-angle (rad) occupied by a spoke slot at radius rho. Measured on the side photo: the 54 mm spoke sits
    in a slot whose edges run ~3.5 mm clear of it from the rim down to r ~ 190 mm, then diverge towards the hub
    (gap ~13 mm at r ~ 110 mm) - the gold-lit wedges seen next to every spoke near the hub."""
    k = g["k"]
    o0, s_k, gs = (0.0270 + 0.0035) * k, 0.190 * k, 0.16

    def o(s):
        return o0 + max(0.0, s_k - s) * gs

    def delta(rho):
        s = bisect(lambda s: s * s + o(s) ** 2 - rho * rho, 0.0, rho, it=50)
        if s is None:
            return math.pi / 2
        return math.atan2(o(s), s)
    return delta


def build_inserts(bm, g, uvl, mat, mat_edge):
    """Six flat light-grey aero panels between the spokes: field 16 mm behind the lip face; at r ~ 230 mm a
    4.5 mm step up to an outer band that rises gently to meet the lip (the ring seen in the side photo; the band
    must clear the tyre bead, which sits inboard of the flange); rolled 4.5 mm edges along the slots and round
    the tips in a machined (titanium-look) finish - the bright outlines that frame every panel in the photo;
    4 mm thick."""
    trk = Track(bm)
    k, r_io, y_f = g["k"], g["r_io"], g["y_f"]
    delta = insert_delta_fn(g)
    rho_apex = bisect(lambda r: delta(r) - 30 * D2R, 0.03, r_io)
    r_step = 0.2295 * k

    def ytop(rho):
        if rho >= r_step + 0.001:
            return y_f + 0.0045 + (rho - r_step - 0.001) / (r_io - r_step - 0.001) * 0.0030
        if rho <= r_step - 0.001:
            return y_f
        return y_f + 0.0045 * (rho - r_step + 0.001) / 0.002

    rows = [r_io, r_io - 0.0016]
    rows += [r_step + 0.001 + (r_io - 0.0016 - r_step - 0.001) * t for t in (0.66, 0.33, 0.0)]
    rows += [r_step - 0.001]
    inner_top = r_step - 0.001
    nin = 7
    for i in range(1, nin + 1):
        rows.append(inner_top - (inner_top - (rho_apex + 0.005)) * i / nin)
    EDGE = ((0.0, 0.0022), (0.0020, 0.0007), (0.0045, 0.0))      # (inset, drop) of the rolled slot edge
    all_edge = []
    for ins in range(6):
        beta = (SPOKE_PHI[ins] + 30) * D2R
        top, back = [], []
        for ri, rho in enumerate(rows):
            half = max(30 * D2R - delta(rho), 1e-4)
            span = rho * half
            cols = []
            for inset, drop in EDGE:
                cols.append((-1.0 + min(inset / span, 0.30 + 0.08 * inset / 0.0045), drop))
            cols += [(-0.5, 0.0), (-0.17, 0.0), (0.17, 0.0), (0.5, 0.0)]
            cols += [(-t, dr) for (t, dr) in reversed(cols[:3])]
            yt = ytop(rho)
            trow, brow = [], []
            for ti, (t, drop) in enumerate(cols):
                if ri == 0:
                    drop = max(drop, 0.0012)
                phi = beta + t * half
                trow.append(bm.verts.new(W(rho, phi, yt - drop)))
                brow.append(bm.verts.new(W(rho, phi, yt - 0.004)))
            top.append(trow)
            back.append(brow)
        at = bm.verts.new(W(rho_apex, beta, ytop(rho_apex) - 0.0022))
        ab = bm.verts.new(W(rho_apex, beta, ytop(rho_apex) - 0.004))
        I, J = len(rows) - 1, len(top[0]) - 1
        edge_faces = []
        for i in range(I):
            for j in range(J):
                f = bm.faces.new((top[i][j], top[i][j + 1], top[i + 1][j + 1], top[i + 1][j]))
                if j < 2 or j >= J - 2:
                    edge_faces.append(f)
                bm.faces.new((back[i + 1][j], back[i + 1][j + 1], back[i][j + 1], back[i][j]))
        for j in range(J):
            edge_faces.append(bm.faces.new((top[I][j], top[I][j + 1], at)))
            bm.faces.new((back[I][j + 1], back[I][j], ab))
        all_edge.extend(edge_faces)
        loop_t = top[0][:] + [top[i][J] for i in range(1, I + 1)] + [at] + [top[i][0] for i in range(I, 0, -1)]
        loop_b = back[0][:] + [back[i][J] for i in range(1, I + 1)] + [ab] + [back[i][0] for i in range(I, 0, -1)]
        n = len(loop_t)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((loop_t[i], loop_b[i], loop_b[j], loop_t[j]))
    faces = trk.faces()
    set_mat(faces, mat)
    set_mat(all_edge, mat_edge)
    recalc(bm, faces)
    planar_uv(bm, faces, uvl, g["r_lo"])
    return faces, rho_apex


def build_spokes(bm, g, uvl, mat_spoke, mat_ti):
    """Six gloss-black faceted blade spokes standing 14-26 mm proud of the aero panels: asymmetric roof (wide
    shallow facet + narrow steep facet), a machined (titanium-look) chamfer along the clockwise edge, deep
    side walls (visible through the slot gaps), ramping into the rim lip."""
    trk = Track(bm)
    k, yl, y_f = g["k"], g["y_lip"], g["y_f"]
    S = [s * k for s in (0.034, 0.048, 0.064, 0.082, 0.100, 0.125, 0.150, 0.175, 0.200, 0.222, 0.240,
                         0.252, 0.260, 0.266, 0.272)]
    YR = [(0.034 * k, 0.0100), (0.100 * k, 0.0095), (0.200 * k, 0.0072), (0.240 * k, 0.0042),
          (0.252 * k, 0.0012), (0.262 * k, -0.0020), (0.272 * k, -0.0045)]
    HW = [(s * k, w * k) for (s, w) in SPOKE_HW]
    yb = y_f - 0.030
    uR = -0.006 * k
    for i in range(6):
        ph = SPOKE_PHI[i] * D2R
        d = Vector((-math.cos(ph), 0.0, math.sin(ph)))
        nn = Vector((-math.cos(ph + math.pi / 2), 0.0, math.sin(ph + math.pi / 2)))
        stations = []
        for s in S:
            hw = lerp_table(HW, s)
            yr = yl + lerp_table(YR, s)
            sec = [(hw, yb), (hw, yr - 0.0100), (hw - 0.0015, yr - 0.0085), (uR, yr),
                   (-hw + 0.0040 * k, yr - 0.0068), (-hw, yr - 0.0120), (-hw, yb)]
            stations.append([bm.verts.new(d * s + nn * u + Vector((0, y, 0))) for (u, y) in sec])
        ns = len(stations[0])
        for m in range(len(S) - 1):
            A, B = stations[m], stations[m + 1]
            for e in range(ns):
                f = bm.faces.new((A[e], A[(e + 1) % ns], B[(e + 1) % ns], B[e]))
                f.material_index = mat_ti if e == 4 else mat_spoke
        bm.faces.new(stations[0]).material_index = mat_spoke
        bm.faces.new(list(reversed(stations[-1]))).material_index = mat_spoke
    faces = trk.faces()
    recalc(bm, faces)
    planar_uv(bm, faces, uvl, g["r_lo"])
    return faces


def boss_profile(g):
    yl, y_f, et, k = g["y_lip"], g["y_f"], g["et"], g["k"]
    s = k
    return [(0.0, yl + 0.0145), (0.010 * s, yl + 0.0143), (0.020 * s, yl + 0.0133), (0.026 * s, yl + 0.0120),
            (0.0295 * s, yl + 0.0100), (0.0305 * s, yl + 0.0080), (0.0310 * s, yl + 0.0058), (0.0330 * s, yl + 0.0058),
            (0.0345 * s, yl + 0.0085), (0.0480 * s, yl + 0.0095), (0.0540 * s, yl + 0.0070),
            (0.0600 * s, yl + 0.0010), (0.0620 * s, yl - 0.0300), (0.0660 * s, y_f - 0.040),
            (0.0870, et + 0.010), (0.0880, et), (0.0300, et), (0.0, et + 0.002)]


def build_boss(bm, g, uvl, mat):
    """Hub boss with the round black centre cap (dome), seam groove and the web to the hub mounting flange."""
    prof = boss_profile(g)
    faces, _ = revolve_y(bm, list(reversed(prof)), 72, mat_fn=lambda i: mat, uvl=uvl, uv_mode="planar",
                         uv_R=g["r_lo"])
    recalc(bm, faces)
    # solid centre disc of the wheel behind the windows between the spoke roots (they read black in the photos)
    k, yp = g["k"], g["y_f"] - 0.012
    plate = [(0.050 * k, yp), (0.104 * k, yp), (0.107 * k, yp - 0.003), (0.107 * k, yp - 0.006),
             (0.050 * k, yp - 0.006), (0.050 * k, yp)]
    fs, _ = revolve_y(bm, plate, 72, mat_fn=lambda i: mat, uvl=uvl, uv_mode="planar", uv_R=g["r_lo"])
    recalc(bm, fs)
    return faces + fs


def build_emblem(bm, g, uvl, mat):
    """Raised Tesla 'T' on the centre cap (upright when a spoke points straight up, as in the side photo)."""
    trk = Track(bm)
    s = 0.001 * g["k"] * 0.95
    half = [(0.0, -16.5), (0.9, -15.3), (1.5, -9.0), (1.9, -1.0), (2.15, 4.6), (4.5, 5.3), (8.0, 6.0),
            (11.5, 6.6), (14.0, 7.0), (15.6, 7.6), (15.2, 8.1), (12.0, 8.55), (8.0, 8.9), (4.0, 9.1), (0.0, 9.15)]
    body = half + [(-x, y) for (x, y) in reversed(half[1:-1])]
    up = [(x, 13.3 - 0.0062 * x * x) for x in (-14, -9, -4.5, 0, 4.5, 9, 14)]
    lo = [(x, 11.7 - 0.0052 * x * x) for x in (14, 9, 4.5, 0, -4.5, -9, -14)]
    arc = up + lo
    cap = [(r, y) for (r, y) in boss_profile(g)[:6]]

    def frame(u, v, z):
        x, y = u * s, (v + 1.6) * s
        r = math.hypot(x, y)
        return Vector((-x, lerp_table(cap, r) + z, y))
    for poly in (body, arc):
        extrude_polygon(bm, poly, frame, -0.0004, 0.0007, mat)
    faces = trk.faces()
    recalc(bm, faces)
    planar_uv(bm, faces, uvl, g["r_lo"])
    return faces


def build_wheel(M, key):
    g = geom(key)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["Tire"], M["TireLettering"], M["Spoke"], M["Disc"], M["Titanium"]]
    build_tire(bm, g, uvl, 0)
    tire_lettering(bm, g, uvl, 0, 1)
    build_barrel(bm, g, uvl, 2)
    build_inserts(bm, g, uvl, 3, 4)
    build_spokes(bm, g, uvl, 2, 4)
    build_boss(bm, g, uvl, 2)
    build_emblem(bm, g, uvl, 4)
    ob = new_mesh_object("Wheel_" + key, bm, mats, smooth=31.0)
    ob["tire"] = "Pirelli P Zero Trofeo R %s" % g["size_text"]
    return ob


# ================================================================================================ BRAKES
def build_disc(M, key):
    """Ventilated, cross-drilled carbon-ceramic rotor (spiral rows of 4 holes, curved internal vanes), floating
    gold-anodised aluminium bell with 12 bobbins, hub flange. Spins with the wheel."""
    g = geom(key)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["BrakeDisc"], M["Gold"], M["Titanium"], M["Aluminum"]]
    r_i, r_o, t, gap, y_d, et = g["r_di"], g["r_do"], g["disc_t"], g["disc_gap"], g["y_d"], g["et"]
    tp = (t - gap) / 2
    NA = g["hole_pat"] * 4
    NR = 4
    hole_r = 0.0035 if key == "Front" else 0.0033
    hole_depth = 0.008
    ch = 0.0012

    def drilled_face(y_face, sgn):
        """Friction face at y_face (outward normal sign sgn along Y) with blind drilled holes."""
        trk = Track(bm)
        ra, rb = r_i + ch, r_o - ch
        grid = {}

        def G(ia, ir):
            key2 = (ia % (2 * NA), ir)
            if key2 not in grid:
                phi = 2 * math.pi * key2[0] / (2 * NA)
                r = ra + (rb - ra) * ir / (2 * NR)
                grid[key2] = bm.verts.new(W(r, phi, y_face))
            return grid[key2]
        for ia in range(NA):
            for ir in range(NR):
                a0, r0 = 2 * ia, 2 * ir
                ring = [G(a0, r0), G(a0 + 1, r0), G(a0 + 2, r0), G(a0 + 2, r0 + 1), G(a0 + 2, r0 + 2),
                        G(a0 + 1, r0 + 2), G(a0, r0 + 2), G(a0, r0 + 1)]
                holed = (ia % 4) == ir
                if holed:
                    cphi = 2 * math.pi * (a0 + 1) / (2 * NA)
                    cr = ra + (rb - ra) * (r0 + 1) / (2 * NR)
                    c = W(cr, cphi, y_face)
                    hv = []
                    for v in ring:
                        dvec = v.co - c
                        dvec.y = 0
                        hv.append(bm.verts.new(c + dvec.normalized() * hole_r))
                    hb = [bm.verts.new(v.co - Vector((0, sgn * hole_depth, 0))) for v in hv]
                    for q in range(8):
                        bm.faces.new((ring[q], ring[(q + 1) % 8], hv[(q + 1) % 8], hv[q]))
                        bm.faces.new((hv[q], hv[(q + 1) % 8], hb[(q + 1) % 8], hb[q]))
                    bm.faces.new(list(reversed(hb)))
                else:
                    cv = G(a0 + 1, r0 + 1)
                    for q in range(0, 8, 2):
                        bm.faces.new((ring[q], ring[q + 1], cv, ring[(q + 7) % 8]))
        edge_in = [G(i, 0) for i in range(2 * NA)]
        edge_out = [G(i, 2 * NR) for i in range(2 * NA)]
        return trk, edge_in, edge_out

    def plate(y_out, y_in, sgn):
        """One friction plate: drilled outer face at y_out, flat vent-side face at y_in, chamfered OD / ID."""
        trk_all = Track(bm)
        _, e_in, e_out = drilled_face(y_out, sgn)
        n = len(e_in)
        ring = lambda r, y: [bm.verts.new(W(r, 2 * math.pi * i / n, y)) for i in range(n)]
        od1 = ring(r_o, y_out - sgn * ch)
        od2 = ring(r_o, y_in)
        id1 = ring(r_i, y_out - sgn * ch)
        id2 = ring(r_i, y_in)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((e_out[i], e_out[j], od1[j], od1[i]))
            bm.faces.new((od1[i], od1[j], od2[j], od2[i]))
            bm.faces.new((od2[i], od2[j], id2[j], id2[i]))
            bm.faces.new((id2[i], id2[j], id1[j], id1[i]))
            bm.faces.new((id1[i], id1[j], e_in[j], e_in[i]))
        fs = trk_all.faces()
        set_mat(fs, 0)
        recalc(bm, fs)
        return fs

    plate(y_d + t / 2, y_d + t / 2 - tp, 1)
    plate(y_d - t / 2, y_d - t / 2 + tp, -1)
    # curved vanes in the vent gap
    trk = Track(bm)
    nv = g["vanes"]
    for i in range(nv):
        ph0 = 2 * math.pi * i / nv
        pts = []
        for j in range(3):
            u = j / 2
            r = r_i + 0.003 + (r_o - 0.0015 - r_i - 0.003) * u
            pts.append((r, ph0 + 0.32 * u * u))
        secs = []
        for (r, ph) in pts:
            hw = 0.0022 if key == "Front" else 0.0020
            dph = hw / r
            ylo, yhi = y_d - gap / 2 - 0.0005, y_d + gap / 2 + 0.0005
            secs.append([bm.verts.new(W(r, ph - dph, ylo)), bm.verts.new(W(r, ph + dph, ylo)),
                         bm.verts.new(W(r, ph + dph, yhi)), bm.verts.new(W(r, ph - dph, yhi))])
        for a, b in zip(secs, secs[1:]):
            for e in range(4):
                bm.faces.new((a[e], a[(e + 1) % 4], b[(e + 1) % 4], b[e]))
        bm.faces.new(list(reversed(secs[0])))
        bm.faces.new(secs[-1])
    fs = trk.faces()
    set_mat(fs, 0)
    recalc(bm, fs)
    # floating bell (gold anodised): face clamped between hub and wheel, wall, flange inside the ring's ID
    yo = y_d + t / 2
    prof = [(0.036, et), (0.090, et), (0.094, et - 0.0015), (0.096, et - 0.006), (0.099, yo - 0.002),
            (r_i - 0.0005, yo - 0.002), (r_i - 0.0005, yo - 0.0075), (0.104, yo - 0.0075), (0.102, et - 0.007),
            (0.092, et - 0.0065), (0.036, et - 0.0065), (0.036, et)]
    fs, _ = revolve_y(bm, prof, 64, mat_fn=lambda i: 1)
    recalc(bm, fs)
    # 12 bobbins (drive pins) bridging bell and ring
    for i in range(12):
        ph = 2 * math.pi * (i + 0.5) / 12
        c0 = W(r_i - 0.0005, ph, yo - 0.0075)
        c1 = W(r_i - 0.0005, ph, yo + 0.0018)
        fs = cylinder(bm, c0, c1, 0.0058, segs=8, mat=2, bevel=0.0008)
        recalc(bm, fs)
    # hub flange and bearing hub (spins)
    prof = [(0.030, et - 0.0065), (0.072, et - 0.0065), (0.072, et - 0.017), (0.046, et - 0.022),
            (0.046, et - 0.047), (0.030, et - 0.047), (0.030, et - 0.0065)]
    fs, _ = revolve_y(bm, prof, 40, mat_fn=lambda i: 3)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 4.0)
    return new_mesh_object("Brake_%s_Disc" % key, bm, mats, smooth=35.0)


def caliper_xform(g):
    R0, phc = g["cal_r0"], g["cal_phi"] * D2R

    def bend(s, h, a):
        return W(R0 + h, phc + s / R0, a)
    return bend


def caliper_lug_points(g):
    """Centres of the two radial mounting lugs (wheel frame) - the upright's caliper ears end here."""
    L = g["cal_len"]
    bend = caliper_xform(g)
    return [bend(sg * (L / 2 - 0.030), -0.057 - 0.017 - 0.006, g["y_d"] - 0.078) for sg in (-1, 1)]


def build_caliper(M, key):
    """Gold monoblock caliper (6-piston front / 4-piston rear) trailing the axle as in the photos: two kidney
    shaped halves, two bridges over the rotor with the pad window between, piston bulges, radial mounting lugs,
    bleed nipples, banjo fitting, pads with backing plates, retaining pin and anti-rattle spring."""
    g = geom(key)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["CaliperGold"], M["Rubber"], M["Aluminum"], M["Titanium"]]
    L, y_d, t = g["cal_len"], g["y_d"], g["disc_t"]
    bend = caliper_xform(g)
    hb, ht0 = -0.048, 0.045
    hl = L / 2

    def kidney(q, c):
        s = c[0] + q.x
        sn = max(-1.0, min(1.0, s / hl))
        top = ht0 - 0.010 * sn * sn
        bot = hb + 0.020 * sn ** 4
        h0 = c[1] + q.y
        tt = (h0 - hb) / (ht0 - hb)
        return bend(s, bot + (top - bot) * tt, c[2] + q.z)

    a_in, a_out = 0.035 * (t / 0.038) ** 0.3, 0.070
    for sg in (1, -1):
        ac = y_d + sg * 0.5 * (a_in + a_out)
        c = (0.0, 0.5 * (hb + ht0), ac)
        fs = rounded_box(bm, (hl, 0.5 * (ht0 - hb), 0.5 * (a_out - a_in)), 0.011,
                         xform=lambda q, c=c: kidney(q, c), nmid=(6, 2, 1), mat=0)
        recalc(bm, fs)
        # piston bulges on the outer faces
        np_ = len(g["pistons"])
        for pi, rp in enumerate(g["pistons"]):
            s0 = (pi - (np_ - 1) / 2) * (0.087 if np_ == 3 else 0.090)
            prof = [(0.0, 0.0028), (rp - 0.0022, 0.0028), (rp, 0.0012), (rp + 0.0008, -0.003), (0.0, -0.003)]
            base = y_d + sg * a_out
            fs = revolve_axis(bm, prof, bend(s0, -0.004, base), Vector((0, sg, 0)), 20, mat_fn=lambda i: 0)
            # bend correction for the flat bulge is negligible (rp << R0)
            recalc(bm, fs)
    # bridges over the rotor (pad window between them)
    for sg in (-1, 1):
        sc = sg * (hl - 0.045)
        fs = rounded_box(bm, (0.032, 0.0095, 0.062), 0.007,
                         xform=lambda q, sc=sc: bend(sc + q.x, 0.0305 + q.y, y_d + q.z), nmid=(2, 1, 2), mat=0)
        recalc(bm, fs)
    # pads (friction material + backing plates)
    for sg in (1, -1):
        for (a0, a1, hb0, ht1, mi, sl) in ((t / 2 + 0.0005, t / 2 + 0.0125, -0.053, 0.0115, 1, hl - 0.040),
                                           (t / 2 + 0.0125, t / 2 + 0.0175, -0.055, 0.0175, 2, hl - 0.032)):
            ac = y_d + sg * 0.5 * (a0 + a1)
            hc = 0.5 * (hb0 + ht1)
            fs = rounded_box(bm, (sl, 0.5 * (ht1 - hb0), 0.5 * (a1 - a0)), 0.0025,
                             xform=lambda q, ac=ac, hc=hc: bend(q.x, hc + q.y, ac + q.z), nmid=(4, 1, 0), mat=mi, seg=2)
            recalc(bm, fs)
    # pad retaining pin + anti-rattle spring in the window
    fs = cylinder(bm, bend(0, 0.0255, y_d - 0.060), bend(0, 0.0255, y_d + 0.060), 0.0032, segs=12, mat=3,
                  bevel=0.0008)
    recalc(bm, fs)
    fs = rounded_box(bm, (0.030, 0.0009, 0.026), 0.0008, xform=lambda q: bend(q.x, 0.0225 + q.y + 0.004 *
                     (1 - (q.x / 0.03) ** 2), y_d + q.z), nmid=(3, 0, 1), mat=2, seg=2)
    recalc(bm, fs)
    # radial mounting lugs + bolts (inboard half)
    for sg in (-1, 1):
        s0 = sg * (hl - 0.030)
        fs = rounded_box(bm, (0.016, 0.017, 0.014), 0.005,
                         xform=lambda q, s0=s0: bend(s0 + q.x, -0.057 + q.y, y_d - 0.078 + q.z), mat=0, seg=2)
        recalc(bm, fs)
        p0, p1 = bend(s0, -0.040, y_d - 0.084), bend(s0, -0.033, y_d - 0.084)
        fs = cylinder(bm, p0, p1, 0.0085, segs=6, mat=3, bevel=0.0006)
        recalc(bm, fs)
    # bleed nipples on the upper end of both halves, with dust caps
    s_b = hl - 0.026
    top_b = ht0 - 0.010 * (s_b / hl) ** 2
    for sg in (1, -1):
        a = y_d + sg * 0.052
        p0 = bend(s_b, top_b - 0.002, a)
        p1 = bend(s_b, top_b + 0.004, a)
        p2 = bend(s_b, top_b + 0.011, a)
        p3 = bend(s_b, top_b + 0.016, a)
        for fs in (cylinder(bm, p0, p1, 0.0052, segs=6, mat=3, bevel=0.0005),
                   cylinder(bm, p1, p2, 0.0030, segs=10, mat=3),
                   cylinder(bm, p2, p3, 0.0042, segs=12, mat=1, bevel=0.001)):
            recalc(bm, fs)
    # brake-line banjo fitting on the inboard face
    pa = bend(0.055, 0.004, y_d - a_out + 0.002)
    pb = bend(0.055, 0.004, y_d - a_out - 0.012)
    pc = bend(0.055, 0.004, y_d - a_out - 0.020)
    for fs in (cylinder(bm, pa, pb, 0.0095, segs=14, mat=2, bevel=0.0015),
               cylinder(bm, pb, pc, 0.0068, segs=6, mat=3, bevel=0.0006)):
        recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 6.0)
    return new_mesh_object("Brake_%s_Caliper" % key, bm, mats, smooth=40.0)


# ================================================================================================ SUSPENSION
def corner_hardpoints(corner):
    """Hardpoints (Blender frame, metres) at static ride height for corner FL/FR/RL/RR.
    Front: SLA double wishbone, front-steer track rod, pushrod -> rocker -> transverse coil-over (as Slide 5).
    Rear: double wishbone with a leading toe link, pushrod/rocker/transverse coil-over above the half-shaft."""
    front = corner[0] == "F"
    side = 1.0 if corner[1] == "L" else -1.0
    g = geom("Front" if front else "Rear")
    xa = FA if front else RA
    WC = Vector((xa, CORNER_Y[corner[0]], g["R"]))
    hp = {}
    if front:
        hp["LBJ"] = WC + Vector((0.000, -0.085, -0.180))
        hp["UBJ"] = WC + Vector((-0.022, -0.125, 0.190))
        hp["TO"] = WC + Vector((0.135, -0.075, 0.025))
        hp["TI"] = Vector((xa + 0.135, 0.400, WC.z + 0.001))      # optimised: < 0.07 deg bump steer over travel
        hp["LF"] = Vector((xa + 0.200, 0.320, 0.155))
        hp["LR"] = Vector((xa - 0.200, 0.320, 0.150))
        hp["UF"] = Vector((xa + 0.160, 0.400, 0.505))
        hp["UR"] = Vector((xa - 0.150, 0.400, 0.495))
        hp["PL"] = hp["LBJ"] + Vector((0.030, -0.045, 0.030))
        hp["PU"] = Vector((xa + 0.030, 0.430, 0.585))
    else:
        hp["LBJ"] = WC + Vector((0.000, -0.090, -0.185))
        hp["UBJ"] = WC + Vector((0.010, -0.120, 0.195))
        hp["TO"] = WC + Vector((0.145, -0.080, 0.015))
        hp["TI"] = Vector((xa + 0.145, 0.410, WC.z - 0.009))      # optimised: < 0.07 deg bump steer over travel
        hp["LF"] = Vector((xa + 0.210, 0.330, 0.165))
        hp["LR"] = Vector((xa - 0.190, 0.330, 0.165))
        hp["UF"] = Vector((xa + 0.160, 0.420, 0.535))
        hp["UR"] = Vector((xa - 0.150, 0.420, 0.535))
        hp["PL"] = hp["LBJ"] + Vector((0.085, -0.050, 0.030))
        hp["PU"] = Vector((xa + 0.085, 0.440, 0.600))
        hp["IC"] = Vector((xa, 0.280, WC.z - 0.007))          # inboard CV (motor output)
        hp["OC"] = WC + Vector((0.0, -0.105, 0.0))             # outboard CV centre
    # rocker: pushrod arm (90 mm) perpendicular to the pushrod, damper arm (80 mm) vertical -> horizontal damper
    pdir = (hp["PU"] - hp["PL"]).normalized()
    arm = Vector((0.0, -pdir.z, pdir.y)) if pdir.z > 0 else Vector((0.0, pdir.z, -pdir.y))
    if arm.y < 0:
        arm = -arm
    hp["RP"] = hp["PU"] - arm.normalized() * 0.090
    hp["DP"] = hp["RP"] + Vector((0.0, 0.0, 0.080))
    hp["DI"] = Vector((hp["DP"].x, 0.065 if front else 0.075, hp["DP"].z))
    hp["WC"] = WC
    hp["rocker_axis"] = Vector((1.0, 0.0, 0.0))
    if side < 0:
        hp = {k: (Vector((v.x, -v.y, v.z)) if k != "rocker_axis" else v.copy()) for k, v in hp.items()}
    return hp


# ------------------------------------------------------------------------------------------------ kinematics
def rot_about(p, origin, axis, ang):
    return origin + Quaternion(axis, ang) @ (p - origin)


def trilaterate(p1, p2, p3, r1, r2, r3):
    ex = (p2 - p1).normalized()
    i = ex.dot(p3 - p1)
    ey = (p3 - p1 - ex * i)
    if ey.length < 1e-12:
        return []
    ey.normalize()
    ez = ex.cross(ey)
    d = (p2 - p1).length
    j = ey.dot(p3 - p1)
    x = (r1 * r1 - r2 * r2 + d * d) / (2 * d)
    y = (r1 * r1 - r3 * r3 + i * i + j * j) / (2 * j) - (i / j) * x
    z2 = r1 * r1 - x * x - y * y
    if z2 < 0:
        if z2 < -1e-6:
            return []
        z2 = 0.0
    z = math.sqrt(z2)
    base = p1 + ex * x + ey * y
    return [base + ez * z, base - ez * z]


def frame3(a, b, c):
    e1 = (b - a).normalized()
    e2 = (c - a) - e1 * e1.dot(c - a)
    e2.normalize()
    e3 = e1.cross(e2)
    return Matrix((e1, e2, e3)).transposed()


class CornerKinematics:
    def __init__(self, hp, front):
        self.hp = hp
        self.front = front
        self.axL = (hp["LF"] - hp["LR"]).normalized()
        self.axU = (hp["UF"] - hp["UR"]).normalized()
        self.axR = hp["rocker_axis"]
        self.Lk = (hp["UBJ"] - hp["LBJ"]).length
        self.d1 = (hp["TO"] - hp["LBJ"]).length
        self.d2 = (hp["TO"] - hp["UBJ"]).length
        self.d3 = (hp["TO"] - hp["TI"]).length
        self.Lp = (hp["PU"] - hp["PL"]).length
        self.F0 = frame3(hp["LBJ"], hp["UBJ"], hp["TO"])
        self.Ld0 = (hp["DP"] - hp["DI"]).length

    def pose(self, thL, rack=0.0, rocker=True):
        hp = self.hp
        LBJ = rot_about(hp["LBJ"], hp["LR"], self.axL, thL)
        thU = root_near(lambda a: (rot_about(hp["UBJ"], hp["UR"], self.axU, a) - LBJ).length - self.Lk,
                        -0.7, 0.7, near=thL)
        if thU is None:
            return None
        UBJ = rot_about(hp["UBJ"], hp["UR"], self.axU, thU)
        TI = hp["TI"] + Vector((0.0, rack, 0.0))
        guess = LBJ + (hp["TO"] - hp["LBJ"])
        sols = trilaterate(LBJ, UBJ, TI, self.d1, self.d2, self.d3)
        if not sols:
            return None
        TO = min(sols, key=lambda s: (s - guess).length)
        R = frame3(LBJ, UBJ, TO) @ self.F0.transposed()
        WC = LBJ + R @ (hp["WC"] - hp["LBJ"])
        PL = rot_about(hp["PL"], hp["LR"], self.axL, thL)
        if not rocker:
            return dict(thL=thL, thU=thU, LBJ=LBJ, UBJ=UBJ, TO=TO, TI=TI, R=R, WC=WC, PL=PL)
        thR = root_near(lambda a: (rot_about(hp["PU"], hp["RP"], self.axR, a) - PL).length - self.Lp,
                        -1.3, 1.3, near=0.0)
        if thR is None:
            return None
        PU = rot_about(hp["PU"], hp["RP"], self.axR, thR)
        DP = rot_about(hp["DP"], hp["RP"], self.axR, thR)
        out = dict(thL=thL, thU=thU, thR=thR, LBJ=LBJ, UBJ=UBJ, TO=TO, TI=TI, R=R, WC=WC, PL=PL, PU=PU, DP=DP,
                   Ld=(DP - hp["DI"]).length)
        if "OC" in hp:
            out["OC"] = LBJ + R @ (hp["OC"] - hp["LBJ"])
        return out

    def pose_travel(self, dz, rack=0.0):
        z0 = self.hp["WC"].z

        def f(a):
            p = self.pose(a, rack, rocker=False)
            return None if p is None else p["WC"].z - (z0 + dz)
        th = root_near(f, -0.4, 0.4, n=40, near=0.0)
        return self.pose(th, rack) if th is not None else None


def wheel_angles(R, side):
    a = R @ Vector((0.0, side, 0.0))
    f = R @ Vector((1.0, 0.0, 0.0))
    camber = -math.degrees(math.asin(max(-1.0, min(1.0, a.z))))     # negative = top of the wheel inboard
    toe = math.degrees(math.atan2(f.y, f.x))
    return camber, toe


# ------------------------------------------------------------------------------------------------ part meshes
def forged_leg(bm, p0, p1, w0, w1, mat, up=Vector((0, 0, 1)), bow=0.0):
    """Forged I-beam-like arm leg (superellipse section, slight bow) from p0 to p1."""
    p0, p1 = Vector(p0), Vector(p1)
    cs = []
    sz = []
    for i in range(7):
        t = i / 6
        c = p0.lerp(p1, t) + Vector(up) * (bow * math.sin(math.pi * t))
        cs.append(c)
        sz.append((w0[0] + (w1[0] - w0[0]) * t, w0[1] + (w1[1] - w0[1]) * t))
    return loft(bm, cs, sz, n=12, mat=mat, up=up, p=3.2)


def clevis(bm, c, axis, gap, ear_r, ear_t, toward, length, mat, bolt_mat=None):
    """Two ears straddling a joint at c along `axis`, extending in direction `toward` by `length`."""
    axis = Vector(axis).normalized()
    toward = Vector(toward).normalized()
    side_dir = axis.cross(toward).normalized()
    fs = []
    outline = [(ear_r * math.cos(a), ear_r * math.sin(a))
               for a in [math.pi / 2 + math.pi * i / 8 for i in range(9)]] + [(length, -ear_r), (length, ear_r)]
    c = Vector(c)
    frame = lambda u, v, z: c + toward * u + side_dir * v + axis * z
    for sg in (-1, 1):
        z0, z1 = sorted((sg * gap / 2, sg * (gap / 2 + ear_t)))
        fs += extrude_polygon(bm, outline, frame, z0, z1, mat)
    if bolt_mat is not None:
        fs += cylinder(bm, Vector(c) - axis * (gap / 2 + ear_t + 0.006), Vector(c) + axis * (gap / 2 + ear_t + 0.006),
                       ear_r * 0.42, segs=6, mat=bolt_mat, bevel=0.001)
    return fs


def rod_end(bm, c, axis, r_out, thick, mat_body, mat_ball, shank_dir=None, shank_len=0.0, shank_r=0.0):
    axis = Vector(axis).normalized()
    fs = cylinder(bm, Vector(c) - axis * thick / 2, Vector(c) + axis * thick / 2, r_out, segs=14, mat=mat_body,
                  bevel=0.0015)
    fs += sphere(bm, c, thick * 0.55, mat=mat_ball, u=10, v=6)
    fs += cylinder(bm, Vector(c) - axis * (thick / 2 + 0.006), Vector(c) + axis * (thick / 2 + 0.006),
                   r_out * 0.32, segs=8, mat=mat_ball)
    if shank_dir is not None and shank_len > 0:
        sd = Vector(shank_dir).normalized()
        fs += cylinder(bm, Vector(c) + sd * (r_out * 0.6), Vector(c) + sd * (r_out * 0.6 + shank_len), shank_r,
                       segs=12, mat=mat_body, bevel=0.001)
    return fs


def build_arm(M, corner, hp, which):
    """Lower or upper wishbone: forged legs, rubber-bushed pivot housings, ball-joint housing, (lower) pushrod
    clevis and gusset plate. Origin on the chassis hinge axis (midpoint of the two pivots)."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Rubber"], M["Titanium"], M["Aluminum"]]
    lower = which == "Lower"
    PF, PR = (hp["LF"], hp["LR"]) if lower else (hp["UF"], hp["UR"])
    BJ = hp["LBJ"] if lower else hp["UBJ"]
    ax = (PF - PR).normalized()
    kp = (hp["UBJ"] - hp["LBJ"]).normalized()
    w0 = (0.021, 0.0125) if lower else (0.017, 0.0105)
    w1 = (0.016, 0.0110) if lower else (0.013, 0.0095)
    fs = []
    for P in (PF, PR):
        fs += cylinder(bm, P - ax * 0.024, P + ax * 0.024, 0.026 if lower else 0.023, segs=16, mat=0, bevel=0.003)
        fs += cylinder(bm, P - ax * 0.027, P + ax * 0.027, 0.0165 if lower else 0.015, segs=12, mat=1)
        fs += cylinder(bm, P - ax * 0.040, P + ax * 0.040, 0.0075, segs=6, mat=2, bevel=0.001)
        d = (BJ - P)
        start = P + d.normalized() * 0.022
        end = BJ - d.normalized() * 0.022
        fs += forged_leg(bm, start, end, w0, w1, 0, bow=0.012 if lower else 0.008)
    # ball-joint housing (axis along the kingpin)
    off = -0.004 if lower else 0.004
    fs += cylinder(bm, BJ + kp * (off - 0.017), BJ + kp * (off + 0.017), 0.027 if lower else 0.024, segs=20, mat=0,
                   bevel=0.003)
    if lower:
        # gusset plate between the legs near the ball joint + pushrod clevis
        a = BJ + (PF - BJ).normalized() * 0.10
        b = BJ + (PR - BJ).normalized() * 0.10
        nrm = (a - BJ).cross(b - BJ).normalized()
        plate_c = (BJ + a + b + hp["PL"]) / 4
        x_ax = (a - b).normalized()
        y_ax = nrm.cross(x_ax).normalized()
        span_x = (a - b).length / 2 + 0.01
        span_y = 0.045
        fs += rbox_at(bm, plate_c - nrm * 0.002, (span_x * 0.8, span_y, 0.004), 0.0035, axes=(x_ax, y_ax, nrm),
                      mat=0, nmid=(2, 2, 0))
        fs += clevis(bm, hp["PL"], Vector((1, 0, 0)), 0.018, 0.016, 0.007, -nrm if nrm.z > 0 else nrm,
                     0.030, 0, bolt_mat=2)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    pivot = (PF + PR) / 2
    return new_mesh_object("Susp_%s_%sArm" % (corner, which), bm, mats, smooth=40.0, pivot=pivot)


def build_upright(M, corner, hp):
    """Cast hub carrier: bearing housing, upper and lower extensions to the ball joints (ball studs + boots),
    steering/toe arm with rod-end boss, two caliper mounting ears ending at the caliper's radial lugs."""
    front = corner[0] == "F"
    side = 1.0 if corner[1] == "L" else -1.0
    g = geom("Front" if front else "Rear")
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Rubber"], M["Titanium"], M["Aluminum"]]
    WC = hp["WC"]
    Y = Vector((0, side, 0))
    fs = []
    y_out = g["et"] - 0.026
    y_in = -0.085
    fs += cylinder(bm, WC + Y * y_in, WC + Y * y_out, 0.058, segs=28, mat=0, bevel=0.004)
    fs += cylinder(bm, WC + Y * (y_in - 0.004), WC + Y * (y_in + 0.016), 0.066, segs=28, mat=0, bevel=0.003)
    # extensions
    ymid = -0.072
    # cast web around the bearing housing that the arms grow out of
    fs += rbox_at(bm, WC + Y * (ymid - 0.004), (0.050, 0.016, 0.072), 0.012,
                  axes=(Vector((1, 0, 0)), Y, Vector((0, 0, 1))), mat=0, nmid=(1, 0, 1), seg=4)
    for BJ, base_z, w in ((hp["UBJ"], 0.050, (0.030, 0.015)), (hp["LBJ"], -0.050, (0.034, 0.017))):
        base = WC + Y * ymid + Vector((0, 0, base_z))
        mid = base.lerp(BJ, 0.55) + Y * (-0.006)
        end = BJ
        fs += loft(bm, [base, mid, end], [w, (w[0] * 0.8, w[1] * 0.95), (w[0] * 0.6, w[1] * 0.9)], n=14, mat=0,
                   up=Y, p=3.0)
        kp = (hp["UBJ"] - hp["LBJ"]).normalized()
        s = 1 if BJ is hp["UBJ"] else -1
        # ball stud + boot towards the arm's housing
        fs += sphere(bm, BJ, 0.0145, mat=2, u=12, v=8)
        fs += cylinder(bm, BJ - kp * s * 0.004, BJ - kp * s * 0.022, 0.018, segs=16, mat=1, r1=0.013, bevel=0.002)
    # steering arm (front) / toe-link arm (rear) at the leading side
    TO = hp["TO"]
    base = WC + Y * (-0.070) + Vector((0.045, 0, 0.005))
    fs += loft(bm, [base, base.lerp(TO, 0.5), TO], [(0.020, 0.016), (0.016, 0.013), (0.013, 0.012)], n=12, mat=0,
               up=Vector((0, 0, 1)), p=3.0)
    fs += cylinder(bm, TO - Vector((0, 0, 0.018)), TO + Vector((0, 0, 0.018)), 0.015, segs=16, mat=0, bevel=0.002)
    # caliper ears (meet the caliper's radial lugs)
    for lug in caliper_lug_points(g):
        p = WC + Vector((lug.x, side * lug.y, lug.z))
        base = WC + Y * (lug.y) + (Vector((lug.x, 0, lug.z)).normalized() * 0.050)
        fs += loft(bm, [base, base.lerp(p, 0.5), p], [(0.016, 0.012), (0.013, 0.011), (0.012, 0.011)], n=12,
                   mat=0, up=Y, p=3.0)
        fs += rbox_at(bm, p, (0.014, 0.010, 0.012), 0.004,
                      axes=(Vector((lug.x, 0, lug.z)).normalized().cross(Y), Vector((lug.x, 0, lug.z)).normalized(), Y),
                      mat=0)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_Upright" % corner, bm, mats, smooth=40.0, pivot=WC)


def build_link(M, corner, name, p_in, p_out, r, end_axis_in, end_axis_out):
    """Track rod / toe link / pushrod: tube with rod ends at both ends and a hex adjuster. Origin at p_in."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Titanium"], M["Aluminum"]]
    p_in, p_out = Vector(p_in), Vector(p_out)
    d = (p_out - p_in).normalized()
    fs = []
    fs += rod_end(bm, p_in, end_axis_in, r * 1.55, r * 1.35, 2, 1, shank_dir=d, shank_len=0.03, shank_r=r * 0.75)
    fs += rod_end(bm, p_out, end_axis_out, r * 1.55, r * 1.35, 2, 1, shank_dir=-d, shank_len=0.03, shank_r=r * 0.75)
    a = p_in + d * (r * 1.55 * 0.6 + 0.028)
    b = p_out - d * (r * 1.55 * 0.6 + 0.028)
    fs += cylinder(bm, a, b, r, segs=14, mat=0, bevel=0.0015)
    fs += cylinder(bm, a - d * 0.004, a + d * 0.010, r * 1.25, segs=6, mat=2, bevel=0.001)
    fs += cylinder(bm, b - d * 0.010, b + d * 0.004, r * 1.25, segs=6, mat=2, bevel=0.001)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_%s" % (corner, name), bm, mats, smooth=40.0, pivot=p_in)


def build_rocker(M, corner, hp):
    """Cast bell-crank (Slide 5): twin plates joined by a central web around the pivot boss, splined
    anti-roll-bar bore, pushrod and damper clevises. Pivots about X."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Titanium"], M["BlackGloss"]]
    RP, PU, DP = hp["RP"], hp["PU"], hp["DP"]
    pts = []
    for c, r in ((RP, 0.036), (PU, 0.019), (DP, 0.019)):
        for i in range(24):
            a = 2 * math.pi * i / 24
            pts.append((c.y + r * math.cos(a), c.z + r * math.sin(a)))
    hull = hull2d(pts)
    fs = []
    x0 = RP.x
    frame = lambda u, v, z: Vector((x0 + z, u, v))
    for sg in (-1, 1):
        fs += extrude_polygon(bm, hull, frame, sg * 0.0115, sg * 0.0200, 0) if sg > 0 else \
            extrude_polygon(bm, hull, frame, -0.0200, -0.0115, 0)
    # central web (full thickness) around the pivot and towards both pickups (keeps the clevis gaps open)
    web = []
    for c, r in ((RP, 0.040), (RP.lerp(PU, 0.55), 0.016), (RP.lerp(DP, 0.55), 0.016)):
        for i in range(24):
            a = 2 * math.pi * i / 24
            web.append((c.y + r * math.cos(a), c.z + r * math.sin(a)))
    fs += extrude_polygon(bm, hull2d(web), frame, -0.0116, 0.0116, 0)
    # pivot boss + splined ARB bore (dark star) + bearing caps
    X = Vector((1, 0, 0))
    fs += cylinder(bm, RP - X * 0.030, RP + X * 0.030, 0.030, segs=24, mat=0, bevel=0.003)
    star = []
    for i in range(32):
        a = 2 * math.pi * i / 32
        r = 0.0125 if i % 2 == 0 else 0.0108
        star.append((r * math.cos(a), r * math.sin(a)))
    fs += extrude_polygon(bm, star, lambda u, v, z: Vector((x0 + z, RP.y + u, RP.z + v)), 0.029, 0.0315, 2)
    for P in (PU, DP):
        fs += cylinder(bm, P - X * 0.026, P + X * 0.026, 0.0065, segs=6, mat=1, bevel=0.001)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_Rocker" % corner, bm, mats, smooth=40.0, pivot=RP)


DAMPER = dict(body_end=0.176, thread=(0.052, 0.090), collar=(0.090, 0.102), seat_rod=0.034)


def build_damper(M, corner, hp):
    """Coil-over damper body (static part, origin at the chassis eye): rod-end eye, body tube, threaded spring
    platform with collar + lock screws, seal head, piggy-back reservoir with adjuster knob (Slide 5)."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Aluminum"], M["Titanium"], M["BlackGloss"]]
    DI, DP = hp["DI"], hp["DP"]
    ax = (DP - DI).normalized()
    X = Vector((1, 0, 0))
    fs = rod_end(bm, DI, X, 0.0175, 0.020, 0, 2)
    prof = [(0.0, 0.010), (0.011, 0.010), (0.012, 0.028), (0.019, 0.032), (0.0245, 0.040), (0.0255, 0.046),
            (0.0235, 0.050), (0.0235, DAMPER["thread"][0])]
    t0, t1 = DAMPER["thread"]
    n = 10
    for i in range(n):
        ta = t0 + (t1 - t0) * i / n
        prof += [(0.0262, ta + 0.0018), (0.0248, ta + (t1 - t0) / n)]
    prof += [(0.0235, DAMPER["collar"][1] + 0.002), (0.0235, DAMPER["body_end"] - 0.008), (0.0200, DAMPER["body_end"] - 0.003),
             (0.0125, DAMPER["body_end"]), (0.0, DAMPER["body_end"])]
    fs += revolve_axis(bm, prof, DI, ax, 20, mat_fn=lambda i: 0 if i < 7 or i > len(prof) - 6 else 1)
    # spring collar (lower spring platform) with two lock screws
    c0, c1 = DAMPER["collar"]
    fs += cylinder(bm, DI + ax * c0, DI + ax * c1, 0.043, segs=28, mat=3, bevel=0.002)
    xb, yb, _ = basis(ax)
    for a in (0.0, math.pi):
        dvec = xb * math.cos(a) + yb * math.sin(a)
        p = DI + ax * (0.5 * (c0 + c1)) + dvec * 0.043
        fs += cylinder(bm, p - dvec * 0.002, p + dvec * 0.004, 0.0035, segs=6, mat=2)
    # piggy-back reservoir above the body
    up = Vector((0, 0, 1))
    r0 = DI + up * 0.050
    fs += cylinder(bm, r0 + ax * 0.040, r0 + ax * 0.150, 0.0205, segs=24, mat=0, bevel=0.004)
    fs += cylinder(bm, r0 + ax * 0.150, r0 + ax * 0.162, 0.013, segs=20, mat=1, bevel=0.002)      # adjuster knob
    fs += rbox_at(bm, DI + ax * 0.058 + up * 0.026, (0.012, 0.012, 0.016), 0.004, axes=(X, ax, up), mat=0)
    fs += cylinder(bm, r0 + ax * 0.040 - up * 0.004, r0 + ax * 0.030 - up * 0.004, 0.006, segs=6, mat=2)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_Damper" % corner, bm, mats, smooth=40.0, pivot=DI)


def build_damper_rod(M, corner, hp):
    """Chrome piston rod (runs 0.2 m into the body), rod-end eye at the rocker, upper spring seat, bump stop.
    Origin at the rocker eye."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["Chrome"], M["SuspForged"], M["Rubber"], M["Titanium"], M["BlackGloss"]]
    DI, DP = hp["DI"], hp["DP"]
    ax = (DI - DP).normalized()
    X = Vector((1, 0, 0))
    L0 = (DP - DI).length
    fs = rod_end(bm, DP, X, 0.016, 0.018, 1, 3)
    fs += cylinder(bm, DP + ax * 0.010, DP + ax * 0.026, 0.0105, segs=16, mat=1, bevel=0.001)
    s = DAMPER["seat_rod"]
    fs += cylinder(bm, DP + ax * (s - 0.013), DP + ax * s, 0.041, segs=28, mat=4, bevel=0.002)
    fs += cylinder(bm, DP + ax * (s - 0.019), DP + ax * (s - 0.013), 0.020, segs=20, mat=1, bevel=0.001)
    fs += cylinder(bm, DP + ax * 0.024, DP + ax * (L0 - DAMPER["body_end"] + 0.20), 0.0085, segs=16, mat=0)
    ex = L0 - DAMPER["body_end"]
    fs += cylinder(bm, DP + ax * (ex - 0.026), DP + ax * (ex - 0.004), 0.0150, segs=16, mat=2, r1=0.0135,
                   bevel=0.002)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_DamperRod" % corner, bm, mats, smooth=40.0, pivot=DP)


def build_spring(M, corner, hp):
    """Gold coil spring between the body collar and the rod seat (closed, ground ends). Origin on the body-side
    seat; scale along its axis (local Y, the damper axis) by the table's spring_scale to compress it."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    DI, DP = hp["DI"], hp["DP"]
    ax = (DP - DI).normalized()
    L0 = (DP - DI).length
    s0 = DI + ax * DAMPER["collar"][1]
    Ls = L0 - DAMPER["seat_rod"] - DAMPER["collar"][1]
    xb, yb, _ = basis(ax)
    Rm, rw = 0.0345, 0.0062
    turns = 4.8
    per = 16
    npts = int(turns * per) + 1
    pts = []
    for i in range(npts):
        u = i / (npts - 1)
        th = 2 * math.pi * turns * u
        # closed ends: first/last 0.6 turn at wire pitch
        e = 0.6 / turns
        if u < e:
            t = rw + (u / e) * (2 * rw * 0.6)
        elif u > 1 - e:
            t = Ls - rw - ((1 - u) / e) * (2 * rw * 0.6)
        else:
            a = rw + 2 * rw * 0.6
            b = Ls - rw - 2 * rw * 0.6
            t = a + (b - a) * (u - e) / (1 - 2 * e)
        pts.append(s0 + ax * t + (xb * math.cos(th) + yb * math.sin(th)) * Rm)
    rings = []
    for i, p in enumerate(pts):
        tg = (pts[min(i + 1, npts - 1)] - pts[max(i - 1, 0)]).normalized()
        nrm = (p - (s0 + ax * ax.dot(p - s0))).normalized()
        bn = tg.cross(nrm).normalized()
        nrm = bn.cross(tg)
        rings.append([bm.verts.new(p + (nrm * math.cos(a) + bn * math.sin(a)) * rw)
                      for a in [2 * math.pi * j / 7 for j in range(7)]])
    fs = []
    for a, b in zip(rings, rings[1:]):
        for j in range(7):
            fs.append(bm.faces.new((a[j], a[(j + 1) % 7], b[(j + 1) % 7], b[j])))
    fs.append(bm.faces.new(list(reversed(rings[0]))))
    fs.append(bm.faces.new(rings[-1]))
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    ob = new_mesh_object("Susp_%s_Spring" % corner, bm, [M["Gold"]], smooth=50.0, pivot=s0)
    ob["free_length"] = Ls
    return ob


def build_halfshaft(M, corner, hp):
    """Rear drive shaft: inboard and outboard CV joints with convoluted boots, tubular shaft. Origin at the
    inboard CV centre, aims at the outboard CV centre and spins about its own axis."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["SuspForged"], M["Rubber"], M["Aluminum"]]
    IC, OC = hp["IC"], hp["OC"]
    ax = (OC - IC).normalized()
    L = (OC - IC).length
    fs = cylinder(bm, IC - ax * 0.030, IC + ax * 0.026, 0.044, segs=24, mat=2, bevel=0.004)
    fs += cylinder(bm, OC - ax * 0.030, OC + ax * 0.024, 0.043, segs=24, mat=2, bevel=0.004)

    def boot(p, d):
        prof = [(0.0, 0.0), (0.040, 0.0)]
        for i in range(6):
            prof += [(0.040 - i * 0.0042, 0.006 + i * 0.010), (0.034 - i * 0.0042, 0.011 + i * 0.010)]
        prof += [(0.017, 0.072), (0.0, 0.072)]
        return revolve_axis(bm, prof, p, d, 20, mat_fn=lambda i: 1)
    fs += boot(IC + ax * 0.024, ax)
    fs += boot(OC - ax * 0.028, -ax)
    fs += cylinder(bm, IC + ax * 0.09, OC - ax * 0.09, 0.0145, segs=16, mat=0)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_Halfshaft" % corner, bm, mats, smooth=40.0, pivot=IC)


def build_mounts(M, corner, hp):
    """Static chassis brackets: clevises for the four wishbone pivots, the rocker pivot bracket, the damper
    chassis eye and (rear) the toe-link bracket, each on a base plate. Origin at the world origin."""
    front = corner[0] == "F"
    side = 1.0 if corner[1] == "L" else -1.0
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["Aluminum"], M["Titanium"]]
    inward = Vector((0, -side, 0))
    fs = []
    for P, Q in ((hp["LF"], hp["LR"]), (hp["UF"], hp["UR"])):
        ax = (P - Q).normalized()
        for J in (P, Q):
            fs += clevis(bm, J, ax, 0.056, 0.019, 0.005, inward, 0.040, 0, bolt_mat=1)
            fs += rbox_at(bm, J + inward * 0.040, (0.036, 0.004, 0.026), 0.003,
                          axes=(ax, inward, ax.cross(inward)), mat=0, nmid=(0, 0, 0))
    X = Vector((1, 0, 0))
    RP = hp["RP"]
    down_in = (inward + Vector((0, 0, -0.6))).normalized()
    for sg in (-1, 1):
        cc = RP + X * sg * 0.0375
        fs += rbox_at(bm, cc + down_in * 0.026, (0.004, 0.038, 0.028), 0.003,
                      axes=(X, down_in, X.cross(down_in)), mat=0, nmid=(0, 0, 0))
    fs += rbox_at(bm, RP + down_in * 0.064, (0.046, 0.004, 0.032), 0.003, axes=(X, down_in, X.cross(down_in)), mat=0,
                  nmid=(0, 0, 0))
    fs += cylinder(bm, RP - X * 0.045, RP + X * 0.045, 0.009, segs=6, mat=1, bevel=0.001)
    DI = hp["DI"]
    fs += clevis(bm, DI, X, 0.022, 0.018, 0.005, inward, 0.030, 0, bolt_mat=1)
    fs += rbox_at(bm, DI + inward * 0.030, (0.028, 0.004, 0.024), 0.003, axes=(X, inward, Vector((0, 0, 1))), mat=0,
                  nmid=(0, 0, 0))
    if not front:
        TI = hp["TI"]
        fs += clevis(bm, TI, Vector((0, 0, 1)), 0.020, 0.018, 0.006, inward, 0.036, 0, bolt_mat=1)
        fs += rbox_at(bm, TI + inward * 0.036, (0.028, 0.004, 0.030), 0.003, axes=(X, inward, Vector((0, 0, 1))),
                      mat=0)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_%s_Mounts" % corner, bm, mats, smooth=40.0)


def build_steering_rack(M, hpL):
    """Steer-by-wire rack (static housing) with actuator, clamps and bellows; the track rods' inner ball joints
    sit at the bellows ends (y = +-0.30) and slide along Y by the rack offset."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.verify()
    mats = [M["Aluminum"], M["Rubber"], M["SuspForged"]]
    TI = hpL["TI"]
    x, z = TI.x, TI.z
    Y = Vector((0, 1, 0))
    yh = TI.y - 0.088
    fs = cylinder(bm, Vector((x, -yh, z)), Vector((x, yh, z)), 0.025, segs=24, mat=0, bevel=0.003)
    fs += cylinder(bm, Vector((x - 0.055, -0.07, z + 0.015)), Vector((x - 0.055, 0.07, z + 0.015)), 0.045, segs=28,
                   mat=2, bevel=0.006)
    fs += rbox_at(bm, Vector((x - 0.028, 0.0, z + 0.008)), (0.030, 0.050, 0.030), 0.006, mat=2)
    for sg in (-1, 1):
        prof = [(0.0, 0.0), (0.024, 0.0)]
        for i in range(7):
            prof += [(0.026, 0.004 + i * 0.0097), (0.019, 0.0090 + i * 0.0097)]
        prof += [(0.014, 0.072), (0.0, 0.072)]
        fs += revolve_axis(bm, prof, Vector((x, sg * (yh - 0.003), z)), Y * sg, 16, mat_fn=lambda i: 1)
        fs += rbox_at(bm, Vector((x, sg * (yh - 0.07), z - 0.032)), (0.030, 0.014, 0.008), 0.003, mat=2)
    recalc(bm, fs)
    bm.normal_update()
    box_uv(bm, list(bm.faces), uvl, 5.0)
    return new_mesh_object("Susp_F_SteeringRack", bm, mats, smooth=40.0)


def build_corner(M, corner):
    hp = corner_hardpoints(corner)
    front = corner[0] == "F"
    X = Vector((1, 0, 0))
    out = {}
    out["LowerArm"] = build_arm(M, corner, hp, "Lower")
    out["UpperArm"] = build_arm(M, corner, hp, "Upper")
    out["Upright"] = build_upright(M, corner, hp)
    kp = (hp["UBJ"] - hp["LBJ"]).normalized()
    if front:
        out["TieRod"] = build_link(M, corner, "TieRod", hp["TI"], hp["TO"], 0.0095, Vector((0, 0, 1)),
                                   Vector((0, 0, 1)))
    else:
        out["ToeLink"] = build_link(M, corner, "ToeLink", hp["TI"], hp["TO"], 0.0095, Vector((0, 0, 1)),
                                    Vector((0, 0, 1)))
        out["Halfshaft"] = build_halfshaft(M, corner, hp)
    out["Pushrod"] = build_link(M, corner, "Pushrod", hp["PL"], hp["PU"], 0.0105, X, X)
    out["Rocker"] = build_rocker(M, corner, hp)
    out["Damper"] = build_damper(M, corner, hp)
    out["DamperRod"] = build_damper_rod(M, corner, hp)
    out["Spring"] = build_spring(M, corner, hp)
    out["Mounts"] = build_mounts(M, corner, hp)
    return hp, out


# ------------------------------------------------------------------------------------------------ rig JSON
def vl(v, nd=5):
    return [round(float(c), nd) for c in v]


def rig_description(corners):
    doc = {
        "about": "Tesla Roadster 2026 suspension rig for Unreal (generated by roadster_wheels.py).",
        "frame": "Blender metres: origin on the ground under the wheelbase midpoint, +X forward, +Y car's left, +Z up. "
                 "Static ride height = wheel centre at the unloaded tyre radius.",
        "to_unreal": "position_cm = (x, -y, z) * 100; a rotation of angle a about Blender axis (ax, ay, az) is a "
                     "rotation of -a about Unreal axis (ax, -ay, az); quaternion (w, x, y, z) -> (w, -x, y, -z).",
        "mesh_convention": "Every Susp_* mesh is modelled in its static pose with zero rotation and its origin at "
                           "'pivot'. To reproduce the static pose place the component at the pivot with no rotation.",
        "joint_types": {
            "hinge": "rotate about 'axis' (right-hand rule, Blender frame) through the pivot by the table angle "
                     "(degrees, 0 = static).",
            "rigid": "upright: place the origin at table.wheel_centre and apply table.upright_quat (rotation from the "
                     "static pose, about the origin). Steering: additionally rotate about the current kingpin axis "
                     "(LBJ->UBJ) through LBJ by steer_table.kingpin_deg.",
            "aim": "keep the origin on 'pivot' (or on the moving point named in pivot_follows), and rotate so that "
                   "static_dir maps onto the current direction pivot->target (minimal rotation, e.g. "
                   "FQuat::FindBetweenNormals).",
            "aim_scale": "as 'aim', then scale along local axis 'scale_axis' by table.spring_scale.",
            "spin": "additionally spins about its own axis with the wheel (same angle as the wheel).",
            "static": "fixed to the body.",
        },
        "travel_definition": "travel_m = vertical motion of the wheel centre from static (+ = bump). Tables are solved "
                             "with the full linkage geometry (wishbones, upright, track rod/toe link, pushrod, "
                             "rocker, damper).",
        "corners": {},
    }
    for corner, (hp, objs, kin) in corners.items():
        front = corner[0] == "F"
        side = 1.0 if corner[1] == "L" else -1.0
        C = "Susp_" + corner + "_"
        parts = []
        parts.append(dict(name=C + "LowerArm", pivot=vl((hp["LF"] + hp["LR"]) / 2), joint="hinge",
                          axis=vl(kin.axL), driven_by="wheel travel", table="lower_arm_deg",
                          points={"front_pivot": vl(hp["LF"]), "rear_pivot": vl(hp["LR"]), "ball_joint": vl(hp["LBJ"]),
                                  "pushrod_pickup": vl(hp["PL"])}))
        parts.append(dict(name=C + "UpperArm", pivot=vl((hp["UF"] + hp["UR"]) / 2), joint="hinge",
                          axis=vl(kin.axU), driven_by="wheel travel", table="upper_arm_deg",
                          points={"front_pivot": vl(hp["UF"]), "rear_pivot": vl(hp["UR"]), "ball_joint": vl(hp["UBJ"])}))
        parts.append(dict(name=C + "Upright", pivot=vl(hp["WC"]), joint="rigid",
                          driven_by="wheel travel (+ steering on the front)", table="wheel_centre, upright_quat",
                          points={"lower_ball_joint": vl(hp["LBJ"]), "upper_ball_joint": vl(hp["UBJ"]),
                                  ("track_rod_outer" if front else "toe_link_outer"): vl(hp["TO"])}))
        link = "TieRod" if front else "ToeLink"
        parts.append(dict(name=C + link, pivot=vl(hp["TI"]), joint="aim", static_dir=vl((hp["TO"] - hp["TI"]).normalized()),
                          target="upright point " + ("track_rod_outer" if front else "toe_link_outer") + " (table.TO)",
                          pivot_follows=("steering rack: pivot.y += steer_table.rack_m * (+1 on both sides; rack "
                                         "moves as one piece)") if front else "fixed (chassis)",
                          driven_by="wheel travel" + (" + steering" if front else "")))
        if not front:
            parts.append(dict(name=C + "Halfshaft", pivot=vl(hp["IC"]), joint="aim+spin",
                              static_dir=vl((hp["OC"] - hp["IC"]).normalized()), target="table.OC (outboard CV centre)",
                              driven_by="wheel travel; spins with the wheel"))
        parts.append(dict(name=C + "Pushrod", pivot=vl(hp["PL"]), joint="aim",
                          static_dir=vl((hp["PU"] - hp["PL"]).normalized()), pivot_follows="table.PL (on the lower arm)",
                          target="table.PU (on the rocker)", driven_by="wheel travel"))
        parts.append(dict(name=C + "Rocker", pivot=vl(hp["RP"]), joint="hinge", axis=vl(kin.axR),
                          driven_by="pushrod", table="rocker_deg",
                          points={"pushrod_pickup": vl(hp["PU"]), "damper_pickup": vl(hp["DP"])}))
        parts.append(dict(name=C + "Damper", pivot=vl(hp["DI"]), joint="aim",
                          static_dir=vl((hp["DP"] - hp["DI"]).normalized()), target="table.DP (rocker eye)",
                          driven_by="rocker"))
        parts.append(dict(name=C + "DamperRod", pivot=vl(hp["DP"]), joint="aim",
                          static_dir=vl((hp["DI"] - hp["DP"]).normalized()), pivot_follows="table.DP",
                          target="damper chassis eye " + str(vl(hp["DI"])), driven_by="rocker"))
        sp = objs["Spring"]
        parts.append(dict(name=C + "Spring", pivot=vl(sp.location), joint="aim_scale",
                          static_dir=vl((hp["DP"] - hp["DI"]).normalized()), scale_axis="Y",
                          target="table.DP", pivot_follows="damper body (moves with Susp_%sDamper's rotation about "
                                                           "its pivot)" % C, table="spring_scale",
                          free_length_m=round(sp["free_length"], 5), driven_by="rocker"))
        parts.append(dict(name=C + "Mounts", pivot=[0, 0, 0], joint="static"))
        travel = [round(-0.06 + 0.01 * i, 3) for i in range(15)]
        tab = {k: [] for k in ("travel_m", "lower_arm_deg", "upper_arm_deg", "rocker_deg", "wheel_centre",
                               "upright_quat", "camber_deg", "toe_deg", "LBJ", "UBJ", "TO", "PL", "PU", "DP",
                               "damper_length_m", "damper_compression_m", "spring_scale")}
        if not front:
            tab["OC"] = []
        Ls = sp["free_length"]
        for dz in travel:
            p = kin.pose_travel(dz)
            if p is None:
                continue
            cam, toe = wheel_angles(p["R"], side)
            q = p["R"].to_quaternion()
            tab["travel_m"].append(dz)
            tab["lower_arm_deg"].append(round(math.degrees(p["thL"]), 4))
            tab["upper_arm_deg"].append(round(math.degrees(p["thU"]), 4))
            tab["rocker_deg"].append(round(math.degrees(p["thR"]), 4))
            tab["wheel_centre"].append(vl(p["WC"]))
            tab["upright_quat"].append([round(q.w, 6), round(q.x, 6), round(q.y, 6), round(q.z, 6)])
            tab["camber_deg"].append(round(cam, 3))
            tab["toe_deg"].append(round(toe, 3))
            for k2 in ("LBJ", "UBJ", "TO", "PL", "PU", "DP"):
                tab[k2].append(vl(p[k2]))
            if not front:
                tab["OC"].append(vl(p["OC"]))
            tab["damper_length_m"].append(round(p["Ld"], 5))
            tab["damper_compression_m"].append(round(kin.Ld0 - p["Ld"], 5))
            tab["spring_scale"].append(round((Ls - (kin.Ld0 - p["Ld"])) / Ls, 5))
        entry = dict(wheel_centre_static=vl(hp["WC"]), hardpoints={k: vl(v) for k, v in hp.items()},
                     kingpin_axis=vl((hp["UBJ"] - hp["LBJ"]).normalized()), parts=parts, travel_table=tab)
        n = len(tab["travel_m"])
        if n > 2:
            i0 = tab["travel_m"].index(0.0)
            i1 = min(i0 + 1, n - 1)
            entry["motion_ratio_damper_per_wheel"] = round(
                (tab["damper_compression_m"][i1] - tab["damper_compression_m"][i0]) /
                (tab["travel_m"][i1] - tab["travel_m"][i0]), 4)
        if front:
            st = {"steer_deg": [], "rack_m": [], "kingpin_deg": [], "upright_quat": [], "TO": [], "TI": []}
            racks = [i * 0.005 - 0.090 for i in range(37)]
            poses = []
            for rk in racks:
                p = kin.pose(0.0, rk)
                if p is None:
                    continue
                cam, toe = wheel_angles(p["R"], side)
                poses.append((toe, rk, p))
            poses.sort(key=lambda t: t[0])
            for toe, rk, p in poses:
                q = p["R"].to_quaternion()
                kpd = math.degrees(q.angle) * (1 if q.axis.dot(kin.hp["UBJ"] - kin.hp["LBJ"]) >= 0 else -1)
                st["steer_deg"].append(round(toe, 3))
                st["rack_m"].append(round(rk, 4))
                st["kingpin_deg"].append(round(kpd, 3))
                st["upright_quat"].append([round(q.w, 6), round(q.x, 6), round(q.y, 6), round(q.z, 6)])
                st["TO"].append(vl(p["TO"]))
                st["TI"].append(vl(p["TI"]))
            st["note"] = ("steer_deg = road-wheel yaw (+ = nose left, Blender +Z); rack_m = rack displacement along "
                          "+Y (both track rods move together). Solved at static height. Unreal's physics steer is "
                          "+ = right, i.e. steer_deg = -degrees(W.Steer).")
            entry["steer_table"] = st
        doc["corners"][corner] = entry
    return doc


# ================================================================================================ build
def build(M, write_rig=True, rig_path=RIG_JSON):
    """Build wheels, brakes and suspension. Returns {name: object}. Writes export/suspension_rig.json."""
    extra_materials(M)
    objs = {}
    for key in ("Front", "Rear"):
        objs["Wheel_" + key] = build_wheel(M, key)
        objs["Brake_%s_Disc" % key] = build_disc(M, key)
        objs["Brake_%s_Caliper" % key] = build_caliper(M, key)
    corners = {}
    for corner in ("FL", "FR", "RL", "RR"):
        hp, parts = build_corner(M, corner)
        for k2, ob in parts.items():
            objs[ob.name] = ob
        corners[corner] = (hp, parts, CornerKinematics(hp, corner[0] == "F"))
    rack = build_steering_rack(M, corners["FL"][0])
    objs[rack.name] = rack
    if write_rig:
        doc = rig_description(corners)
        doc["static_parts"] = [rack.name] + ["Susp_%s_Mounts" % c for c in corners]
        os.makedirs(os.path.dirname(rig_path), exist_ok=True)
        with open(rig_path, "w") as f:
            json.dump(doc, f, indent=1)
    return objs


def triangle_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)
