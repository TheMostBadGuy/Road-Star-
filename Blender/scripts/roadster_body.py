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
    2: [(2.215, 0.640, 0.210), (1.95, 0.900, 0.140), (1.40, 0.945, 0.128), (0.40, 0.950, 0.125), (-0.60, 0.950, 0.128), (-1.40, 0.945, 0.150), (-1.85, 0.920, 0.250), (-2.085, 0.860, 0.360)],
    # 3 lower crease: white panel above, black sill below (rises towards the rear)
    3: [(2.225, 0.665, 0.330), (1.98, 0.925, 0.300), (1.40, 0.968, 0.200), (0.80, 0.968, 0.158), (0.52, 0.968, 0.225), (-0.20, 0.968, 0.262), (-0.83, 0.966, 0.315), (-1.40, 0.960, 0.400), (-1.88, 0.935, 0.440), (-2.090, 0.875, 0.470)],
    # 4 side, widest (fender / haunch)
    4: [(2.230, 0.690, 0.430), (2.00, 0.950, 0.470), (1.40, 0.995, 0.520), (0.80, 0.990, 0.500), (0.00, 0.988, 0.540), (-0.80, 0.992, 0.600), (-1.40, 1.000, 0.640), (-1.90, 0.975, 0.700), (-2.095, 0.900, 0.720)],
    # 5 shoulder crease: front fender crest (nose tip -> A-pillar base), then the bright belt crease, rising to the tail corner
    5: [(2.240, 0.705, 0.522), (2.10, 0.830, 0.574), (1.90, 0.920, 0.640), (1.65, 0.960, 0.712), (1.36, 0.975, 0.802, "s"), (0.95, 0.968, 0.792), (0.63, 0.960, 0.776), (0.00, 0.950, 0.810), (-0.70, 0.945, 0.850), (-1.39, 0.940, 0.889), (-1.80, 0.920, 0.912), (-2.097, 0.870, 0.930)],
    # 6 fender inner edge (hood side) -> windshield corner -> side-glass bottom (DLO) -> sail outer edge
    6: [(2.225, 0.665, 0.462), (1.95, 0.635, 0.565), (1.65, 0.600, 0.668), (1.43, 0.570, 0.745, "s"), (1.36, 0.905, 0.797, "s"), (1.25, 0.895, 0.808), (0.90, 0.875, 0.825), (0.52, 0.875, 0.842), (0.00, 0.860, 0.900), (-0.50, 0.825, 0.960), (-1.01, 0.770, 1.055), (-1.45, 0.800, 0.990), (-1.80, 0.810, 0.955), (-2.097, 0.820, 0.928)],
    # 7 hood outer panel line -> side-glass top / roof-rail outer -> sail top edge
    7: [(2.225, 0.470, 0.465), (1.95, 0.460, 0.568), (1.65, 0.440, 0.668), (1.43, 0.420, 0.745, "s"), (1.36, 0.780, 0.790, "s"), (1.00, 0.680, 0.935), (0.60, 0.560, 1.052), (0.20, 0.520, 1.112), (-0.30, 0.508, 1.116), (-0.70, 0.515, 1.091), (-1.05, 0.540, 1.064), (-1.45, 0.520, 0.995), (-1.80, 0.480, 0.958), (-2.097, 0.450, 0.930)],
    # 8 hood vent edge -> roof glass edge -> rear-window edge
    8: [(2.225, 0.300, 0.464), (1.95, 0.290, 0.567), (1.65, 0.280, 0.668), (1.43, 0.270, 0.745, "s"), (1.36, 0.540, 0.776, "s"), (1.00, 0.500, 0.938), (0.60, 0.530, 1.056), (0.20, 0.495, 1.118), (-0.30, 0.485, 1.122), (-0.70, 0.490, 1.096), (-1.05, 0.510, 1.068), (-1.45, 0.440, 0.975), (-1.80, 0.410, 0.940), (-2.097, 0.390, 0.922)],
    # 9 centreline: hood, windshield, roof peak (z 1.132 @ x -0.16), recessed rear window, tail
    9: [(2.225, 0.0, 0.463), (1.95, 0.0, 0.566), (1.65, 0.0, 0.667), (1.43, 0.0, 0.745, "s"), (1.36, 0.0, 0.765, "s"), (1.00, 0.0, 0.930), (0.60, 0.0, 1.058), (0.20, 0.0, 1.122), (-0.16, 0.0, 1.132), (-0.70, 0.0, 1.102), (-1.05, 0.0, 1.070), (-1.45, 0.0, 0.960), (-1.80, 0.0, 0.925), (-2.097, 0.0, 0.915)],
}
N_ROWS = len(ROWS)

