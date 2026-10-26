"""Moving body panels of the 2026 Roadster, cut from the finished shell, with their real hinge geometry.

Every moving panel is extracted from the subdivided body after it is complete, so the closed car is exactly the static
car. Each cut leaves a shut-line gap, the panel gets its real thickness, and the shell gets the jamb / bay it closes onto.
Hinges (pivot point, unit axis, travel) are written to the export manifest and the generated RoadsterRig.h, so Unreal
moves the meshes about the same axes Blender used.

Sources (Research/roadster_specs.md section 4.4, added 2026-10-25):
- Doors: butterfly/dihedral, "a main hinge behind the A-pillar and a second at the upper centre edge of the windshield";
  removable glass roof panels in both doors, the central roof remains (T-top) - MotorTrend first look via Yahoo Autos,
  Tesla press kit via TeslaNorth 2026-10-25. Open pose checked against official photos 03-Slide-3 and 04-World.
- Rear wing: "a roughly 8-inch section of the tail pops up and out"; Stowed / Low Drag / High Downforce; "tilts 90 degrees,
  acting as an airbrake" under hard braking and cornering; Optimus actuators (MotorTrend; Hagerty/Moravy). Stowed outline
  from official photo 03-Slide-2 (deck seam at the rear-window trailing edge); deploy ~1.2 s and pose from Sawyer
  Merritt's 2026-10-16 wing video (frames 0.5-1.7 s).
- Barge boards: "Idaho-shaped body panels that tilt open towards the rear" behind the front fenders, below a winglet
  (MotorTrend); outline measured on official side photo 02-Pilot (2.251 mm/px).
- Headlights: "the rear edge of two small triangular panels at the front corners drops down" (MotorTrend; Motor1).
- Hood vent: "a rectangular panel at the base of the windshield tilts down" (MotorTrend).
- Front wings: two wings ahead of the front wheels move up and down, up to 80 degrees (MotorTrend; Hagerty/Moravy).
- Skirts: "black, curtain-like structures lower from the rocker panels to the ground"; four independent skirts; front and
  rear retract at speed (MotorTrend; Hagerty; Not a Tesla App).
All positions are reference-calibrated estimates, not Tesla CAD.

Blender frame: +X forward from the wheelbase midpoint, +Y = car's LEFT, +Z up, metres.
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix
import roadster_body as RB

V = Vector
# body material slots after build_roadster.extract_vent: 0 Paint, 1 Glass, 2 BlackGloss, 3 Underbody, 4 RearGlass
PAINT, GLASS, BLACK, UNDER, REARGLASS = 0, 1, 2, 3, 4

GAP = 0.0030               # visible shut-line gap, taken from the moving panel (the shell keeps the cut line)
SPINE_HALF = 0.150         # fixed centre roof spine, measured on Slide-3 (+-0.158 m, perspective-corrected)
HINGES = {}                # name -> {"pivot", "axis", "travel": {state: degrees}}  (exported)
WING = {}                  # rear-wing kinematics (exported)
SKIRTS = {}                # skirt drop axes / reach (exported)


# ============================================================================= small helpers
def row_z(row, x):
    return RB.row_at(row, x)[1]


def row_y(row, x):
    return RB.row_at(row, x)[0]


def inside_poly(px, pz, poly):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, zi = poly[i]
        xj, zj = poly[j]
        if (zi > pz) != (zj > pz) and px < (xj - xi) * (pz - zi) / (zj - zi) + xi:
            inside = not inside
        j = i
    return inside


def poly_edge_distance(px, pz, poly):
    best = 1e9
    for i in range(len(poly)):
        a, b = V((*poly[i - 1], 0)), V((*poly[i], 0))
        p = V((px, pz, 0))
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
        best = min(best, (a + ab * t - p).length)
    return best


def bisect(bm, co, no, box):
    """Cut the faces whose centre lies in box ((x0,x1),(y0,y1),(z0,z1)) along the plane (co, no)."""
    (x0, x1), (y0, y1), (z0, z1) = box
    faces = []
    for f in bm.faces:
        c = f.calc_center_median()
        if x0 <= c.x <= x1 and y0 <= c.y <= y1 and z0 <= c.z <= z1:
            faces.append(f)
    if not faces:
        return
    edges = list({e for f in faces for e in f.edges})
    verts = list({v for f in faces for v in f.verts})
    bmesh.ops.bisect_plane(bm, geom=verts + edges + faces, dist=1e-7, plane_co=V(co), plane_no=V(no).normalized())


def segment_plane(p, q):
    """Plane containing the XZ segment p->q and the Y axis (side-view cut line)."""
    d = V((q[0] - p[0], 0.0, q[1] - p[1]))
    return V((p[0], 0.0, p[1])), V((d.z, 0.0, -d.x)).normalized()


def new_object(name, bm, mats):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    return ob


def copy_faces(src_bm, faces, uv=True):
    """New bmesh holding copies of `faces` (material, smooth flag and UVs kept)."""
    nbm = bmesh.new()
    uv_s = src_bm.loops.layers.uv.active if uv else None
    uv_d = nbm.loops.layers.uv.new("UVMap") if uv_s else None
    vmap = {}
    for f in faces:
        vs = []
        for v in f.verts:
            if v not in vmap:
                vmap[v] = nbm.verts.new(v.co)
            vs.append(vmap[v])
        try:
            nf = nbm.faces.new(vs)
        except ValueError:
            continue
        nf.material_index = f.material_index
        nf.smooth = f.smooth
        if uv_s:
            for ls, ld in zip(f.loops, nf.loops):
                ld[uv_d].uv = ls[uv_s].uv
    nbm.normal_update()
    return nbm


def extract(ob, pred, name):
    """Move the faces of `ob` matching pred(face) into a new object. Returns (new object, its source-space bmesh copy)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    sel = [f for f in bm.faces if pred(f)]
    nbm = copy_faces(bm, sel)
    keep = nbm.copy()
    bmesh.ops.delete(bm, geom=sel, context='FACES')
    bm.to_mesh(ob.data)
    ob.data.update()
    bm.free()
    piece = new_object(name, nbm, list(ob.data.materials))
    return piece, keep, len(sel)


