"""Detail parts for the Roadster: lights, trim, aero devices, thrusters, fans, wheels and interior.

Blender frame (same as roadster_body): +X forward from the wheelbase midpoint, +Y = car's LEFT, +Z up.
Every part is a separate object whose name maps to an Unreal static mesh (SM_<name>). Moving parts
(wheels, fans, wing, hood vent, barge boards, skirts, yoke) get their pivot at the joint.
Dimensions: measured from Tesla's official photos (see Research/roadster_specs.md); rear-view
proportions from the "Gravity" image (car width 2.0 m -> 3 mm/px).
"""
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
import roadster_body as RB

WB = RB.WB
FA, RA = WB / 2, -WB / 2


# ----------------------------------------------------------------------------- helpers
def new_obj(name, bm, mats, parent=None):
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for m in mats:
        ob.data.materials.append(m)
    if parent:
        ob.parent = parent
    return ob


def body_point(x, row_a, row_b, t, side=1, out=0.0):
    """Point on the body facet between two rows at station x (t=0 on row_a), pushed `out` along the facet normal."""
    ya, za = RB.row_at(row_a, x)
    yb, zb = RB.row_at(row_b, x)
    p = Vector((x, (ya + (yb - ya) * t) * side, za + (zb - za) * t))
    if out:
        # facet normal ~ cross(row tangent, x-axis)
        pa2 = Vector((x - 0.01, RB.row_at(row_a, x - 0.01)[0] * side, RB.row_at(row_a, x - 0.01)[1]))
        pa = Vector((x, ya * side, za))
        pb = Vector((x, yb * side, zb))
        n = (pb - pa).cross(pa2 - pa).normalized()
        if n.y * side < 0 and abs(n.y) > abs(n.z):
            n = -n
        if abs(n.z) >= abs(n.y) and n.z < 0:
            n = -n
        p += n * out
    return p


def ribbon(bm, pts, width, normal_hint=None, mat=0, thickness=0.0):
    """Flat ribbon (optionally thick) through 3D points; width across, normal from hint."""
    verts_l, verts_r = [], []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        n = normal_hint(p) if callable(normal_hint) else (normal_hint or Vector((0, 0, 1)))
        s = t.cross(n).normalized() * (width / 2)
        verts_l.append(bm.verts.new(p + s))
        verts_r.append(bm.verts.new(p - s))
    for i in range(len(pts) - 1):
        f = bm.faces.new((verts_l[i], verts_r[i], verts_r[i + 1], verts_l[i + 1]))
        f.material_index = mat
    return verts_l, verts_r


def box(bm, center, size, mat=0, rot=None):
    m = Matrix.Translation(center)
    if rot is not None:
        m = m @ rot
    r = bmesh.ops.create_cube(bm, size=1.0, matrix=m @ Matrix.Diagonal((size[0], size[1], size[2], 1)))
    for f in {f for v in r["verts"] for f in v.link_faces}:
        f.material_index = mat
    return r["verts"]


def cylinder(bm, center, radius, depth, axis='Y', segs=32, mat=0, radius2=None, cap=True):
    rot = {'X': Matrix.Rotation(math.pi / 2, 4, 'Y'), 'Y': Matrix.Rotation(math.pi / 2, 4, 'X'), 'Z': Matrix.Identity(4)}[axis]
    r = bmesh.ops.create_cone(bm, cap_ends=cap, segments=segs, radius1=radius, radius2=radius if radius2 is None else radius2,
                              depth=depth, matrix=Matrix.Translation(center) @ rot)
    for f in {f for v in r["verts"] for f in v.link_faces}:
        f.material_index = mat
    return r["verts"]


def revolve(bm, profile, segs, axis='Y', mat_fn=None, center=Vector((0, 0, 0)), uv_layer=None):
    """Revolve a (radius, axial) profile around the given axis. Returns rings of verts.
    mat_fn(i) gives the material for the strip between profile points i and i+1."""
    rings = []
    for k in range(segs):
        a = 2 * math.pi * k / segs
        ring = []
        for (r, ax) in profile:
            if axis == 'Y':
                co = Vector((r * math.cos(a), ax, r * math.sin(a)))
            elif axis == 'X':
                co = Vector((ax, r * math.cos(a), r * math.sin(a)))
            else:
                co = Vector((r * math.cos(a), r * math.sin(a), ax))
            ring.append(bm.verts.new(center + co))
        rings.append(ring)
    uv = bm.loops.layers.uv.verify() if uv_layer else None
    n = len(profile)
    for k in range(segs):
        a, b = rings[k], rings[(k + 1) % segs]
        for i in range(n - 1):
            f = bm.faces.new((a[i], b[i], b[i + 1], a[i + 1]))
            if mat_fn:
                f.material_index = mat_fn(i)
            if uv:
                us = (k / segs, (k + 1) / segs, (k + 1) / segs, k / segs)
                vs = (i / (n - 1), i / (n - 1), (i + 1) / (n - 1), (i + 1) / (n - 1))
                for loop, u, v in zip(f.loops, us, vs):
                    loop[uv].uv = (u, v)
    return rings


