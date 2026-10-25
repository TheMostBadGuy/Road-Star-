"""Procedural body shell for the 2nd-gen Tesla Roadster (2026 reveal car).

The body is lofted from "row curves": each row is one design line of the car (floor edge, sill,
shoulder crease, belt, roof rail, centreline, ...) given as key points (x, y, z) in metres.
x: forward from the wheelbase midpoint; y: right (half body, mirrored); z: up from the ground.
Points were measured from Tesla's near-orthographic side photo (2.251 mm/px, wheelbase 2.825 m)
and its front / top photos; see Research/roadster_specs.md section 3.

Faces between consecutive rows are ruled facets (the car is a faceted wedge), rows are sharp edges.
"""
import bpy, bmesh, math
from mathutils import Vector

# ----------------------------------------------------------------------------- design data
NOSE_X, TAIL_X = 2.235, -2.100
WB = 2.825
FRONT_AXLE, REAR_AXLE = WB / 2, -WB / 2

# Rows from the floor centreline (0) around the side to the roof centreline (9).
# Each row: list of (x, y, z), x descending (nose -> tail).
ROWS = {
    # 0 floor centre
    0: [(2.20, 0.0, 0.205), (1.95, 0.0, 0.120), (1.40, 0.0, 0.112), (-1.20, 0.0, 0.112), (-1.70, 0.0, 0.150), (-2.07, 0.0, 0.330)],
    # 1 floor outer edge
    1: [(2.20, 0.58, 0.205), (1.95, 0.74, 0.120), (1.40, 0.80, 0.112), (-1.20, 0.80, 0.112), (-1.70, 0.78, 0.150), (-2.07, 0.74, 0.330)],
    # 2 rocker / lower-body bottom edge
    2: [(2.215, 0.670, 0.160), (1.95, 0.900, 0.140), (1.40, 0.945, 0.128), (0.40, 0.950, 0.125), (-0.60, 0.950, 0.128), (-1.40, 0.945, 0.150), (-1.85, 0.920, 0.250), (-2.085, 0.860, 0.360)],
    # 3 lower crease: white panel above, black sill below (rises towards the rear)
    3: [(2.225, 0.688, 0.295), (1.98, 0.930, 0.270), (1.40, 0.968, 0.240), (0.80, 0.968, 0.252), (-0.20, 0.968, 0.296), (-0.83, 0.966, 0.385), (-1.40, 0.960, 0.460), (-1.88, 0.935, 0.540), (-2.090, 0.875, 0.620)],
    # 4 side, widest (fender / haunch)
    4: [(2.230, 0.690, 0.430), (2.00, 0.950, 0.470), (1.40, 0.995, 0.520), (0.80, 0.990, 0.500), (0.00, 0.988, 0.540), (-0.80, 0.992, 0.600), (-1.40, 1.000, 0.640), (-1.90, 0.975, 0.700), (-2.095, 0.900, 0.720)],
    # 5 shoulder crease: front fender crest (nose tip -> A-pillar base), then the bright belt crease, rising to the tail corner
    # front part measured on the calibrated Slide-1 front photo: the crest runs straight (in front view) from the
    # hood's front corner at the nose to the fender's top corner beside the windshield base
    5: [(2.240, 0.675, 0.465), (2.10, 0.748, 0.574), (1.90, 0.790, 0.640), (1.65, 0.836, 0.712), (1.30, 0.900, 0.806, "s"),
        # side photo (2.251 mm/px): behind the fender peak the crease drops to the door top-front, then rises to the tail
        (0.87, 0.965, 0.673, "s"), (0.18, 0.962, 0.730), (-0.16, 0.958, 0.775), (-0.70, 0.950, 0.840), (-1.06, 0.945, 0.890), (-1.39, 0.940, 0.896), (-1.80, 0.920, 0.902), (-2.097, 0.870, 0.910)],
    # 6 fender inner edge (hood side) -> windshield corner -> side-glass bottom (DLO) -> sail outer edge
    6: [(2.225, 0.662, 0.462), (1.95, 0.605, 0.565), (1.65, 0.552, 0.668), (1.43, 0.510, 0.745, "s"), (1.26, 0.830, 0.800, "s"),
        (1.00, 0.865, 0.787), (0.76, 0.875, 0.780), (0.18, 0.868, 0.820), (-0.16, 0.855, 0.867), (-0.74, 0.800, 0.943), (-0.89, 0.785, 0.970), (-1.04, 0.770, 1.058, "s"), (-1.45, 0.790, 1.000), (-1.78, 0.800, 0.966), (-2.097, 0.820, 0.928)],
    # 7 hood outer panel line -> side-glass top / roof-rail outer -> sail top edge
    7: [(2.225, 0.470, 0.465), (1.95, 0.460, 0.568), (1.65, 0.440, 0.668), (1.43, 0.420, 0.745, "s"), (1.33, 0.700, 0.785, "s"), (1.00, 0.680, 0.935), (0.60, 0.560, 1.052), (0.20, 0.520, 1.112), (-0.30, 0.508, 1.118), (-0.70, 0.515, 1.110), (-1.05, 0.540, 1.064), (-1.45, 0.520, 0.995), (-1.80, 0.480, 0.958), (-2.097, 0.450, 0.930)],
    # 8 hood vent edge -> roof glass edge -> rear-window edge
    8: [(2.225, 0.300, 0.464), (1.95, 0.290, 0.567), (1.65, 0.280, 0.668), (1.43, 0.270, 0.745, "s"), (1.36, 0.540, 0.776, "s"), (1.00, 0.500, 0.938), (0.60, 0.530, 1.056), (0.20, 0.495, 1.118), (-0.30, 0.485, 1.122), (-0.70, 0.490, 1.096), (-1.05, 0.510, 1.068), (-1.45, 0.440, 0.975), (-1.80, 0.410, 0.940), (-2.097, 0.390, 0.922)],
    # 9 centreline: hood, windshield, roof peak (z 1.132 @ x -0.16), recessed rear window, tail
    9: [(2.225, 0.0, 0.463), (1.95, 0.0, 0.566), (1.65, 0.0, 0.667), (1.43, 0.0, 0.745, "s"), (1.36, 0.0, 0.765, "s"), (1.00, 0.0, 0.930), (0.60, 0.0, 1.058), (0.20, 0.0, 1.122), (-0.16, 0.0, 1.132), (-0.70, 0.0, 1.102), (-1.05, 0.0, 1.070), (-1.45, 0.0, 0.960), (-1.80, 0.0, 0.925), (-2.097, 0.0, 0.915)],
}
N_ROWS = len(ROWS)