def apply_modifier(ob, mod):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.select_set(False)


def solidify(ob, thickness, mat_offset=0, rim_offset=0, offset=-1.0):
    m = ob.modifiers.new("Solidify", 'SOLIDIFY')
    m.thickness = thickness
    m.offset = offset
    m.use_rim = True
    m.use_even_offset = True
    m.use_quality_normals = True
    m.material_offset = mat_offset
    m.material_offset_rim = rim_offset
    apply_modifier(ob, m)


def inset_border(ob, d):
    """Pull the open border of a panel back along its own surface by d (the shut-line gap)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    dirs = {}
    for e in bm.edges:
        if not e.is_boundary:
            continue
        f = e.link_faces[0]
        a, b = e.verts
        t = b.co - a.co
        if t.length < 1e-9:
            continue
        t.normalize()
        n = f.normal.cross(t)
        if n.dot(f.calc_center_median() - (a.co + b.co) * 0.5) < 0:
            n = -n
        for v in (a, b):
            dirs.setdefault(v, []).append(n)
    for v, ds in dirs.items():
        m = sum(ds, V())
        if m.length < 1e-9:
            continue
        m.normalize()
        c = max(0.5, sum(m.dot(x) for x in ds) / len(ds))
        v.co += m * (d / c)
    bm.to_mesh(ob.data)
    ob.data.update()
    bm.free()


def smooth_shade(ob, angle=28.0):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    lim = math.radians(angle)
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        if len(e.link_faces) == 2:
            a, b = e.link_faces
            e.smooth = a.normal.angle(b.normal, 0.0) < lim and a.material_index == b.material_index
        else:
            e.smooth = True
    bm.to_mesh(ob.data)
    ob.data.update()
    bm.free()


def set_pivot(ob, p):
    p = V(p)
    ob.data.transform(Matrix.Translation(ob.location - p))
    ob.location = p


def ensure_uvs(ob):
    if ob.data.uv_layers:
        return
    for o in bpy.context.selected_objects:
        o.select_set(False)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.01, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    ob.select_set(False)


def merge_into(dst, src):
    """Append src's geometry (world space) to dst, remapping materials; src is deleted."""
    mats = list(dst.data.materials)
    remap = []
    for m in src.data.materials:
        if m not in mats:
            dst.data.materials.append(m)
            mats.append(m)
        remap.append(mats.index(m))
    bm = bmesh.new()
    bm.from_mesh(dst.data)
    sbm = bmesh.new()
    sbm.from_mesh(src.data)
    sbm.transform(dst.matrix_world.inverted() @ src.matrix_world)
    uv_d = bm.loops.layers.uv.active or bm.loops.layers.uv.new("UVMap")
    uv_s = sbm.loops.layers.uv.active
    vmap = {v: bm.verts.new(v.co) for v in sbm.verts}
    for f in sbm.faces:
        try:
            nf = bm.faces.new([vmap[v] for v in f.verts])
        except ValueError:
            continue
        nf.material_index = remap[f.material_index] if f.material_index < len(remap) else 0
        nf.smooth = f.smooth
        if uv_s:
            for ls, ld in zip(f.loops, nf.loops):
                ld[uv_d].uv = ls[uv_s].uv
    for e in sbm.edges:
        if not e.smooth:
            ne = bm.edges.get([vmap[v] for v in e.verts])
            if ne:
                ne.smooth = False
    bm.to_mesh(dst.data)
    dst.data.update()
    bm.free()
    sbm.free()
    me = src.data
    bpy.data.objects.remove(src, do_unlink=True)
    if me.users == 0:
        bpy.data.meshes.remove(me)


def socket(body, piece_bm, depth, jamb_mat, floor_mat=None, depth_fn=None, slot_map=None):
    """Add the jamb (and optionally the floor) a panel closes onto: the panel's open border extruded `depth`
    into the shell along the surface normal, and a floor that is the panel surface offset by `depth`."""
    pbm = piece_bm
    pbm.normal_update()
    off = {}
    for v in pbm.verts:
        d = depth_fn(v.co) if depth_fn else depth
        off[v] = v.co - v.normal * d
    bm = bmesh.new()
    bm.from_mesh(body.data)
    nv = {}

    def vert(key, co):
        if key not in nv:
            nv[key] = bm.verts.new(co)
        return nv[key]

    for e in pbm.edges:
        if not e.is_boundary:
            continue
        a, b = e.verts
        f = e.link_faces[0]
        q = [vert(('s', a), a.co), vert(('s', b), b.co), vert(('o', b), off[b]), vert(('o', a), off[a])]
        try:
            nf = bm.faces.new(q)
        except ValueError:
            continue
        nf.material_index = jamb_mat
        nf.smooth = False
        nf.normal_update()
        # visible side faces the opening (away from the panel interior)
        if nf.normal.dot((a.co + b.co) * 0.5 - f.calc_center_median()) < 0:
            nf.normal_flip()
    if floor_mat is not None:
        for f in pbm.faces:
            try:
                nf = bm.faces.new([vert(('o', v), off[v]) for v in f.verts])
            except ValueError:
                continue
            nf.material_index = floor_mat
            nf.smooth = True
            nf.normal_update()
            if nf.normal.dot(f.normal) < 0:
                nf.normal_flip()
    bm.to_mesh(body.data)
    body.data.update()
    bm.free()


def hinge(name, pivot, axis, travel):
    a = V(axis).normalized()
    HINGES[name] = {"pivot": [float(c) for c in pivot], "axis": [float(c) for c in a], "travel": travel}


def open_sign(pivot, axis, probe, want):
    """+1 or -1: the rotation sign that moves `probe` along `want` (small-angle test)."""
    a = V(axis).normalized()
    r = V(probe) - V(pivot)
    return 1.0 if a.cross(r).dot(V(want)) > 0 else -1.0