def set_origin(ob, point):
    """Move the object's origin to `point` (world) without moving the geometry."""
    off = Vector(point) - ob.location
    ob.data.transform(Matrix.Translation(-off))
    ob.location = Vector(point)


def smooth(ob, angle=35):
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle))
    ob.select_set(False)


# ----------------------------------------------------------------------------- lights & trim
def front_lights(M):
    bm = bmesh.new()
    # full-width light bar under the hood lip (OFFICIAL: "full-width thin white LED light bar")
    x = RB.NOSE_X - 0.006
    pts = [Vector((x - 0.02 * (abs(y) / 0.66) ** 3, y, 0.449)) for y in [i / 40 * 1.32 - 0.66 for i in range(41)]]
    ribbon(bm, pts, 0.009, Vector((1, 0, 0.15)), mat=0)
    # pop-up headlight pockets in the fender top facets: dark triangular recess + LED array
    for side in (1, -1):
        a = body_point(2.08, 6, 5, 0.18, side, out=0.004)
        b = body_point(1.90, 6, 5, 0.12, side, out=0.004)
        c = body_point(2.00, 6, 5, 0.72, side, out=0.004)
        v = [bm.verts.new(p) for p in (a, b, c)]
        f = bm.faces.new(v if side > 0 else list(reversed(v)))
        f.material_index = 1
        for i in range(5):
            t = 0.2 + 0.13 * i
            p = a.lerp(c, t * 0.85) + (b - a) * 0.25
            box(bm, p + Vector((0, 0, 0.004)), (0.018, 0.010, 0.006), mat=0)
    ob = new_obj("Front_Lights", bm, [M["LightWhite"], M["BlackGloss"]])
    return ob


def tail_lights(M):
    """Rear lighting and trim from the Gravity rear photo (scale ~1.6 mm/px at the tail):
    a 26 mm full-width red bar (y +-0.86) in a gloss-black band at the top of the rear face, a titanium lip
    band under it, the rear camera above the plate and red reflector strips at the lower corners."""
    bm = bmesh.new()
    x = RB.TAIL_X - 0.003

    def strip(y0, y1, z0, z1, dx, mat, n=48, sag=0.0):
        ys = [y0 + (y1 - y0) * i / n for i in range(n + 1)]
        top = [bm.verts.new(Vector((x - dx - 0.006 * (abs(y) / 0.86) ** 3, y, z1 - sag * (abs(y) / 0.86) ** 2))) for y in ys]
        bot = [bm.verts.new(Vector((x - dx - 0.006 * (abs(y) / 0.86) ** 3, y, z0 - sag * (abs(y) / 0.86) ** 2))) for y in ys]
        for i in range(n):
            bm.faces.new((bot[i], bot[i + 1], top[i + 1], top[i])).material_index = mat
    strip(-0.875, 0.875, 0.898, 0.936, 0.001, 1)            # gloss-black surround
    strip(-0.86, 0.86, 0.904, 0.930, 0.004, 0, sag=0.004)   # the light bar (slight droop at the ends, as photographed)
    strip(-0.88, 0.88, 0.872, 0.890, 0.002, 2)              # titanium lip band
    # rear camera housing above the plate
    box(bm, Vector((x - 0.012, 0, 0.838)), (0.03, 0.06, 0.022), mat=1)
    # red reflector strips at the lower corners (just above the black lower body)
    for s in (1, -1):
        pts = [Vector((x - 0.003, s * (0.70 + 0.16 * i / 8), 0.628)) for i in range(9)]
        ribbon(bm, pts, 0.008, Vector((-1, 0, 0)), mat=0)
    ob = new_obj("Tail_Lights", bm, [M["LightRed"], M["BlackGloss"], M["Titanium"]])
    return ob


def side_details(M):
    bm = bmesh.new()
    for side in (1, -1):
        # The amber marker + black winglet strip are roadster_transform.winglet(); the door shut lines are real cuts
        # (roadster_transform.cut_doors), so neither is painted on here any more.
        # canopy frame bars on the glass (side photo, 2.251 mm/px): a double bar at the front of the door glass
        # and another where the door glass meets the rear quarter glass (both slant forward towards the roof)
        for (x0, z0, x1, z1, w) in ((0.755, 0.788, 0.41, 1.092, 0.010), (0.654, 0.786, 0.296, 1.108, 0.010),
                                    (-0.24, 0.882, -0.40, 1.123, 0.009), (-0.366, 0.891, -0.49, 1.123, 0.009)):
            pts = []
            for i in range(14):
                t = i / 13
                x = x0 + (x1 - x0) * t
                z = z0 + (z1 - z0) * t
                best = None
                for k in range(61):
                    tt = k / 60
                    for ra, rb in ((5, 6), (6, 7), (7, 8)):
                        p = body_point(x, ra, rb, tt, side, out=0.002)
                        if best is None or abs(p.z - z) < abs(best.z - z):
                            best = p
                pts.append(best)
            ribbon(bm, pts, w, lambda p, s=side: Vector((0, s, 0.6)).normalized(), mat=1)
    ob = new_obj("Side_Details", bm, [M["Amber"], M["BlackGloss"]])
    return ob