# Hood vent: the rear hood panel between the windshield base and the cross seam seen in the front photo
# (x 1.455..1.775), spanning the hood between the row-7 panel lines. Front edge is the hinge.
VENT_X = (1.775, 1.455)

# Stations (x) where the loft is sampled: dense at the ends and around design breaks.
def make_stations():
    xs = [2.235, 2.23, 2.21, 2.18, 2.14, 2.10, 2.04, 1.98, 1.90]
    x = 1.82
    while x > -1.92:
        xs.append(round(x, 4))
        x -= 0.08
    xs += [-1.95, -2.00, -2.04, -2.07, -2.09, -2.097]
    xs += list(VENT_X)          # hood-vent panel edges get their own stations (clean cut lines)
    # every sharp key point gets its own station so the corner is captured exactly
    for pts in ROWS.values():
        for p in pts:
            if len(p) > 3:
                xs.append(p[0])
    xs = sorted(set(round(v, 4) for v in xs), reverse=True)
    # drop stations closer than 2 cm to a sharp station (avoid slivers)
    sharp = {round(p[0], 4) for pts in ROWS.values() for p in pts if len(p) > 3}
    return [v for v in xs if v in sharp or all(abs(v - s) > 0.02 for s in sharp)]

STATIONS = make_stations()


def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def row_at(row, x):
    """(y, z) of a row at station x, Catmull-Rom through the key points (clamped at ends)."""
    pts = ROWS[row]
    if x >= pts[0][0]:
        return pts[0][1], pts[0][2]
    if x <= pts[-1][0]:
        return pts[-1][1], pts[-1][2]
    for i in range(len(pts) - 1):
        xa, xb = pts[i][0], pts[i + 1][0]
        if xa >= x >= xb:
            t = (xa - x) / (xa - xb)
            a, b = pts[i], pts[i + 1]
            # sharp key points: tangent follows the chord on that side (no overshoot across the corner)
            p0 = pts[max(i - 1, 0)] if len(a) == 3 else tuple(2 * a[k] - b[k] for k in range(3))
            p3 = pts[min(i + 2, len(pts) - 1)] if len(b) == 3 else tuple(2 * b[k] - a[k] for k in range(3))
            y = catmull(p0[1], a[1], b[1], p3[1], t)
            z = catmull(p0[2], a[2], b[2], p3[2], t)
            return y, z
    return pts[-1][1], pts[-1][2]