# ============================================================================= doors
# Side-view outline of the left door (x, z). Front: shut line behind the barge board (02-Pilot), the A-pillar base
# diagonal, the door-glass front frame bar (second bar), the windshield header line at the roof. Rear: the second
# rear frame bar (door glass / quarter glass) and the mid-door cut. Bottom: the black-sill crease (row 3).
DOOR_FRONT_X0 = 0.776          # shut line at the sill
DOOR_XZ = [(0.870, 0.673), (0.660, 0.786), (0.296, 1.108), (0.296, 1.40), (-0.500, 1.40), (-0.490, 1.123),
           (-0.366, 0.891), (-0.200, None)]
HINGE_LOW = V((0.655, 0.0, 0.775))      # main hinge behind the A-pillar base (y filled from the surface)
HINGE_HIGH = V((0.300, SPINE_HALF, 1.118))   # second hinge, upper centre edge of the windshield
DOOR_OPEN_DEG = 98.0           # matched against Slide-3 (doors stand ~vertical, splayed out) and World


_OUTLINE = []


def door_outline():
    """Closed XZ polygon of the door (left side); the bottom follows row 3 two centimetres low (material decides)."""
    if _OUTLINE:
        return _OUTLINE
    pts = [(DOOR_FRONT_X0, row_z(3, DOOR_FRONT_X0) - 0.02)]
    pts += [p for p in DOOR_XZ[:-1]]
    xr = DOOR_XZ[-1][0]
    pts.append((xr, row_z(3, xr) - 0.02))
    n = 24
    for k in range(1, n):
        x = xr + (DOOR_FRONT_X0 - xr) * k / n
        pts.append((x, row_z(3, x) - 0.02))
    _OUTLINE.extend(pts)
    return _OUTLINE


def door_cut_planes():
    """Side-view cut lines: front shut line, A-pillar base, glass front bar, header (x 0.296), roof rear edge,
    rear glass bar, rear panel cut. The bottom edge is the sill crease (a material boundary, no cut needed)."""
    p = [(DOOR_FRONT_X0, row_z(3, DOOR_FRONT_X0))] + list(DOOR_XZ[:-1]) + [(DOOR_XZ[-1][0], row_z(3, DOOR_XZ[-1][0]))]
    return [segment_plane(p[i], p[i + 1]) for i in range(len(p) - 1) if not (p[i][1] == 1.40 and p[i + 1][1] == 1.40)]


def is_door_face(f, side):
    c = f.calc_center_median()
    if c.y * side < SPINE_HALF or f.material_index not in (PAINT, GLASS):
        return False
    if not (-0.62 < c.x < 0.95 and c.z > 0.18):
        return False
    return inside_poly(c.x, c.z, door_outline())


def surface_y(ob_bm, x, z, side):
    """Outer skin y (left = +) at (x, z) found by casting along Y from outside."""
    best = None
    for f in ob_bm.faces:
        c = f.calc_center_median()
        if abs(c.x - x) < 0.03 and abs(c.z - z) < 0.03 and c.y * side > 0.3:
            if best is None or c.y * side > best:
                best = c.y * side
    return best


def cut_doors(body, M):
    out = {}
    bm = bmesh.new()
    bm.from_mesh(body.data)
    box = ((-0.70, 1.00), (-1.30, 1.30), (0.15, 1.45))
    for co, no in door_cut_planes():
        bisect(bm, co, no, box)
    for s in (1, -1):
        bisect(bm, (0, s * SPINE_HALF, 0), (0, 1, 0), ((-0.6, 0.4), (-0.5, 0.5), (0.95, 1.45)))
    bm.to_mesh(body.data)
    body.data.update()
    hy = None
    for side, sfx in ((1, "L"), (-1, "R")):
        piece, src, n = extract(body, lambda f, s=side: is_door_face(f, s), "DoorSkin_" + sfx)
        print("DOOR %s faces %d" % (sfx, n))
        if hy is None:
            hy = surface_y(src, HINGE_LOW.x, HINGE_LOW.z, 1) or 0.88
        socket(body, src, 0.045, BLACK)
        src.free()
        # split: below the belt (DLO, row 6) = door body; above = removable glass panel (glass, roof rail, roof glass)
        bmp = bmesh.new()
        bmp.from_mesh(piece.data)
        glass = [f for f in bmp.faces if f.material_index == GLASS or f.calc_center_median().z > row_z(6, f.calc_center_median().x) + 0.015]
        gbm = copy_faces(bmp, glass)
        bmesh.ops.delete(bmp, geom=glass, context='FACES')
        bmp.to_mesh(piece.data)
        piece.data.update()
        bmp.free()
        gl = new_object("DoorGlass_" + sfx, gbm, [M["Glass"], M["BlackGloss"], M["Paint"], M["BlackGloss"]])
        # remap glass-panel materials: Glass 0, rail paint 2
        bmg = bmesh.new()
        bmg.from_mesh(gl.data)
        for f in bmg.faces:
            f.material_index = 0 if f.material_index == GLASS else 2
        bmg.to_mesh(gl.data)
        gl.data.update()
        bmg.free()
        # door body: paint skin, 35 mm deep, shut faces black, inner skin dark
        piece.name = piece.data.name = "Door_" + sfx
        bmp = bmesh.new()
        bmp.from_mesh(piece.data)
        for f in bmp.faces:
            f.material_index = 0
        bmp.to_mesh(piece.data)
        piece.data.update()
        bmp.free()
        piece.data.materials.clear()
        for k in ("Paint", "Paint", "BlackGloss", "InteriorDark"):
            piece.data.materials.append(M[k] if k in M else M["BlackGloss"])
        inset_border(piece, GAP)
        inset_border(gl, 0.0012)
        solidify(piece, 0.035, mat_offset=3, rim_offset=2)
        solidify(gl, 0.007, mat_offset=0, rim_offset=1)
        smooth_shade(piece, 26)
        smooth_shade(gl, 26)
        ensure_uvs(piece)          # before the cabin trim (which has carbon UVs) is merged in
        ensure_uvs(gl)
        out["Door_" + sfx] = piece
        out["DoorGlass_" + sfx] = gl
    # hinge: main hinge behind the A-pillar base, second hinge at the upper centre edge of the windshield
    for side, sfx in ((1, "L"), (-1, "R")):
        low = V((HINGE_LOW.x, side * (hy - 0.02), HINGE_LOW.z))
        high = V((HINGE_HIGH.x, side * HINGE_HIGH.y, HINGE_HIGH.z))
        axis = (high - low).normalized()
        # the door's rear-lower corner must rise
        sign = open_sign(low, axis, (-0.25, side * 0.97, 0.33), (0, 0, 1))
        hinge("Door_" + sfx, low, axis, {"open": sign * DOOR_OPEN_DEG})
        hinge("DoorGlass_" + sfx, low, axis, {"open": sign * DOOR_OPEN_DEG})
        for k in ("Door_", "DoorGlass_"):
            set_pivot(out[k + sfx], low)
    return out