def splitter(M):
    bm = bmesh.new()
    # carbon front splitter lip under the fascia, slightly proud of the nose
    prof = [(2.26, 0.0), (2.25, 0.55), (2.20, 0.72), (1.95, 0.90), (1.80, 0.90)]
    top, bot = [], []
    for side in (1, -1):
        pass
    verts = []
    for (x, y) in prof:
        verts.append((x, y))
    outline = [(x, y) for (x, y) in verts] + [(x, -y) for (x, y) in reversed(verts[1:])]
    vt = [bm.verts.new((x, y, 0.125)) for x, y in outline]
    vb = [bm.verts.new((x - 0.01, y * 0.98, 0.105)) for x, y in outline]
    vback_t = [bm.verts.new((1.75, 0.0, 0.125))]
    n = len(outline)
    for i in range(n - 1):
        f = bm.faces.new((vt[i], vt[i + 1], vb[i + 1], vb[i]))
    bm.faces.new(vt)
    bm.faces.new(list(reversed(vb)))
    bm.verts.remove(vback_t[0])
    ob = new_obj("Splitter", bm, [M["Carbon"]])
    return ob


def plate(M):
    bm = bmesh.new()
    # silver embossed "RDSTR" plate (US size 305 x 152 mm, photo: 191 x 96 px at 1.6 mm/px), centre z 0.733
    zc = 0.733
    box(bm, Vector((RB.TAIL_X - 0.010, 0, zc)), (0.008, 0.305, 0.152), mat=0)
    # raised rim 6 mm in from the edge
    for (dy, dz, sy, sz) in ((0, 0.069, 0.29, 0.004), (0, -0.069, 0.29, 0.004), (0.145, 0, 0.004, 0.142), (-0.145, 0, 0.004, 0.142)):
        box(bm, Vector((RB.TAIL_X - 0.0155, dy, zc + dz)), (0.003, sy, sz), mat=0)
    ob = new_obj("Plate", bm, [M["PlateSilver"]])
    # embossed letters (text object converted to mesh)
    bpy.ops.object.text_add(location=(RB.TAIL_X - 0.016, 0, zc))
    t = bpy.context.active_object
    t.data.body = "RDSTR"
    t.data.align_x = 'CENTER'; t.data.align_y = 'CENTER'
    # tall, narrow square-shouldered letters as on the official plate (Agency FB is the closest system face)
    font_path = r"C:\Windows\Fonts\AGENCYR.TTF"
    if os.path.exists(font_path):
        t.data.font = bpy.data.fonts.load(font_path)
        t.data.space_character = 1.25
    t.data.size = 0.118
    t.data.extrude = 0.002
    t.rotation_euler = (math.pi / 2, 0, -math.pi / 2)
    bpy.ops.object.convert(target='MESH')
    t.name = "Plate_Text"
    t.data.materials.append(M["PlateSilver"])
    t.parent = None
    return ob, t


def rear_face(M):
    """Black recess below the tail bar, diffuser box (carbon) with strakes, slats and the fan shrouds."""
    bm = bmesh.new()
    x0 = RB.TAIL_X
    # recessed black band across the rear face below the light bar
    box(bm, Vector((x0 - 0.004, 0, 0.535)), (0.01, 1.62, 0.13), mat=1)
    # diffuser / fan box: 1.26 m wide, 0.36 m tall, protrudes ~6 cm past the tail
    # opening ~5 cm proud of the tail, box runs 0.42 m forward into the car
    xo = x0 - 0.05
    W, Hh, D = 1.26, 0.36, 0.42
    zc = 0.115 + Hh / 2
    xc = xo + D / 2
    box(bm, Vector((xc, 0, zc + Hh / 2)), (D, W + 0.04, 0.02), mat=0)                 # top
    box(bm, Vector((xc, W / 2 + 0.01, zc)), (D, 0.02, Hh), mat=0)                     # sides
    box(bm, Vector((xc, -W / 2 - 0.01, zc)), (D, 0.02, Hh), mat=0)
    box(bm, Vector((xc, 0, zc - Hh / 2)), (D, W, 0.015), mat=0)                       # floor
    box(bm, Vector((xo + D, 0, zc)), (0.02, W, Hh), mat=1)                            # front wall (dark)
    # three vertical strakes between the four fans, running to the opening
    for k in (-1, 0, 1):
        box(bm, Vector((xo + 0.14, k * W / 4, zc)), (0.28, 0.012, Hh - 0.02), mat=0)
    # fan shrouds (rings) for 4 axial fans, ~0.28 m diameter
    for k in range(4):
        y = (k - 1.5) * W / 4
        cylinder(bm, Vector((xo + 0.14, y, zc - 0.01)), 0.145, 0.06, axis='X', segs=40, mat=1, cap=False)
    # horizontal diffuser slats under the box
    for k in range(4):
        box(bm, Vector((xo + 0.18, 0, 0.125 + 0.0 * k)), (0.36, W, 0.006), mat=0) if k == 0 else None
    # nozzle rail above the fan box
    box(bm, Vector((x0 + 0.02, 0, 0.545)), (0.10, 1.10, 0.07), mat=1)
    ob = new_obj("Rear_Diffuser", bm, [M["Carbon"], M["BlackGloss"]])
    return ob