# ----------------------------------------------------------------------------- materials
MAT_NAMES = ["Paint", "Glass", "BlackGloss", "Underbody", "RearGlass", "Paint"]   # slot 5: hood-vent marker


def band_material(band, x):
    """Material index for the facet between row `band` and `band+1` at station x."""
    if band <= 1:
        return 3                      # underbody
    if band == 2:
        return 2                      # black sill / lower body
    if band in (3, 4, 5):
        return 0
    if band in (7, 8) and VENT_X[1] <= x <= VENT_X[0]:
        return 5                      # hood-vent panel (split off as its own part after subdivision)
    if band == 6:                     # side glass between the DLO and the roof rail (windshield corner -> glass tip)
        return 1 if -1.04 <= x <= 1.26 else 0
    if band == 7:                     # roof rail: glass over the windshield wrap, paint from the B-point back
        return 1 if (0.62 <= x <= 1.34) else 0
    if band == 8:                     # roof glass / rear window recess
        if -1.05 <= x <= 1.36:
            return 1
        if -1.70 <= x < -1.05:        # recessed dark rear window; white deck panel behind it (Hero / Slide-2)
            return 4
        return 0
    return 0


# Panel crown between design rows: band -> (number of sub-rows, outward bulge in metres at mid-panel).
# Tesla's surfaces are tensioned, gently convex panels between razor creases.
CROWN = {3: (1, 0.006), 4: (2, 0.012), 6: (2, 0.018), 8: (3, 0.016)}
# Design rows that are true creases (row 4, the widest side line, and the centreline 0/9 flow smoothly)
CREASE_ROWS = {1, 2, 3, 5, 6, 7, 8}


def dense_rows():
    """[(band, t, bulge)] - original row r is (r, 0, 0); sub-rows sit inside band b at fraction t."""
    out = []
    for r in range(N_ROWS):
        out.append((r, 0.0, 0.0))
        if r < N_ROWS - 1 and r in CROWN:
            n, amt = CROWN[r]
            for k in range(1, n + 1):
                out.append((r, k / (n + 1), amt))
    return out


DENSE = dense_rows()


SHARP_X = sorted({p[0] for pts in ROWS.values() for p in pts if len(p) > 3})


def crown_fade(band, x):
    """Crown weight: zero at the windshield-base corner (rows fold sharply there) and on the flat
    hood-vent panel (band 8 ahead of the windshield), easing in over ~15 cm."""
    if band == 8 and x > SHARP_X[-1] - 0.04:
        return 0.0
    d = min(abs(x - s) for s in SHARP_X)
    u = min(1.0, max(0.0, (d - 0.04) / 0.15))
    return u * u * (3 - 2 * u)


def dense_point(x, band, t, bulge):
    ya, za = row_at(band, x)
    if t == 0.0:
        return ya, za
    yb, zb = row_at(band + 1, x)
    y, z = ya + (yb - ya) * t, za + (zb - za) * t
    if bulge:
        dy, dz = yb - ya, zb - za
        L = math.hypot(dy, dz)
        if L > 1e-4:
            ny, nz = dz / L, -dy / L
            # outward = away from the section centre (y 0, z 0.6)
            if ny * (y - 0.0) + nz * (z - 0.6) < 0:
                ny, nz = -ny, -nz
            s = bulge * math.sin(math.pi * t) * min(1.0, L / 0.25) * crown_fade(band, x)
            y, z = y + ny * s, z + nz * s
    return max(0.0, y), z