def sill_plates(M):
    """Carbon tread plates on the sill tops the doors close onto (between the jamb and the tub)."""
    bm = bmesh.new()
    xs = [DOOR_FRONT_X0 - 0.01 + (-0.19 - DOOR_FRONT_X0 + 0.01) * k / 30 for k in range(31)]
    for s in (1, -1):
        rows = []
        for x in xs:
            z = row_z(3, x) - 0.012
            yo = row_y(3, x) - 0.048
            rows.append((bm.verts.new((x, s * yo, z)), bm.verts.new((x, s * 0.700, z + 0.004))))
        for i in range(len(rows) - 1):
            q = [rows[i][0], rows[i + 1][0], rows[i + 1][1], rows[i][1]]
            f = bm.faces.new(q if s > 0 else list(reversed(q)))
            f.material_index = 0
    bm.normal_update()
    for f in bm.faces:
        if f.normal.z < 0:
            f.normal_flip()
    return new_object("Sill_Plates", bm, [M["Carbon"]])


def split_door_trim(interior, doors, side_details, M):
    """Door trim (inner panels) and the door-mounted roof trim move with the doors: split them out of the cabin
    and the canopy frame bars out of Side_Details."""
    poly = door_outline()
    for ob in (interior, side_details):
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        box = ((-0.70, 1.00), (-1.30, 1.30), (0.15, 1.45))
        for co, no in door_cut_planes():
            bisect(bm, co, no, box)
        for s in (1, -1):
            bisect(bm, (0, s * SPINE_HALF, 0), (0, 1, 0), ((-0.6, 0.4), (-0.6, 0.6), (0.90, 1.45)))
        bm.to_mesh(ob.data)
        ob.data.update()
        bm.free()
    moved = {}
    for side, sfx in ((1, "L"), (-1, "R")):
        def side_trim(f, s=side):
            c = f.calc_center_median()
            if not inside_poly(c.x, c.z, poly) or poly_edge_distance(c.x, c.z, poly) < 0.006:
                return False
            belt = row_z(6, c.x)
            if c.z < belt + 0.01:
                return c.y * s > 0.74          # door trim panel (the tub sill inner wall stays)
            # door-mounted roof trim (headliner side panel) under the door's roof glass only: behind the windshield
            # header (x 0.296) and close to the roof, so the A-pillar braces and header trim stay with the body
            return c.y * s > SPINE_HALF + 0.004 and c.z > 0.93 and c.x < 0.291 and c.x > -0.495

        trim, src, n = extract(interior, side_trim, "DoorTrimTmp_" + sfx)
        src.free()
        bmt = bmesh.new()
        bmt.from_mesh(trim.data)
        roof = [f for f in bmt.faces if f.calc_center_median().z > row_z(6, f.calc_center_median().x) + 0.01]
        rbm = copy_faces(bmt, roof)
        bmesh.ops.delete(bmt, geom=roof, context='FACES')
        bmt.to_mesh(trim.data)
        trim.data.update()
        bmt.free()
        rtrim = new_object("RoofTrimTmp_" + sfx, rbm, list(trim.data.materials))
        bars, src, nb = extract(side_details, lambda f, s=side: (lambda c: c.y * s > 0.3 and inside_poly(c.x, c.z, poly)
                                                                         and poly_edge_distance(c.x, c.z, poly) > 0.004)(f.calc_center_median()),
                                "DoorBarsTmp_" + sfx)
        src.free()
        print("DOOR TRIM %s: cabin %d faces, frame bars %d faces" % (sfx, n, nb))
        for tmp, dst in ((trim, doors["Door_" + sfx]), (rtrim, doors["DoorGlass_" + sfx]), (bars, doors["DoorGlass_" + sfx])):
            if len(tmp.data.polygons):
                merge_into(dst, tmp)
            else:
                bpy.data.objects.remove(tmp, do_unlink=True)
        moved[sfx] = n
    return moved


# ============================================================================= rear wing
X_SEAM = -1.705                       # deck seam = rear-window trailing edge (Slide-2)
END_SEAM = ((-1.705, 0.975), (-2.040, 0.640))    # end-plate front seam on the tail corner (Sawyer frame 0.1 s)
END_BOTTOM = 0.640                    # end-plate lower edge (top of the black lower body, Hero rear photo)
WING_PIVOT = V((-1.860, 0.0, 0.888))  # pitch axis under the wing, ~38 % chord, at the strut tips
WING_LIFT = V((-0.205, 0.0, 0.270))   # deployed travel (World photo / wing video: ~0.27 m up, ~0.2 m back)
WING_PITCH = {"stowed": 0.0, "low_drag": 5.5, "high_downforce": 19.0, "airbrake": 80.0}   # trailing edge up
WING_TIMES = {"deploy_s": 1.2, "pitch_s": 0.35, "airbrake_s": 0.40}


def end_seam_x(z):
    (xa, za), (xb, zb) = END_SEAM
    return xa + (xb - xa) * (z - za) / (zb - za)