def fan_rotor(M, name, center):
    """Axial suction fan: conical spinner + 7 swept blades. Spins about X."""
    bm = bmesh.new()
    cylinder(bm, Vector((0, 0, 0)), 0.045, 0.09, axis='X', segs=24, mat=0, radius2=0.012)   # spinner
    for b in range(7):
        a = 2 * math.pi * b / 7
        rot = Matrix.Rotation(a, 4, 'X') @ Matrix.Rotation(math.radians(28), 4, 'Z')
        v = box(bm, Vector((0, 0, 0)), (0.006, 0.05, 0.10), mat=1)
        for vv in v:
            vv.co = rot @ (vv.co + Vector((0, 0, 0.088)))
    ob = new_obj(name, bm, [M["Titanium"], M["BlackGloss"]])
    ob.location = center
    return ob


def nozzle(bm, center, axis_dir, mat_bell, mat_body, r_exit=0.055, length=0.11):
    """Converging-diverging cold-gas nozzle bell pointing along axis_dir."""
    d = Vector(axis_dir).normalized()
    rot = d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
    prof = [(0.03, 0.0), (0.022, 0.03), (0.03, 0.05), (r_exit * 0.8, 0.09), (r_exit, length), (r_exit - 0.004, length), (r_exit * 0.78, 0.09), (0.026, 0.05)]
    rings = []
    segs = 28
    for k in range(segs):
        a = 2 * math.pi * k / segs
        ring = [bm.verts.new(Vector(center) + rot @ Vector((r * math.cos(a), r * math.sin(a), z))) for (r, z) in prof]
        rings.append(ring)
    for k in range(segs):
        A, B = rings[k], rings[(k + 1) % segs]
        for i in range(len(prof) - 1):
            f = bm.faces.new((A[i], B[i], B[i + 1], A[i + 1]))
            f.material_index = mat_bell if i >= 2 else mat_body


def thrusters(M):
    bms = {}
    # 4 rear nozzles in a row under the plate (Thrust & Rocket)
    bm = bmesh.new()
    for k in range(4):
        y = (k - 1.5) * 0.25
        nozzle(bm, Vector((RB.TAIL_X - 0.04, y, 0.545)), (-1, 0, 0), 0, 1)
    rear = new_obj("Thrusters_Rear", bm, [M["Gold"], M["Titanium"]])
    # Rocket: one downward lift nozzle at each corner (behind front wheel / ahead of rear wheel), 2 front yaw nozzles
    bm = bmesh.new()
    for (x, y) in ((FA - 0.48, 0.80), (FA - 0.48, -0.80), (RA + 0.50, 0.82), (RA + 0.50, -0.82)):
        nozzle(bm, Vector((x, y, 0.16)), (0, 0, -1), 0, 1, r_exit=0.05, length=0.10)
    for s in (1, -1):
        nozzle(bm, Vector((RB.NOSE_X - 0.10, s * 0.40, 0.17)), (0, s, 0), 0, 1, r_exit=0.03, length=0.06)
    lift = new_obj("Thrusters_Rocket", bm, [M["Gold"], M["Titanium"]])
    return rear, lift


def copv(M):
    """Rocket trim: 400 L carbon COPV in place of the passenger seat (EXEC)."""
    bm = bmesh.new()
    prof = [(0.0, -0.58), (0.10, -0.57), (0.20, -0.52), (0.245, -0.42), (0.25, -0.30), (0.25, 0.30), (0.245, 0.42), (0.20, 0.52), (0.10, 0.57), (0.0, 0.58)]
    revolve(bm, prof, 32, axis='X', mat_fn=lambda i: 1 if i in (0, 8) else 0, center=Vector((-0.10, -0.38, 0.47)))
    ob = new_obj("COPV_Rocket", bm, [M["Carbon"], M["Gold"]])
    smooth(ob, 50)
    return ob


