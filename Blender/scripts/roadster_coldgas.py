"""Cold-gas thruster system and rear suction-fan box of the 2026 Tesla Roadster (Thrust / Rocket trims).

    import roadster_materials, roadster_coldgas
    parts = roadster_coldgas.build(roadster_materials.materials())   # {name: bpy object}

Objects (Unreal static mesh = SM_<name>):
    Thrusters_Rear    static  4 rear converging-diverging nozzles, EM valves, actuators, harness, feed manifold
    Thrusters_Rocket  static  4 corner lift stacks, 2 lateral yaw thrusters, their feed lines (Rocket only)
    COPV_Rocket       static  400 L vessel in the passenger-seat bay, bosses, on-tank valve, saddles, straps,
                              transfer line to the HP manifold (Rocket only)
    COPV_Trunk        static  200 L transverse vessel under the rear deck, bosses, on-tank valve, saddles, straps
    ColdGas_Plumbing  static  fill receptacle, HP manifold, burst disc, P/T transducers, isolation valve, filter,
                              dome-loaded regulator + pilot, LP block, relief valve, HP lines and P-clips
    Fan_Box           static  carbon suction box (frame, nozzle shelf, 4 bays, 3 strakes, apron, flanks), ramp duct,
                              shrouds, stator vanes, fan motors, inlet grille, diffuser slats
    Fan_0 .. Fan_3    moving  rotor only; origin on the rotor axis at the blade mid-plane; spin about +X

Static objects keep their origin at the world origin (geometry is in body coordinates).
Frame: metres; origin on the ground under the wheelbase midpoint; +X forward, +Y = car's left, +Z up.

Measurements (Research/images, see report in preview_coldgas.py):
  * Lateral positions: Tesla "Gravity" image, lower (level-camera) car, scale from the RDSTR plate taken as a US
    12 x 6 in plate (194 x 98 px -> 1.57 mm/px horizontally, 1.55 mm/px vertically at the tail plane).
  * Heights: anchored to the body's rear face - the carbon box top edge meets the end of the white rear face
    at z 0.645 (the same anchor puts the tail light bar at z 0.915, matching roadster_body).
  * Corner lift nozzles: "Gravity" (rear pair, firing) and "World" (front pair behind the front wheel).
  * Fan / nozzle detail: patent_fan_suction_fig_sawyermerritt_2 (crop of the Gravity render).
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix

UVS = 2.0   # UV units per metre (a 0.5 m tile); carbon / filament materials tile further in Unreal
TAU = 2.0 * math.pi

# =============================================================================================== layout
# ---- suction box (rear view)
BOX_FACE_X = -2.135            # rear face of the carbon frame (body tail is at x -2.10)
BOX_TOP = 0.645                 # top edge = lower edge of the white rear face
BOX_BOT = 0.160
BOX_HW = 0.635                  # outer half width
RIM = 0.025                     # frame rim width (rear view)
BOX_IN_HW = BOX_HW - RIM        # 0.610 inner opening half width
TOP_CHAMF = (0.490, 0.569)      # top chamfer: from (|y| 0.490, BOX_TOP) to (BOX_HW, 0.569)
BOT_CHAMF = (0.575, 0.220)      # bottom chamfer: from (|y| 0.575, BOX_BOT) to (BOX_HW, 0.220)
SHELF_X = -2.100                # nozzle shelf back face
SHELF_LIP_Z = (0.516, 0.531)    # bay ceiling rear lip (bottom, top)
LIP_Z = 0.312                   # lower apron lip top (centre bays); rises to LIP_Z_OUT at the side walls
LIP_Z_OUT = 0.336
DUCT_CEIL_Z = 0.545             # duct ceiling centre-plane above the fans
STRAKE_Y = (-0.295, 0.0, 0.295)

FAN_X, FAN_Z = -1.955, 0.380                  # rotor centre (axis x blade mid-plane)
FAN_TILT = math.radians(35.0)                    # fan axes pitched up toward the rear (exhaust rearward + upward)
A_DN = Vector((-math.cos(FAN_TILT), 0.0, math.sin(FAN_TILT)))    # exhaust direction
A_UP = -A_DN                                                       # upstream, into the duct
V_UP = Vector((math.sin(FAN_TILT), 0.0, math.cos(FAN_TILT)))      # 'up' in the fan disc plane
BH_T = -0.045                                    # fan bulkhead rear face along A_UP from the rotor centre
BH_V = (-0.150, 0.165)                           # bulkhead extent along V_UP (bottom, top)
FAN_Y = (-0.452, -0.145, 0.145, 0.452)          # Fan_0 .. Fan_3 (right -> left)
FAN_TIP_R = 0.1245
SHROUD_R = (0.1265, 0.1315)

# ---- rear thrusters
NOZ_Z = 0.584
NOZ_Y = (-0.352, -0.118, 0.118, 0.352)
NOZ_EXIT_X = -2.150
NOZ_L = 0.120                    # inlet flange face -> exit plane
NOZ_RT, NOZ_RE = 0.011, 0.036    # throat / exit radius (22 / 72 mm)
MANIFOLD_X = -1.870

# ---- corner lift thrusters (exit points, firing straight down)
LIFT_REAR = dict(x=-2.000, y=0.740, z_exit=0.200)
LIFT_FRONT = dict(x=0.975, y=0.855, z_exit=0.090)
LIFT_RT, LIFT_RE = 0.0055, 0.028          # 11 / 56 mm

# ---- lateral yaw thrusters (front corners, exhausting outward)
YAW = dict(x=1.970, y_exit=0.912, z=0.235)
YAW_RT, YAW_RE = 0.0045, 0.015            # 9 / 30 mm

# ---- vessels
COPV_R = dict(c=Vector((0.040, -0.475, 0.545)), r_in=0.330, wall=0.035, L_in=1.390, boss_r=0.045)   # axis X
COPV_T = dict(c=Vector((-1.120, 0.0, 0.680)), r_in=0.274, wall=0.033, L_in=1.034, boss_r=0.042)     # axis Y

# ---- plumbing nodes (trunk bay, behind the trunk vessel, above the duct ramp)
PL_X, PL_Z = -1.560, 0.620
M1_Y, V1_Y, F1_Y, R1_Y, D1_Y = 0.420, 0.285, 0.160, 0.0, -0.225


# =============================================================================================== materials
def extra_materials():
    """New materials owned by this module (Unreal must define M_Chrome, M_Stainless, M_COPV_Filament)."""
    from roadster_materials import mat
    out = {
        "Chrome": mat("M_Chrome", (0.92, 0.92, 0.93), rough=0.06, metal=1.0),
        "Stainless": mat("M_Stainless", (0.62, 0.62, 0.64), rough=0.32, metal=1.0),
    }
    fil = mat("M_COPV_Filament", (0.030, 0.030, 0.033), rough=0.30, coat=0.6)
    try:   # preview look: helical filament bands from the UVs (u around, v along) -> diagonal bands = helix
        nt = fil.node_tree
        if "FilamentWave" not in nt.nodes:
            bsdf = nt.nodes.get("Principled BSDF")
            tc = nt.nodes.new("ShaderNodeTexCoord")
            wave = nt.nodes.new("ShaderNodeTexWave"); wave.name = "FilamentWave"
            wave.wave_type = 'BANDS'; wave.bands_direction = 'DIAGONAL'
            wave.inputs["Scale"].default_value = 60.0
            wave.inputs["Distortion"].default_value = 0.0
            ramp = nt.nodes.new("ShaderNodeValToRGB")
            ramp.color_ramp.elements[0].color = (0.018, 0.018, 0.020, 1)
            ramp.color_ramp.elements[1].color = (0.060, 0.060, 0.066, 1)
            nt.links.new(tc.outputs["UV"], wave.inputs["Vector"])
            nt.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
            nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    except Exception:
        pass
    out["Filament"] = fil
    return out


# =============================================================================================== mesh builder
class Part:
    """One output object. Geometry is accumulated in a bmesh; materials are referenced by key."""

    def __init__(self, name, M, keys):
        self.name, self.M, self.keys = name, M, list(keys)
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")

    def m(self, key):
        return self.keys.index(key)

    def finish(self, smooth_angle=40.0, origin=None):
        bm = self.bm
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        thr = math.radians(smooth_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) != 2:
                e.smooth = False
            else:
                e.smooth = e.calc_face_angle(math.pi) <= thr
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for k in self.keys:
            me.materials.append(self.M[k])
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.collection.objects.link(ob)
        if origin is not None:
            me.transform(Matrix.Translation(-Vector(origin)))
            ob.location = Vector(origin)
        me.update()
        return ob


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


# =============================================================================================== primitives
def perp(w, ref=None):
    w = Vector(w).normalized()
    if ref is None:
        ref = Vector((0, 0, 1)) if abs(w.z) < 0.95 else Vector((1, 0, 0))
    ref = Vector(ref)
    u = (ref - w * ref.dot(w))
    if u.length < 1e-9:
        u = Vector((1, 0, 0)) - w * w.x
    u.normalize()
    return u, w.cross(u)


def _face(P, verts, mat, uvs=None):
    f = P.bm.faces.new(verts)
    f.material_index = mat
    if uvs:
        for l, c in zip(f.loops, uvs):
            l[P.uv].uv = c
    return f


def lathe(P, prof, origin, axis, segs, mats, ref=None, closed=False, phase=0.0):
    """Revolve a profile [(r, t), ...] about `axis` through `origin`. r == 0 points are poles.
    mats: int or list per profile segment. Normals are fixed globally in Part.finish()."""
    bm = P.bm
    o, w = Vector(origin), Vector(axis).normalized()
    u, v = perp(w, ref)
    nseg0 = len(prof) if closed else len(prof) - 1
    if isinstance(mats, int):
        mats = [mats] * nseg0
    # drop zero-length profile segments (keeps the material of the segment that follows)
    p2, m2 = [prof[0]], []
    for i in range(1, len(prof)):
        if abs(prof[i][0] - p2[-1][0]) < 1e-7 and abs(prof[i][1] - p2[-1][1]) < 1e-7:
            continue
        m2.append(mats[i - 1])
        p2.append(prof[i])
    if closed:
        m2.append(mats[nseg0 - 1])
        if abs(p2[-1][0] - p2[0][0]) < 1e-7 and abs(p2[-1][1] - p2[0][1]) < 1e-7:
            p2.pop(); m2.pop(-2)
    prof, mats = p2, m2
    n = len(prof)
    nseg = n if closed else n - 1
    cs = [(math.cos(phase + TAU * k / segs), math.sin(phase + TAU * k / segs)) for k in range(segs)]
    rings = []
    for (r, t) in prof:
        c = o + w * t
        if r <= 1e-7:
            rings.append(bm.verts.new(c))
        else:
            rings.append([bm.verts.new(c + (u * ca + v * sa) * r) for (ca, sa) in cs])
    s = [0.0]
    for i in range(1, n):
        s.append(s[-1] + math.hypot(prof[i][0] - prof[i - 1][0], prof[i][1] - prof[i - 1][1]))
    s_close = s[-1] + math.hypot(prof[0][0] - prof[-1][0], prof[0][1] - prof[-1][1])
    U = TAU * max(max(p[0] for p in prof), 0.002) * UVS
    for idx in range(nseg):
        i, j = idx, (idx + 1) % n
        A, B = rings[i], rings[j]
        vi, vj = s[i] * UVS, (s[j] if j > i else s_close) * UVS
        if not isinstance(A, list) and not isinstance(B, list):
            continue
        for k in range(segs):
            k2 = (k + 1) % segs
            ua, ub = U * k / segs, U * (k + 1) / segs
            if isinstance(A, list) and isinstance(B, list):
                _face(P, (A[k], A[k2], B[k2], B[k]), mats[idx], ((ua, vi), (ub, vi), (ub, vj), (ua, vj)))
            elif isinstance(A, list):
                _face(P, (A[k], A[k2], B), mats[idx], ((ua, vi), (ub, vi), (0.5 * (ua + ub), vj)))
            else:
                _face(P, (A, B[k2], B[k]), mats[idx], ((0.5 * (ua + ub), vi), (ub, vj), (ua, vj)))
    return rings


def ring(P, origin, axis, r_in, r_out, t0, t1, mat, segs=24, ch=0.0, ref=None, phase=0.0):
    """Closed annular solid (washer / band / sleeve) with optional edge chamfer."""
    ch = min(ch, 0.45 * (r_out - r_in), 0.45 * (t1 - t0))
    prof = [(r_in, t0), (r_out - ch, t0), (r_out, t0 + ch), (r_out, t1 - ch), (r_out - ch, t1), (r_in, t1)]
    return lathe(P, prof, origin, axis, segs, mat, ref=ref, closed=True, phase=phase)


def cyl(P, origin, axis, r, t0, t1, mat, segs=24, ch=0.0, ref=None, phase=0.0):
    """Closed cylinder with chamfered end edges, from t0 to t1 along the axis."""
    ch = min(ch, 0.45 * r, 0.45 * (t1 - t0))
    prof = [(0.0, t0), (r - ch, t0), (r, t0 + ch), (r, t1 - ch), (r - ch, t1), (0.0, t1)]
    return lathe(P, prof, origin, axis, segs, mat, ref=ref, phase=phase)


def hexnut(P, origin, axis, af, t0, t1, mat, bore=0.0, ref=None):
    """Hex nut / hex body across-flats `af` between t0 and t1 (chamfered corners)."""
    R = af / math.sqrt(3.0)
    c = 0.12 * R
    if bore > 0:
        prof = [(bore, t0), (R * 0.86, t0), (R, t0 + c), (R, t1 - c), (R * 0.86, t1), (bore, t1)]
        return lathe(P, prof, origin, axis, 6, mat, ref=ref, closed=True, phase=math.pi / 6)
    prof = [(0, t0), (R * 0.86, t0), (R, t0 + c), (R, t1 - c), (R * 0.86, t1), (0, t1)]
    return lathe(P, prof, origin, axis, 6, mat, ref=ref, phase=math.pi / 6)


def rbox(P, center, size, r, mat, rot=None, n=1):
    """Box with rounded edges (radius r, n segments per 45 deg). size = full extents. Box-projected UVs."""
    h = [size[0] / 2.0, size[1] / 2.0, size[2] / 2.0]
    r = max(0.0, min(r, 0.95 * min(h)))
    R = rot.to_3x3() if rot is not None else Matrix.Identity(3)
    c = Vector(center)

    def samples(hh):
        if r <= 0:
            return [-hh, hh]
        inner = hh - r
        band = [-(inner + r * math.tan(math.radians(45.0 * k / n))) for k in range(n, 0, -1)]
        mid = [-inner, inner] if inner > 1e-6 else [0.0]
        return band + mid + [-b for b in reversed(band)]

    S = [samples(hh) for hh in h]
    cache = {}
    bm = P.bm

    def vert(p):
        key = (round(p[0], 7), round(p[1], 7), round(p[2], 7))
        vv = cache.get(key)
        if vv is None:
            inner = Vector([max(-(h[i] - r), min(h[i] - r, p[i])) for i in range(3)])
            d = Vector(p) - inner
            q = inner + d.normalized() * r if (r > 0 and d.length > 1e-9) else Vector(p)
            vv = bm.verts.new(c + R @ q)
            cache[key] = vv
        return vv

    for i in range(3):
        j, k = (i + 1) % 3, (i + 2) % 3
        for s in (-1, 1):
            for a in range(len(S[j]) - 1):
                for b in range(len(S[k]) - 1):
                    corners = []
                    for (aa, bb) in ((a, b), (a + 1, b), (a + 1, b + 1), (a, b + 1)):
                        p = [0.0, 0.0, 0.0]
                        p[i], p[j], p[k] = s * h[i], S[j][aa], S[k][bb]
                        corners.append(p)
                    if s < 0:
                        corners.reverse()
                    _face(P, [vert(p) for p in corners], mat, [(p[j] * UVS, p[k] * UVS) for p in corners])


def fillet(pts, radius, seg90=6):
    """Polyline with every interior corner replaced by a circular arc (pipe bend)."""
    pts = [Vector(p) for p in pts]
    if len(pts) < 3 or radius <= 0:
        return pts
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        p0, p1, p2 = out[-1], pts[i], pts[i + 1]
        d0, d1 = p1 - p0, p2 - p1
        L0, L1 = d0.length, d1.length
        if L0 < 1e-6 or L1 < 1e-6:
            continue
        d0, d1 = d0 / L0, d1 / L1
        phi = d0.angle(d1, 0.0)
        if phi < 1e-3:
            out.append(p1)
            continue
        tl = min(radius * math.tan(phi / 2), 0.48 * L0, 0.48 * L1)
        re = tl / math.tan(phi / 2)
        a, b = p1 - d0 * tl, p1 + d1 * tl
        bis = (d1 - d0).normalized()
        c = p1 + bis * (re / math.cos(phi / 2))
        va, vb = a - c, b - c
        ax = va.cross(vb)
        if ax.length < 1e-12:
            out.append(p1)
            continue
        ax.normalize()
        ang = va.angle(vb)
        ns = max(2, int(math.ceil(seg90 * phi / (math.pi / 2))))
        for k in range(ns + 1):
            out.append(c + Matrix.Rotation(ang * k / ns, 3, ax) @ va)
    out.append(pts[-1])
    return out


def tube(P, pts, r, mat, segs=14, bend=None, caps=True):
    """Swept circular tube along a polyline (corners filleted with `bend`, default 3 r). Returns (pts, tangents)."""
    segs = min(segs, 12)
    pts = fillet(pts, bend if bend is not None else 3.0 * r, seg90=4)
    clean = [pts[0]]
    for p in pts[1:]:
        if (p - clean[-1]).length > 1e-5:
            clean.append(p)
    pts = clean
    n = len(pts)
    T = []
    for i in range(n):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == n - 1:
            t = pts[-1] - pts[-2]
        else:
            t = (pts[i + 1] - pts[i]).normalized() + (pts[i] - pts[i - 1]).normalized()
        T.append(t.normalized())
    N = [perp(T[0])[0]]
    for i in range(1, n):
        q = T[i - 1].rotation_difference(T[i])
        nn = q @ N[-1]
        nn = (nn - T[i] * nn.dot(T[i])).normalized()
        N.append(nn)
    bm = P.bm
    rings = []
    for i in range(n):
        B = T[i].cross(N[i])
        rings.append([bm.verts.new(pts[i] + (N[i] * math.cos(TAU * k / segs) + B * math.sin(TAU * k / segs)) * r)
                      for k in range(segs)])
    s = 0.0
    U = TAU * r * UVS
    for i in range(n - 1):
        s1 = s + (pts[i + 1] - pts[i]).length
        for k in range(segs):
            k2 = (k + 1) % segs
            ua, ub = U * k / segs, U * (k + 1) / segs
            _face(P, (rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]), mat,
                  ((ua, s * UVS), (ub, s * UVS), (ub, s1 * UVS), (ua, s1 * UVS)))
        s = s1
    if caps:
        for rr, rev in ((rings[0], True), (rings[-1], False)):
            vs = list(reversed(rr)) if rev else rr
            _face(P, vs, mat, [(0.5 + 0.5 * math.cos(TAU * k / segs), 0.5 + 0.5 * math.sin(TAU * k / segs)) for k in range(segs)])
    return pts, T


def loft(P, sections, mat, caps=(True, True), closed=True, mats=None):
    """Connect equal-length sections of 3D points. closed: sections are loops. Caps are triangulated n-gons."""
    bm = P.bm
    rows = [[bm.verts.new(Vector(p)) for p in sec] for sec in sections]
    m = len(rows[0])
    su = [0.0]
    for k in range(1, m + (1 if closed else 0)):
        su.append(su[-1] + (Vector(sections[0][k % m]) - Vector(sections[0][k - 1])).length)
    cen = [sum((Vector(p) for p in sec), Vector()) / len(sec) for sec in sections]
    sv = [0.0]
    for i in range(1, len(sections)):
        sv.append(sv[-1] + (cen[i] - cen[i - 1]).length)
    for i in range(len(rows) - 1):
        for k in range(m if closed else m - 1):
            k2 = (k + 1) % m
            mt = mats[k] if mats else mat
            _face(P, (rows[i][k], rows[i][k2], rows[i + 1][k2], rows[i + 1][k]), mt,
                  ((su[k] * UVS, sv[i] * UVS), (su[k + 1] * UVS, sv[i] * UVS),
                   (su[k + 1] * UVS, sv[i + 1] * UVS), (su[k] * UVS, sv[i + 1] * UVS)))
    capf = []
    if closed:
        if caps[0]:
            capf.append(_face(P, list(reversed(rows[0])), mat))
        if caps[1]:
            capf.append(_face(P, rows[-1], mat))
        for f in capf:
            for l in f.loops:
                co = l.vert.co
                l[P.uv].uv = (co.x * UVS + co.z * UVS, co.y * UVS)
        if capf:
            bmesh.ops.triangulate(bm, faces=capf, quad_method='BEAUTY', ngon_method='BEAUTY')
    return rows


def sheet(P, curve, normals, ext, ranges, thick, mat, side='in', nsub=1):
    """Thick sheet swept from a profile curve along direction `ext`.
    curve: 3D points (the profile at ext-offset 0); normals: per-point unit offset directions for the thickness;
    ranges: per-point (a, b) extents along `ext`; side: which way the thickness goes from the curve.
    All quads, closed."""
    ext = Vector(ext).normalized()
    sgn = {'in': (-1.0, 0.0), 'out': (0.0, 1.0), 'mid': (-0.5, 0.5)}[side]
    secs = []
    for p, nrm, (a, b) in zip(curve, normals, ranges):
        p, nrm = Vector(p), Vector(nrm).normalized()
        A = p + nrm * (sgn[0] * thick)
        B = p + nrm * (sgn[1] * thick)
        col = []
        # loop around the cross-section rectangle: (B,a)->(B,b)->(A,b)->(A,a) with nsub along ext
        for q in range(nsub + 1):
            col.append(B + ext * (a + (b - a) * q / nsub))
        for q in range(nsub + 1):
            col.append(A + ext * (b - (b - a) * q / nsub))
        secs.append(col)
    return loft(P, secs, mat, caps=(True, True), closed=True)


def curve_normals_2d(pts, up):
    """In-plane normals for a polyline lying in the plane perpendicular to `up` (normal = tangent x up)."""
    up = Vector(up)
    out = []
    n = len(pts)
    for i in range(n):
        a = Vector(pts[max(i - 1, 0)]); b = Vector(pts[min(i + 1, n - 1)])
        t = (b - a).normalized()
        out.append(t.cross(up).normalized())
    return out


def fillet2d(poly, r, seg=4):
    """Round every corner of a closed 2D polygon [(a, b), ...] with radius r."""
    out = []
    n = len(poly)
    for i in range(n):
        p0 = Vector((poly[i - 1][0], poly[i - 1][1], 0.0))
        p1 = Vector((poly[i][0], poly[i][1], 0.0))
        p2 = Vector((poly[(i + 1) % n][0], poly[(i + 1) % n][1], 0.0))
        for p in fillet([p0, p1, p2], r, seg90=seg)[1:-1]:
            out.append((p.x, p.y))
    return out


def offset_poly(poly, d):
    """Offset a CCW 2D polygon inward by d (miter)."""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = Vector(poly[i - 1]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        e0, e1 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n0, n1 = Vector((-e0.y, e0.x)), Vector((-e1.y, e1.x))     # left normals = inward for CCW
        m = n0 + n1
        if m.length < 1e-9:
            m = n0
        m = m / (1.0 + n0.dot(n1)) if (1.0 + n0.dot(n1)) > 1e-6 else n0
        q = p1 + m * d
        out.append((q.x, q.y))
    return out


def holed_cell(P, origin, eu, ev, en, u0, u1, v0, v1, hc, hr, thick, mat, segs=32, wall_mat=None):
    """Closed plate cell [u0,u1]x[v0,v1] in the plane (origin, eu, ev) with a circular hole of radius hr at hc,
    thickness along +en. Quad ring from the hole to the rectangle."""
    eu, ev, en = Vector(eu), Vector(ev), Vector(en)
    o = Vector(origin)
    cu, cv = hc
    corners = [(u1, v1), (u0, v1), (u0, v0), (u1, v0)]
    angs = sorted({TAU * k / segs for k in range(segs)} |
                  {math.atan2(b - cv, a - cu) % TAU for (a, b) in corners})
    inner, outer = [], []
    for a in angs:
        ca, sa = math.cos(a), math.sin(a)
        inner.append((cu + hr * ca, cv + hr * sa))
        tx = ((u1 - cu) / ca if ca > 1e-12 else ((u0 - cu) / ca if ca < -1e-12 else 1e9))
        ty = ((v1 - cv) / sa if sa > 1e-12 else ((v0 - cv) / sa if sa < -1e-12 else 1e9))
        t = min(tx, ty)
        outer.append((cu + t * ca, cv + t * sa))
    P3 = lambda uv, w: o + eu * uv[0] + ev * uv[1] + en * w
    secs = []
    for k in range(len(angs)):
        secs.append([P3(outer[k], 0.0), P3(outer[k], thick), P3(inner[k], thick), P3(inner[k], 0.0)])
    secs.append(secs[0])
    bm = P.bm
    rows = [[bm.verts.new(p) for p in s] for s in secs[:-1]]
    rows.append(rows[0])
    for i in range(len(rows) - 1):
        for k in range(4):
            k2 = (k + 1) % 4
            mt = wall_mat if (wall_mat is not None and k == 2) else mat
            vs = (rows[i][k], rows[i][k2], rows[i + 1][k2], rows[i + 1][k])
            uvq = [((v.co - o).dot(eu) * UVS, (v.co - o).dot(ev) * UVS) for v in vs]
            _face(P, vs, mt, uvq)


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# =============================================================================================== engineering parts
def bell_points(rt, re, t_throat, t_exit, th_n=math.radians(28), th_e=math.radians(9), n=10):
    """Rao-style bell (quadratic Bezier) from the throat (rt, t_throat) to the exit (re, t_exit)."""
    p0 = Vector((t_throat, rt))
    p2 = Vector((t_exit, re))
    # control point: intersection of the throat tangent and the exit tangent
    d0 = Vector((math.cos(th_n), math.sin(th_n)))
    d2 = Vector((math.cos(th_e), math.sin(th_e)))
    den = d0.x * d2.y - d0.y * d2.x
    s = ((p2.x - p0.x) * d2.y - (p2.y - p0.y) * d2.x) / den
    p1 = p0 + d0 * s
    out = []
    for k in range(1, n + 1):
        t = k / n
        q = p0 * (1 - t) ** 2 + p1 * 2 * t * (1 - t) + p2 * t * t
        out.append((q.y, q.x))
    return out


def cd_nozzle_profile(rt, re, L, r_bore, r_body, r_flange, t_throat, lip=0.0045, wall=0.0045, flange_t=0.008,
                      throat_jacket=None, nb=10):
    """Closed (r, t) profile of a converging-diverging nozzle, t = 0 at the inlet flange face, t = L at the exit.
    Returns (profile, materials-per-segment-key list) with keys 'in' (lined bore) / 'out' (shell)."""
    prof, keys = [], []

    def add(p, k):
        prof.append(p); keys.append(k)

    # inner: bore -> convergent -> rounded throat -> bell
    add((r_bore, 0.0), 'out')
    add((r_bore, 0.35 * t_throat), 'in')
    tc = t_throat - 0.55 * t_throat
    for k in range(1, 4):                                  # 40 deg convergent with a rounded throat
        f = k / 3.0
        r = r_bore + (rt - r_bore) * (1 - (1 - f) ** 2.2)
        add((r, 0.35 * t_throat + (t_throat - 0.35 * t_throat) * f), 'in')
    for p in bell_points(rt, re, t_throat, L, n=nb):
        add(p, 'in')
    # lip (rounded) and outer shell back to the flange
    add((re + 0.35 * lip, L + 0.0012), 'lip')
    add((re + 0.75 * lip, L + 0.0012), 'lip')
    add((re + lip, L - 0.0005), 'out')
    bell = bell_points(rt, re, t_throat, L, n=nb)
    jacket = throat_jacket if throat_jacket else rt + 2.4 * wall
    for (r, t) in reversed(bell[2:-1]):
        add((max(r + wall, jacket if t < t_throat + 0.25 * (L - t_throat) else r + wall), t), 'out')
    add((jacket, t_throat), 'out')
    add((r_body, t_throat - 0.25 * t_throat), 'out')
    add((r_body, flange_t + 0.003), 'out')
    add((r_flange - 0.002, flange_t), 'out')
    add((r_flange, flange_t - 0.002), 'out')
    add((r_flange, 0.0015), 'out')
    add((r_flange - 0.0015, 0.0), 'out')
    return prof, keys


def connector(P, base, axis, mats, r=0.008, L=0.018):
    """Circular aerospace-style electrical connector (shell + coupling nut) pointing along axis."""
    ax = Vector(axis).normalized()
    cyl(P, base, ax, r * 0.75, 0.0, L * 0.35, mats['metal'], segs=12, ch=0.0005)
    lathe(P, [(0, L * 0.30), (r * 0.95, L * 0.30), (r, L * 0.36), (r, L * 0.80), (r * 0.9, L * 0.86),
              (r * 0.70, L * 0.86), (r * 0.70, L), (0, L)], base, ax, 12, mats['plug'])


def p_clip(P, at, tangent, r_tube, out_dir, mats, reach=0.022):
    """Cushioned P-clip: rubber sleeve + steel band around the tube, tab to the mount, bolt head."""
    t = Vector(tangent).normalized()
    od = Vector(out_dir)
    od = (od - t * od.dot(t)).normalized()
    lathe(P, [(r_tube + 0.0004, -0.0075), (r_tube + 0.0030, -0.0075), (r_tube + 0.0030, -0.0065), (r_tube + 0.0048, -0.0065),
              (r_tube + 0.0048, 0.0065), (r_tube + 0.0030, 0.0065), (r_tube + 0.0030, 0.0075), (r_tube + 0.0004, 0.0075)],
          at, t, 12, [mats['rubber'], mats['rubber'], mats['band'], mats['band'], mats['band'], mats['rubber'], mats['rubber'], mats['rubber']], closed=True)
    side = t.cross(od).normalized()
    rot = Matrix((od, side, t)).transposed().to_4x4()        # local x = out, y = side, z = tube axis
    tab_c = Vector(at) + od * (r_tube + 0.003 + reach / 2)
    rbox(P, tab_c, (reach, 0.004, 0.013), 0.0012, mats['band'], rot=rot, n=1)
    hexnut(P, Vector(at) + od * (r_tube + 0.003 + reach * 0.78) + side * 0.002, side, 0.008, 0.0, 0.004, mats['band'])


def nut_pair(P, at, axis, r_tube, mats, af=None, L=0.016):
    """Tube fitting at a port: hex body + compression nut + sleeve."""
    ax = Vector(axis).normalized()
    af = af or max(0.012, 2.6 * r_tube)
    hexnut(P, at, ax, af, 0.0, L * 0.45, mats['fit'], bore=r_tube * 0.9)
    hexnut(P, at, ax, af * 0.92, L * 0.5, L, mats['fit'], bore=r_tube * 0.9)
    ring(P, at, ax, r_tube * 0.9, r_tube * 1.18, L, L + 0.006, mats['fit'], segs=14)


# =============================================================================================== Fan rotor
def fan_rotor(M, name, center):
    """Rotor: hub drum + dome + conical spinner with an open ring tip, 7 wide-chord swept blades.
    Built around the origin (rotor axis = +X); air is pushed toward -X when spun positively about +X."""
    P = Part(name, M, ["BlackGloss", "Carbon", "Aluminum"])
    HUB, BLD, TIP = P.m("BlackGloss"), P.m("Carbon"), P.m("Aluminum")
    X = Vector((1, 0, 0))
    o = Vector((0, 0, 0))
    ax = Vector((-1, 0, 0))          # profile t runs rearward (downstream)
    Rh = 0.062
    domec = 0.022                     # dome sphere centre t
    prof = [(0.0, -0.040), (0.054, -0.040), (0.058, -0.037), (Rh, -0.033), (Rh, domec)]
    for k in range(1, 7):             # dome: sphere of radius Rh up to the cone seat
        a = math.radians(90.0 * k / 7.0) * 0.62
        prof.append((Rh * math.cos(a), domec + Rh * math.sin(a)))
    rseat = prof[-1][0]
    tseat = prof[-1][1]
    prof += [(rseat - 0.0025, tseat + 0.0008), (0.034, tseat + 0.003)]
    for k in range(1, 7):             # gently convex cone to the tip ring
        f = k / 6.0
        prof.append((0.034 + (0.0185 - 0.034) * f - 0.002 * math.sin(math.pi * f), tseat + 0.003 + 0.036 * f))
    tt = prof[-1][1]
    prof += [(0.0190, tt + 0.002), (0.0178, tt + 0.0045), (0.0135, tt + 0.0045), (0.0125, tt + 0.003),
             (0.0122, tt - 0.010), (0.0, tt - 0.010)]
    mats = []
    for i in range(len(prof) - 1):
        mats.append(TIP if prof[i][1] >= tt - 0.0005 and prof[i + 1][1] >= tt - 0.011 and i >= len(prof) - 6 else HUB)
    lathe(P, prof, o, ax, 36, mats)

    # blades: wrapped on cylinders; chord pitch from 38 deg (root) to 21 deg (tip), scimitar sweep
    nb, ns, nc = 7, 8, 9
    r0, r1 = Rh - 0.004, FAN_TIP_R
    for b in range(nb):
        th0 = TAU * b / nb
        secs = []
        for i in range(ns):
            s = i / (ns - 1)
            r = r0 + (r1 - r0) * s
            phi = math.radians(38.0 + (21.0 - 38.0) * s)
            chord = 0.050 + 0.020 * s ** 0.8
            tmax = 0.0045 + (0.0024 - 0.0045) * s
            sweep = -0.022 * s * s                     # tip trails in rotation
            camber = 0.05 * chord
            d = Vector((-math.sin(phi), -math.cos(phi)))      # chord LE->TE in (x, arc)
            nrm = Vector((math.cos(phi), -math.sin(phi)))     # suction side (upstream)
            top, bot = [], []
            for k in range(nc):
                c = 0.5 * (1 - math.cos(math.pi * k / (nc - 1)))       # cosine spacing
                th = 5.0 * tmax * (0.2969 * math.sqrt(c) - 0.126 * c - 0.3516 * c * c + 0.2843 * c ** 3 - 0.1036 * c ** 4)
                th = max(th, 0.0004)
                cm = camber * 4 * c * (1 - c)
                base = d * ((c - 0.5) * chord) + Vector((0.0, sweep))
                top.append(base + nrm * (cm + th))
                bot.append(base + nrm * (cm - th))
            loop2 = top + list(reversed(bot[1:-1]))
            sec = []
            for (xx, arc) in loop2:
                th_ = th0 + arc / r
                sec.append(Vector((xx, r * math.cos(th_), r * math.sin(th_))))
            secs.append(sec)
        loft(P, secs, BLD, caps=(True, True))
    ob = P.finish(smooth_angle=55.0)
    ob.location = Vector(center)
    ob.rotation_euler = (0.0, FAN_TILT, 0.0)    # mesh spins about its local X; pitch is carried by the object
    return ob


# =============================================================================================== Fan box
def _box_outline():
    """Outer frame outline (y, z), CCW seen from behind with +y to the right of the plotted plane."""
    yt, zc = TOP_CHAMF
    yb, zb = BOT_CHAMF
    return [(yb, BOX_BOT), (BOX_HW, zb), (BOX_HW, zc), (yt, BOX_TOP),
            (-yt, BOX_TOP), (-BOX_HW, zc), (-BOX_HW, zb), (-yb, BOX_BOT)]


def lip_z(y):
    return LIP_Z + (LIP_Z_OUT - LIP_Z) * smoothstep(0.40, 0.55, abs(y))


def ramp_z(x):
    """Duct ceiling centre-plane: flat over the fans, sweeping down to the underfloor at x -1.45."""
    if x <= -1.84:
        return DUCT_CEIL_Z
    return 0.128 + (DUCT_CEIL_Z - 0.128) * smoothstep(-1.45, -1.83, x)


def fan_box(M):
    P = Part("Fan_Box", M, ["Carbon", "BlackGloss", "Titanium", "Aluminum", "Rubber", "Stainless"])
    CF, BK, TI, AL, RU, SS = (P.m(k) for k in ("Carbon", "BlackGloss", "Titanium", "Aluminum", "Rubber", "Stainless"))
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Z = Vector((0, 0, 1))
    x0, x1 = BOX_FACE_X, BOX_FACE_X + 0.040     # frame depth

    # ---- 1. outer frame ring (rounded outline, chamfered front edges)
    outer = fillet2d(_box_outline(), 0.018, seg=4)
    inner = offset_poly(outer, RIM)
    ch = 0.003
    out_c = offset_poly(outer, ch)
    in_c = offset_poly(inner, -ch)
    secs = []
    for k in range(len(outer)):
        (yo, zo), (yi, zi), (yoc, zoc), (yic, zic) = outer[k], inner[k], out_c[k], in_c[k]
        secs.append([Vector((x0, yoc, zoc)), Vector((x0 + ch, yo, zo)), Vector((x1, yo, zo)),
                     Vector((x1, yi, zi)), Vector((x0 + ch, yi, zi)), Vector((x0, yic, zic))])
    secs.append(secs[0])
    rows = [[P.bm.verts.new(p) for p in s] for s in secs[:-1]]
    rows.append(rows[0])
    for i in range(len(rows) - 1):
        for k in range(6):
            k2 = (k + 1) % 6
            vs = (rows[i][k], rows[i][k2], rows[i + 1][k2], rows[i + 1][k])
            _face(P, vs, CF, [((v.co.y + v.co.x) * UVS, v.co.z * UVS) for v in vs])

    zi_top = BOX_TOP - RIM            # 0.620 inner top
    # ---- 2. top cover (bent plate) from behind the frame to the valve bay, chamfers to the side walls
    yt, zc = TOP_CHAMF
    cv = fillet([Vector((0, -BOX_HW, zc)), Vector((0, -yt, BOX_TOP)), Vector((0, yt, BOX_TOP)), Vector((0, BOX_HW, zc))], 0.02, 3)
    nrm = [Vector((0, -n.y, -n.z)) if False else n for n in curve_normals_2d(cv, X)]
    # make normals point down/in (thickness goes inward)
    nrm = [n if n.z <= 0 else -n for n in nrm]
    sheet(P, [Vector((x1, p.y, p.z)) for p in cv], [-n for n in nrm], X, [(0.0, 0.050)] * len(cv), 0.004, CF, side='in')

    # ---- 3. nozzle shelf: back plate with 4 nozzle holes, rib between nozzles, bay-ceiling lip
    zb0, zb1 = SHELF_LIP_Z[1], zi_top + 0.004
    cuts = [-BOX_IN_HW - 0.004, -0.235, 0.0, 0.235, BOX_IN_HW + 0.004]
    for i, yc in enumerate(NOZ_Y):
        holed_cell(P, Vector((SHELF_X, 0, 0)), Y, Z, X, cuts[i], cuts[i + 1], zb0, zb1, (yc, NOZ_Z), 0.0300, 0.004, CF, segs=40)
    ribs = [(-BOX_IN_HW, NOZ_Y[0] - 0.050), (NOZ_Y[0] + 0.050, NOZ_Y[1] - 0.050), (NOZ_Y[1] + 0.050, NOZ_Y[2] - 0.050),
            (NOZ_Y[2] + 0.050, NOZ_Y[3] - 0.050), (NOZ_Y[3] + 0.050, BOX_IN_HW)]
    for (a, b) in ribs:
        rbox(P, Vector((SHELF_X - 0.005, (a + b) / 2, NOZ_Z)), (0.010, b - a, 0.009), 0.0035, CF)
    # fan bulkhead plane (perpendicular to the tilted fan axes): rear face at t = BH_T along A_UP
    Pb = Vector((FAN_X, 0.0, FAN_Z)) + A_UP * BH_T
    Bk = Pb + V_UP * BH_V[0]            # bottom edge (rear face)
    Tp = Pb + V_UP * BH_V[1]            # top edge (rear face)

    def x_bulk(z):
        return Pb.x + math.tan(FAN_TILT) * (z - Pb.z)

    # bay ceiling: plate from the bullnose rear lip rising to the bulkhead top edge (outlet converges)
    sec = [(-2.1255, SHELF_LIP_Z[0] + 0.004), (-2.1272, SHELF_LIP_Z[0] + 0.0075), (-2.1255, SHELF_LIP_Z[1] - 0.002),
           (-2.121, SHELF_LIP_Z[1]), (SHELF_X + 0.002, SHELF_LIP_Z[1]), (Tp.x + 0.006, Tp.z + 0.007),
           (Tp.x + 0.004, Tp.z - 0.002), (-2.121, SHELF_LIP_Z[0])]
    ys = [-BOX_IN_HW - 0.004, BOX_IN_HW + 0.004]
    loft(P, [[Vector((x, y, z)) for (x, z) in sec] for y in ys], CF)

    # ---- 4. lower apron (solid) with the bullnose lip, rising at the outer bays; front face closes the duct
    yss = [-BOX_IN_HW - 0.004, -0.55, -0.50, -0.45, -0.40, 0.40, 0.45, 0.50, 0.55, BOX_IN_HW + 0.004]
    apr = []
    for y in yss:
        zl = lip_z(y)
        sec = [(-2.125, BOX_BOT + 0.012), (-2.125, zl - 0.010), (-2.1235, zl - 0.0035), (-2.119, zl),
               (-2.110, zl + 0.0005), (Bk.x + 0.003, Bk.z - 0.002), (Bk.x + 0.003, BOX_BOT + 0.012)]
        apr.append([Vector((x, y, z)) for (x, z) in sec])
    loft(P, apr, CF)
    # recessed fastener heads on the apron (3, under the strakes)
    for y in STRAKE_Y:
        lathe(P, [(0, 0.0), (0.0085, 0.0), (0.0095, -0.0012), (0.0095, -0.0025), (0, -0.0025)],
              Vector((-2.125, y, BOX_BOT + 0.036)), X, 16, BK)

    # ---- 5. strakes: thin vanes from the rear opening to the tilted bulkhead, flared into ceiling and floor
    def ceil_z(x):
        f = (x - (-2.121)) / (Tp.x - (-2.121))
        return SHELF_LIP_Z[0] + (Tp.z - SHELF_LIP_Z[0]) * max(0.0, min(1.0, f))

    def floor_z(x, y):
        f = (x - (-2.110)) / (Bk.x - (-2.110))
        return lip_z(y) + (Bk.z - lip_z(y)) * max(0.0, min(1.0, f))

    x_rear = -2.1155
    for ys_ in STRAKE_Y:
        secs = []
        for sv in (0.0, 0.03, 0.07, 0.13, 0.50, 0.80, 0.88, 0.94, 0.98, 1.0):
            hw = 0.0040 + 0.0060 * max(0.0, 1 - sv / 0.13) ** 2 + 0.0160 * max(0.0, (sv - 0.80) / 0.20) ** 2
            zf = lambda x: floor_z(x, ys_) - 0.004 + sv * (ceil_z(x) + 0.008 - floor_z(x, ys_))
            xf = -2.0
            for _ in range(8):
                xf = x_bulk(zf(xf)) + 0.006
            xs_ = [xf, 0.5 * (xf + x_rear), x_rear]
            pts = [Vector((x, ys_ - hw, zf(x))) for x in xs_]
            for k in range(1, 6):
                a = math.pi * k / 6
                x = x_rear - hw * math.sin(a)
                pts.append(Vector((x, ys_ - hw * math.cos(a), zf(x))))
            pts += [Vector((x, ys_ + hw, zf(x))) for x in reversed(xs_)]
            secs.append(pts)
        loft(P, secs, CF)

    # ---- 6. fan bulkhead (tilted) with 4 shroud openings
    cuts = [-BOX_IN_HW - 0.004, -0.295, 0.0, 0.295, BOX_IN_HW + 0.004]
    for i, yc in enumerate(FAN_Y):
        holed_cell(P, Pb, Y, V_UP, A_UP, cuts[i], cuts[i + 1], BH_V[0], BH_V[1], (yc, 0.0), SHROUD_R[1] + 0.001, 0.010, CF, segs=32)

    # ---- 7. shrouds, stator vanes, motors (all on the tilted fan axes)
    for yc in FAN_Y:
        c = Vector((FAN_X, yc, FAN_Z))
        sh = [(SHROUD_R[0], 0.090), (SHROUD_R[0], -0.038), (SHROUD_R[0] + 0.003, -0.0455), (SHROUD_R[1] + 0.0065, -0.0475),
              (SHROUD_R[1] + 0.0065, -0.0445), (SHROUD_R[1], -0.042), (SHROUD_R[1], 0.083), (SHROUD_R[1] - 0.002, 0.090)]
        lathe(P, sh, c, A_UP, 40, CF, closed=True, ref=Y)
        # stator vanes (9) between the motor nose and the shroud, de-swirl camber
        for k in range(9):
            th0 = TAU * (k + 0.5) / 9
            secs = []
            for rr in (0.050, 0.090, SHROUD_R[0] + 0.002):
                pts = []
                for (dt, da) in ((-0.014, 0.0), (-0.004, 0.0040), (0.012, 0.0028), (0.016, 0.0), (0.012, -0.0010), (-0.004, 0.0012)):
                    th = th0 + (da + 0.010 * (0.014 + dt) / 0.03) / rr * 0.6
                    pts.append(c + A_UP * (0.066 + dt) + (Y * math.cos(th) + V_UP * math.sin(th)) * rr)
                secs.append(pts)
            loft(P, secs, TI)
        # motor (heat-pump-compressor-derived PMSM): finned aluminium can, black end caps, connector, cable
        mp = [(0.0, 0.044), (0.046, 0.044), (0.054, 0.047), (0.056, 0.053)]
        tt = 0.060
        while tt < 0.200:
            mp += [(0.056, tt), (0.0605, tt + 0.003), (0.0605, tt + 0.016), (0.056, tt + 0.019)]
            tt += 0.028
        mp += [(0.056, 0.205), (0.054, 0.217), (0.040, 0.225), (0.022, 0.227), (0.0, 0.227)]
        mats = [BK if (mp[i][1] < 0.051 or mp[i][1] > 0.204) else AL for i in range(len(mp) - 1)]
        lathe(P, mp, c, A_UP, 22, mats, ref=Y)
        cb = c + A_UP * 0.200 + V_UP * 0.068
        rot = Matrix((A_UP, Y, V_UP)).transposed().to_4x4()
        rbox(P, cb, (0.034, 0.034, 0.020), 0.003, BK, rot=rot)
        connector(P, cb + V_UP * 0.010, V_UP, {'metal': AL, 'plug': BK}, r=0.007, L=0.016)
        tube(P, [cb + V_UP * 0.026, cb + V_UP * 0.060 + A_UP * 0.02, Vector((-1.75, yc, ramp_z(-1.75) - 0.008))],
             0.0045, RU, segs=10, bend=0.02)

    # ---- 8. duct: ceiling ramp, side walls, compartment front wall, inlet grille
    xs = [Tp.x + 0.004] + [(-1.84 + (-1.45 + 1.84) * k / 14) for k in range(15)]
    xs = sorted(set(round(v, 5) for v in xs))
    cvr = [Vector((x, 0.0, ramp_z(x))) for x in xs]
    nr = curve_normals_2d(cvr, Vector((0, -1, 0)))
    nr = [n if n.z > 0 else -n for n in nr]
    sheet(P, cvr, nr, Y, [(-BOX_IN_HW - 0.009, BOX_IN_HW + 0.009)] * len(cvr), 0.005, CF, side='mid')
    for s in (1, -1):
        xs_w = [-2.094, -2.06, -2.0, -1.95, -1.90, -1.85] + [(-1.80 + (-1.45 + 1.80) * k / 9) for k in range(10)]
        bot = []
        top = []
        for x in xs_w:
            zb_ = 0.13 + (BOT_CHAMF[1] - 0.13) * smoothstep(-1.98, -2.094, x)
            zt_ = max(ramp_z(x) + 0.03, zb_ + 0.006) if x > -2.03 else TOP_CHAMF[1]
            if x <= -2.03:
                zt_ = TOP_CHAMF[1]
            bot.append(zb_)
            top.append(zt_)
        cvw = [Vector((x, s * (BOX_HW - 0.0075), 0)) for x in xs_w]
        sheet(P, cvw, [Vector((0, s, 0))] * len(cvw), Z, list(zip(bot, top)), 0.015, CF, side='mid')
        # rubber seal strip along the wall bottom edge (meets the side skirt)
        tube(P, [Vector((x, s * (BOX_HW - 0.0075), 0.13 + (BOT_CHAMF[1] - 0.13) * smoothstep(-1.98, -2.094, x) - 0.004)) for x in xs_w[2:]],
             0.006, RU, segs=10, bend=0.0)
    # valve-bay front wall (behind the manifold)
    rbox(P, Vector((-1.828, 0, (DUCT_CEIL_Z + BOX_TOP) / 2 + 0.002)), (0.004, 2 * BOX_IN_HW, BOX_TOP - DUCT_CEIL_Z - 0.004), 0.0015, CF)
    # inlet grille (under the duct, behind the underfloor seal)
    for k in range(-5, 6):
        y = k * 0.105
        tube(P, [Vector((-1.56, y, 0.132)), Vector((-2.035, y, 0.132))], 0.0035, BK, segs=8, bend=0.0)
    for x in (-1.62, -1.80, -1.98):
        tube(P, [Vector((x, -0.60, 0.128)), Vector((x, 0.60, 0.128))], 0.0045, BK, segs=8, bend=0.0)

    # ---- 9. side flanks curving out toward the rear wheels
    for s in (1, -1):
        plan = [(-2.098, 0.628), (-2.070, 0.650), (-2.030, 0.683), (-1.985, 0.722), (-1.945, 0.765),
                (-1.912, 0.805), (-1.890, 0.835), (-1.880, 0.852)]
        cvf = [Vector((x, s * y, 0)) for (x, y) in plan]
        nf = curve_normals_2d(cvf, Z)
        nf = [n if (n.x < 0 or n.y * s > 0) and n.x <= 0.5 else -n for n in nf]
        nf = [n if n.x <= 0 else -n for n in nf]
        rng = []
        for i, (x, y) in enumerate(plan):
            f = i / (len(plan) - 1)
            zt_ = 0.488 - 0.018 * smoothstep(0.75, 1.0, f) - 0.020 * smoothstep(0.90, 1.0, f)
            zb_ = 0.205 + 0.060 * f
            rng.append((zb_, zt_))
        sheet(P, cvf, nf, Z, rng, 0.004, CF, side='in', nsub=3)
        # rolled top edge
        tube(P, [Vector((x, s * y, rng[i][1] - 0.002)) + nf[i] * -0.002 for i, (x, y) in enumerate(plan)], 0.0042, CF, segs=10, bend=0.0)
        # tie bracket flank -> box wall
        rbox(P, Vector((-2.03, s * 0.655, 0.40)), (0.05, 0.04, 0.006), 0.002, CF)

    # ---- 10. diffuser slats under the apron
    for z in (0.120, 0.137):
        rbox(P, Vector((-2.050, 0, z)), (0.150, 2 * BOX_IN_HW, 0.005), 0.0022, CF)
    for y in (-0.42, -0.21, 0.0, 0.21, 0.42):
        rbox(P, Vector((-2.050, y, 0.140)), (0.150, 0.004, 0.044), 0.0015, CF)
    # mounting brackets box -> rear subframe (top of the duct ramp)
    for y in (-0.40, 0.40):
        rbox(P, Vector((-1.70, y, ramp_z(-1.70) + 0.035)), (0.06, 0.05, 0.07), 0.004, AL)
        hexnut(P, Vector((-1.70, y, ramp_z(-1.70) + 0.070)), Z, 0.013, 0.0, 0.007, SS)
    return P.finish(smooth_angle=42.0)


# =============================================================================================== rear thrusters
def rear_thrusters(M):
    P = Part("Thrusters_Rear", M, ["Titanium", "Gold", "BlackGloss", "Aluminum", "Stainless", "Rubber"])
    TI, AU, BK, AL, SS, RU = (P.m(k) for k in ("Titanium", "Gold", "BlackGloss", "Aluminum", "Stainless", "Rubber"))
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Z = Vector((0, 0, 1))
    ax = Vector((-1, 0, 0))
    x_in = NOZ_EXIT_X + NOZ_L                       # -2.030 inlet flange face
    fm = {'fit': SS, 'rubber': RU, 'band': SS}
    for y in NOZ_Y:
        o = Vector((x_in, y, NOZ_Z))
        prof, keys = cd_nozzle_profile(NOZ_RT, NOZ_RE, NOZ_L, r_bore=0.0185, r_body=0.030, r_flange=0.046,
                                       t_throat=0.045, lip=0.0050, wall=0.0050, throat_jacket=0.026, nb=8)
        mats = [{'in': AU, 'out': TI, 'lip': TI}[keys[(i + 1) % len(keys)]] for i in range(len(prof))]
        lathe(P, prof, o, ax, 40, mats, closed=True)
        # flange bolts (6) on the exit side of the inlet flange
        for k in range(6):
            a = TAU * (k + 0.5) / 6
            bp = o + ax * 0.008 + (Y * math.cos(a) + Z * math.sin(a)) * 0.0385
            hexnut(P, bp, ax, 0.0075, 0.0, 0.0045, AL)
        # black clamp ring on the shelf face with two bolted ears
        t_clamp = x_in - (SHELF_X - 0.0005)
        ring(P, o, ax, 0.0262, 0.0470, t_clamp, t_clamp + 0.0075, BK, segs=40, ch=0.0012)
        for sgn in (1, -1):
            ec = Vector((SHELF_X - 0.0045, y + sgn * 0.052, NOZ_Z))
            rbox(P, ec, (0.0075, 0.026, 0.020), 0.004, BK)
            hexnut(P, ec + ax * 0.0037 + Y * sgn * 0.004, ax, 0.010, 0.0, 0.006, AL)
        # fast-acting electrically actuated valve: body block + flange + actuator + connector
        vb = Vector((x_in + 0.042, y, NOZ_Z))
        rbox(P, vb, (0.084, 0.060, 0.058), 0.006, BK, n=2)
        ring(P, o + X * 0.0005, X, 0.0, 0.040, 0.0, 0.004, BK, segs=32, ch=0.001) if False else \
            cyl(P, o, X, 0.040, 0.0005, 0.0055, BK, segs=32, ch=0.001)
        act = vb + Vector((0.006, 0, 0.029))
        lathe(P, [(0, 0.0), (0.0235, 0.0), (0.0235, 0.010), (0.0215, 0.012), (0.0215, 0.052), (0.0230, 0.054),
                  (0.0230, 0.066), (0.0200, 0.070), (0.0080, 0.071), (0.0, 0.071)], act, Z, 32,
              [BK, BK, BK, BK, AL, AL, AL, AL, AL])
        connector(P, act + Vector((0.021, 0, 0.040)), X, {'metal': AL, 'plug': BK}, r=0.0075, L=0.017)
        # harness cable: connector -> loom above the manifold
        tube(P, [act + Vector((0.040, 0, 0.040)), act + Vector((0.075, 0, 0.044)), Vector((MANIFOLD_X - 0.012, y, BOX_TOP + 0.030)),
                 Vector((MANIFOLD_X + 0.020, y, BOX_TOP + 0.032))], 0.0032, RU, segs=8, bend=0.015)
        # inlet port: boss + fitting + short stainless pipe to the manifold outlet
        pb = Vector((x_in + 0.084, y, NOZ_Z))
        cyl(P, pb, X, 0.016, 0.0, 0.006, BK, segs=24, ch=0.001)
        nut_pair(P, pb + X * 0.006, X, 0.0115, fm, af=0.030, L=0.014)
        tube(P, [pb + X * 0.010, Vector((MANIFOLD_X - 0.020, y, NOZ_Z))], 0.0115, SS, segs=18, bend=0.0)
    # harness loom across the valve bay
    tube(P, [Vector((MANIFOLD_X + 0.020, -0.40, BOX_TOP + 0.032)), Vector((MANIFOLD_X + 0.020, 0.40, BOX_TOP + 0.032)),
             Vector((MANIFOLD_X + 0.060, 0.45, BOX_TOP + 0.050))], 0.0065, RU, segs=12, bend=0.03)
    # feed manifold (log): stainless, hex end plugs, 4 outlet bosses, inlet boss, saddle brackets
    mo = Vector((MANIFOLD_X, 0, NOZ_Z))
    lathe(P, [(0, -0.405), (0.024, -0.405), (0.028, -0.401), (0.028, 0.401), (0.024, 0.405), (0, 0.405)], mo, Y, 32, SS)
    for s in (1, -1):
        hexnut(P, mo, Y * s, 0.036, 0.405, 0.418, SS)
    for y in NOZ_Y:
        cyl(P, Vector((MANIFOLD_X, y, NOZ_Z)), ax, 0.016, 0.020, 0.034, SS, segs=24, ch=0.0015)
        hexnut(P, Vector((MANIFOLD_X, y, NOZ_Z)), ax, 0.030, 0.034, 0.046, SS, bore=0.0115)
    cyl(P, mo, X, 0.022, 0.018, 0.034, SS, segs=24, ch=0.0015)
    hexnut(P, mo, X, 0.040, 0.034, 0.048, SS, bore=0.016)
    cyl(P, mo, Z, 0.010, 0.026, 0.040, SS, segs=16, ch=0.001)               # manifold pressure tap
    hexnut(P, mo + Z * 0.040, Z, 0.019, 0.0, 0.010, SS)
    cyl(P, mo + Z * 0.050, Z, 0.009, 0.0, 0.030, AL, segs=16, ch=0.001)
    for y in (-0.235, 0.235):
        rbox(P, Vector((MANIFOLD_X, y, NOZ_Z - 0.030)), (0.050, 0.022, 0.012), 0.002, AL)
        ring(P, Vector((MANIFOLD_X, y, NOZ_Z)), Y, 0.0285, 0.032, -0.010, 0.010, SS, segs=32, ch=0.0008)
        rbox(P, Vector((MANIFOLD_X, y, (NOZ_Z - 0.036 + DUCT_CEIL_Z + 0.0025) / 2)),
             (0.030, 0.010, NOZ_Z - 0.036 - DUCT_CEIL_Z - 0.0025), 0.002, AL)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== corner lift / yaw
def lift_stack(P, x, y, z_exit, z_ring0, z_ring1, z_collar, z_valve0, z_valve1, mats, collar=True):
    """Vertical lift thruster (exhaust -Z): nozzle, stacked chrome/brass valve body, stem, collar, EM valve."""
    TI, AU, CR, BK, SS, AL, RU = (mats[k] for k in ('ti', 'au', 'cr', 'bk', 'ss', 'al', 'ru'))
    o = Vector((x, y, z_exit))
    up = Vector((0, 0, 1))
    Ln = z_ring0 - z_exit
    t_th = Ln - 0.022
    prof = []
    km = []

    def add(r, t, m):
        prof.append((r, t)); km.append(m)

    # inside of the bell (from the throat chamber pole down to the exit), t measured upward from the exit
    add(0.0, t_th + 0.012, AU)
    add(0.0085, t_th + 0.012, AU)
    add(LIFT_RT + 0.0015, t_th + 0.003, AU)
    add(LIFT_RT, t_th, AU)
    for (r, t) in bell_points(LIFT_RT, LIFT_RE, 0.0, t_th, th_n=math.radians(26), th_e=math.radians(10), n=7):
        add(r, t_th - t, AU)       # mirrored: t = 0 at the exit
    prof[-1] = (LIFT_RE, 0.0)
    add(LIFT_RE + 0.0016, -0.0010, TI)
    add(LIFT_RE + 0.0036, -0.0010, TI)
    add(LIFT_RE + 0.0045, 0.0008, TI)
    add(LIFT_RE + 0.0042, 0.012, TI)
    add(0.0215, Ln * 0.62, TI)
    add(0.0215, Ln * 0.80, TI)
    add(0.0300, Ln - 0.004, TI)
    add(0.0330, Ln, CR)
    # stacked valve body: alternating chrome / brass rings with grooves
    nring = 4
    h = (z_ring1 - z_ring0) / nring
    for k in range(nring):
        t0 = Ln + k * h
        m = CR if k % 2 == 0 else AU
        rr = 0.0385 if k % 2 == 0 else 0.0365
        add(rr, t0 + 0.0025, m)
        add(rr, t0 + h - 0.0025, m)
        add(0.0325, t0 + h, CR)
    tz = z_ring1 - z_exit
    add(0.016, tz + 0.002, SS)
    add(0.0140, tz + 0.006, SS)
    if collar:
        tc = z_collar - z_exit
        add(0.0140, tc - 0.004, SS)
        add(0.0300, tc - 0.002, CR)
        add(0.0310, tc, CR)
        add(0.0310, tc + 0.014, CR)
        add(0.0290, tc + 0.016, CR)
        add(0.0118, tc + 0.017, SS)
    tv0, tv1 = z_valve0 - z_exit, z_valve1 - z_exit
    add(0.0118, tv0 - 0.010, SS)
    add(0.0220, tv0 - 0.006, BK)
    add(0.0340, tv0 - 0.002, BK)
    add(0.0365, tv0 + 0.004, BK)
    add(0.0365, tv1 - 0.012, BK)
    add(0.0330, tv1 - 0.008, AL)
    add(0.0330, tv1 - 0.002, AL)
    add(0.0280, tv1, AL)
    add(0.0, tv1, AL)
    # the profile runs from the inner pole down through the bell then up the outside: reverse to go outer-first
    mlist = km[1:] + [km[-1]]
    lathe(P, prof, o, up, 24, [km[i + 1] for i in range(len(prof) - 1)])
    # EM valve connector + cable, feed inlet on top
    zc = (z_valve0 + z_valve1) / 2
    connector(P, Vector((x + 0.034, y, zc)), Vector((1, 0, 0)), {'metal': AL, 'plug': BK}, r=0.0075, L=0.017)
    return Vector((x, y, z_valve1))


def yaw_thruster(P, s, mats):
    """Lateral attitude thruster at the front corner: exhausts along s*Y through a flush aperture."""
    TI, AU, BK, SS, AL = (mats[k] for k in ('ti', 'au', 'bk', 'ss', 'al'))
    ax = Vector((0, s, 0))
    L = 0.100
    o = Vector((YAW['x'], s * (YAW['y_exit'] - L), YAW['z']))
    prof, keys = cd_nozzle_profile(YAW_RT, YAW_RE, L, r_bore=0.0095, r_body=0.0165, r_flange=0.026,
                                   t_throat=0.040, lip=0.0035, wall=0.0030, throat_jacket=0.0125, flange_t=0.006, nb=5)
    lathe(P, prof, o, ax, 20, [{'in': AU, 'out': TI, 'lip': TI}[keys[(i + 1) % len(keys)]] for i in range(len(prof))], closed=True)
    # flush bezel in the body side (aligned with the local body facet)
    nrm = Vector((0, s, 0))
    try:
        import roadster_body as RB
        y2, z2 = RB.row_at(2, YAW['x']); y3, z3 = RB.row_at(3, YAW['x'])
        d = Vector((0, y3 - y2, z3 - z2)).normalized()
        nrm = Vector((0, d.z, -d.y)) * s
        if nrm.y * s < 0:
            nrm = -nrm
    except Exception:
        pass
    ex = o + ax * L
    ring(P, ex - ax * 0.002, nrm, YAW_RE + 0.0045, 0.030, -0.001, 0.0025, TI, segs=40, ch=0.0008)
    # valve block, actuator, connector, inlet fitting
    vb = o - ax * 0.034
    rbox(P, vb, (0.050, 0.066, 0.050), 0.005, BK)
    cyl(P, o - ax * 0.001, ax, 0.026, 0.0, 0.004, BK, segs=24, ch=0.0008)
    act = vb + Vector((0, 0, 0.025))
    lathe(P, [(0, 0.0), (0.019, 0.0), (0.019, 0.008), (0.0175, 0.010), (0.0175, 0.044), (0.0185, 0.046), (0.0185, 0.054),
              (0.016, 0.058), (0.0, 0.058)], act, Vector((0, 0, 1)), 28, [BK, BK, BK, AL, AL, AL, AL, AL])
    connector(P, act + Vector((-0.017, 0, 0.034)), Vector((-1, 0, 0)), {'metal': AL, 'plug': BK}, r=0.0065, L=0.015)
    port = vb - ax * 0.033
    nut_pair(P, port, -ax, 0.008, {'fit': SS}, af=0.022, L=0.012)
    return port - ax * 0.016


def rocket_thrusters(M):
    P = Part("Thrusters_Rocket", M, ["Titanium", "Gold", "Chrome", "BlackGloss", "Stainless", "Aluminum", "Rubber"])
    mats = dict(ti=P.m("Titanium"), au=P.m("Gold"), cr=P.m("Chrome"), bk=P.m("BlackGloss"), ss=P.m("Stainless"),
                al=P.m("Aluminum"), ru=P.m("Rubber"))
    SS, RU, AL, BK = mats['ss'], mats['ru'], mats['al'], mats['bk']
    fm = {'fit': SS, 'rubber': RU, 'band': SS}
    R_LINE, R_SMALL = 0.011, 0.008
    D1 = Vector((PL_X, D1_Y, PL_Z))
    for s in (1, -1):
        # ---- rear corner lift stack, valve fed from the top
        rx, ry = LIFT_REAR['x'], s * LIFT_REAR['y']
        top_r = lift_stack(P, rx, ry, LIFT_REAR['z_exit'], 0.295, 0.380, 0.490, 0.655, 0.730, mats, collar=True)
        # mounting bracket stack -> box side wall
        rbox(P, Vector((rx + 0.004, s * 0.690, 0.470)), (0.030, 0.090, 0.008), 0.002, AL)
        ring(P, Vector((rx, ry, 0.0)), Vector((0, 0, 1)), 0.0119, 0.024, 0.462, 0.474, AL, segs=24, ch=0.001)
        # ---- front corner lift stack (behind the front wheel)
        fx, fy = LIFT_FRONT['x'], s * LIFT_FRONT['y']
        top_f = lift_stack(P, fx, fy, LIFT_FRONT['z_exit'], 0.205, 0.285, 0.300, 0.370, 0.445, mats, collar=True)
        rbox(P, Vector((fx, s * 0.890, 0.335)), (0.050, 0.060, 0.008), 0.002, AL)
        # ---- yaw thruster
        yin = yaw_thruster(P, s, mats)

        # ---- feed lines (regulated gas from the LP block D1)
        # D1 -> outboard tee behind the rear wheel well
        tee = Vector((-1.62, s * 0.560, 0.470))
        if s > 0:
            path = [D1 + Vector((0, 0.0, -0.040)), Vector((PL_X, D1_Y, 0.470)), Vector((-1.62, D1_Y + 0.08, 0.470)), tee]
        else:
            path = [D1 + Vector((0, -0.055, -0.040)), Vector((PL_X, D1_Y - 0.055, 0.470)), tee]
        tube(P, path, R_LINE, SS, segs=16, bend=0.04)
        rbox(P, tee, (0.034, 0.034, 0.034), 0.004, SS)
        # rear branch -> rear lift valve top
        tube(P, [tee + Vector((0, 0, 0.017)), Vector((-1.62, s * 0.560, 0.700)), Vector((-1.905, s * 0.560, 0.700)),
                 Vector((-1.935, s * 0.700, 0.770)), Vector((rx, ry, 0.770)), top_r + Vector((0, 0, 0.022))], R_LINE, SS, segs=16, bend=0.04)
        nut_pair(P, top_r, Vector((0, 0, 1)), R_LINE, fm, af=0.028, L=0.016)
        # sill branch -> forward along the rocker -> front lift riser
        sill_y, sill_z = s * 0.905, 0.210
        pts = [tee - Vector((0, 0, 0.017)), Vector((-1.62, s * 0.560, 0.270)), Vector((-0.90, s * 0.560, 0.270)),
               Vector((-0.86, sill_y, sill_z)), Vector((0.880, sill_y, sill_z))]
        tube(P, pts, R_LINE, SS, segs=16, bend=0.05)
        for xc in (-0.55, 0.0, 0.55):
            p_clip(P, Vector((xc, sill_y, sill_z)), Vector((1, 0, 0)), R_LINE, Vector((0, s * 0.3, -1)), fm)
        for xc in (-1.30,):
            p_clip(P, Vector((xc, s * 0.560, 0.270)), Vector((1, 0, 0)), R_LINE, Vector((0, 0, -1)), fm)
        tee2 = Vector((0.880, sill_y, sill_z))
        rbox(P, tee2, (0.034, 0.034, 0.034), 0.004, SS)
        riser = [tee2 + Vector((0.017, 0, 0)), Vector((0.935, sill_y, sill_z)), Vector((0.935, s * 0.915, 0.470)),
                 Vector((fx, fy, 0.470)), top_f + Vector((0, 0, 0.018))]
        tube(P, riser, R_LINE, SS, segs=16, bend=0.035)
        nut_pair(P, top_f, Vector((0, 0, 1)), R_LINE, fm, af=0.028, L=0.016)
        # yaw branch: inboard under the cabin floor, past the front wheel, out to the yaw valve
        yb = [tee2 + Vector((0, 0, -0.017)), Vector((0.880, sill_y, 0.128)), Vector((0.900, s * 0.470, 0.128)),
              Vector((1.880, s * 0.470, 0.128)), Vector((1.930, s * 0.600, YAW['z'])), yin]
        tube(P, yb, R_SMALL, SS, segs=14, bend=0.04)
        for xc in (1.20, 1.70):
            p_clip(P, Vector((xc, s * 0.470, 0.128)), Vector((1, 0, 0)), R_SMALL, Vector((0, 0, 1)), fm)
        # EM valve harness stubs
        tube(P, [Vector((rx + 0.051, ry, 0.6925)), Vector((rx + 0.080, ry, 0.700)), Vector((rx + 0.10, ry - s * 0.06, 0.75))], 0.0030, RU, segs=8, bend=0.02)
        tube(P, [Vector((fx + 0.051, fy, 0.4075)), Vector((fx + 0.075, fy, 0.415)), Vector((fx + 0.080, fy, 0.52))], 0.0030, RU, segs=8, bend=0.02)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== COPVs
def copv_vessel(P, c, axis, r_in, wall, L_in, boss_r, mats, segs=72, ref=None):
    """Carbon-overwrapped pressure vessel (hemispherical liner domes, polar build-up, hoop-layer band),
    aluminium polar bosses on both ends. Returns (pole_out_t, r_out, half_cyl)."""
    FIL, AL = mats['fil'], mats['al']
    r_out = r_in + wall
    hc = L_in / 2 - r_in                     # half cylinder length (liner dome centres at +-hc)
    hoop = 0.0028
    prof = []
    n_d = 10
    # left dome from the pole opening to the equator (outer surface incl. polar build-up)
    for k in range(n_d + 1):
        a = math.radians(90.0) * (1 - k / n_d)          # 90 (pole) -> 0 (equator)
        rr = r_out * math.cos(a)
        if rr < boss_r * 1.45:
            continue
        build = 0.010 * smoothstep(0.55, 0.95, math.sin(a))
        prof.append((rr + build * math.cos(a) * 0.4, -hc - (r_out + build) * math.sin(a)))
    pole_t = prof[0][1]
    # hoop band with drop-offs
    prof += [(r_out, -hc + 0.030), (r_out + hoop, -hc + 0.045)]
    nb = 3
    for k in range(1, nb):
        prof.append((r_out + hoop, -hc + 0.045 + (2 * hc - 0.09) * k / nb))
    prof += [(r_out + hoop, hc - 0.045), (r_out, hc - 0.030)]
    for k in range(n_d + 1):
        a = math.radians(90.0) * k / n_d
        rr = r_out * math.cos(a)
        if rr < boss_r * 1.45:
            continue
        build = 0.010 * smoothstep(0.55, 0.95, math.sin(a))
        prof.append((rr + build * math.cos(a) * 0.4, hc + (r_out + build) * math.sin(a)))
    r_open = prof[-1][0]
    full = [(0.0, prof[0][1] + 0.004)] + prof + [(0.0, prof[-1][1] - 0.004)]
    lathe(P, full, c, axis, segs, FIL, ref=ref)
    # polar bosses: embedded flange + neck with thread rings + face
    ax = Vector(axis).normalized()
    ends = []
    for sgn in (-1, 1):
        a2 = ax * sgn
        t_base = abs(pole_t) - 0.012
        bp = [(0.0, t_base), (r_open + 0.006, t_base), (r_open + 0.006, t_base + 0.010), (boss_r + 0.004, t_base + 0.014),
              (boss_r, t_base + 0.020)]
        tt = t_base + 0.022
        for k in range(4):
            bp += [(boss_r, tt), (boss_r - 0.0025, tt + 0.003), (boss_r, tt + 0.006)]
            tt += 0.006
        bp += [(boss_r, tt + 0.010), (boss_r - 0.003, tt + 0.013), (0.0, tt + 0.013)]
        lathe(P, bp, c, a2, 32, AL)
        ends.append(c + a2 * (tt + 0.013))
    return ends, r_out + hoop, hc


def strap(P, c, axis, r, at, mats, foot_dir, width=0.050, foot_drop=0.06, ref=None):
    """Steel strap with rubber liner around a vessel (axis), clamp tabs on top, feet bolted below."""
    ax = Vector(axis).normalized()
    lathe(P, [(r + 0.0005, -width / 2 - 0.003), (r + 0.0045, -width / 2 - 0.003), (r + 0.0045, -width / 2), (r + 0.0080, -width / 2 + 0.0012),
              (r + 0.0080, width / 2 - 0.0012), (r + 0.0045, width / 2), (r + 0.0045, width / 2 + 0.003), (r + 0.0005, width / 2 + 0.003)],
          c + ax * at, ax, 48, [mats['ru'], mats['band'], mats['band'], mats['band'], mats['band'], mats['ru'], mats['ru'], mats['ru']], closed=True)
    fd = Vector(foot_dir).normalized()
    up = -fd
    side = ax.cross(up).normalized()
    rot = Matrix((side, ax, up)).transposed().to_4x4()
    # clamp tabs + tension bolt on top
    for sg in (1, -1):
        rbox(P, c + ax * at + up * (r + 0.020) + side * sg * 0.011, (0.006, width * 0.8, 0.034), 0.0015, mats['band'], rot=rot)
    tube(P, [c + ax * at + up * (r + 0.026) + side * 0.030, c + ax * at + up * (r + 0.026) - side * 0.030], 0.0045, mats['bolt'], segs=10, bend=0.0)
    hexnut(P, c + ax * at + up * (r + 0.026) + side * 0.030, side, 0.013, 0.0, 0.007, mats['bolt'])
    hexnut(P, c + ax * at + up * (r + 0.026) - side * 0.030, -side, 0.013, 0.0, 0.007, mats['bolt'])
    # feet
    for sg in (1, -1):
        base = c + ax * at + fd * (r + 0.008) * math.cos(math.radians(35)) + side * sg * (r + 0.008) * math.sin(math.radians(35))
        foot = c + ax * at + fd * (r + foot_drop) + side * sg * (r * 0.62)
        mid = (base + foot) / 2
        d = (foot - base)
        rot2 = d.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
        rbox(P, mid, (0.010, width, d.length + 0.01), 0.002, mats['band'], rot=rot2 @ Matrix.Rotation(0, 4, 'Z')) if False else None
        rbox(P, foot + fd * 0.004, (0.07, width, 0.008), 0.002, mats['band'], rot=Matrix((side, ax, up)).transposed().to_4x4())
        tube(P, [base, foot], 0.006, mats['band'], segs=10, bend=0.0)
        hexnut(P, foot - fd * 0.0, up, 0.016, 0.0, 0.008, mats['bolt'])


def saddle(P, c, axis, r, at, mats, down, floor_z, width=0.07):
    """Cradle under a horizontal vessel: arc pad + web + base plate bolted to the floor."""
    ax = Vector(axis).normalized()
    dn = Vector(down).normalized()
    side = ax.cross(-dn).normalized()
    pts, nrm = [], []
    for k in range(13):
        a = math.radians(-50 + 100 * k / 12)
        d = dn * math.cos(a) + side * math.sin(a)
        pts.append(c + ax * at + d * (r + 0.0105))
        nrm.append(d)
    sheet(P, [p - ax * (width / 2) for p in pts], nrm, ax, [(0.0, width)] * len(pts), 0.010, mats['bracket'], side='out')
    # rubber pad
    sheet(P, [c + ax * (at - width / 2 + 0.004) + n * (r + 0.0005) for n in nrm], nrm, ax, [(0.0, width - 0.008)] * len(nrm), 0.010, mats['ru'], side='out')
    zc = (c + ax * at).z
    low = c + ax * at + dn * (r + 0.02)
    h = max(0.01, low.z - floor_z)
    rbox(P, Vector((low.x, low.y, floor_z + h / 2 + 0.004)), (abs(ax.x) * width + abs(ax.y) * 0.24 + 0.02 if abs(ax.x) > 0.5 else 0.24,
                                                               abs(ax.y) * width + abs(ax.x) * 0.24 if abs(ax.y) > 0.5 else 0.24, h), 0.004, mats['bracket']) if False else None
    web_w = (r + 0.01) * 2 * math.sin(math.radians(50))
    sz = (width * 0.6, web_w, h) if abs(ax.x) > 0.5 else (web_w, width * 0.6, h)
    rbox(P, Vector((low.x, low.y, floor_z + 0.008 + h / 2)), sz, 0.004, mats['bracket'])
    bsz = (width + 0.03, web_w + 0.06, 0.008) if abs(ax.x) > 0.5 else (web_w + 0.06, width + 0.03, 0.008)
    rbox(P, Vector((low.x, low.y, floor_z + 0.004)), bsz, 0.002, mats['bracket'])
    for sg in (1, -1):
        off = Vector((0, sg * (web_w / 2 + 0.018), 0)) if abs(ax.x) > 0.5 else Vector((sg * (web_w / 2 + 0.018), 0, 0))
        hexnut(P, Vector((low.x, low.y, floor_z + 0.008)) + off, Vector((0, 0, 1)), 0.016, 0.0, 0.008, mats['bolt'])


def on_tank_valve(P, port, axis, mats, out_dir, solenoid=True):
    """In-tank / on-tank valve (OTV): forged block with solenoid shut-off, manual valve, TPRD and outlet port."""
    ax = Vector(axis).normalized()
    od = Vector(out_dir).normalized()
    side = ax.cross(od).normalized()
    rot = Matrix((ax, side, ax.cross(side))).transposed().to_4x4()
    c = port + ax * 0.030
    rbox(P, c, (0.060, 0.080, 0.070), 0.006, mats['bk'], rot=rot)
    cyl(P, port, ax, 0.040, -0.002, 0.004, mats['al'], segs=32, ch=0.001)
    top = ax.cross(side).normalized()
    if top.z < 0:
        top = -top
    if solenoid:
        lathe(P, [(0, 0), (0.021, 0), (0.021, 0.010), (0.019, 0.012), (0.019, 0.050), (0.0205, 0.052), (0.0205, 0.060), (0.017, 0.064), (0, 0.064)],
              c + top * 0.035, top, 28, [mats['bk']] * 3 + [mats['al']] * 5)
        connector(P, c + top * 0.075, side, {'metal': mats['al'], 'plug': mats['bk']}, r=0.007, L=0.016)
    # manual valve stem + knurled knob
    cyl(P, c + side * 0.040, side, 0.008, 0.0, 0.020, mats['ss'], segs=16)
    lathe(P, [(0, 0.020), (0.016, 0.020), (0.018, 0.024), (0.018, 0.036), (0.015, 0.040), (0, 0.040)], c + side * 0.040, side, 18, mats['al'])
    # TPRD (thermally activated pressure relief) with vent cap
    cyl(P, c - side * 0.040, -side, 0.010, 0.0, 0.018, mats['au'], segs=16, ch=0.001)
    hexnut(P, c - side * 0.040, -side, 0.022, 0.018, 0.028, mats['au'])
    # outlet port + fitting
    pp = c + od * 0.035
    nut_pair(P, pp, od, 0.016, {'fit': mats['ss']}, af=0.036, L=0.016)
    return pp + od * 0.022


def copv_rocket(M):
    P = Part("COPV_Rocket", M, ["Filament", "Aluminum", "Stainless", "Rubber", "BlackGloss", "Gold", "Titanium"])
    mats = dict(fil=P.m("Filament"), al=P.m("Aluminum"), band=P.m("Titanium"), ru=P.m("Rubber"), bk=P.m("BlackGloss"),
                bolt=P.m("Stainless"), ss=P.m("Stainless"), au=P.m("Gold"), bracket=P.m("Aluminum"))
    d = COPV_R
    X = Vector((1, 0, 0))
    ends, r_o, hc = copv_vessel(P, d['c'], X, d['r_in'], d['wall'], d['L_in'], d['boss_r'], mats, segs=56)
    for at in (-0.36, 0.40):
        strap(P, d['c'], X, r_o, at, mats, foot_dir=Vector((0, 0, -1)), foot_drop=-0.12)
        saddle(P, d['c'], X, r_o, at, mats, Vector((0, 0, -1)), 0.176)
    # rear boss: on-tank valve; front boss: TPRD plug
    rear = ends[0]
    out = on_tank_valve(P, rear, -X, mats, Vector((0, 0, -1)))
    front = ends[1]
    hexnut(P, front, X, 0.050, 0.0, 0.014, mats['au'])
    cyl(P, front, X, 0.012, 0.014, 0.030, mats['au'], segs=16, ch=0.001)
    # transfer line: OTV -> through the rear bulkhead (grommet) -> under the trunk vessel -> HP manifold
    SS = mats['ss']
    fm = {'fit': SS, 'rubber': mats['ru'], 'band': SS}
    y0 = d['c'].y
    path = [out, Vector((out.x, y0, 0.270)), Vector((-0.80, y0, 0.270)), Vector((-1.50, y0, 0.270)),
            Vector((-1.50, y0, 0.505)), Vector((-1.50, M1_Y, 0.505)), Vector((PL_X, M1_Y, 0.505)), Vector((PL_X, M1_Y, PL_Z - 0.052))]
    tube(P, path, 0.016, SS, segs=18, bend=0.06)
    ring(P, Vector((-0.780, y0, 0.270)), X, 0.0165, 0.032, -0.022, 0.022, mats['ru'], segs=24, ch=0.003)    # bulkhead grommet
    nut_pair(P, Vector((PL_X, M1_Y, PL_Z - 0.035)), Vector((0, 0, -1)), 0.016, fm, af=0.036, L=0.016)
    for xc in (-1.05, -1.30):
        p_clip(P, Vector((xc, y0, 0.270)), X, 0.016, Vector((0, 0, -1)), fm)
    return P.finish(smooth_angle=40.0)


def copv_trunk(M):
    P = Part("COPV_Trunk", M, ["Filament", "Aluminum", "Stainless", "Rubber", "BlackGloss", "Gold", "Titanium"])
    mats = dict(fil=P.m("Filament"), al=P.m("Aluminum"), band=P.m("Titanium"), ru=P.m("Rubber"), bk=P.m("BlackGloss"),
                bolt=P.m("Stainless"), ss=P.m("Stainless"), au=P.m("Gold"), bracket=P.m("Aluminum"))
    d = COPV_T
    Y = Vector((0, 1, 0))
    ends, r_o, hc = copv_vessel(P, d['c'], Y, d['r_in'], d['wall'], d['L_in'], d['boss_r'], mats, segs=56, ref=Vector((1, 0, 0)))
    floor = 0.330
    # crossmember (rear subframe) under the vessel
    tube(P, [Vector((d['c'].x, -0.615, floor - 0.022)), Vector((d['c'].x, 0.615, floor - 0.022))], 0.022, mats['al'], segs=20, bend=0.0)
    for sg in (1, -1):
        rbox(P, Vector((d['c'].x, sg * 0.627, floor - 0.022)), (0.08, 0.012, 0.09), 0.003, mats['al'])
    for at in (-0.25, 0.25):
        strap(P, d['c'], Y, r_o, at, mats, foot_dir=Vector((0, 0, -1)), foot_drop=-0.12)
        saddle(P, d['c'], Y, r_o, at, mats, Vector((0, 0, -1)), floor)
    out = on_tank_valve(P, ends[1], Y, mats, Vector((-1, 0, 0)))
    hexnut(P, ends[0], -Y, 0.048, 0.0, 0.014, mats['au'])
    cyl(P, ends[0], -Y, 0.012, 0.014, 0.030, mats['au'], segs=16, ch=0.001)
    copv_trunk.outlet = out
    return P.finish(smooth_angle=40.0)


def copv_volume(d):
    r, L = d['r_in'], d['L_in']
    return math.pi * r * r * (L - 2 * r) + 4.0 / 3.0 * math.pi * r ** 3


# =============================================================================================== plumbing
def plumbing(M, trunk_outlet=None):
    P = Part("ColdGas_Plumbing", M, ["Stainless", "Aluminum", "BlackGloss", "Gold", "Rubber", "Titanium", "LightRed"])
    SS, AL, BK, AU, RU, TI, RD = (P.m(k) for k in ("Stainless", "Aluminum", "BlackGloss", "Gold", "Rubber", "Titanium", "LightRed"))
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Z = Vector((0, 0, 1))
    fm = {'fit': SS, 'rubber': RU, 'band': SS}
    cm = {'metal': AL, 'plug': BK}
    R_HP = 0.016          # 32 mm OD high-pressure line (main feed)
    y = lambda v: Vector((PL_X, v, PL_Z))

    # ---- mounting plate for the regulator train (bolted to the rear subframe towers)
    rbox(P, Vector((PL_X, 0.10, PL_Z - 0.072)), (0.16, 0.78, 0.010), 0.003, AL)
    for yy in (-0.26, 0.46):
        rbox(P, Vector((PL_X, yy, (PL_Z - 0.077 + 0.30) / 2)), (0.05, 0.05, PL_Z - 0.077 - 0.30), 0.004, AL)
    for yy in (-0.20, 0.0, 0.20, 0.40):
        for xx in (-0.06, 0.06):
            hexnut(P, Vector((PL_X + xx, yy, PL_Z - 0.067)), Z, 0.012, 0.0, 0.006, SS)

    # ---- M1 HP manifold block: inlets (trunk vessel, Rocket vessel, fill), burst disc, P1, T1, outlet
    m1 = y(M1_Y)
    rbox(P, m1, (0.080, 0.140, 0.072), 0.006, SS)
    rbox(P, m1 - Z * 0.054, (0.060, 0.090, 0.036), 0.004, AL)        # foot
    for pz, ax_, af in ((m1 + Y * 0.070, Y, 0.036), (m1 - Z * 0.036, -Z, 0.036), (m1 + X * 0.040 + Y * 0.035, X, 0.026)):
        nut_pair(P, pz, ax_, 0.016 if af > 0.03 else 0.008, fm, af=af, L=0.014)
    # P1 pressure transducer
    p1 = m1 + Z * 0.036 + Y * 0.030 + X * 0.012
    hexnut(P, p1, Z, 0.024, 0.0, 0.012, SS)
    cyl(P, p1, Z, 0.0115, 0.012, 0.055, SS, segs=20, ch=0.001)
    connector(P, p1 + Z * 0.055, Z, cm, r=0.0085, L=0.018)
    # T1 temperature transducer (thermowell)
    t1 = m1 + Z * 0.036 - Y * 0.035 + X * 0.012
    hexnut(P, t1, Z, 0.019, 0.0, 0.010, SS)
    cyl(P, t1, Z, 0.0065, 0.010, 0.040, SS, segs=14, ch=0.0008)
    lathe(P, [(0, 0.040), (0.010, 0.040), (0.010, 0.060), (0.008, 0.063), (0, 0.063)], t1, Z, 16, BK)
    # burst disc holder (rupture disc) + vent line down into the fan duct (inert N2)
    bd = m1 + Z * 0.036 - X * 0.020 + Y * 0.000
    hexnut(P, bd, Z, 0.034, 0.0, 0.016, AU)
    hexnut(P, bd, Z, 0.034, 0.018, 0.034, AU)
    lathe(P, [(0, 0.034), (0.008, 0.034), (0.008, 0.040), (0, 0.040)], bd, Z, 12, RD)     # red indicator tag
    vent_top = bd + Z * 0.034
    tube(P, [vent_top, vent_top + Z * 0.030 - X * 0.0, vent_top + Vector((-0.080, 0.090, 0.030)), Vector((-1.68, 0.53, 0.66)),
             Vector((-1.68, 0.53, 0.42))], 0.0085, SS, segs=12, bend=0.035)
    lathe(P, [(0, 0.42), (0.020, 0.42), (0.020, 0.405), (0.010, 0.398), (0.0, 0.398)], Vector((-1.68, 0.53, 0)), Z, 16, SS)

    # ---- trunk vessel OTV -> M1
    if trunk_outlet is not None:
        o = trunk_outlet
        tube(P, [o, Vector((-1.400, o.y, o.z)), Vector((-1.470, o.y, PL_Z)), Vector((PL_X, M1_Y + 0.165, PL_Z)), m1 + Y * 0.084],
             R_HP, SS, segs=18, bend=0.06)
        p_clip(P, Vector((-1.30, o.y, o.z)), X, R_HP, Vector((0, 0, -1)), fm)

    # ---- fill receptacle (under a flap in the rear deck): receptacle, check valve, dust cap with tether
    fr = Vector((-1.480, 0.500, 0.870))
    tube(P, [m1 + X * 0.054 + Y * 0.035, Vector((-1.490, M1_Y + 0.035, PL_Z)), Vector((-1.480, 0.500, 0.700)), fr - Z * 0.060],
         0.008, SS, segs=12, bend=0.03)
    lathe(P, [(0, -0.060), (0.012, -0.060), (0.012, -0.022), (0.016, -0.018), (0.016, -0.004), (0, -0.004)], fr, Z, 20, AU)   # check valve
    hexnut(P, fr - Z * 0.004, Z, 0.032, 0.0, 0.014, SS)                        # bulkhead nut
    rbox(P, fr + Z * 0.017, (0.10, 0.10, 0.006), 0.004, AL)                   # mounting plate
    lathe(P, [(0, 0.020), (0.011, 0.020), (0.011, 0.050), (0.0125, 0.052), (0.0125, 0.058), (0, 0.058)], fr, Z, 20, SS)     # receptacle
    lathe(P, [(0, 0.044), (0.0165, 0.044), (0.0175, 0.046), (0.0175, 0.068), (0.0155, 0.071), (0, 0.071)], fr, Z, 24, BK)   # dust cap
    tube(P, [fr + Vector((0.016, 0, 0.050)), fr + Vector((0.040, 0.010, 0.040)), fr + Vector((0.045, 0.020, 0.022))], 0.0016, SS, segs=6, bend=0.01)

    # ---- V1 manual isolation valve (ball valve, lever)
    v1 = y(V1_Y)
    tube(P, [m1 - Y * 0.070, v1 + Y * 0.048], R_HP, SS, segs=18, bend=0.0)
    nut_pair(P, m1 - Y * 0.070, -Y, R_HP, fm, af=0.036, L=0.012)
    lathe(P, [(0, -0.048), (0.024, -0.048), (0.026, -0.040), (0.036, -0.026), (0.040, -0.010), (0.040, 0.010), (0.036, 0.026),
              (0.026, 0.040), (0.024, 0.048), (0, 0.048)], v1, Y, 28, SS)
    cyl(P, v1, Z, 0.010, 0.030, 0.056, SS, segs=16)
    hexnut(P, v1 + Z * 0.056, Z, 0.020, 0.0, 0.008, SS)
    rbox(P, v1 + Z * 0.068 + X * 0.060, (0.140, 0.020, 0.007), 0.0025, RD, rot=None)
    rbox(P, v1 + Z * 0.068 + X * 0.120, (0.040, 0.026, 0.016), 0.006, RU)

    # ---- F1 HP filter
    f1 = y(F1_Y)
    tube(P, [v1 - Y * 0.048, f1 + Y * 0.040], R_HP, SS, segs=18, bend=0.0)
    lathe(P, [(0, -0.040), (0.020, -0.040), (0.024, -0.034), (0.030, -0.030), (0.030, 0.030), (0.024, 0.034), (0.020, 0.040), (0, 0.040)],
          f1, Y, 28, SS)
    hexnut(P, f1, Y, 0.050, -0.012, 0.004, SS)

    # ---- R1 dome-loaded regulator: body, dome, pilot regulator + loading line, gauge port
    r1 = y(R1_Y)
    tube(P, [f1 - Y * 0.040, r1 + Y * 0.075], R_HP, SS, segs=18, bend=0.0)
    lathe(P, [(0, -0.060), (0.050, -0.060), (0.056, -0.054), (0.058, -0.030), (0.058, 0.040), (0.062, 0.044), (0.080, 0.046),
              (0.080, 0.058), (0.076, 0.062), (0, 0.062)], r1, Z, 40, [SS, SS, SS, SS, SS, SS, SS, SS, SS])
    for k in range(8):                                        # dome bolts
        a = TAU * (k + 0.5) / 8
        hexnut(P, r1 + Z * 0.062 + (X * math.cos(a) + Y * math.sin(a)) * 0.070, Z, 0.012, 0.0, 0.007, SS)
    lathe(P, [(0, 0.062), (0.072, 0.062), (0.070, 0.080), (0.060, 0.100), (0.040, 0.114), (0.020, 0.120), (0, 0.121)], r1, Z, 40, AL)
    cyl(P, r1 + Z * 0.118, Z, 0.010, 0.0, 0.016, SS, segs=16, ch=0.001)          # dome port
    for sg in (1, -1):                                         # side ports (in / out)
        cyl(P, r1, Y * sg, 0.026, 0.050, 0.070, SS, segs=24, ch=0.002)
        hexnut(P, r1, Y * sg, 0.040, 0.066, 0.078, SS, bore=R_HP)
    # pilot (loading) regulator on the rear side with its sensing / loading lines and a gauge
    pr = r1 - X * 0.105 + Z * 0.040
    lathe(P, [(0, -0.030), (0.020, -0.030), (0.022, -0.026), (0.022, 0.020), (0.030, 0.024), (0.030, 0.032), (0.012, 0.050),
              (0.012, 0.075), (0, 0.075)], pr, Z, 24, AL)
    lathe(P, [(0, 0.075), (0.016, 0.075), (0.016, 0.082), (0.012, 0.090), (0, 0.090)], pr, Z, 18, BK)    # set knob
    tube(P, [pr - X * 0.0 + Y * 0.022 - Z * 0.010, pr + Vector((0.0, 0.10, -0.010)), Vector((PL_X - 0.10, M1_Y - 0.030, PL_Z + 0.050)),
             Vector((PL_X - 0.040, M1_Y - 0.030, PL_Z + 0.050)), m1 + Vector((-0.040, -0.030, 0.0))], 0.004, SS, segs=8, bend=0.02)
    tube(P, [pr + Z * 0.060 + X * 0.012, pr + Vector((0.050, 0, 0.100)), r1 + Z * 0.134], 0.004, SS, segs=8, bend=0.02)
    g = pr - Y * 0.040
    cyl(P, pr - Y * 0.020, -Y, 0.005, 0.0, 0.020, SS, segs=10)
    lathe(P, [(0, 0.0), (0.024, 0.0), (0.026, 0.004), (0.026, 0.016), (0.024, 0.018), (0, 0.018)], g, -Y, 28, BK)
    cyl(P, g - Y * 0.018, -Y, 0.0225, 0.0, 0.0005, AL, segs=28)

    # ---- D1 LP (regulated) distribution block: P2, relief valve RV1 + vent, outlets (rear feed, lift L / R)
    d1 = y(D1_Y)
    tube(P, [r1 - Y * 0.075, d1 + Y * 0.050], R_HP, SS, segs=18, bend=0.0)
    rbox(P, d1, (0.080, 0.100, 0.072), 0.006, SS)
    rbox(P, d1 - Z * 0.054, (0.060, 0.070, 0.036), 0.004, AL)
    p2 = d1 + Z * 0.036 + X * 0.012
    hexnut(P, p2, Z, 0.024, 0.0, 0.012, SS)
    cyl(P, p2, Z, 0.0115, 0.012, 0.055, SS, segs=20, ch=0.001)
    connector(P, p2 + Z * 0.055, Z, cm, r=0.0085, L=0.018)
    rv = d1 - Y * 0.050
    cyl(P, rv, -Y, 0.014, 0.0, 0.020, AU, segs=18, ch=0.001)
    lathe(P, [(0, 0.020), (0.018, 0.020), (0.018, 0.060), (0.014, 0.066), (0, 0.066)], rv, -Y, 20, AU)
    tube(P, [rv - Y * 0.040 - Z * 0.018, rv + Vector((0, -0.040, -0.120)), Vector((PL_X, -0.33, 0.40))], 0.007, SS, segs=10, bend=0.03)
    nut_pair(P, d1 - Z * 0.036 - Y * 0.0, -Z, 0.011, fm, af=0.028, L=0.012)             # lift L outlet (Rocket)
    nut_pair(P, d1 - Z * 0.036 - Y * 0.055 + Y * 0.0, -Z, 0.011, fm, af=0.028, L=0.012) if False else None
    # rear feed: D1 -> through the valve-bay wall -> manifold log inlet (centre)
    rf = [d1 - X * 0.040, Vector((-1.700, D1_Y, PL_Z)), Vector((-1.760, -0.020, 0.600)), Vector((-1.790, 0.0, NOZ_Z)),
          Vector((MANIFOLD_X + 0.048, 0.0, NOZ_Z))]
    tube(P, rf, R_HP, SS, segs=18, bend=0.07)
    nut_pair(P, d1 - X * 0.040, -X, R_HP, fm, af=0.036, L=0.012)
    p_clip(P, Vector((-1.650, D1_Y, PL_Z)), X, R_HP, Vector((0, 0, -1)), fm)
    ring(P, Vector((-1.828, 0.0, NOZ_Z)), X, R_HP + 0.0005, 0.030, -0.012, 0.012, RU, segs=24, ch=0.002)   # bulkhead grommet
    # harness trunk from the valve-bay loom to the thruster controller (on the plate)
    ecu = Vector((PL_X + 0.010, -0.420, PL_Z - 0.030))
    rbox(P, ecu, (0.110, 0.150, 0.045), 0.006, BK)
    for k in range(4):
        cyl(P, ecu + Vector((0.055, -0.045 + 0.030 * k, 0.004)), X, 0.0075, 0.0, 0.014, AL, segs=12, ch=0.001)
    tube(P, [ecu + Vector((0.07, 0.0, 0.004)), Vector((-1.45, -0.42, 0.66)), Vector((-1.45, 0.30, 0.70)),
             Vector((MANIFOLD_X + 0.060, 0.45, BOX_TOP + 0.050))], 0.0065, RU, segs=10, bend=0.05)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== API
def thruster_table():
    """Nozzle data for the physics (body frame). exhaust = gas direction; thrust on the car = -exhaust."""
    rows = []
    for y in NOZ_Y:
        rows.append(dict(name="Rear_%+.3f" % y, exit=(NOZ_EXIT_X, y, NOZ_Z), exhaust=(-1, 0, 0), throat_d=2 * NOZ_RT, exit_d=2 * NOZ_RE))
    for s, tag in ((1, "L"), (-1, "R")):
        rows.append(dict(name="Lift_Front_" + tag, exit=(LIFT_FRONT['x'], s * LIFT_FRONT['y'], LIFT_FRONT['z_exit']), exhaust=(0, 0, -1),
                         throat_d=2 * LIFT_RT, exit_d=2 * LIFT_RE))
        rows.append(dict(name="Lift_Rear_" + tag, exit=(LIFT_REAR['x'], s * LIFT_REAR['y'], LIFT_REAR['z_exit']), exhaust=(0, 0, -1),
                         throat_d=2 * LIFT_RT, exit_d=2 * LIFT_RE))
        rows.append(dict(name="Yaw_Front_" + tag, exit=(YAW['x'], s * YAW['y_exit'], YAW['z']), exhaust=(0, s, 0),
                         throat_d=2 * YAW_RT, exit_d=2 * YAW_RE))
    return rows


def build(M):
    """Create all cold-gas / fan-box objects. M = roadster_materials.materials(). Returns {name: object}."""
    M = dict(M)
    for k, v in extra_materials().items():
        M.setdefault(k, v)
    out = {}
    out["Fan_Box"] = fan_box(M)
    for i, y in enumerate(FAN_Y):
        out["Fan_%d" % i] = fan_rotor(M, "Fan_%d" % i, (FAN_X, y, FAN_Z))
    out["Thrusters_Rear"] = rear_thrusters(M)
    out["Thrusters_Rocket"] = rocket_thrusters(M)
    out["COPV_Rocket"] = copv_rocket(M)
    out["COPV_Trunk"] = copv_trunk(M)
    out["ColdGas_Plumbing"] = plumbing(M, getattr(copv_trunk, "outlet", None))
    return out