def is_wing_face(f):
    if f.material_index != PAINT:
        return False
    c = f.calc_center_median()
    n = f.normal
    if n.x < -0.70:                     # the rear face (tail light band) stays on the body
        return False
    if c.x < X_SEAM and c.z > 0.86 and n.z > 0.30 and abs(c.y) < 1.0:
        return True                     # deck top behind the rear window
    if abs(c.y) > 0.55 and END_BOTTOM < c.z < 1.0 and c.x < end_seam_x(c.z) and c.x < -1.60:
        return True                     # end plates: upper tail corners
    return False


def cut_wing(body, M):
    bm = bmesh.new()
    bm.from_mesh(body.data)
    tailbox = ((-2.30, -1.55), (-1.20, 1.20), (0.55, 1.15))
    bisect(bm, (X_SEAM, 0, 0), (1, 0, 0), ((-1.80, -1.60), (-1.10, 1.10), (0.85, 1.10)))
    co, no = segment_plane(*END_SEAM)
    bisect(bm, co, no, tailbox)
    bisect(bm, (0, 0, END_BOTTOM), (0, 0, 1), ((-2.30, -1.70), (-1.20, 1.20), (0.55, 0.75)))
    bm.to_mesh(body.data)
    body.data.update()
    bm.free()
    wing, src, n = extract(body, is_wing_face, "Rear_Wing")
    print("WING faces", n)
    # wing bay: jamb + carbon floor 70 mm under the stowed panel
    socket(body, src, 0.070, BLACK, floor_mat=UNDER)
    src.free()
    wing.data.materials.clear()
    for k in ("Paint", "Paint", "BlackGloss", "Carbon", "LightRed"):
        wing.data.materials.append(M[k])
    bmw = bmesh.new()
    bmw.from_mesh(wing.data)
    for f in bmw.faces:
        f.material_index = 0
    bmw.to_mesh(wing.data)
    wing.data.update()
    bmw.free()
    inset_border(wing, GAP)
    solidify(wing, 0.032, mat_offset=3, rim_offset=2)       # carbon underside, black rim
    add_wing_light(wing)
    smooth_shade(wing, 30)
    set_pivot(wing, WING_PIVOT)
    struts = wing_struts(M)
    WING.update({"pivot": list(WING_PIVOT), "pitch_axis": [0.0, 1.0, 0.0], "lift": list(WING_LIFT),
                 "pitch_deg": WING_PITCH, "times": WING_TIMES,
                 "path": "z rises with 1-(1-s)^2, x follows s^2 (pops up, then out)",
                 "struts_follow": "translation only; the wing pitches about the strut-tip axis"})
    return wing, struts


def add_wing_light(wing):
    """Second red light strip on the wing's trailing edge (doubled red line when the wing is up, 07-Augments)."""
    bm = bmesh.new()
    bm.from_mesh(wing.data)
    bm.normal_update()
    # trailing-edge rim faces face rearwards
    rim = [f for f in bm.faces if f.normal.x < -0.80 and f.material_index == 2 and abs(f.calc_center_median().y) < 0.88]
    for f in rim:
        f.material_index = 4
    bm.to_mesh(wing.data)
    wing.data.update()
    bm.free()


def wing_struts(M):
    """Two swan-neck struts (Optimus-actuator housings), built in the STOWED pose: tips at the pitch axis, lower
    sections hidden in the bay until the wing rises. Origin = the wing pivot so both move with the lift path."""
    bm = bmesh.new()
    for s in (1, -1):
        y = s * 0.37
        # tip at the pivot, swan neck curving down/forward into a vertical leg that disappears into the bay floor
        path = [(0.000, 0.000), (0.020, -0.030), (0.045, -0.070), (0.060, -0.120), (0.062, -0.190), (0.060, -0.300), (0.060, -0.360)]
        prev = None
        for i, (dx, dz) in enumerate(path):
            p = WING_PIVOT + V((dx, y, dz))
            q = WING_PIVOT + V((path[min(i + 1, len(path) - 1)][0], y, path[min(i + 1, len(path) - 1)][1]))
            r = WING_PIVOT + V((path[max(i - 1, 0)][0], y, path[max(i - 1, 0)][1]))
            t = (q - r).normalized()
            side = V((0, 1, 0))
            w = t.cross(side).normalized() * (0.030 if i > 0 else 0.022)
            ring = [bm.verts.new(p + w + side * 0.008), bm.verts.new(p - w + side * 0.008),
                    bm.verts.new(p - w - side * 0.008), bm.verts.new(p + w - side * 0.008)]
            if prev:
                for k in range(4):
                    bm.faces.new((prev[k], prev[(k + 1) % 4], ring[(k + 1) % 4], ring[k])).material_index = 0
            prev = ring
        # pivot boss (titanium) across the strut tip
        r = bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=0.016, radius2=0.016, depth=0.05,
                                  matrix=Matrix.Translation(WING_PIVOT + V((0, y, 0))) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
        for f in {f for v in r["verts"] for f in v.link_faces}:
            f.material_index = 1
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = new_object("Wing_Struts", bm, [M["BlackGloss"], M["Titanium"]])
    smooth_shade(ob, 40)
    set_pivot(ob, WING_PIVOT)
    return ob


def charge_port(M):
    """Charge port on the bay's front wall, revealed when the wing rises (Not a Tesla App; wing video 6 s, blue light)."""
    bm = bmesh.new()
    x = X_SEAM - 0.012
    zc = row_z(9, X_SEAM) - 0.045
    yc = 0.18
    def quad(p0, p1, p2, p3, m):
        f = bm.faces.new([bm.verts.new(p) for p in (p0, p1, p2, p3)])
        f.material_index = m
    w, h = 0.085, 0.040
    quad(V((x, yc - w / 2, zc - h / 2)), V((x, yc + w / 2, zc - h / 2)), V((x, yc + w / 2, zc + h / 2)), V((x, yc - w / 2, zc + h / 2)), 0)
    for k in range(24):
        a0, a1 = 2 * math.pi * k / 24, 2 * math.pi * (k + 1) / 24
        r0, r1 = 0.012, 0.016
        cx, cz = yc - 0.020, zc
        p = lambda r, a: V((x - 0.001, cx + r * math.cos(a), cz + r * math.sin(a)))
        quad(p(r0, a0), p(r1, a0), p(r1, a1), p(r0, a1), 1)
    bm.normal_update()
    for f in bm.faces:
        if f.normal.x < 0:
            pass
        else:
            f.normal_flip()
    return new_object("Charge_Port", bm, [M["BlackGloss"], M["LightBlue"]])