def build_body(name="Roadster_Body"):
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    ND = len(DENSE)

    grid = []
    for x in STATIONS:
        ring = []
        for (band, t, bulge) in DENSE:
            y, z = dense_point(x, band, t, bulge)
            ring.append(bm.verts.new((x, y, z)))
        grid.append(ring)

    def cap_ring(ring, dx):
        return [bm.verts.new((v.co.x + dx, 0.0, v.co.z)) for v in ring]

    # Nose: the fender fronts flank a full-width black fascia set 1.5 cm back under the hood lip (LED bar line).
    # Ring B pulls the lower rows (up to row 4) in to y=0.655 and back; upper rows (hood lip) stay put.
    nose_b = []
    for k, v in enumerate(grid[0]):
        band, t, _ = DENSE[k]
        if band < 4 or (band == 4 and t == 0.0):
            nose_b.append(bm.verts.new((v.co.x - 0.015, min(v.co.y, 0.655), v.co.z)))
        else:
            nose_b.append(v)
    nose = cap_ring(nose_b, 0.0)
    tail = cap_ring(grid[-1], -0.004)
    rings = [nose, nose_b] + grid + [tail]

    for s in range(len(rings) - 1):
        a, b = rings[s], rings[s + 1]
        xmid = 0.5 * (a[0].co.x + b[0].co.x)
        for k in range(ND - 1):
            band = DENSE[k][0]
            quad = [a[k], b[k], b[k + 1], a[k + 1]]
            if len(set(quad)) < 4:
                continue
            try:
                f = bm.faces.new(quad)
            except ValueError:
                continue
            if s == 0:      # fascia (front photo): gloss band under the LED bar, matte intake below 0.32 m
                zc = sum(v.co.z for v in quad) / 4
                f.material_index = 0 if zc > 0.456 else (2 if zc > 0.32 else 3)
            elif s == 1:    # step between the fascia and the fender fronts: white above the fender-front black line
                zc = sum(v.co.z for v in quad) / 4
                f.material_index = 0 if zc > 0.456 else 2
            elif s == len(rings) - 2:   # rear face: white above the black lower body (z 0.645, rear photo)
                zc = sum(v.co.z for v in quad) / 4
                f.material_index = 2 if zc < 0.645 else 0
            else:
                f.material_index = band_material(band, xmid)

    # Subdivision creases: the cage is smoothed by a Catmull-Clark modifier (tensioned, crowned panels);
    # only the real design lines stay razor sharp.
    cl = bm.edges.layers.float.get("crease_edge") or bm.edges.layers.float.new("crease_edge")
    sharp_x = {round(v, 4) for v in SHARP_X}

    def crease(v1, v2):
        if v1 is v2:
            return
        e = bm.edges.get((v1, v2))
        if e is not None:
            e[cl] = 1.0

    for s in range(len(rings)):
        ring = rings[s]
        # the only true cross-section fold: hood rear edge into the windshield / cowl (x 1.43)
        station_sharp = 2 <= s < len(rings) - 1 and abs(ring[0].co.x - 1.43) < 1e-3
        for k in range(ND):
            band, t, _ = DENSE[k]
            # along the car: design rows
            if s < len(rings) - 1 and t == 0.0 and band in CREASE_ROWS:
                crease(ring[k], rings[s + 1][k])
            # around the section: nose blade, tail cut-off and the windshield-base fold
            if k < ND - 1 and (s in (1, 2, len(rings) - 2) or (station_sharp and band >= 6)):
                crease(ring[k], ring[k + 1])
    # every material boundary (glass edges, black sill, fascia) is a hard edge
    for e in bm.edges:
        if len(e.link_faces) == 2 and e.link_faces[0].material_index != e.link_faces[1].material_index:
            e[cl] = 1.0

    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    return obj