# ----------------------------------------------------------------------------- moving aero
def rear_wing(M):
    """Deployable rear wing (Gravity / Augments / World photos): 2.0 m span, 0.40 m chord inverted cambered
    airfoil with a slight smile, large outward-splayed end plates, two gooseneck struts at y +-0.38 hooking
    onto the upper surface, red light strip on the trailing edge.
    Built in its deployed (High Downforce) pose; origin at the strut pivot on the deck."""
    bm = bmesh.new()
    span, chord = 2.00, 0.40
    xle, zbase = -1.90, 1.17
    aoa = math.radians(-11)                      # trailing edge up
    m, p, tk = 0.06, 0.40, 0.085                 # NACA-style camber / position / thickness (inverted)
    n = 28
    def section(t):
        xc = (1 - math.cos(math.pi * t)) / 2
        yt = 5 * tk * (0.2969 * math.sqrt(xc) - 0.1260 * xc - 0.3516 * xc ** 2 + 0.2843 * xc ** 3 - 0.1036 * xc ** 4)
        yc = m / p ** 2 * (2 * p * xc - xc ** 2) if xc < p else m / (1 - p) ** 2 * ((1 - 2 * p) + 2 * p * xc - xc ** 2)
        yc = -yc                                 # inverted: camber pushes down
        return xc, yc - yt, yc + yt             # x, upper surface (pressure side faces up after inversion), lower
    prof_top, prof_bot = [], []
    for i in range(n + 1):
        xc, a, b = section(i / n)
        prof_top.append((xc, max(a, b)))
        prof_bot.append((xc, min(a, b)))
    rot = Matrix.Rotation(-aoa, 3, 'Y')
    ns = 24
    rows_t, rows_b = [], []
    for j in range(ns + 1):
        y = -span / 2 + span * j / ns
        dz = 0.035 * (abs(y) / (span / 2)) ** 2       # gentle smile towards the tips
        base = Vector((xle, y, zbase + dz))
        rows_t.append([bm.verts.new(base + rot @ Vector((-px * chord, 0, pz * chord))) for (px, pz) in prof_top])
        rows_b.append([bm.verts.new(base + rot @ Vector((-px * chord, 0, pz * chord))) for (px, pz) in prof_bot])
    for j in range(ns):
        for i in range(n):
            bm.faces.new((rows_t[j][i], rows_t[j][i + 1], rows_t[j + 1][i + 1], rows_t[j + 1][i])).material_index = 0
            bm.faces.new((rows_b[j][i], rows_b[j + 1][i], rows_b[j + 1][i + 1], rows_b[j][i + 1])).material_index = 0
        # trailing edge: light strip
        bm.faces.new((rows_t[j][-1], rows_b[j][-1], rows_b[j + 1][-1], rows_t[j + 1][-1])).material_index = 2
    for j in (0, ns):
        bm.faces.new(rows_t[j] + list(reversed(rows_b[j][1:-1]))).material_index = 0
    # end plates: 0.48 m long, reach 0.20 m below the wing, splayed outward 16 degrees
    splay = math.radians(16)
    for s in (-1, 1):
        y0 = s * (span / 2 + 0.004)
        tip = 0.035
        outline = [(-0.02, 0.055), (-0.46, 0.085), (-0.44, -0.20), (-0.06, -0.15)]
        for off, mat in ((0.0, 0), (0.007, 0)):
            pts = []
            for (dx, dz) in outline:
                yo = y0 + s * (off + max(0.0, -dz) * math.tan(splay))
                pts.append(bm.verts.new(Vector((xle + dx, yo, zbase + tip + dz))))
            f = bm.faces.new(pts if s > 0 else list(reversed(pts)))
            f.material_index = mat
    # gooseneck struts (rectangular section 14 x 55 mm), deck pivot -> up ahead of the wing -> hook onto its top
    path = [(-2.08, 0.93), (-2.05, 1.01), (-2.00, 1.09), (-1.975, 1.16), (-1.98, 1.225), (-2.01, 1.255), (-2.05, 1.255), (-2.075, 1.232)]
    for s in (-1, 1):
        y = s * 0.38
        pts = [Vector((x, y, z)) for (x, z) in path]
        prev = None
        for i, p in enumerate(pts):
            t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
            side = Vector((0, 1, 0))
            w = t.cross(side).normalized() * 0.0275
            ring = [bm.verts.new(p + w + side * 0.007), bm.verts.new(p - w + side * 0.007), bm.verts.new(p - w - side * 0.007), bm.verts.new(p + w - side * 0.007)]
            if prev:
                for k in range(4):
                    bm.faces.new((prev[k], prev[(k + 1) % 4], ring[(k + 1) % 4], ring[k])).material_index = 1
            prev = ring
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = new_obj("Rear_Wing", bm, [M["Titanium"], M["BlackGloss"], M["LightRed"]])
    smooth(ob, 40)
    set_origin(ob, (-2.10, 0.0, 0.93))
    return ob

def hood_vent(M):
    bm = bmesh.new()
    # centre hood panel that hinges open at its front edge (UI: Hood Vent Open / Close)
    xs = [1.46 + i * 0.05 for i in range(10)]
    rows = []
    for x in xs:
        z = RB.row_at(9, x)[1] + 0.001
        rows.append((bm.verts.new((x, 0.285, z)), bm.verts.new((x, -0.285, z))))
    for i in range(len(rows) - 1):
        bm.faces.new((rows[i][0], rows[i + 1][0], rows[i + 1][1], rows[i][1])).material_index = 0
    ob = new_obj("Hood_Vent", bm, [M["Paint"]])
    set_origin(ob, (1.91, 0, RB.row_at(9, 1.91)[1]))
    return ob