# ============================================================================= headlight covers (pop-down)
def tri_frame(tri, side):
    a, b, c = (V((x, y * side, z)) for x, y, z in tri)
    n = (b - a).cross(c - a).normalized()
    if n.z < 0:
        n = -n
    return a, b, c, n


def bary_inside(p, a, b, c, n, margin=0.0):
    def side(u, v):
        return ((v - u).cross(p - u)).dot(n)
    s1, s2, s3 = side(a, b), side(b, c), side(c, a)
    pos = s1 >= -margin and s2 >= -margin and s3 >= -margin
    neg = s1 <= margin and s2 <= margin and s3 <= margin
    return pos or neg


HEAD_DEPTH_TOP, HEAD_DEPTH_TIP = 0.072, 0.010
HEAD_OPEN_DEG = 11.0


def headlight_pockets(body, M, tri):
    """Cut the triangular cover panels; return them with the pocket geometry (wedge, deepest at the rear edge)."""
    covers, pockets = {}, []
    for side, sfx in ((1, "L"), (-1, "R")):
        a, b, c, n = tri_frame(tri, side)
        bm = bmesh.new()
        bm.from_mesh(body.data)
        box = ((min(a.x, b.x, c.x) - 0.05, max(a.x, b.x, c.x) + 0.05),
               (min(a.y, b.y, c.y) - 0.05, max(a.y, b.y, c.y) + 0.05), (min(a.z, b.z, c.z) - 0.05, max(a.z, b.z, c.z) + 0.05))
        for p, q in ((a, b), (b, c), (c, a)):
            no = (q - p).cross(n).normalized()
            bisect(bm, p, no, box)
        bm.to_mesh(body.data)
        body.data.update()
        bm.free()

        def pred(f, a=a, b=b, c=c, n=n):
            p = f.calc_center_median()
            return abs((p - a).dot(n)) < 0.035 and bary_inside(p - n * (p - a).dot(n), a, b, c, n)

        cover, src, cnt = extract(body, pred, "HeadlightCover_" + sfx)
        print("HEADLIGHT COVER %s faces %d" % (sfx, cnt))
        top = (a + b) * 0.5
        span = (top - c).length

        def depth(co, top=top, c=c, span=span):
            t = max(0.0, min(1.0, 1.0 - (co - c).dot((top - c).normalized()) / span))   # 0 at the rear edge, 1 at the tip
            return HEAD_DEPTH_TOP + (HEAD_DEPTH_TIP - HEAD_DEPTH_TOP) * t
        socket(body, src, 0.0, BLACK, floor_mat=BLACK, depth_fn=depth)
        src.free()
        cover.data.materials.clear()
        for k in ("Paint", "Paint", "BlackGloss", "BlackGloss"):
            cover.data.materials.append(M[k])
        bmc = bmesh.new()
        bmc.from_mesh(cover.data)
        for f in bmc.faces:
            f.material_index = 0
        bmc.to_mesh(cover.data)
        cover.data.update()
        bmc.free()
        inset_border(cover, 0.0015)
        solidify(cover, 0.004, mat_offset=3, rim_offset=2)
        smooth_shade(cover, 30)
        axis = (b - a).normalized()
        sign = open_sign(c, axis, top, -n)
        hinge("HeadlightCover_" + sfx, c, axis, {"open": sign * HEAD_OPEN_DEG})
        set_pivot(cover, c)
        covers["HeadlightCover_" + sfx] = cover
        pockets.append((a, b, c, n, depth))
    return covers, pockets


def headlight_modules(bm, pockets, mat_led, mat_housing, mat_reflector):
    """Jewel LED module on the pocket's rear wall (faces forward through the opening the dropped cover leaves)."""
    for (a, b, c, n, depth) in pockets:
        da = depth(a)
        top_a, top_b = a - n * 0.004, b - n * 0.004
        bot_a, bot_b = a - n * (da - 0.004), b - n * (da - 0.004)
        fwd = (c - (a + b) * 0.5).normalized()
        # reflector plate on the rear wall
        f = bm.faces.new([bm.verts.new(p + fwd * 0.003) for p in (bot_a, bot_b, top_b, top_a)])
        f.material_index = mat_reflector
        along = b - a
        down = (bot_a - top_a).normalized()
        for k in range(6):
            t0, t1 = 0.10 + k * 0.135, 0.10 + k * 0.135 + 0.095
            p0 = a + along * t0 + down * 0.016 + fwd * 0.006
            p1 = a + along * t1 + down * 0.016 + fwd * 0.006
            q = [p0, p1, p1 + down * 0.020, p0 + down * 0.020]
            f = bm.faces.new([bm.verts.new(p) for p in q])
            f.material_index = mat_led
        # thin DRL line along the lower edge of the module
        p0 = a + along * 0.08 + down * 0.044 + fwd * 0.006
        p1 = a + along * 0.92 + down * 0.044 + fwd * 0.006
        f = bm.faces.new([bm.verts.new(p) for p in (p0, p1, p1 + down * 0.004, p0 + down * 0.004)])
        f.material_index = mat_led


# ============================================================================= barge boards
BARGE_TOP = 0.588            # under the winglet strip (02-Pilot: z 0.608-0.636)
BARGE_OPEN_DEG = 20.0        # rear edge out, opening towards the rear (MotorTrend)


