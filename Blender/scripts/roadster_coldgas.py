"""Cold-gas thruster system and rear suction-fan box of the 2026 Tesla Roadster (Thrust / Rocket trims).

    import roadster_materials, roadster_coldgas
    parts = roadster_coldgas.build(roadster_materials.materials())   # {name: bpy object}

Objects (Unreal static mesh = SM_<name>):
    Thrusters_Rear    static  4 rear converging-diverging nozzles, EM valves, actuators, harness, feed manifold
    Thrusters_Rocket  static  4 corner lift stacks, 2 lateral yaw thrusters, their feed lines (Rocket only)
    COPV_Rocket       static  ~292 L vessel in the passenger-seat bay (largest that clears the spine, door armrest, sill wall and
                              dash; Tesla claims 400 L), bosses, on-tank valve, saddles, straps, transfer line through the
                              rear bulkhead, forged elbow E2, isolation valve ISO2 (80 mm bore) into the header (Rocket only)
    COPV_Trunk        static  ~144 L transverse vessel between the rear bulkhead and the rear suspension (largest that clears
                              the bulkhead, toe links, upper-arm pivots and tyres; Tesla claims 200 L), on-tank valve, straps
    ColdGas_Plumbing  static  trunk riser, forged elbow E1, isolation valve ISO1 (75 mm bore, electric + manual), HP header H1
                              (burst disc, P/T transducers, fill line to a receptacle in the left rear quarter), 89 mm main to
                              the dome-loaded regulator (electronic pilot), LP block D1 (P2, relief valve), rear feed, vents,
                              supports, thruster ECU + harness
    Fan_Box           static  carbon suction box (frame, nozzle shelf, 4 bays, 3 strakes, apron, flanks), ramp duct,
                              shrouds, stator vanes, fan motors, inlet grille, diffuser slats
    Fan_0 .. Fan_3    moving  rotor only; origin on the rotor axis at the blade mid-plane. The mesh is built around its
                              LOCAL +X axis; the object carries rotation_euler = (0, 35 deg, 0) because the real fans are
                              pitched 35 deg (exhaust rearward + upward, see fan_manifest()). Spin = positive about local +X.

Static objects keep their origin at the world origin (geometry is in body coordinates).
Nozzle throats / exits and feed-line bores follow Unreal/RoadsterSim/.../ColdGasSystem.h (15 deg conical divergents);
thruster_table() / nozzle_manifest() give exit points and exhaust directions for the physics and the rig header.
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
RIM = 0.022                     # frame rim width (rear view)
BOX_IN_HW = BOX_HW - RIM        # 0.613 inner opening half width
TOP_CHAMF = (0.490, 0.569)      # top chamfer: from (|y| 0.490, BOX_TOP) to (BOX_HW, 0.569)
BOT_CHAMF = (0.575, 0.220)      # bottom chamfer: from (|y| 0.575, BOX_BOT) to (BOX_HW, 0.220)
SHELF_X = -2.100                # nozzle shelf back face
SHELF_LIP_Z = (0.516, 0.531)    # bay ceiling rear lip (bottom, top)
LIP_Z = 0.312                   # lower apron lip top (centre bays); rises to LIP_Z_OUT at the side walls
LIP_Z_OUT = 0.336
DUCT_CEIL_Z = 0.527             # duct ceiling centre-plane above the fans
STRAKE_Y = (-0.295, 0.0, 0.295)

FAN_X, FAN_Z = -1.955, 0.380                  # rotor centre (axis x blade mid-plane)
FAN_TILT = math.radians(35.0)                    # fan axes pitched up toward the rear (exhaust rearward + upward)
A_DN = Vector((-math.cos(FAN_TILT), 0.0, math.sin(FAN_TILT)))    # exhaust direction
A_UP = -A_DN                                                       # upstream, into the duct
V_UP = Vector((math.sin(FAN_TILT), 0.0, math.cos(FAN_TILT)))      # 'up' in the fan disc plane
BH_T = -0.045                                    # fan bulkhead rear face along A_UP from the rotor centre
BH_V = (-0.150, 0.150)                           # bulkhead extent along V_UP (bottom, top)
FAN_Y = (-0.452, -0.145, 0.145, 0.452)          # Fan_0 .. Fan_3 (right -> left)
FAN_TIP_R = 0.1245
SHROUD_R = (0.1265, 0.1315)

# ---- nozzle and line sizes follow Unreal/.../ColdGasSystem.h (throat / exit / feed-line diameters, 15 deg cones)
DIV_HALF = math.radians(15.0)    # conical divergent half-angle (all nozzles)

# ---- rear thrusters
NOZ_Z = 0.578
NOZ_Y = (-0.352, -0.118, 0.118, 0.352)
NOZ_EXIT_X = -2.150
NOZ_L = 0.120                    # inlet flange face -> exit plane
NOZ_RT, NOZ_RE = 0.0175, 0.036   # throat / exit radius (35.0 / 72 mm, eps 4.23)
R_FEED_REAR = 0.030              # 60 mm feed line per rear nozzle
MAN_X = (-1.865, -1.765)         # rear manifold block (rear face, front face)
MAN_Z = (0.546, 0.646)

# ---- corner lift thrusters (exit points, firing straight down)
LIFT_REAR = dict(x=-2.000, y=0.740, z_exit=0.200)
LIFT_FRONT = dict(x=0.965, y=0.915, z_exit=0.090)    # outboard of the cabin side panel (|y| 0.857), inside the skin
LIFT_RT, LIFT_RE = 0.01425, 0.028         # 28.5 / 56 mm
R_FEED_LIFT = 0.025                       # 50 mm feed lines
LIFT_X, LIFT_Z = -1.255, 0.705            # lift distribution crossing (ahead of the regulator, above the dampers)

# ---- lateral yaw thrusters (front corners, exhausting outward through a flush aperture in the body side)
YAW = dict(x=1.970, z=0.235)
YAW_RT, YAW_RE = 0.0079, 0.0155           # 15.8 / 31 mm
R_FEED_YAW = 0.015                        # 30 mm feed lines

# ---- vessels (Type-IV COPVs, 690 bar rated). Sizes are the largest that clear the cabin (spine, door armrest,
#      sill wall, dash) and the rear suspension (roadster_wheels rig) - see the module report.
COPV_R = dict(c=Vector((0.134, -0.447, 0.535)), r_in=0.2742, wall=0.035, L_in=1.4184, boss_r=0.045)   # axis X
COPV_T = dict(c=Vector((-0.985, -0.037, 0.420)), r_in=0.220, wall=0.030, L_in=1.092, boss_r=0.042)    # axis Y

# ---- high-pressure train. Row 1 (isolation valves + header) rides on the rear bulkhead above the trunk vessel;
#      the regulator and LP block lie aft of it over the rear dampers; all lines cross the suspension above z 0.69.
R_HP = 0.0445                    # 89 mm OD heavy-wall HP mains (3 in XXS class)
ROW_X, TR_Z = -0.975, 0.800      # row-1 axis (x) and train height
E2_Y, ISO2_Y, H1_Y, ISO1_Y = -0.447, -0.262, 0.000, 0.262
R1_X = -1.400                    # regulator centre (axis along -X, loading dome aft)
D1 = Vector((-1.380, -0.270, 0.840))     # LP distribution block
MAN_IN = Vector((-1.815, -0.270, 0.646))  # rear manifold block top inlet
ISO_BORE = (0.075, 0.080)                # trunk / Rocket isolation valve bores
LIFT_PORT_Z = 0.810                      # LP block lift take-off height

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
        # every part is built from closed islands: guarantee outward normals (positive signed volume) per island,
        # independent of the recalc heuristic (which can pick the inside of thin ring-shaped islands)
        seen = set()
        for f0 in bm.faces:
            if f0 in seen:
                continue
            island, stack, vol = [], [f0], 0.0
            seen.add(f0)
            while stack:
                f = stack.pop()
                island.append(f)
                vs = [v.co for v in f.verts]
                for k in range(1, len(vs) - 1):
                    vol += vs[0].dot(vs[k].cross(vs[k + 1]))
                for e in f.edges:
                    for g in e.link_faces:
                        if g not in seen:
                            seen.add(g)
                            stack.append(g)
            if vol < 0:
                bmesh.ops.reverse_faces(bm, faces=island, flip_multires=False)
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
    """Hex nut / hex body across-flats `af` between t0 and t1 (chamfered corners; plain hex below 15 mm)."""
    R = af / math.sqrt(3.0)
    if af < 0.015 and bore <= 0:
        return lathe(P, [(0, t0), (R, t0), (R, t1), (0, t1)], origin, axis, 6, mat, ref=ref, phase=math.pi / 6)
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
    segs = min(segs, 12) if r < 0.02 else (16 if r < 0.035 else 22)
    pts = fillet(pts, bend if bend is not None else 3.0 * r, seg90=4 if r < 0.035 else 5)
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
    lathe(P, prof, o, ax, 32, mats)

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


RAMP_X0 = -1.645                 # duct inlet front lip (clear of the rear lower-arm pivots at x -1.60)


def ramp_z(x):
    """Duct ceiling centre-plane: flat over the fans, sweeping down to the underfloor at x -1.45."""
    if x <= -1.86:
        return DUCT_CEIL_Z
    return 0.128 + (DUCT_CEIL_Z - 0.128) * smoothstep(RAMP_X0, -1.86, x)


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

    zi_top = BOX_TOP - RIM            # 0.623 inner top
    # ---- 2. top cover (bent plate) from behind the frame to the valve bay, chamfers to the side walls
    yt, zc = TOP_CHAMF
    cv = fillet([Vector((0, -BOX_HW, zc)), Vector((0, -yt, BOX_TOP)), Vector((0, yt, BOX_TOP)), Vector((0, BOX_HW, zc))], 0.02, 3)
    nrm = [n if n.z <= 0 else -n for n in curve_normals_2d(cv, X)]    # thickness goes down / inward
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
        lathe(P, sh, c, A_UP, 36, CF, closed=True, ref=Y)
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
            mp += [(0.056, tt), (0.0605, tt + 0.003), (0.0605, tt + 0.024), (0.056, tt + 0.027)]
            tt += 0.038
        mp += [(0.056, 0.205), (0.054, 0.217), (0.040, 0.225), (0.022, 0.227), (0.0, 0.227)]
        mats = [BK if (mp[i][1] < 0.051 or mp[i][1] > 0.204) else AL for i in range(len(mp) - 1)]
        lathe(P, mp, c, A_UP, 18, mats, ref=Y)
        cb = c + A_UP * 0.200 + V_UP * 0.068
        rot = Matrix((A_UP, Y, V_UP)).transposed().to_4x4()
        rbox(P, cb, (0.034, 0.034, 0.020), 0.003, BK, rot=rot)
        connector(P, cb + V_UP * 0.010, V_UP, {'metal': AL, 'plug': BK}, r=0.007, L=0.016)
        tube(P, [cb + V_UP * 0.026, cb + V_UP * 0.060 + A_UP * 0.02, Vector((-1.75, yc, ramp_z(-1.75) - 0.008))],
             0.0045, RU, segs=10, bend=0.02)

    # ---- 8. duct: ceiling ramp, side walls, compartment front wall, inlet grille
    xs = [Tp.x + 0.004] + [(-1.86 + (RAMP_X0 + 1.86) * k / 12) for k in range(13)]
    xs = sorted(set(round(v, 5) for v in xs))
    cvr = [Vector((x, 0.0, ramp_z(x))) for x in xs]
    nr = curve_normals_2d(cvr, Vector((0, -1, 0)))
    nr = [n if n.z > 0 else -n for n in nr]
    sheet(P, cvr, nr, Y, [(-BOX_IN_HW - 0.009, BOX_IN_HW + 0.009)] * len(cvr), 0.005, CF, side='mid')
    for s in (1, -1):
        xs_w = [-2.094, -2.06, -2.0, -1.95, -1.90] + [(-1.86 + (RAMP_X0 + 1.86) * k / 8) for k in range(9)]
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
    rbox(P, Vector((-1.755, 0, (DUCT_CEIL_Z + BOX_TOP) / 2 + 0.002)), (0.004, 2 * BOX_IN_HW, BOX_TOP - DUCT_CEIL_Z - 0.004), 0.0015, CF)
    # inlet grille (under the duct, behind the underfloor seal)
    for k in range(-5, 6):
        y = k * 0.105
        tube(P, [Vector((RAMP_X0 - 0.015, y, 0.132)), Vector((-2.035, y, 0.132))], 0.0035, BK, segs=8, bend=0.0)
    for x in (RAMP_X0 - 0.035, -1.85, -1.99):
        tube(P, [Vector((x, -0.60, 0.128)), Vector((x, 0.60, 0.128))], 0.0045, BK, segs=8, bend=0.0)

    # ---- 9. side flanks curving out toward the rear wheels
    for s in (1, -1):
        plan = [(-2.098, 0.628), (-2.068, 0.646), (-2.022, 0.668), (-1.968, 0.698), (-1.928, 0.738),
                (-1.902, 0.782), (-1.886, 0.822), (-1.879, 0.852)]
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
        rbox(P, Vector((RAMP_X0 - 0.030, y, ramp_z(RAMP_X0 - 0.030) + 0.035)), (0.05, 0.05, 0.07), 0.004, AL)
        hexnut(P, Vector((RAMP_X0 - 0.030, y, ramp_z(RAMP_X0 - 0.030) + 0.070)), Z, 0.013, 0.0, 0.007, SS)
    return P.finish(smooth_angle=42.0)


# =============================================================================================== big hardware
def cone_nozzle_profile(rt, re, L, r_bore, r_body, r_flange, conv_len, lip=0.0045, wall=0.0045, flange_t=0.008,
                        jacket=None, half=DIV_HALF):
    """Closed (r, t) profile of a converging / 15-degree conical diverging nozzle: t = 0 at the inlet flange face,
    t = L at the exit plane. Returns (profile, keys) with keys 'in' (gold-lined bore), 'lip', 'out' (shell)."""
    ldiv = (re - rt) / math.tan(half)
    tt = L - ldiv                                      # throat station
    jacket = jacket or (rt + 2.6 * wall)
    prof, keys = [], []

    def add(p, k):
        prof.append(p); keys.append(k)

    add((r_bore, 0.0), 'out')
    add((r_bore, tt - conv_len), 'in')
    for k in range(1, 4):                              # convergent (sine blend) into the throat
        f = k / 3.0
        add((r_bore + (rt - r_bore) * math.sin(0.5 * math.pi * f), tt - conv_len * (1 - f)), 'in')
    rr = 0.6 * rt                                      # downstream throat round-off, then the straight cone
    add((rt + rr * (1 - math.cos(half * 0.5)), tt + rr * math.sin(half * 0.5)), 'in')
    t1 = tt + rr * math.sin(half)
    add((rt + rr * (1 - math.cos(half)), t1), 'in')
    for f in (0.5, 1.0):
        t = t1 + (L - t1) * f
        add((rt + rr * (1 - math.cos(half)) + (t - t1) * math.tan(half), t), 'in')
    prof[-1] = (re, L)
    add((re + 0.35 * lip, L + 0.0012), 'lip')
    add((re + 0.75 * lip, L + 0.0012), 'lip')
    add((re + lip, L - 0.0005), 'out')
    add((re + wall, L - 0.006), 'out')
    tj = tt + 0.30 * ldiv
    add((max(jacket, rt + (tj - tt) * math.tan(half) + wall), tj), 'out')
    add((jacket, tt - 0.004), 'out')
    add((r_body, tt - 0.6 * conv_len), 'out')
    add((r_body, flange_t + 0.003), 'out')
    add((r_flange - 0.002, flange_t), 'out')
    add((r_flange, flange_t - 0.002), 'out')
    add((r_flange, 0.0015), 'out')
    add((r_flange - 0.0015, 0.0), 'out')
    return prof, keys


def flange_pair(P, at, axis, r_pipe, mats, nb=8, th=0.016, ref=None):
    """Bolted hub-flange joint on a pipe at `at` (two weld-neck flanges, studs + nuts)."""
    ax = Vector(axis).normalized()
    rf = r_pipe * 1.75 + 0.014
    for sg in (-1, 1):
        prof = [(r_pipe - 0.001, sg * 0.001), (rf, sg * 0.001), (rf, sg * th), (r_pipe + 0.012, sg * (th + 0.010)),
                (r_pipe + 0.004, sg * (th + 0.022)), (r_pipe - 0.001, sg * (th + 0.022))]
        if sg < 0:
            prof = list(reversed(prof))
        lathe(P, prof, at, ax, 24 if r_pipe > 0.02 else 16, mats['fit'], closed=True, ref=ref)
    u, v = perp(ax, ref)
    rb = rf - 0.012
    for k in range(nb):
        a = TAU * (k + 0.5) / nb
        c = Vector(at) + (u * math.cos(a) + v * math.sin(a)) * rb
        for sg in (-1, 1):
            hexnut(P, c, ax * sg, 0.0145 if r_pipe > 0.02 else 0.010, th, th + 0.008 + (0.004 if sg > 0 else 0.0), mats['bolt'])


def handwheel(P, c, up, R, mats):
    """Manual override handwheel: rim, three spokes, hub."""
    up = Vector(up).normalized()
    u, v = perp(up)
    rim = [(R + 0.007 * math.cos(TAU * k / 6), 0.007 * math.sin(TAU * k / 6)) for k in range(6)]
    lathe(P, rim, c, up, 28, mats['wheel'], closed=True)
    for k in range(3):
        a = TAU * k / 3
        tube(P, [c, c + (u * math.cos(a) + v * math.sin(a)) * R], 0.0045, mats['wheel'], segs=8, bend=0.0)
    cyl(P, c, up, 0.016, -0.010, 0.012, mats['bolt'], segs=16, ch=0.002)


def ball_valve(P, c, axis, bore, mats, up=Vector((0, 0, 1)), length=0.22, motor_dir=None):
    """Isolation valve: trunnion ball valve (bore 75-80 mm, 690 bar class), flanged ends, electric actuator
    (gearbox + motor + position indicator) and a manual override handwheel. Axis = flow direction."""
    ax = Vector(axis).normalized()
    up = Vector(up).normalized()
    side = up.cross(ax).normalized()
    rb = bore * 0.62 + 0.035
    rf = bore * 0.62 + 0.042
    h = length / 2
    prof = [(bore / 2, -h), (rf, -h), (rf, -h + 0.026), (rf - 0.020, -h + 0.034), (rb * 0.93, -0.055), (rb, -0.028),
            (rb, 0.028), (rb * 0.93, 0.055), (rf - 0.020, h - 0.034), (rf, h - 0.026), (rf, h), (bore / 2, h)]
    lathe(P, prof, c, ax, 36, mats['body'], closed=True, ref=up)
    u, v = perp(ax, up)
    for sg in (-1, 1):                                    # flange studs
        for k in range(6):
            a = TAU * (k + 0.5) / 6
            p = c + ax * (sg * (h - 0.013)) + (u * math.cos(a) + v * math.sin(a)) * (rf - 0.011)
            hexnut(P, p - ax * (sg * 0.013), ax * sg, 0.016, 0.026, 0.034, mats['bolt'])
    # body joint bolts and trunnion bosses
    for k in range(8):
        a = TAU * (k + 0.5) / 8
        hexnut(P, c + (u * math.cos(a) + v * math.sin(a)) * (rb - 0.004), ax, 0.012, -0.004, 0.004, mats['bolt'])
    cyl(P, c, -up, 0.032, rb - 0.010, rb + 0.016, mats['body'], segs=24, ch=0.003)
    # stem bonnet, gearbox, motor, indicator, handwheel
    cyl(P, c, up, 0.034, rb - 0.010, rb + 0.030, mats['body'], segs=24, ch=0.003)
    gb = c + up * (rb + 0.030 + 0.045)
    rot = Matrix((ax, side, up)).transposed().to_4x4()
    rbox(P, gb, (0.150, 0.120, 0.090), 0.010, mats['act'], rot=rot, n=2)
    md = Vector(motor_dir).normalized() if motor_dir is not None else side
    reach = 0.060 if abs(md.dot(side)) > 0.9 else 0.045
    cyl(P, gb + md * reach, md, 0.038, 0.0, 0.090, mats['act'], segs=28, ch=0.004)
    cyl(P, gb + md * (reach + 0.090), md, 0.030, 0.0, 0.012, mats['body'], segs=24, ch=0.002)
    connector(P, gb + md * (reach + 0.060) + up * 0.038, up, {'metal': mats['body'], 'plug': mats['plug']}, r=0.009, L=0.018)
    lathe(P, [(0, 0.0), (0.020, 0.0), (0.020, 0.010), (0.014, 0.020), (0, 0.022)], gb - side * 0.035 + up * 0.045, up, 16, mats['ind'])
    cyl(P, gb + side * 0.025 + up * 0.045, up, 0.008, 0.0, 0.040, mats['bolt'], segs=12)
    handwheel(P, gb + side * 0.025 + up * 0.070, up, 0.058, mats)


def dome_regulator(P, c, axis, out_dir, mats, pilot_dir=None):
    """Dome-loaded high-flow pressure regulator (Cv ~ 225-365), laid along `axis` (inlet end -> loading dome):
    forged body with the inlet on its end face and the outlet port radial along `out_dir`, bolted loading dome,
    electronic pilot (solenoid pilot regulator + dome gauge) on a bracket along `pilot_dir`. Returns the pilot point."""
    w = Vector(axis).normalized()
    u = Vector(out_dir); u = (u - w * u.dot(w)).normalized()
    pd = Vector(pilot_dir) if pilot_dir is not None else -u
    pd = (pd - w * pd.dot(w)).normalized()
    body = [(0, -0.100), (0.082, -0.100), (0.090, -0.092), (0.092, -0.060), (0.092, 0.050), (0.097, 0.060), (0.124, 0.064),
            (0.124, 0.084), (0.120, 0.088), (0, 0.088)]
    lathe(P, body, c, w, 48, mats['body'], ref=u)
    dome = [(0, 0.088), (0.117, 0.088), (0.117, 0.100), (0.108, 0.128), (0.090, 0.156), (0.062, 0.176), (0.030, 0.186), (0, 0.188)]
    lathe(P, dome, c, w, 48, mats['dome'], ref=u)
    v = w.cross(u)
    for k in range(12):
        a = TAU * (k + 0.5) / 12
        hexnut(P, c + w * 0.084 + (u * math.cos(a) + v * math.sin(a)) * 0.110, w, 0.014, 0.004, 0.013, mats['bolt'])
    cyl(P, c + w * 0.186, w, 0.011, 0.0, 0.020, mats['bolt'], segs=16, ch=0.001)
    cyl(P, c, -w, 0.060, 0.090, 0.124, mats['body'], segs=32, ch=0.004)             # inlet hub (end face)
    cyl(P, c - w * 0.020, u, 0.060, 0.070, 0.124, mats['body'], segs=32, ch=0.004)   # outlet hub (radial)
    # electronic pilot on a bracket beside the body
    pr = c + pd * 0.150 + w * 0.010 - Vector((0, 0, 0.050))
    rbox(P, c + pd * 0.105 + w * 0.010 - Vector((0, 0, 0.030)), (0.040, 0.050, 0.050), 0.004, mats['dome'],
         rot=Matrix((w, pd, w.cross(pd))).transposed().to_4x4())
    up = w.cross(pd).normalized()
    if up.z < 0:
        up = -up
    lathe(P, [(0, -0.035), (0.024, -0.035), (0.026, -0.030), (0.026, 0.020), (0.032, 0.024), (0.032, 0.034), (0.014, 0.046),
              (0.014, 0.060), (0, 0.060)], pr, up, 24, mats['dome'])
    rbox(P, pr + up * 0.078, (0.040, 0.046, 0.034), 0.005, mats['act'])
    connector(P, pr + up * 0.095, pd, {'metal': mats['dome'], 'plug': mats['plug']}, r=0.0075, L=0.015)
    tube(P, [pr - pd * 0.026 + up * 0.010, c + pd * 0.090 + w * 0.120, c + w * 0.206], 0.005, mats['bolt'], segs=8, bend=0.02)
    g = pr + w * 0.045
    cyl(P, pr + w * 0.020, w, 0.005, 0.0, 0.020, mats['bolt'], segs=10)
    lathe(P, [(0, 0.0), (0.030, 0.0), (0.032, 0.004), (0.032, 0.020), (0.030, 0.022), (0, 0.022)], g, w, 28, mats['act'])
    cyl(P, g + w * 0.022, w, 0.028, 0.0, 0.0006, mats['dome'], segs=28)
    return pr


def header_block(P, c, size, mats):
    rbox(P, c, size, 0.010, mats['body'], n=2)


def port(P, at, axis, r_pipe, mats):
    """Forged port boss + weld-neck flange on a block face."""
    ax = Vector(axis).normalized()
    cyl(P, at, ax, r_pipe + 0.012, -0.004, 0.012, mats['body'], segs=28 if r_pipe > 0.02 else 18, ch=0.002)


# =============================================================================================== rear thrusters
def rear_thrusters(M):
    P = Part("Thrusters_Rear", M, ["Titanium", "Gold", "BlackGloss", "Aluminum", "Stainless", "Rubber"])
    TI, AU, BK, AL, SS, RU = (P.m(k) for k in ("Titanium", "Gold", "BlackGloss", "Aluminum", "Stainless", "Rubber"))
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Z = Vector((0, 0, 1))
    ax = Vector((-1, 0, 0))
    x_in = NOZ_EXIT_X + NOZ_L                       # -2.030 inlet flange face
    fm = {'fit': SS, 'rubber': RU, 'band': SS, 'bolt': SS}
    loom_x = MAN_X[1] + 0.035
    for y in NOZ_Y:
        o = Vector((x_in, y, NOZ_Z))
        prof, keys = cone_nozzle_profile(NOZ_RT, NOZ_RE, NOZ_L, r_bore=0.0245, r_body=0.033, r_flange=0.0435, conv_len=0.022,
                                         lip=0.0050, wall=0.0045, jacket=0.028)
        mats = [{'in': AU, 'out': TI, 'lip': TI}[keys[(i + 1) % len(keys)]] for i in range(len(prof))]
        lathe(P, prof, o, ax, 36, mats, closed=True)
        for k in range(6):                                  # flange bolts
            a = TAU * (k + 0.5) / 6
            hexnut(P, o + ax * 0.008 + (Y * math.cos(a) + Z * math.sin(a)) * 0.0380, ax, 0.0075, 0.0, 0.0045, AL)
        # black clamp ring on the shelf face with two bolted ears
        t_clamp = x_in - (SHELF_X - 0.0005)
        ring(P, o, ax, 0.0285, 0.0440, t_clamp, t_clamp + 0.0075, BK, segs=40, ch=0.0012)
        for sgn in (1, -1):
            ec = Vector((SHELF_X - 0.0045, y + sgn * 0.050, NOZ_Z))
            rbox(P, ec, (0.0075, 0.026, 0.020), 0.004, BK)
            hexnut(P, ec + ax * 0.0037 + Y * sgn * 0.004, ax, 0.010, 0.0, 0.006, AL)
        # fast-acting electrically actuated valve (40 mm poppet): body block, outlet flange, torque-motor actuator
        vb = Vector((x_in + 0.055, y, NOZ_Z))
        rbox(P, vb, (0.110, 0.092, 0.076), 0.008, BK, n=2)
        cyl(P, o, X, 0.0425, 0.0005, 0.007, BK, segs=32, ch=0.001)
        act = vb + Vector((0.010, 0, 0.038))
        lathe(P, [(0, 0.0), (0.030, 0.0), (0.030, 0.012), (0.027, 0.015), (0.027, 0.064), (0.029, 0.067), (0.029, 0.080),
                  (0.025, 0.085), (0.010, 0.086), (0, 0.086)], act, Z, 32, [BK, BK, BK, AL, AL, AL, AL, AL, AL])
        connector(P, act + Vector((0.027, 0, 0.050)), X, {'metal': AL, 'plug': BK}, r=0.0085, L=0.018)
        tube(P, [act + Vector((0.048, 0, 0.050)), act + Vector((0.075, 0, 0.056)), Vector((loom_x - 0.010, y, BOX_TOP + 0.050)),
                 Vector((loom_x, y, BOX_TOP + 0.052))], 0.0035, RU, segs=8, bend=0.015)
        # inlet: boss + bolted 60 mm hub flange + feed pipe into the manifold block
        pb = Vector((x_in + 0.110, y, NOZ_Z))
        cyl(P, pb, X, R_FEED_REAR + 0.010, 0.0, 0.006, BK, segs=28, ch=0.0015)
        tube(P, [pb + X * 0.004, Vector((MAN_X[0] + 0.004, y, NOZ_Z))], R_FEED_REAR, SS, segs=24, bend=0.0)
        nut_pair(P, pb + X * 0.008, X, R_FEED_REAR, fm, af=0.072, L=0.022)
    # harness loom across the valve bay
    tube(P, [Vector((loom_x, -0.40, BOX_TOP + 0.052)), Vector((loom_x, 0.40, BOX_TOP + 0.052)),
             Vector((loom_x + 0.050, 0.47, BOX_TOP + 0.080))], 0.0075, RU, segs=12, bend=0.03)
    # feed manifold: forged rectangular block, 4 outlet hubs (rear face), inlet hub (front face, from the LP block)
    mc = Vector(((MAN_X[0] + MAN_X[1]) / 2, 0.0, (MAN_Z[0] + MAN_Z[1]) / 2))
    rbox(P, mc, (MAN_X[1] - MAN_X[0], 0.840, MAN_Z[1] - MAN_Z[0]), 0.012, SS, n=2)
    for sg in (1, -1):                                    # end caps
        cap = mc + Y * sg * 0.420
        cyl(P, cap, Y * sg, 0.040, 0.0, 0.010, SS, segs=24, ch=0.002)
        for k in range(4):
            a = TAU * (k + 0.5) / 4
            hexnut(P, cap + (X * math.cos(a) + Z * math.sin(a)) * 0.030, Y * sg, 0.012, 0.010, 0.017, SS)
    for y in NOZ_Y:
        cyl(P, Vector((MAN_X[0], y, NOZ_Z)), ax, R_FEED_REAR + 0.008, -0.002, 0.008, SS, segs=24, ch=0.0015)
    cyl(P, MAN_IN, Z, R_HP + 0.010, -0.002, 0.012, SS, segs=28, ch=0.002)       # inlet hub (from the LP block)
    cyl(P, mc + Z * 0.050 + Y * 0.20, Z, 0.010, 0.0, 0.014, SS, segs=16, ch=0.001)        # pressure tap
    hexnut(P, mc + Z * 0.064 + Y * 0.20, Z, 0.020, 0.0, 0.010, SS)
    cyl(P, mc + Z * 0.074 + Y * 0.20, Z, 0.0095, 0.0, 0.032, AL, segs=16, ch=0.001)
    connector(P, mc + Z * 0.106 + Y * 0.20, Z, {'metal': AL, 'plug': BK}, r=0.008, L=0.016)
    for y in (-0.235, 0.0, 0.235):                         # feet to the duct roof
        rbox(P, Vector((mc.x, y, (MAN_Z[0] + DUCT_CEIL_Z + 0.0025) / 2)), (0.060, 0.030, MAN_Z[0] - DUCT_CEIL_Z - 0.0025 + 0.004), 0.002, AL)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== corner lift / yaw
def lift_stack(P, x, y, z_exit, z_ring0, z_ring1, z_collar, z_valve0, z_valve1, mats, collar=True):
    """Vertical lift thruster (exhaust -Z): 15-deg conical nozzle, stacked chrome/brass valve body (poppet + voice-coil
    actuator rings), stem, collar, solenoid pilot valve on top with the feed inlet."""
    TI, AU, CR, BK, SS, AL = (mats[k] for k in ('ti', 'au', 'cr', 'bk', 'ss', 'al'))
    o = Vector((x, y, z_exit))
    up = Vector((0, 0, 1))
    Ln = z_ring0 - z_exit
    ldiv = (LIFT_RE - LIFT_RT) / math.tan(DIV_HALF)
    prof, km = [], []

    def add(r, t, m):
        prof.append((r, t)); km.append(m)

    # inside: chamber pole -> convergent -> throat -> 15 deg cone to the exit (t measured up from the exit plane)
    add(0.0, Ln - 0.010, AU)
    add(0.0200, Ln - 0.010, AU)
    add(0.0200, ldiv + 0.020, AU)
    add(LIFT_RT + 0.0035, ldiv + 0.006, AU)
    add(LIFT_RT, ldiv, AU)
    add(LIFT_RT + 0.5 * ldiv * math.tan(DIV_HALF), 0.5 * ldiv, AU)
    add(LIFT_RE, 0.0, AU)
    add(LIFT_RE + 0.0016, -0.0010, TI)
    add(LIFT_RE + 0.0036, -0.0010, TI)
    add(LIFT_RE + 0.0045, 0.0008, TI)
    add(LIFT_RE + 0.0042, 0.010, TI)
    add(LIFT_RT + 0.0105, ldiv * 0.75, TI)
    add(0.0255, ldiv + 0.004, TI)
    add(0.0255, Ln - 0.012, TI)
    add(0.0330, Ln - 0.004, TI)
    add(0.0340, Ln, CR)
    # stacked valve body: alternating chrome / brass rings with grooves
    nring = 4
    h = (z_ring1 - z_ring0) / nring
    for k in range(nring):
        t0 = Ln + k * h
        m = CR if k % 2 == 0 else AU
        rr = 0.0390 if k % 2 == 0 else 0.0372
        add(rr, t0 + 0.0025, m)
        add(rr, t0 + h - 0.0025, m)
        add(0.0335, t0 + h, CR)
    tz = z_ring1 - z_exit
    add(0.018, tz + 0.002, SS)
    add(0.0160, tz + 0.006, SS)
    if collar:
        tc = z_collar - z_exit
        add(0.0160, tc - 0.004, SS)
        add(0.0300, tc - 0.002, CR)
        add(0.0315, tc, CR)
        add(0.0315, tc + 0.014, CR)
        add(0.0295, tc + 0.016, CR)
        add(0.0145, tc + 0.017, SS)
    tv0, tv1 = z_valve0 - z_exit, z_valve1 - z_exit
    add(0.0145, tv0 - 0.010, SS)
    add(0.0260, tv0 - 0.006, BK)
    add(0.0390, tv0 - 0.002, BK)
    add(0.0420, tv0 + 0.004, BK)
    add(0.0420, tv1 - 0.012, BK)
    add(0.0380, tv1 - 0.008, AL)
    add(0.0380, tv1 - 0.002, AL)
    add(0.0330, tv1, AL)
    add(0.0, tv1, AL)
    lathe(P, prof, o, up, 20, [km[i + 1] for i in range(len(prof) - 1)])
    zc = (z_valve0 + z_valve1) / 2
    connector(P, Vector((x + 0.040, y, zc)), Vector((1, 0, 0)), {'metal': AL, 'plug': BK}, r=0.0075, L=0.017)
    return Vector((x, y, z_valve1))


def yaw_exit_y():
    """Body-side surface |y| at the yaw aperture (between design rows 2 and 3 of roadster_body)."""
    try:
        import roadster_body as RB
        y2, z2 = RB.row_at(2, YAW['x']); y3, z3 = RB.row_at(3, YAW['x'])
        t = (YAW['z'] - z2) / (z3 - z2)
        return y2 + (y3 - y2) * t, Vector((0, z3 - z2, -(y3 - y2))).normalized()
    except Exception:
        return 0.919, Vector((0, 1, 0))


def yaw_thruster(P, s, mats):
    """Lateral attitude thruster at the front corner: exhausts along s*Y through a flush aperture in the body side."""
    TI, AU, BK, SS, AL = (mats[k] for k in ('ti', 'au', 'bk', 'ss', 'al'))
    ys, nrm = yaw_exit_y()
    nrm = Vector((0, s * abs(nrm.y), nrm.z))
    ax = Vector((0, s, 0))
    L = 0.090
    y_exit = ys - 0.002
    o = Vector((YAW['x'], s * (y_exit - L), YAW['z']))
    prof, keys = cone_nozzle_profile(YAW_RT, YAW_RE, L, r_bore=0.0115, r_body=0.0190, r_flange=0.0285, conv_len=0.014,
                                     lip=0.0030, wall=0.0030, jacket=0.0135, flange_t=0.006)
    lathe(P, prof, o, ax, 20, [{'in': AU, 'out': TI, 'lip': TI}[keys[(i + 1) % len(keys)]] for i in range(len(prof))], closed=True)
    # flush titanium bezel in the body side (aligned with the local body facet)
    ex = Vector((YAW['x'], s * ys, YAW['z']))
    ring(P, ex - nrm * 0.0035, nrm, YAW_RE + 0.0035, 0.029, 0.0, 0.0045, TI, segs=32, ch=0.0008)
    # valve block, actuator, connector, inlet hub flange
    vb = o - ax * 0.036
    rbox(P, vb, (0.056, 0.072, 0.056), 0.006, BK, n=2)
    cyl(P, o - ax * 0.001, ax, 0.030, 0.0, 0.004, BK, segs=24, ch=0.0008)
    act = vb + Vector((0, 0, 0.028))
    lathe(P, [(0, 0.0), (0.021, 0.0), (0.021, 0.008), (0.019, 0.010), (0.019, 0.046), (0.020, 0.048), (0.020, 0.056),
              (0.017, 0.060), (0.0, 0.060)], act, Vector((0, 0, 1)), 24, [BK, BK, BK, AL, AL, AL, AL, AL])
    connector(P, act + Vector((-0.019, 0, 0.034)), Vector((-1, 0, 0)), {'metal': AL, 'plug': BK}, r=0.0065, L=0.015)
    port_c = vb - ax * 0.036
    cyl(P, port_c, -ax, R_FEED_YAW + 0.006, -0.002, 0.004, BK, segs=20, ch=0.001)
    flange_pair(P, port_c - ax * 0.012, -ax, R_FEED_YAW, {'fit': SS, 'bolt': SS}, nb=4, th=0.007)
    return port_c - ax * 0.004


def rocket_thrusters(M):
    P = Part("Thrusters_Rocket", M, ["Titanium", "Gold", "Chrome", "BlackGloss", "Stainless", "Aluminum", "Rubber"])
    mats = dict(ti=P.m("Titanium"), au=P.m("Gold"), cr=P.m("Chrome"), bk=P.m("BlackGloss"), ss=P.m("Stainless"),
                al=P.m("Aluminum"), ru=P.m("Rubber"))
    SS, RU, AL, BK = mats['ss'], mats['ru'], mats['al'], mats['bk']
    fm = {'fit': SS, 'rubber': RU, 'band': SS, 'bolt': SS}
    RL, RY = R_FEED_LIFT, R_FEED_YAW
    X = Vector((1, 0, 0)); Zv = Vector((0, 0, 1))
    xc, zc = LIFT_X, LIFT_Z                       # lift distribution crossing, just ahead of the regulator
    t0 = Vector((xc, D1.y, zc))
    e3 = Vector((xc, D1.y, LIFT_PORT_Z))
    tube(P, [Vector((D1.x + 0.082, D1.y, LIFT_PORT_Z)), e3 - X * 0.032], RL, SS, bend=0.0)
    rbox(P, e3, (0.064, 0.064, 0.064), 0.006, SS)
    tube(P, [e3 - Zv * 0.032, t0 + Zv * 0.032], RL, SS, bend=0.0)
    rbox(P, t0, (0.064, 0.064, 0.064), 0.006, SS)
    for s in (1, -1):
        tee = Vector((xc, s * 0.560, zc))
        tube(P, [t0 + Vector((0, s * 0.032, 0)), tee - Vector((0, s * 0.032, 0))], RL, SS, bend=0.0)
        rbox(P, tee, (0.064, 0.064, 0.064), 0.006, SS)
        # ---- rear corner lift stack, valve fed from the top through the rear-face pocket
        rx, ry = LIFT_REAR['x'], s * LIFT_REAR['y']
        top_r = lift_stack(P, rx, ry, LIFT_REAR['z_exit'], 0.295, 0.380, 0.490, 0.655, 0.735, mats, collar=True)
        rbox(P, Vector((rx + 0.004, s * 0.6955, 0.470)), (0.030, 0.019, 0.008), 0.002, AL)      # tab to the flank
        ring(P, Vector((rx, ry, 0.0)), Zv, 0.0150, 0.026, 0.462, 0.474, AL, segs=24, ch=0.001)
        tube(P, [tee - X * 0.032, Vector((xc - 0.060, s * 0.560, zc)), Vector((xc - 0.110, s * 0.560, 0.800)), Vector((-1.930, s * 0.560, 0.800)),
                 Vector((-1.975, s * 0.700, 0.800)), Vector((rx, ry, 0.800)), top_r + Zv * 0.030], RL, SS, bend=0.05)
        cyl(P, top_r, Zv, RL + 0.008, -0.002, 0.008, AL, segs=24, ch=0.0015)
        flange_pair(P, top_r + Zv * 0.020, Zv, RL, fm, nb=4, th=0.008)
        # ---- front corner lift stack (behind the front wheel, outboard of the cabin side panel)
        fx, fy = LIFT_FRONT['x'], s * LIFT_FRONT['y']
        top_f = lift_stack(P, fx, fy, LIFT_FRONT['z_exit'], 0.205, 0.285, 0.300, 0.370, 0.450, mats, collar=True)
        rbox(P, Vector((fx, s * (LIFT_FRONT['y'] + 0.034), 0.335)), (0.050, 0.020, 0.008), 0.002, AL)
        # ---- yaw thruster
        yin = yaw_thruster(P, s, mats)
        # ---- sill line: tee -> forward above the trunk vessel -> over the HP row -> down behind the bulkhead
        #      (outboard of the vessel ends) -> rocker -> front tee
        sill_y, sill_z = s * 0.905, 0.210
        pts = [tee + X * 0.032, Vector((-1.100, s * 0.560, zc)), Vector((-1.060, s * 0.560, 0.920)), Vector((-0.905, s * 0.560, 0.920)),
               Vector((-0.900, s * 0.780, 0.920)), Vector((-0.900, s * 0.780, sill_z)), Vector((-0.840, sill_y, sill_z)),
               Vector((0.895 - 0.032, sill_y, sill_z))]
        tube(P, pts, RL, SS, bend=0.07)
        for xcl in (-0.40, 0.40):
            p_clip(P, Vector((xcl, sill_y, sill_z)), X, RL, Vector((0, s * 0.3, -1)), fm)
        p_clip(P, Vector((-0.900, s * 0.780, 0.55)), Zv, RL, Vector((-1, 0, 0)), fm)
        tee2 = Vector((0.895, sill_y, sill_z))
        rbox(P, tee2, (0.064, 0.064, 0.064), 0.006, SS)
        tube(P, [tee2 + Zv * 0.032, Vector((0.895, sill_y, 0.505)), Vector((fx, fy, 0.505)), top_f + Zv * 0.030], RL, SS, bend=0.045)
        cyl(P, top_f, Zv, RL + 0.006, -0.002, 0.008, AL, segs=24, ch=0.0015)
        nut_pair(P, top_f + Zv * 0.008, Zv, RL, fm, af=0.062, L=0.020)
        # yaw branch: down, inboard under the cabin floor, forward inboard of the front wishbone pivots, out to the valve
        yb = [tee2 - Zv * 0.032, Vector((0.895, sill_y, 0.128)), Vector((0.930, s * 0.250, 0.128)),
              Vector((1.880, s * 0.250, 0.128)), Vector((1.935, s * 0.640, YAW['z'])), yin]
        tube(P, yb, RY, SS, bend=0.045)
        for xcl in (1.20, 1.70):
            p_clip(P, Vector((xcl, s * 0.250, 0.128)), X, RY, Vector((0, 0, 1)), fm)
        # EM valve harness stubs
        tube(P, [Vector((rx + 0.057, ry, 0.695)), Vector((rx + 0.085, ry, 0.705)), Vector((rx + 0.10, ry - s * 0.07, 0.76))], 0.0032, RU, segs=8, bend=0.02)
        tube(P, [Vector((fx - 0.057, fy, 0.410)), Vector((fx - 0.080, fy, 0.420)), Vector((fx - 0.085, fy, 0.53))], 0.0032, RU, segs=8, bend=0.02)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== COPVs
def copv_vessel(P, c, axis, r_in, wall, L_in, boss_r, mats, segs=72, ref=None, blind=(False, False)):
    """Carbon-overwrapped pressure vessel: hemispherical liner domes, polar build-up, hoop-layer band with drop-offs,
    aluminium polar bosses (blind = low-profile closed boss). Returns (boss end points [-, +], r_out_hoop, hc)."""
    FIL, AL = mats['fil'], mats['al']
    r_out = r_in + wall
    hc = L_in / 2 - r_in
    hoop = 0.0028
    prof = []
    n_d = 10
    for k in range(n_d + 1):
        a = math.radians(90.0) * (1 - k / n_d)
        rr = r_out * math.cos(a)
        if rr < boss_r * 1.45:
            continue
        build = 0.010 * smoothstep(0.55, 0.95, math.sin(a))
        prof.append((rr + build * math.cos(a) * 0.4, -hc - (r_out + build) * math.sin(a)))
    pole_t = prof[0][1]
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
    ax = Vector(axis).normalized()
    ends = []
    for i, sgn in enumerate((-1, 1)):
        a2 = ax * sgn
        t_base = abs(pole_t) - 0.012
        bp = [(0.0, t_base), (r_open + 0.006, t_base), (r_open + 0.006, t_base + 0.010), (boss_r + 0.004, t_base + 0.014),
              (boss_r, t_base + 0.018)]
        if blind[i]:
            bp += [(boss_r - 0.004, t_base + 0.024), (0.0, t_base + 0.025)]
            tt = t_base + 0.012
        else:
            tt = t_base + 0.020
            for k in range(4):
                bp += [(boss_r, tt), (boss_r - 0.0025, tt + 0.003), (boss_r, tt + 0.006)]
                tt += 0.006
            bp += [(boss_r, tt + 0.010), (boss_r - 0.003, tt + 0.013), (0.0, tt + 0.013)]
        lathe(P, bp, c, a2, 32, AL, ref=ref)
        ends.append(c + a2 * (tt + 0.013))
    return ends, r_out + hoop, hc


def strap(P, c, axis, r, at, mats, foot_dir, width=0.050, ref=None):
    """Titanium strap with rubber liner around a vessel, clamp tabs + tension bolt on top, feet bolted below."""
    ax = Vector(axis).normalized()
    lathe(P, [(r + 0.0005, -width / 2 - 0.003), (r + 0.0040, -width / 2 - 0.003), (r + 0.0040, -width / 2), (r + 0.0070, -width / 2 + 0.0012),
              (r + 0.0070, width / 2 - 0.0012), (r + 0.0040, width / 2), (r + 0.0040, width / 2 + 0.003), (r + 0.0005, width / 2 + 0.003)],
          c + ax * at, ax, 48, [mats['ru'], mats['band'], mats['band'], mats['band'], mats['band'], mats['ru'], mats['ru'], mats['ru']],
          closed=True, ref=ref)
    fd = Vector(foot_dir).normalized()
    up = -fd
    side = ax.cross(up).normalized()
    rot = Matrix((side, ax, up)).transposed().to_4x4()
    for sg in (1, -1):
        rbox(P, c + ax * at + up * (r + 0.020) + side * sg * 0.011, (0.006, width * 0.8, 0.032), 0.0015, mats['band'], rot=rot)
    tube(P, [c + ax * at + up * (r + 0.026) + side * 0.030, c + ax * at + up * (r + 0.026) - side * 0.030], 0.0045, mats['bolt'], segs=10, bend=0.0)
    hexnut(P, c + ax * at + up * (r + 0.026) + side * 0.030, side, 0.013, 0.0, 0.007, mats['bolt'])
    hexnut(P, c + ax * at + up * (r + 0.026) - side * 0.030, -side, 0.013, 0.0, 0.007, mats['bolt'])


def saddle(P, c, axis, r, at, mats, floor_z, width=0.07, half_deg=50.0):
    """Cradle under a horizontal vessel: arc pad with rubber liner, web, base plate bolted to the floor / rail."""
    ax = Vector(axis).normalized()
    dn = Vector((0, 0, -1))
    side = ax.cross(-dn).normalized()
    pts, nrm = [], []
    for k in range(13):
        a = math.radians(-half_deg + 2 * half_deg * k / 12)
        d = dn * math.cos(a) + side * math.sin(a)
        pts.append(c + ax * at + d * (r + 0.0105))
        nrm.append(d)
    sheet(P, [p - ax * (width / 2) for p in pts], nrm, ax, [(0.0, width)] * len(pts), 0.010, mats['bracket'], side='out')
    sheet(P, [c + ax * (at - width / 2 + 0.004) + n * (r + 0.0005) for n in nrm], nrm, ax, [(0.0, width - 0.008)] * len(nrm), 0.010, mats['ru'], side='out')
    low = c + ax * at + dn * (r + 0.02)
    h = max(0.01, low.z - floor_z)
    web_w = (r + 0.01) * 2 * math.sin(math.radians(half_deg))
    sz = (width * 0.6, web_w, h) if abs(ax.x) > 0.5 else (web_w, width * 0.6, h)
    rbox(P, Vector((low.x, low.y, floor_z + 0.008 + h / 2)), sz, 0.004, mats['bracket'])
    bsz = (width + 0.03, web_w + 0.02, 0.008) if abs(ax.x) > 0.5 else (web_w + 0.02, width + 0.03, 0.008)
    rbox(P, Vector((low.x, low.y, floor_z + 0.004)), bsz, 0.002, mats['bracket'])
    for sg in (1, -1):
        off = Vector((0, sg * (web_w / 2 - 0.012), 0)) if abs(ax.x) > 0.5 else Vector((sg * (web_w / 2 - 0.012), 0, 0))
        hexnut(P, Vector((low.x, low.y, floor_z + 0.008)) + off + ax * 0.025, Vector((0, 0, 1)), 0.016, 0.0, 0.008, mats['bolt'])


def on_tank_valve(P, port_pt, axis, mats, out_dir, depth=0.050, solenoid=True):
    """On-tank valve (OTV): forged block on the polar boss - solenoid shut-off, manual valve, TPRD, outlet hub.
    The outlet points along out_dir (coaxial with the boss, straight up, or sideways); the other fittings take the
    remaining faces."""
    ax = Vector(axis).normalized()
    od = Vector(out_dir).normalized()
    top = Vector((0, 0, 1)) - ax * ax.z
    top.normalize()
    side = top.cross(ax).normalized()
    coax = abs(od.dot(ax)) > 0.9
    otop = abs(od.dot(top)) > 0.9
    sol_dir = side if otop else top                     # solenoid shut-off
    sd = -side if otop else (side if coax else -od)     # manual shut-off valve
    td = -top if otop else (-side if coax else -top)    # TPRD (thermally activated relief)
    c = port_pt + ax * (depth / 2)
    rot = Matrix((ax, side, top)).transposed().to_4x4()
    rbox(P, c, (depth, 0.100, 0.100), 0.008, mats['bk'], rot=rot, n=2)
    cyl(P, port_pt, ax, 0.048, -0.002, 0.004, mats['al'], segs=32, ch=0.001)
    if solenoid:
        lathe(P, [(0, 0), (0.023, 0), (0.023, 0.010), (0.021, 0.012), (0.021, 0.052), (0.0225, 0.054), (0.0225, 0.062), (0.019, 0.066), (0, 0.066)],
              c + sol_dir * 0.050, sol_dir, 24, [mats['bk']] * 3 + [mats['al']] * 5)
        cdir = ax if abs(sol_dir.dot(ax)) < 0.9 else top
        connector(P, c + sol_dir * 0.090, cdir, {'metal': mats['al'], 'plug': mats['bk']}, r=0.0075, L=0.016)
    sv = c + sd * 0.050
    cyl(P, sv, sd, 0.009, 0.0, 0.016, mats['ss'], segs=16)
    lathe(P, [(0, 0.016), (0.017, 0.016), (0.019, 0.020), (0.019, 0.032), (0.016, 0.036), (0, 0.036)], sv, sd, 18, mats['al'])
    tp = c + td * 0.050
    cyl(P, tp, td, 0.010, 0.0, 0.016, mats['au'], segs=16, ch=0.001)
    hexnut(P, tp, td, 0.022, 0.016, 0.026, mats['au'])
    pp = c + od * (depth / 2 if coax else 0.050)
    cyl(P, pp, od, R_HP + 0.010, -0.004, 0.010, mats['bk'], segs=28, ch=0.002)
    return pp + od * 0.010


def copv_mats(P):
    return dict(fil=P.m("Filament"), al=P.m("Aluminum"), band=P.m("Titanium"), ru=P.m("Rubber"), bk=P.m("BlackGloss"),
                bolt=P.m("Stainless"), ss=P.m("Stainless"), au=P.m("Gold"), bracket=P.m("Aluminum"), fit=P.m("Stainless"),
                body=P.m("Stainless"), act=P.m("BlackGloss"), plug=P.m("BlackGloss"), ind=P.m("LightRed"), wheel=P.m("BlackGloss"))


def copv_rocket(M):
    P = Part("COPV_Rocket", M, ["Filament", "Aluminum", "Stainless", "Rubber", "BlackGloss", "Gold", "Titanium", "LightRed"])
    mats = copv_mats(P)
    d = COPV_R
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Zv = Vector((0, 0, 1))
    ends, r_o, hc = copv_vessel(P, d['c'], X, d['r_in'], d['wall'], d['L_in'], d['boss_r'], mats, segs=48, blind=(False, True))
    for at in (-0.36, 0.36):
        strap(P, d['c'], X, r_o, at, mats, foot_dir=Vector((0, 0, -1)))
        saddle(P, d['c'], X, r_o, at, mats, 0.151, half_deg=40.0)
    # rear boss: on-tank valve, outlet up; front boss: blind with TPRD plug
    out = on_tank_valve(P, ends[0], -X, mats, Zv, depth=0.050)
    hexnut(P, ends[1], X, 0.040, 0.0, 0.008, mats['au'])
    # transfer line: OTV riser behind the vessel (cabin side) -> through the rear bulkhead high -> forged elbow E2 ->
    # isolation valve ISO2 (80 mm bore) -> HP header H1 (common header with the trunk vessel: crossfeed)
    y0 = d['c'].y
    e2 = Vector((ROW_X, E2_Y, TR_Z))
    tube(P, [out, Vector((out.x, out.y, TR_Z)), Vector((e2.x + 0.045, E2_Y, TR_Z))], R_HP, mats['ss'], bend=0.08)
    rbox(P, e2, (0.090, 0.090, 0.090), 0.010, mats['ss'])
    x_bh = -0.64 - (TR_Z - 0.15) * 0.24
    ring(P, Vector((x_bh, (out.y + E2_Y) / 2, TR_Z)), X, R_HP + 0.0005, R_HP + 0.022, -0.012, 0.012, mats['ru'], segs=32, ch=0.003)
    iso2 = Vector((ROW_X, ISO2_Y, TR_Z))
    tube(P, [e2 + Y * 0.045, iso2 - Y * 0.110], R_HP, mats['ss'], bend=0.0)
    ball_valve(P, iso2, Y, ISO_BORE[1], mats, up=-X, length=0.220, motor_dir=Zv)
    tube(P, [iso2 + Y * 0.110, Vector((ROW_X, H1_Y - 0.065, TR_Z))], R_HP, mats['ss'], bend=0.0)
    fmp = {'fit': mats['ss'], 'bolt': mats['ss'], 'rubber': mats['ru'], 'band': mats['ss']}
    flange_pair(P, Vector((ROW_X, (iso2.y + 0.110 + H1_Y - 0.065) / 2, TR_Z)), Y, R_HP, fmp, nb=8, th=0.012)
    # bracket ISO2 / E2 -> rear bulkhead
    for yy in (E2_Y, ISO2_Y):
        rbox(P, Vector(((ROW_X + 0.090 + x_bh) / 2 - 0.004, yy, TR_Z - 0.060)), (abs(x_bh - ROW_X) - 0.085, 0.040, 0.008), 0.002, mats['al'])
    return P.finish(smooth_angle=40.0)


def copv_trunk(M):
    P = Part("COPV_Trunk", M, ["Filament", "Aluminum", "Stainless", "Rubber", "BlackGloss", "Gold", "Titanium", "LightRed"])
    mats = copv_mats(P)
    d = COPV_T
    X = Vector((1, 0, 0)); Y = Vector((0, 1, 0)); Zv = Vector((0, 0, 1))
    ends, r_o, hc = copv_vessel(P, d['c'], Y, d['r_in'], d['wall'], d['L_in'], d['boss_r'], mats, segs=48,
                                ref=Vector((1, 0, 0)), blind=(True, False))
    c = d['c']
    for at in (-0.30, 0.30):
        strap(P, c, Y, r_o, at, mats, foot_dir=Vector((1, 0, 0)), ref=Vector((1, 0, 0)))      # clamp aft
        # strap feet: brackets from the band to the rear bulkhead (front) and to the floor rail (below)
        x_bh = -0.64 - (c.z - 0.15) * 0.24
        x0 = c.x + r_o + 0.007
        xb = -0.64 - (c.z + 0.045 - 0.15) * 0.24 - 0.002          # leaning bulkhead at the pad's top edge
        rbox(P, Vector(((x0 + xb) / 2 - 0.002, c.y + at, c.z)), (xb - x0 + 0.004, 0.060, 0.090), 0.003, mats['al'])
        rbox(P, Vector((c.x + 0.10, c.y + at, 0.158)), (0.140, 0.060, 0.012), 0.002, mats['al'])
        tube(P, [Vector((c.x + 0.10, c.y + at, 0.164)), Vector((c.x + 0.10, c.y + at, c.z - r_o * 0.92))], 0.010, mats['al'], segs=12, bend=0.0)
    out = on_tank_valve(P, ends[1], Y, mats, Zv, depth=0.050)
    hexnut(P, ends[0], -Y, 0.040, 0.0, 0.008, mats['au'])
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
    fm = {'fit': SS, 'rubber': RU, 'band': SS, 'bolt': SS}
    vm = dict(body=SS, bolt=SS, act=BK, plug=BK, ind=RD, wheel=BK, fit=SS, dome=AL)
    cm = {'metal': AL, 'plug': BK}
    R = lambda y, x=ROW_X: Vector((x, y, TR_Z))
    x_bh = -0.64 - (TR_Z - 0.15) * 0.24

    # ---- trunk vessel OTV riser -> forged elbow E1 -> isolation valve ISO1 (75 mm bore) -> header H1
    o = trunk_outlet if trunk_outlet is not None else Vector((ROW_X, 0.62, 0.48))
    e1 = Vector((o.x, o.y, TR_Z))
    rbox(P, e1, (0.090, 0.090, 0.090), 0.010, SS)
    tube(P, [o, e1 - Z * 0.045], R_HP, SS, bend=0.0)
    flange_pair(P, Vector((o.x, o.y, 0.700)), Z, R_HP, fm, nb=8, th=0.012)
    iso1 = R(ISO1_Y)
    tube(P, [e1 - Y * 0.045, Vector((e1.x, iso1.y + 0.180, TR_Z)), Vector((iso1.x, iso1.y + 0.130, TR_Z)), iso1 + Y * 0.110], R_HP, SS, bend=0.05)
    ball_valve(P, iso1, Y, ISO_BORE[0], vm, up=-X, length=0.220, motor_dir=Z)
    h1 = R(H1_Y)
    header_block(P, h1, (0.140, 0.130, 0.150), vm)
    tube(P, [iso1 - Y * 0.110, h1 + Y * 0.065], R_HP, SS, bend=0.0)
    flange_pair(P, (iso1 - Y * 0.110 + h1 + Y * 0.065) / 2, Y, R_HP, fm, nb=8, th=0.012)
    port(P, h1 + Y * 0.065, Y, R_HP, vm)
    port(P, h1 - Y * 0.065, -Y, R_HP, vm)                 # Rocket vessel via ISO2 (plugged on Thrust)
    for yy in (e1.y, ISO1_Y, H1_Y):                       # brackets to the rear bulkhead
        rbox(P, Vector(((ROW_X + 0.075 + x_bh) / 2 - 0.004, yy, TR_Z - 0.060)), (abs(x_bh - ROW_X) - 0.070, 0.040, 0.008), 0.002, AL)
    # P1 / T1 transducers, burst disc, fill line
    p1 = h1 + Z * 0.075 + X * 0.035 + Y * 0.035
    hexnut(P, p1, Z, 0.026, 0.0, 0.012, SS)
    cyl(P, p1, Z, 0.0125, 0.012, 0.060, SS, segs=20, ch=0.001)
    connector(P, p1 + Z * 0.060, Z, cm, r=0.009, L=0.018)
    t1 = h1 + Z * 0.075 + X * 0.035 - Y * 0.035
    hexnut(P, t1, Z, 0.020, 0.0, 0.010, SS)
    cyl(P, t1, Z, 0.007, 0.010, 0.045, SS, segs=14, ch=0.0008)
    lathe(P, [(0, 0.045), (0.011, 0.045), (0.011, 0.066), (0.009, 0.069), (0, 0.069)], t1, Z, 16, BK)
    bd = h1 + Z * 0.075 - X * 0.035 - Y * 0.030
    hexnut(P, bd, Z, 0.042, 0.0, 0.020, AU)
    hexnut(P, bd, Z, 0.042, 0.022, 0.042, AU)
    lathe(P, [(0, 0.042), (0.009, 0.042), (0.009, 0.048), (0, 0.048)], bd, Z, 12, RD)
    # burst-disc vent: up, aft over the regulator, down through the duct roof into the fan duct (inert N2)
    vt = bd + Z * 0.048
    tube(P, [vt, vt + Z * 0.020, Vector((vt.x - 0.060, -0.150, vt.z + 0.020)), Vector((-1.250, -0.150, 0.890)), Vector((-1.700, -0.150, 0.880)),
             Vector((-1.700, -0.150, 0.160))], 0.0095, SS, bend=0.04)
    ring(P, Vector((-1.700, -0.150, ramp_z(-1.700))), Z, 0.0100, 0.020, -0.008, 0.008, RU, segs=16, ch=0.002)
    lathe(P, [(0, 0.160), (0.016, 0.160), (0.016, 0.145), (0.008, 0.139), (0, 0.139)], Vector((-1.700, -0.150, 0)), Z, 16, SS)
    # fill receptacle in the left rear quarter (behind a flap in the body side): check valve, receptacle, dust cap
    fp = h1 + Z * 0.075 + Y * 0.040 - X * 0.030
    port(P, fp, Z, 0.010, vm)
    fr = Vector((-0.990, 0.880, 0.640))
    fl = [fp + Z * 0.010, Vector((fp.x, fp.y, 0.965)), Vector((-0.990, 0.720, 0.965)), Vector((-0.990, 0.720, fr.z)), fr - Y * 0.060]
    tube(P, fl, 0.010, SS, bend=0.03)
    lathe(P, [(0, -0.060), (0.014, -0.060), (0.014, -0.022), (0.018, -0.018), (0.018, -0.004), (0, -0.004)], fr, Y, 20, AU)
    hexnut(P, fr - Y * 0.004, Y, 0.036, 0.0, 0.014, SS)
    rbox(P, fr + Y * 0.017, (0.08, 0.006, 0.08), 0.004, AL)
    lathe(P, [(0, 0.020), (0.012, 0.020), (0.012, 0.050), (0.0135, 0.052), (0.0135, 0.058), (0, 0.058)], fr, Y, 20, SS)
    lathe(P, [(0, 0.044), (0.0175, 0.044), (0.0185, 0.046), (0.0185, 0.068), (0.0165, 0.071), (0, 0.071)], fr, Y, 24, BK)

    # ---- H1 -> main HP line -> R1 dome-loaded regulator (laid aft over the rear dampers)
    r1 = Vector((R1_X, 0.0, TR_Z))
    tube(P, [h1 - X * 0.070, r1 + X * 0.124], R_HP, SS, bend=0.0)
    port(P, h1 - X * 0.070, -X, R_HP, vm)
    flange_pair(P, Vector((-1.150, 0.0, TR_Z)), X, R_HP, fm, nb=8, th=0.012)
    pr = dome_regulator(P, r1, -X, -Y, vm, pilot_dir=Y)
    tube(P, [h1 + Z * 0.075 + Y * 0.040 + X * 0.030, h1 + Z * 0.100 + Y * 0.040 + X * 0.030,
             Vector((R1_X + 0.080, 0.150, TR_Z + 0.080)), pr + Vector((0.026, 0.0, 0.010))], 0.0045, SS, segs=8, bend=0.03)
    post_top = r1 - Z * 0.092 - X * 0.050
    rbox(P, Vector((post_top.x, 0.0, (post_top.z + 0.520) / 2)), (0.040, 0.040, post_top.z - 0.520), 0.004, AL)
    rbox(P, Vector((post_top.x, 0.0, 0.524)), (0.090, 0.090, 0.008), 0.002, AL)

    # ---- D1 LP distribution block: P2, relief valve RV1 + vent, outlets: rear feed (-X), lift crossing (+X)
    d1 = D1
    header_block(P, d1, (0.140, 0.140, 0.140), vm)
    ro = r1 + X * 0.020 - Y * 0.124
    tube(P, [ro, Vector((ro.x, d1.y + 0.070, ro.z))], R_HP, SS, bend=0.0)
    flange_pair(P, Vector((ro.x, (ro.y + d1.y + 0.070) / 2, ro.z)), -Y, R_HP, fm, nb=8, th=0.010)
    port(P, Vector((d1.x, d1.y + 0.070, ro.z)), Y, R_HP, vm)
    port(P, Vector((d1.x + 0.070, d1.y, LIFT_PORT_Z)), X, R_FEED_LIFT, vm)            # lift take-off (Rocket; plugged on Thrust)
    p2 = d1 + Z * 0.070 + X * 0.030
    hexnut(P, p2, Z, 0.026, 0.0, 0.012, SS)
    cyl(P, p2, Z, 0.0125, 0.012, 0.060, SS, segs=20, ch=0.001)
    connector(P, p2 + Z * 0.060, Z, cm, r=0.009, L=0.018)
    tube(P, [p2 + Z * 0.012 + Y * 0.030, Vector((d1.x + 0.030, d1.y + 0.030, TR_Z + 0.090)), Vector((R1_X + 0.060, 0.130, TR_Z + 0.085)),
             pr + Vector((0.026, -0.010, 0.030))], 0.0045, SS, segs=8, bend=0.03)
    rv = d1 - Y * 0.070
    cyl(P, rv, -Y, 0.016, 0.0, 0.022, AU, segs=18, ch=0.001)
    lathe(P, [(0, 0.022), (0.024, 0.022), (0.024, 0.072), (0.019, 0.080), (0, 0.080)], rv, -Y, 20, AU)
    tube(P, [rv - Y * 0.050 + Z * 0.024, rv - Y * 0.050 + Z * 0.060, Vector((-1.700, -0.440, d1.z + 0.060)), Vector((-1.700, -0.440, 0.160))],
         0.0095, SS, segs=10, bend=0.03)
    ring(P, Vector((-1.700, -0.440, ramp_z(-1.700))), Z, 0.0100, 0.020, -0.008, 0.008, RU, segs=16, ch=0.002)
    lathe(P, [(0, 0.160), (0.016, 0.160), (0.016, 0.145), (0.008, 0.139), (0, 0.139)], Vector((-1.700, -0.440, 0)), Z, 16, SS)
    # rear feed: D1 -> aft over the dampers -> down through the valve-bay into the manifold block (top inlet)
    rf0 = d1 - X * 0.070
    port(P, rf0, -X, R_HP, vm)
    rf = [rf0 - X * 0.010, Vector((MAN_IN.x + 0.110, d1.y, d1.z)), Vector((MAN_IN.x, MAN_IN.y, d1.z)), MAN_IN + Z * 0.034]
    tube(P, rf, R_HP, SS, bend=0.08)
    flange_pair(P, MAN_IN + Z * 0.026, -Z, R_HP, fm, nb=8, th=0.010)
    # ---- thruster controller (ECU) and harness trunk to the valve-bay loom
    ecu = Vector((-1.210, -0.470, 0.900))
    rbox(P, ecu, (0.110, 0.150, 0.045), 0.006, BK, n=2)
    for k in range(4):
        cyl(P, ecu + Vector((-0.055, -0.045 + 0.030 * k, 0.004)), -X, 0.0075, 0.0, 0.014, AL, segs=12, ch=0.001)
    tube(P, [ecu + Vector((-0.070, 0.0, 0.004)), Vector((-1.400, -0.450, 0.880)), Vector((-1.680, -0.300, 0.860)),
             Vector((MAN_X[1] + 0.085, -0.300, BOX_TOP + 0.090)), Vector((MAN_X[1] + 0.035, -0.400, BOX_TOP + 0.052))],
         0.0075, RU, segs=10, bend=0.06)
    return P.finish(smooth_angle=40.0)


# =============================================================================================== API
def thruster_table():
    """Nozzle data for the physics (body frame). exhaust = gas direction; thrust on the car = -exhaust."""
    ys = yaw_exit_y()[0] - 0.002
    names = {-0.352: "Rear_R2", -0.118: "Rear_R1", 0.118: "Rear_L1", 0.352: "Rear_L2"}
    rows = []
    for y in NOZ_Y:
        rows.append(dict(name=names[y], exit=(NOZ_EXIT_X, y, NOZ_Z), exhaust=(-1, 0, 0), throat_d=2 * NOZ_RT, exit_d=2 * NOZ_RE,
                         feed_line_d=2 * R_FEED_REAR))
    for s, tag in ((1, "L"), (-1, "R")):
        rows.append(dict(name="Lift_F" + tag, exit=(LIFT_FRONT['x'], s * LIFT_FRONT['y'], LIFT_FRONT['z_exit']), exhaust=(0, 0, -1),
                         throat_d=2 * LIFT_RT, exit_d=2 * LIFT_RE, feed_line_d=2 * R_FEED_LIFT))
    for s, tag in ((1, "L"), (-1, "R")):
        rows.append(dict(name="Lift_R" + tag, exit=(LIFT_REAR['x'], s * LIFT_REAR['y'], LIFT_REAR['z_exit']), exhaust=(0, 0, -1),
                         throat_d=2 * LIFT_RT, exit_d=2 * LIFT_RE, feed_line_d=2 * R_FEED_LIFT))
    for s, tag in ((1, "L"), (-1, "R")):
        rows.append(dict(name="Yaw_%s_firing" % tag, exit=(YAW['x'], s * round(ys, 4), YAW['z']), exhaust=(0, s, 0),
                         throat_d=2 * YAW_RT, exit_d=2 * YAW_RE, feed_line_d=2 * R_FEED_YAW))
    return rows


def nozzle_manifest():
    """{name: {"pos", "dir", "exit_d", ...}} in the shape build_roadster.write_rig_header() reads.
    "dir" is the EXHAUST (plume) direction, unit vector, Blender frame; thrust on the car is -dir."""
    return {r["name"]: {"pos": list(r["exit"]), "dir": list(r["exhaust"]), "exit_d": r["exit_d"], "throat_d": r["throat_d"],
                        "feed_line_d": r["feed_line_d"]} for r in thruster_table()}


def fan_manifest():
    """{Fan_k: {"pivot", "axis", "exhaust", "pitch_deg"}}: pivot = rotor centre (blade mid-plane on the axis), axis = the
    mesh's local +X expressed in the body frame (spin positive about it), exhaust = air direction (= -axis)."""
    ax = Vector((math.cos(FAN_TILT), 0.0, -math.sin(FAN_TILT)))
    return {"Fan_%d" % i: {"pivot": [FAN_X, y, FAN_Z], "axis": [round(v, 5) for v in ax], "exhaust": [round(-v, 5) for v in ax],
                           "pitch_deg": math.degrees(FAN_TILT), "tip_d": 2 * FAN_TIP_R, "blades": 7} for i, y in enumerate(FAN_Y)}


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