def barge_boards(M):
    objs = []
    for side, nm in ((1, "Barge_L"), (-1, "Barge_R")):
        bm = bmesh.new()
        p = body_point(0.95, 3, 4, 0.5, side, out=0.0)
        box(bm, Vector((0, 0, 0)), (0.32, 0.012, 0.16), mat=0)
        ob = new_obj(nm, bm, [M["Carbon"]])
        ob.location = (p.x - 0.12, p.y - side * 0.03, p.z)   # stowed inside the slot; UE swings it out
        objs.append(ob)
    return objs


def skirts(M):
    """Deployable suction skirts around the sealed hexagonal underfloor (patent). Origin at floor level."""
    bm = bmesh.new()
    hexa = [(1.55, 0.0), (1.20, 0.80), (-1.15, 0.80), (-1.55, 0.0), (-1.15, -0.80), (1.20, -0.80)]
    for i in range(6):
        a, b = hexa[i], hexa[(i + 1) % 6]
        mid = Vector(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, 0.08))
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        box(bm, mid, (L, 0.01, 0.07), mat=0, rot=Matrix.Rotation(ang, 4, 'Z'))
    ob = new_obj("Skirts", bm, [M["Rubber"]])
    set_origin(ob, (0, 0, 0.112))
    return ob


# ----------------------------------------------------------------------------- wheels
def tire_profile(r_out, r_rim, width):
    hw = width / 2
    side = r_out - r_rim
    return [
        (r_rim + 0.004, -hw * 0.92), (r_rim + side * 0.35, -hw * 1.00), (r_rim + side * 0.75, -hw * 0.99),
        (r_out - 0.012, -hw * 0.93), (r_out - 0.002, -hw * 0.80), (r_out, -hw * 0.55),
        (r_out - 0.007, -hw * 0.50), (r_out - 0.007, -hw * 0.44), (r_out, -hw * 0.40),     # outer groove
        (r_out, -hw * 0.06), (r_out - 0.007, -hw * 0.02), (r_out - 0.007, hw * 0.04), (r_out, hw * 0.08),  # centre groove
        (r_out, hw * 0.55), (r_out - 0.002, hw * 0.80), (r_out - 0.012, hw * 0.93),
        (r_rim + side * 0.75, hw * 0.99), (r_rim + side * 0.35, hw * 1.00), (r_rim + 0.004, hw * 0.92),
    ]


def wheel(M, name, r_out, rim_in, width):
    """Wheel + tire, origin at hub centre, axis along +Y (outer face towards +Y; mirror for the right side).
    Tire UVs: u around the circumference, v across the profile (sidewall text lives in the texture)."""
    r_rim = rim_in * 0.0254 / 2
    bm = bmesh.new()
    prof = tire_profile(r_out, r_rim, width)
    revolve(bm, prof, 96, axis='Y', mat_fn=lambda i: 0, uv_layer=True)
    hw = width / 2
    # rim barrel + lip
    rim_prof = [(r_rim + 0.004, hw * 0.92), (r_rim + 0.012, hw * 0.94), (r_rim + 0.012, hw * 0.99), (r_rim - 0.004, hw * 1.0),
                (r_rim - 0.01, hw * 0.9), (r_rim - 0.02, -hw * 0.85), (r_rim + 0.004, -hw * 0.92)]
    revolve(bm, rim_prof, 96, axis='Y', mat_fn=lambda i: 1)
    # aero disc (light grey), slightly dished
    disc_prof = [(0.0, hw * 0.70), (0.06, hw * 0.71), (r_rim * 0.75, hw * 0.80), (r_rim - 0.012, hw * 0.88)]
    revolve(bm, disc_prof, 96, axis='Y', mat_fn=lambda i: 2)
    # 6 blade spokes: thin faceted gloss-black blades with a machined chamfer edge
    for s in range(6):
        a = 2 * math.pi * s / 6 + math.radians(4)
        rot = Matrix.Rotation(a, 4, 'Y')
        L0, L1 = 0.07, r_rim - 0.01
        y0 = hw * 0.90
        ridge = [Vector((L0, y0 + 0.022, 0.006)), Vector((L1, y0 + 0.008, 0.004))]
        e0 = [Vector((L0, y0 + 0.010, 0.026)), Vector((L1, y0 - 0.002, 0.017))]
        e1 = [Vector((L0, y0 - 0.006, -0.026)), Vector((L1, y0 - 0.012, -0.018))]
        V = lambda p: bm.verts.new(rot @ p)
        r0, r1 = V(ridge[0]), V(ridge[1])
        a0, a1 = V(e0[0]), V(e0[1])
        b0, b1 = V(e1[0]), V(e1[1])
        bm.faces.new((r0, r1, a1, a0)).material_index = 3    # machined chamfer face
        bm.faces.new((r0, b0, b1, r1)).material_index = 1    # black face
        bm.faces.new((a0, a1, b1, b0)).material_index = 1
        bm.faces.new((r0, a0, b0)).material_index = 1
        bm.faces.new((r1, b1, a1)).material_index = 1
    # centre cap
    cylinder(bm, Vector((0, hw * 0.93, 0)), 0.055, 0.02, axis='Y', segs=32, mat=1)
    ob = new_obj(name, bm, [M["Tire"], M["Spoke"], M["Disc"], M["Titanium"]])
    smooth(ob, 40)
    return ob