def shut_front_x(z):
    z0 = row_z(3, DOOR_FRONT_X0)
    return DOOR_FRONT_X0 + (0.870 - DOOR_FRONT_X0) * (z - z0) / (0.673 - z0)


def is_barge_face(f, side):
    if f.material_index != PAINT:
        return False
    c = f.calc_center_median()
    if c.y * side < 0.80 or not (0.70 < c.x < 1.35):
        return False
    if c.z > BARGE_TOP or c.z < row_z(3, c.x) - 0.03:
        return False
    return c.x > shut_front_x(c.z) + 0.002


def cut_barges(body, M):
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bisect(bm, (0, 0, BARGE_TOP), (0, 0, 1), ((0.70, 1.35), (-1.2, 1.2), (0.45, 0.70)))
    bm.to_mesh(body.data)
    body.data.update()
    bm.free()
    out = {}
    arch_c = V((RB.FRONT_AXLE, 0, 0.3465))
    arch_r = 0.3425 + 0.022
    for side, sfx in ((1, "L"), (-1, "R")):
        piece, src, n = extract(body, lambda f, s=side: is_barge_face(f, s), "Barge_" + sfx)
        print("BARGE %s faces %d" % (sfx, n))
        # front edge = the arch: hinge from the top-front to the bottom-front corner of the panel
        zt, zb = BARGE_TOP, row_z(3, 1.06)
        xt = arch_c.x - math.sqrt(arch_r ** 2 - (zt - arch_c.z) ** 2)
        xb = arch_c.x - math.sqrt(max(arch_r ** 2 - (zb - arch_c.z) ** 2, 0.0))
        ys = [v.co.y for v in src.verts]
        yt = max(ys, key=lambda y: y * side) if ys else side * 0.97
        top = V((xt, yt - side * 0.004, zt))
        bot = V((xb, yt - side * 0.002, zb))
        socket(body, src, 0.05, BLACK, floor_mat=UNDER)
        src.free()
        piece.data.materials.clear()
        for k in ("Paint", "Paint", "BlackGloss", "Carbon"):
            piece.data.materials.append(M[k])
        bmb = bmesh.new()
        bmb.from_mesh(piece.data)
        for f in bmb.faces:
            f.material_index = 0
        bmb.to_mesh(piece.data)
        piece.data.update()
        bmb.free()
        inset_border(piece, GAP)
        solidify(piece, 0.008, mat_offset=3, rim_offset=2)
        smooth_shade(piece, 30)
        axis = (top - bot).normalized()
        probe = V((shut_front_x(0.42) + 0.02, side * 0.98, 0.42))
        sign = open_sign(bot, axis, probe, (0, side, 0))
        hinge("Barge_" + sfx, bot, axis, {"open": sign * BARGE_OPEN_DEG})
        set_pivot(piece, bot)
        out["Barge_" + sfx] = piece
    return out