# Stations (x) where the loft is sampled: dense at the ends and around design breaks.
def make_stations():
    xs = [2.235, 2.23, 2.21, 2.18, 2.14, 2.10, 2.04, 1.98, 1.90]
    x = 1.82
    while x > -1.92:
        xs.append(round(x, 4))
        x -= 0.08
    xs += [-1.95, -2.00, -2.04, -2.07, -2.09, -2.097]
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
MAT_NAMES = ["Paint", "Glass", "BlackGloss", "Underbody", "RearGlass"]


def band_material(band, x):
    """Material index for the facet between row `band` and `band+1` at station x."""
    if band <= 1:
        return 3                      # underbody
    if band == 2:
        return 2                      # black sill / lower body
    if band in (3, 4, 5):
        return 0
    canopy = -1.01 <= x <= 1.36
    if band == 6:                     # side glass between belt and roof rail
        return 1 if canopy else 0
    if band == 7:                     # roof rail: glass over the windshield wrap, paint from the B-point back
        return 1 if (0.62 <= x <= 1.36) else 0
    if band == 8:                     # roof glass / rear window recess
        if -1.05 <= x <= 1.36:
            return 1
        if -1.85 <= x < -1.05:
            return 4
        return 0
    return 0


def build_body(name="Roadster_Body"):
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()

    grid = []
    for x in STATIONS:
        ring = []
        for r in range(N_ROWS):
            y, z = row_at(r, x)
            ring.append(bm.verts.new((x, y, z)))
        grid.append(ring)

    # nose and tail caps: collapsed rings on the centreline (front face / rear face)
    def cap_ring(ring, dx):
        out = []
        for v in ring:
            out.append(bm.verts.new((v.co.x + dx, 0.0, v.co.z)))
        return out

    # Nose: outer white "blades" (fender fronts) flank a black fascia recessed 3 cm behind them.
    # Ring B pulls the lower rows (0-4) in to y=0.56 and back 3 cm; upper rows (hood lip) stay put.
    nose_b = []
    for r, v in enumerate(grid[0]):
        if r <= 4:
            nose_b.append(bm.verts.new((v.co.x - 0.03, min(v.co.y, 0.56), v.co.z)))
        else:
            nose_b.append(v)
    nose = cap_ring(nose_b, 0.0)
    tail = cap_ring(grid[-1], -0.004)
    rings = [nose, nose_b] + grid + [tail]

    for s in range(len(rings) - 1):
        a, b = rings[s], rings[s + 1]
        xmid = 0.5 * (a[0].co.x + b[0].co.x)
        for r in range(N_ROWS - 1):
            quad = [a[r], b[r], b[r + 1], a[r + 1]]
            if len(set(quad)) < 4:
                continue
            try:
                f = bm.faces.new(quad)
            except ValueError:
                continue
            if s == 0:      # recessed fascia: black below the hood lip, paint above
                zc = sum(v.co.z for v in quad) / 4
                f.material_index = 2 if zc < 0.44 else 0
            elif s == 1:    # blade faces between the fascia recess and the outer nose
                f.material_index = 2 if r <= 1 else 0
            elif s == len(rings) - 2:   # rear face
                zc = sum(v.co.z for v in quad) / 4
                f.material_index = 2 if zc < 0.52 else 0
            else:
                f.material_index = band_material(r, xmid)

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()

    # sharp creases along every design row
    mesh.update()
    for e in mesh.edges:
        v0, v1 = mesh.vertices[e.vertices[0]].co, mesh.vertices[e.vertices[1]].co
        if abs(v0.x - v1.x) > 1e-4 and abs(v0.y - v1.y) + abs(v0.z - v1.z) < 0.6:
            pass
    return obj