def brakes(M, name, r_disc, x, y_sign, width):
    """Drilled carbon-ceramic disc (spins with the wheel) and gold caliper (fixed)."""
    bm = bmesh.new()
    prof = [(0.10, -0.018), (r_disc, -0.018), (r_disc, 0.018), (0.10, 0.018), (0.10, -0.018)]
    revolve(bm, prof, 64, axis='Y', mat_fn=lambda i: 0)
    cylinder(bm, Vector((0, 0.03, 0)), 0.10, 0.05, axis='Y', segs=32, mat=0)
    disc = new_obj(name + "_Disc", bm, [M["BrakeDisc"]])
    bm = bmesh.new()
    # caliper at the trailing-top of the disc (fixed to the upright)
    a = math.radians(50)
    c = Vector((math.cos(a) * (r_disc - 0.03) * -1, 0.0, math.sin(a) * (r_disc - 0.03)))
    box(bm, c, (0.10, 0.07, 0.17), mat=0, rot=Matrix.Rotation(-a + math.pi / 2, 4, 'Y'))
    cal = new_obj(name + "_Caliper", bm, [M["Gold"]])
    return disc, cal


# ----------------------------------------------------------------------------- interior
def interior(M):
    """Cabin per the official interior photo: Ultrawhite + grey microsuede, carbon door panels with red
    ambient strips, floating landscape screen, squircle yoke (left-hand drive = +Y), two forward displays."""
    parts = {}
    bm = bmesh.new()
    # floor / tub
    box(bm, Vector((0.05, 0, 0.16)), (2.1, 1.70, 0.03), mat=2)
    # dash: low slab across the car, suede top
    box(bm, Vector((0.98, 0, 0.68)), (0.34, 1.62, 0.08), mat=1)
    box(bm, Vector((1.05, 0, 0.56)), (0.24, 1.62, 0.18), mat=2)
    # centre tunnel spine (white), tall between the seats
    box(bm, Vector((0.25, 0, 0.36)), (1.25, 0.22, 0.38), mat=0)
    box(bm, Vector((0.62, 0, 0.47)), (0.40, 0.24, 0.20), mat=3)      # carbon console with charger
    # door panels: carbon with suede and red ambient strips
    for s in (1, -1):
        box(bm, Vector((0.25, s * 0.86, 0.50)), (1.30, 0.04, 0.42), mat=3)
        box(bm, Vector((0.30, s * 0.835, 0.60)), (1.10, 0.012, 0.012), mat=4)
        box(bm, Vector((0.25, s * 0.84, 0.70)), (1.20, 0.03, 0.10), mat=1)
    # A-pillar braces (suede) from the dash corners into the roof
    for s in (1, -1):
        a = Vector((0.96, s * 0.80, 0.74)); b = Vector((0.36, s * 0.60, 1.06))
        mid = (a + b) / 2
        d = (b - a)
        rot = d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
        box(bm, mid, (0.035, 0.035, d.length), mat=1, rot=rot)
    # rear-view camera display on a triangular mount at the top of the windshield
    box(bm, Vector((0.40, 0, 1.03)), (0.02, 0.24, 0.06), mat=2)
    # bulkhead behind the seats and firewall ahead of the footwell
    box(bm, Vector((-0.78, 0, 0.62)), (0.04, 1.6, 0.90), mat=2)
    box(bm, Vector((1.18, 0, 0.45)), (0.04, 1.85, 0.62), mat=2)
    # wheel-well liners: closed boxes around each front / rear wheel so the cabin never sees the tires
    for (wx, r) in ((FA, 0.3425), (RA, 0.3672)):
        for s in (1, -1):
            box(bm, Vector((wx, s * 0.64, r + 0.02)), (2 * r + 0.10, 0.02, 2 * r + 0.06), mat=2)
            box(bm, Vector((wx, s * 0.80, 2 * r + 0.06)), (2 * r + 0.10, 0.34, 0.02), mat=2)
    cab = new_obj("Interior", bm, [M["Leather"], M["Suede"], M["InteriorDark"], M["Carbon"], M["RedAmbient"]])
    parts["Interior"] = cab

    # seats: fixed buckets moulded into the structure; Ultrawhite with grey suede bolsters
    for s, nm in ((1, "Seat_Driver"), (-1, "Seat_Passenger")):
        bm = bmesh.new()
        y = s * 0.38
        box(bm, Vector((0.02, y, 0.24)), (0.55, 0.42, 0.10), mat=0)                      # cushion
        bk = Matrix.Rotation(math.radians(-22), 4, 'Y')
        box(bm, Vector((-0.33, y, 0.58)), (0.10, 0.42, 0.70), mat=0, rot=bk)            # back
        for b in (1, -1):
            box(bm, Vector((0.0, y + b * 0.22, 0.30)), (0.50, 0.06, 0.14), mat=1)       # cushion bolsters
            box(bm, Vector((-0.30, y + b * 0.22, 0.58)), (0.12, 0.06, 0.62), mat=1, rot=bk)
        parts[nm] = new_obj(nm, bm, [M["Leather"], M["Suede"]])

    # yoke: closed rounded-rectangle ("squircle") rim, pentagon hub, BOOST button. Pivot on the column axis.
    bm = bmesh.new()
    W, H, r = 0.34, 0.24, 0.07
    pts = []
    for k in range(64):
        a = 2 * math.pi * k / 64
        cx, cy = math.cos(a), math.sin(a)
        # superellipse
        e = 4.0
        px = W / 2 * math.copysign(abs(cx) ** (2 / e), cx)
        py = H / 2 * math.copysign(abs(cy) ** (2 / e), cy)
        pts.append((px, py))
    rings = []
    for (px, py) in pts:
        ring = []
        for j in range(8):
            b = 2 * math.pi * j / 8
            n2 = Vector((px, 0, py)).normalized()
            off = n2 * (0.016 * math.cos(b)) + Vector((0, 0.016 * math.sin(b), 0))
            ring.append(bm.verts.new(Vector((0.0, px, py)) + Vector((off.y, off.x, off.z))))
        rings.append(ring)
    for k in range(64):
        A, B = rings[k], rings[(k + 1) % 64]
        for j in range(8):
            f = bm.faces.new((A[j], B[j], B[(j + 1) % 8], A[(j + 1) % 8]))
            f.material_index = 0
    # spokes and pentagon hub
    box(bm, Vector((0.01, 0, 0)), (0.025, W - 0.03, 0.05), mat=1)
    pent = [bm.verts.new((0.02, 0.075 * math.cos(math.pi / 2 + 2 * math.pi * i / 5), 0.075 * math.sin(math.pi / 2 + 2 * math.pi * i / 5) - 0.01)) for i in range(5)]
    bm.faces.new(pent).material_index = 1
    box(bm, Vector((0.022, 0.11, -0.03)), (0.012, 0.03, 0.018), mat=2)                 # BOOST button (left spoke)
    yoke = new_obj("Yoke", bm, [M["Leather"], M["Suede"], M["LightRed"]])
    smooth(yoke, 45)
    yoke.rotation_euler = (0, math.radians(-24), 0)    # column rake
    yoke.location = (0.62, 0.38, 0.72)
    parts["Yoke"] = yoke

    # centre touchscreen: floating landscape ~15.4" 16:10 (0.332 x 0.207 m), silver bezel, short stand
    bm = bmesh.new()
    box(bm, Vector((0, 0, 0)), (0.012, 0.345, 0.220), mat=0)
    box(bm, Vector((0.03, 0, -0.14)), (0.03, 0.06, 0.08), mat=0)
    bez = new_obj("Screen_Bezel", bm, [M["Aluminum"]])
    bez.rotation_euler = (0, math.radians(-14), 0)
    bez.location = (0.80, 0.0, 0.84)
    parts["Screen_Bezel"] = bez
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    v = [bm.verts.new(p) for p in ((-0.0065, 0.166, -0.1035), (-0.0065, -0.166, -0.1035), (-0.0065, -0.166, 0.1035), (-0.0065, 0.166, 0.1035))]
    f = bm.faces.new(v)
    for loop, (u, vv) in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
        loop[uv].uv = (u, vv)
    scr = new_obj("Screen_Display", bm, [M["Screen"]])
    scr.rotation_euler = bez.rotation_euler
    scr.location = bez.location
    parts["Screen_Display"] = scr

    # two smaller forward displays on the dash top at the base of the windshield (positions EST)
    for nm, y in (("Forward_Display_A", 0.38), ("Forward_Display_B", 0.10)):
        bm = bmesh.new()
        uv = bm.loops.layers.uv.new("UVMap")
        box(bm, Vector((0.004, 0, 0)), (0.008, 0.20, 0.06), mat=1)
        v = [bm.verts.new(p) for p in ((-0.0005, 0.095, -0.026), (-0.0005, -0.095, -0.026), (-0.0005, -0.095, 0.026), (-0.0005, 0.095, 0.026))]
        f = bm.faces.new(v)
        f.material_index = 0
        for loop, (u, vv) in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
            loop[uv].uv = (u, vv)
        ob = new_obj(nm, bm, [M["ScreenSmall"], M["InteriorDark"]])
        ob.rotation_euler = (0, math.radians(-35), 0)
        ob.location = (0.92, y, 0.75)
        parts[nm] = ob
    return parts