def skin_bvh(body):
    from mathutils.bvhtree import BVHTree
    bm = bmesh.new()
    bm.from_mesh(body.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree


def skin_y(tree, x, z, side):
    """Outer skin |y| at (x, z), by a ray cast from outside the car towards the centreline."""
    hit = tree.ray_cast(V((x, side * 1.6, z)), V((0, -side, 0)), 1.6)
    return abs(hit[0].y) if hit[0] is not None else None


def winglet(M, tree):
    """Black winglet strip with the amber side marker at its front, above each barge board (02-Pilot: x 0.862-1.132,
    z 0.606-0.638), standing 12 mm proud of the skin."""
    bm = bmesh.new()
    for s in (1, -1):
        xs = [1.132 - (1.132 - 0.862) * k / 14 for k in range(15)]
        top, bot, top_o, bot_o = [], [], [], []
        for x in xs:
            yb = (skin_y(tree, x, 0.622, s) or 0.98) - 0.001
            top.append(bm.verts.new((x, s * yb, 0.638)))
            bot.append(bm.verts.new((x, s * yb, 0.606)))
            top_o.append(bm.verts.new((x, s * (yb + 0.012), 0.6345)))
            bot_o.append(bm.verts.new((x, s * (yb + 0.012), 0.6095)))
        for i in range(len(xs) - 1):
            lens = xs[i] > 0.998
            for (p, q, m) in ((top, top_o, 0), (top_o, bot_o, 1 if lens else 0), (bot_o, bot, 0)):
                f = bm.faces.new((p[i], p[i + 1], q[i + 1], q[i]))
                f.material_index = m
        bm.faces.new((top[0], top_o[0], bot_o[0], bot[0]))
        bm.faces.new((top[-1], bot[-1], bot_o[-1], top_o[-1]))
    bm.normal_update()
    for f in bm.faces:
        c = f.calc_center_median()
        if f.normal.y * (1 if c.y > 0 else -1) < -0.3:
            f.normal_flip()
    return new_object("Winglets", bm, [M["BlackGloss"], M["Amber"]])


# ============================================================================= front wings (in the lower intakes)
FRONT_FLAP = {"low_drag": 0.0, "high_downforce": 26.0, "max": 80.0}


INTAKE_Y = (0.105, 0.555)      # lower fascia intake mouths, either side of the centre bar (Slide-1 matte lower fascia)
INTAKE_Z = (0.192, 0.300)


def cut_intakes(body):
    """Open the two lower front intakes (the matte lower fascia) and line them with a dark duct 0.2 m deep."""
    bm = bmesh.new()
    bm.from_mesh(body.data)
    box = ((2.05, 2.35), (-0.8, 0.8), (0.10, 0.40))
    for s in (1, -1):
        for y in INTAKE_Y:
            bisect(bm, (0, s * y, 0), (0, 1, 0), box)
    for z in INTAKE_Z:
        bisect(bm, (0, 0, z), (0, 0, 1), box)
    bm.to_mesh(body.data)
    body.data.update()
    bm.free()

    def pred(f):
        c = f.calc_center_median()
        return c.x > 2.15 and f.normal.x > 0.6 and INTAKE_Z[0] < c.z < INTAKE_Z[1] and INTAKE_Y[0] < abs(c.y) < INTAKE_Y[1]

    tmp, src, n = extract(body, pred, "IntakeTmp")
    print("INTAKE faces", n)
    socket(body, src, 0.20, BLACK, floor_mat=BLACK)
    src.free()
    me = tmp.data
    bpy.data.objects.remove(tmp, do_unlink=True)
    bpy.data.meshes.remove(me)


def front_wings(M):
    """The two front wings in the lower intakes ahead of the front wheels ("wings in the air intakes", NaTA; "can move
    up and down", MotorTrend; up to 80 degrees, Moravy). Built level (Low Drag); pivot on the quarter-chord line;
    positive travel lifts the trailing edge."""
    out = {}
    chord = 0.100
    for side, sfx in ((1, "L"), (-1, "R")):
        bm = bmesh.new()
        xle, zc = 2.195, 0.246
        yi, yo = side * (INTAKE_Y[0] + 0.012), side * (INTAKE_Y[1] - 0.012)
        prof = []
        for k in range(17):
            t = k / 16
            xc = (1 - math.cos(math.pi * t)) / 2
            th = 0.10 * 5 * (0.2969 * math.sqrt(xc) - 0.1260 * xc - 0.3516 * xc ** 2 + 0.2843 * xc ** 3 - 0.1036 * xc ** 4)
            camber = 0.05 * (1 - (2 * xc - 1) ** 2)
            prof.append((xc, -camber + th / 2, -camber - th / 2))
        rows = []
        for y in (yi, yo):
            up = [bm.verts.new((xle - xc * chord, y, zc + u * chord)) for (xc, u, l) in prof]
            lo = [bm.verts.new((xle - xc * chord, y, zc + l * chord)) for (xc, u, l) in prof]
            rows.append((up, lo))
        (u0, l0), (u1, l1) = rows
        for i in range(len(prof) - 1):
            bm.faces.new((u0[i], u0[i + 1], u1[i + 1], u1[i]))
            bm.faces.new((l0[i], l1[i], l1[i + 1], l0[i + 1]))
        bm.faces.new((u0[-1], l0[-1], l1[-1], u1[-1]))
        bm.faces.new(u0 + list(reversed(l0[1:-1])))
        bm.faces.new(u1 + list(reversed(l1[1:-1])))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        ob = new_object("FrontWing_" + sfx, bm, [M["Carbon"]])
        smooth_shade(ob, 40)
        pivot = V((xle - 0.25 * chord, (yi + yo) / 2, zc))
        sign = open_sign(pivot, (0, 1, 0), (xle - chord, (yi + yo) / 2, zc), (0, 0, 1))
        hinge("FrontWing_" + sfx, pivot, (0, 1, 0), {k: sign * v for k, v in FRONT_FLAP.items()})
        set_pivot(ob, pivot)
        out["FrontWing_" + sfx] = ob
    return out


# ============================================================================= skirts
SKIRT_H = 0.120              # curtain height; hidden in the sill/floor when retracted
SKIRT_REACH = 0.135          # maximum drop (ride height 0.12 m + compliance)


def skirts(M):
    """Four independent suction skirts around the hexagonal sealed floor (patent; MotorTrend 'curtains')."""
    out = {}
    hexa = [(1.55, 0.0), (1.20, 0.86), (-1.15, 0.86), (-1.55, 0.0), (-1.15, -0.86), (1.20, -0.86)]
    parts = {"Skirt_Front": [(5, 0), (0, 1)], "Skirt_L": [(1, 2)], "Skirt_Rear": [(2, 3), (3, 4)], "Skirt_R": [(4, 5)]}
    ztop = 0.112 + SKIRT_H        # retracted: the lower edge flush with the floor skin (z 0.112)
    for name, segs in parts.items():
        bm = bmesh.new()
        for (i, j) in segs:
            a, b = V((*hexa[i], 0)), V((*hexa[j], 0))
            d = (b - a)
            L = d.length
            ang = math.atan2(d.y, d.x)
            mid = (a + b) * 0.5
            r = bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation((mid.x, mid.y, ztop - SKIRT_H / 2))
                                      @ Matrix.Rotation(ang, 4, 'Z') @ Matrix.Diagonal((L, 0.008, SKIRT_H, 1)))
            # wear strip on the lower edge
            r2 = bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation((mid.x, mid.y, ztop - SKIRT_H + 0.006))
                                       @ Matrix.Rotation(ang, 4, 'Z') @ Matrix.Diagonal((L, 0.014, 0.012, 1)))
            for f in {f for v in r2["verts"] for f in v.link_faces}:
                f.material_index = 1
        ob = new_object(name, bm, [M["Rubber"], M["BlackGloss"]])
        c = V((sum(hexa[i][0] for s in segs for i in s) / (2 * len(segs)), sum(hexa[i][1] for s in segs for i in s) / (2 * len(segs)), ztop))
        set_pivot(ob, c)
        out[name] = ob
    SKIRTS.update({"axis": [0.0, 0.0, -1.0], "height": SKIRT_H, "reach": SKIRT_REACH,
                   "note": "retracted lower edge at the floor skin (z 0.112); deployed drops until the edge reaches the surface"})
    return out


# ============================================================================= entry point
def build(body, interior, side_details, M):
    """Cut every moving panel from the finished shell. Returns ({name: moving object}, [static objects], info)."""
    tree = skin_bvh(body)              # the uncut skin, for parts that sit on it
    parts = {}
    parts.update(cut_doors(body, M))
    moved = split_door_trim(interior, parts, side_details, M)
    wing, struts = cut_wing(body, M)
    parts["Rear_Wing"] = wing
    parts["Wing_Struts"] = struts
    parts.update(cut_barges(body, M))
    cut_intakes(body)
    parts.update(front_wings(M))
    parts.update(skirts(M))
    for ob in parts.values():
        ensure_uvs(ob)
    statics = [sill_plates(M), charge_port(M), winglet(M, tree)]
    return parts, statics, moved
