"""Build the Roadster in Blender (background mode).

blender -b -P build_roadster.py -- <out_dir> [--views] [--export]
Blender frame: +X forward, +Y = car's LEFT, +Z up (FBX import into UE flips Y, so UE +Y = right).
"""
import bpy, bmesh, math, sys, os, importlib
from mathutils import Vector, Matrix

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import roadster_body
importlib.reload(roadster_body)
import roadster_parts
importlib.reload(roadster_parts)
import json
import roadster_materials
importlib.reload(roadster_materials)
from roadster_materials import mat, materials

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else os.path.join(SCRIPT_DIR, "..", "renders")
DO_VIEWS = "--views" in argv
DO_EXPORT = "--export" in argv
os.makedirs(OUT, exist_ok=True)

WB = roadster_body.WB
FA, RA = WB / 2, -WB / 2
R_FRONT, R_REAR = 0.3425, 0.3672        # 295/30 R20, 335/30 R21
TRACK_F, TRACK_R = 1.70, 1.68


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    return sc




def build_body(M):
    body = roadster_body.build_body("Roadster_Body")
    for key in roadster_body.MAT_NAMES:
        body.data.materials.append(M[key])
    mirror = body.modifiers.new("Mirror", 'MIRROR')
    mirror.use_axis = (False, True, False)
    mirror.use_clip = True
    mirror.merge_threshold = 0.002
    return body


def arch_cutters():
    cutters = []
    for x, r in ((FA, R_FRONT), (RA, R_REAR)):
        # measured arch: circle ~2 cm outside the tire, top at ~0.70 m; cut only outboard of y=0.62
        for side in (1, -1):
            bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=r + 0.022, depth=0.80, location=(x, side * 1.02, r + 0.004), rotation=(math.pi / 2, 0, 0))
            c = bpy.context.active_object
            c.name = "ArchCut"
            cutters.append(c)
    return cutters


HEADLIGHT_TRI = ((1.90, 0.779, 0.632), (1.95, 0.608, 0.614), (2.15, 0.677, 0.481))   # top-outer, top-inner, bottom


def headlight_frame(side):
    """Pocket triangle (left side mirrored by `side`) and the facet's outward normal."""
    a, b, c = (Vector((x, y * side, z)) for x, y, z in HEADLIGHT_TRI)
    n = (b - a).cross(c - a).normalized()
    if n.z < 0:
        n = -n
    return a, b, c, n


def headlight_pocket_cutter(side, M):
    a, b, c, n = headlight_frame(side)
    bm = bmesh.new()
    top = [bm.verts.new(p + n * 0.05) for p in (a, b, c)]
    bot = [bm.verts.new(p - n * 0.035) for p in (a, b, c)]
    bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    for i in range(3):
        j = (i + 1) % 3
        bm.faces.new((top[i], bot[i], bot[j], top[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("PocketCut")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("PocketCut", me)
    bpy.context.collection.objects.link(ob)
    me.materials.append(M["BlackGloss"])
    return ob


def front_lights(M):
    """Nose LED bar under the hood lip and the pop-down headlight units inside the facet pockets."""
    bm = bmesh.new()

    def quad(p0, p1, p2, p3, mat):
        f = bm.faces.new([bm.verts.new(p) for p in (p0, p1, p2, p3)])
        f.material_index = mat

    # full-width LED bar: 7 mm tall, 2 cm deep light guide just under the hood lip (z 0.456 in the front photo)
    x0 = roadster_body.NOSE_X - 0.004
    n = 48
    for i in range(n):
        ya, yb = -0.645 + 1.29 * i / n, -0.645 + 1.29 * (i + 1) / n
        xa = x0 - 0.012 * (abs(ya) / 0.645) ** 4
        xb = x0 - 0.012 * (abs(yb) / 0.645) ** 4
        quad(Vector((xa, ya, 0.4485)), Vector((xb, yb, 0.4485)), Vector((xb, yb, 0.4555)), Vector((xa, ya, 0.4555)), 0)
    for side in (1, -1):
        a, b, c, nrm = headlight_frame(side)
        depth = 0.028
        A, B, C = a - nrm * depth, b - nrm * depth, c - nrm * depth
        # reflector bowl (aluminium) on the pocket floor
        f = bm.faces.new([bm.verts.new(p) for p in (A, B, C)])
        f.material_index = 2
        # slim LED module: a row of 6 projector cells along the top edge, plus a thin DRL line
        top_dir = (B - A)
        down = ((C - (A + B) * 0.5)).normalized()
        for k in range(6):
            t0, t1 = 0.10 + k * 0.135, 0.10 + k * 0.135 + 0.09
            p0 = A + top_dir * t0 + down * 0.012 + nrm * 0.004
            p1 = A + top_dir * t1 + down * 0.012 + nrm * 0.004
            quad(p0, p1, p1 + down * 0.016, p0 + down * 0.016, 0)
        quad(A + down * 0.004 + nrm * 0.006, B + down * 0.004 + nrm * 0.006, B + down * 0.008 + nrm * 0.006, A + down * 0.008 + nrm * 0.006, 0)
        # black housing walls between the reflector and the body cut
        for p, q in ((A, B), (B, C), (C, A)):
            quad(p, q, q + nrm * (depth - 0.002), p + nrm * (depth - 0.002), 1)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("Front_Lights")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("Front_Lights", me)
    bpy.context.collection.objects.link(ob)
    for k in ("LightWhite", "BlackGloss", "Aluminum"):
        me.materials.append(M[k])
    return ob


def apply_arches(body, M):
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Mirror")
    # Catmull-Clark surfacing of the creased cage (before the arch cuts, so the cuts stay crisp)
    sub = body.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = sub.render_levels = 3
    sub.use_creases = True
    sub.boundary_smooth = 'PRESERVE_CORNERS'
    bpy.ops.object.modifier_apply(modifier=sub.name)
    # headlight pockets: triangular recesses in the fender inner facets (calibrated front photo)
    for side in (1, -1):
        c = headlight_pocket_cutter(side, M)
        b = body.modifiers.new("Pocket", 'BOOLEAN')
        b.operation = 'DIFFERENCE'
        b.solver = 'EXACT'
        b.material_mode = 'TRANSFER'
        b.object = c
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.modifier_apply(modifier=b.name)
        bpy.data.objects.remove(c, do_unlink=True)
    for c in arch_cutters():
        b = body.modifiers.new("Arch", 'BOOLEAN')
        b.operation = 'DIFFERENCE'
        b.solver = 'EXACT'
        b.object = c
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.modifier_apply(modifier=b.name)
        bpy.data.objects.remove(c, do_unlink=True)


def extract_vent(body, M):
    """Split the hood-vent panel off the subdivided body (exactly flush), leave a dark duct under it."""
    bm = bmesh.new()
    bm.from_mesh(body.data)
    vent_faces = [f for f in bm.faces if f.material_index == 5]
    vbm = bmesh.new()
    vmap = {}
    for f in vent_faces:
        vbm.faces.new([vmap.setdefault(v, vbm.verts.new(v.co)) for v in f.verts])
    inner = {v for f in vent_faces for v in f.verts if all(g.material_index == 5 for g in v.link_faces)}
    for v in inner:
        v.co.z -= 0.045
    for f in vent_faces:
        f.material_index = 2
    bm.to_mesh(body.data)
    bm.free()
    body.data.materials.pop(index=5)
    # panel: 2 mm shut-line all round, 4 mm thick, origin on the hinge (front edge, centreline)
    xs = [v.co.x for v in vbm.verts]
    cx = 0.5 * (max(xs) + min(xs)); sx = 1.0 - 0.004 / (max(xs) - min(xs))
    ys = [v.co.y for v in vbm.verts]; sy = 1.0 - 0.004 / (max(ys) - min(ys))
    for v in vbm.verts:
        v.co.x = cx + (v.co.x - cx) * sx
        v.co.y *= sy
    front = max(vbm.verts, key=lambda v: v.co.x - abs(v.co.y))
    hinge = Vector((max(xs) - 0.002, 0.0, front.co.z))
    bmesh.ops.recalc_face_normals(vbm, faces=vbm.faces)
    for v in vbm.verts:
        v.co -= hinge
    mesh = bpy.data.meshes.new("Hood_Vent")
    vbm.to_mesh(mesh)
    vbm.free()
    ob = bpy.data.objects.new("Hood_Vent", mesh)
    bpy.context.collection.objects.link(ob)
    ob.location = hinge
    mesh.materials.append(M["Paint"])
    sol = ob.modifiers.new("Thickness", 'SOLIDIFY')
    sol.thickness = 0.004
    sol.offset = -1.0
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=sol.name)
    for p in mesh.polygons:
        p.use_smooth = True
    return ob


def conform(obj, body, offset):
    """Snap trim strips (built on the design rows) onto the subdivided body surface."""
    sw = obj.modifiers.new("Conform", 'SHRINKWRAP')
    sw.target = body
    sw.wrap_method = 'NEAREST_SURFACEPOINT'
    sw.wrap_mode = 'OUTSIDE_SURFACE'
    sw.offset = offset
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=sw.name)
    return obj


def smooth(obj, angle=28):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle))
    obj.select_set(False)


def simple_wheel(name, x, y, r, width, M):
    """Placeholder wheel used for the silhouette check (detailed wheel comes from roadster_parts)."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=r, depth=width, location=(x, y, r), rotation=(math.pi / 2, 0, 0))
    w = bpy.context.active_object
    w.name = name
    w.data.materials.append(M["Tire"])
    return w


def setup_render(sc, w, h, engine='BLENDER_WORKBENCH'):
    try:
        sc.render.engine = engine
    except TypeError:
        sc.render.engine = 'BLENDER_EEVEE_NEXT'
    if engine == 'BLENDER_WORKBENCH':
        sh = sc.display.shading
        sh.light = 'STUDIO'
        sh.color_type = 'MATERIAL'
        sh.show_object_outline = True
        sh.show_cavity = True
        sh.cavity_type = 'BOTH'
        sh.show_specular_highlight = True
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.36, 0.38, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    sc.world = world
    bpy.ops.object.light_add(type='SUN', location=(0, 0, 10), rotation=(math.radians(40), math.radians(10), math.radians(30)))
    bpy.context.active_object.data.energy = 3.0
    bpy.ops.object.light_add(type='AREA', location=(0, 6, 4))
    a = bpy.context.active_object
    a.data.energy = 3000; a.data.size = 8
    a.rotation_euler = (math.radians(-55), 0, math.radians(180))


def camera(name, loc, rot, ortho=None, lens=50):
    cam_data = bpy.data.cameras.new(name)
    if ortho:
        cam_data.type = 'ORTHO'
        cam_data.ortho_scale = ortho
    else:
        cam_data.lens = lens
    cam = bpy.data.objects.new(name, cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = loc
    cam.rotation_euler = rot
    return cam


def render(sc, cam, path, w, h):
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    sc = reset_scene()
    M = materials()
    body = build_body(M)
    apply_arches(body, M)
    vent = extract_vent(body, M)
    smooth(body, 24)
    P = roadster_parts
    static = [body]
    static.append(front_lights(M))
    static.append(P.tail_lights(M))
    static.append(conform(P.side_details(M), body, 0.0012))
    static.append(P.splitter(M))
    pl, pt = P.plate(M)
    static += [pl, pt]
    static.append(P.rear_face(M))
    rear_thr, rocket_thr = P.thrusters(M)
    copv = P.copv(M)
    interior = P.interior(M)
    wing = P.rear_wing(M)
    barges = P.barge_boards(M)
    skirt = P.skirts(M)
    fans = []
    for k in range(4):
        y = (k - 1.5) * 1.26 / 4
        fans.append(P.fan_rotor(M, "Fan_%d" % k, (roadster_body.TAIL_X - 0.05 + 0.10, y, 0.284)))
    wf = P.wheel(M, "Wheel_Front", R_FRONT, 20, 0.295)
    wr = P.wheel(M, "Wheel_Rear", R_REAR, 21, 0.335)
    dfront, cfront = P.brakes(M, "Brake_Front", 0.205, FA, 1, 0.295)
    drear, crear = P.brakes(M, "Brake_Rear", 0.200, RA, 1, 0.335)

    # preview instances of the wheels / brakes at the four corners (left side = +Y faces outward)
    corners = {"FL": (FA, TRACK_F / 2, R_FRONT, wf, dfront, cfront), "FR": (FA, -TRACK_F / 2, R_FRONT, wf, dfront, cfront),
               "RL": (RA, TRACK_R / 2, R_REAR, wr, drear, crear), "RR": (RA, -TRACK_R / 2, R_REAR, wr, drear, crear)}
    templates = {wf, wr, dfront, cfront, drear, crear}
    for key, (x, y, r, w, d, c) in corners.items():
        for src, dy in ((w, 0.0), (d, -0.07), (c, -0.07)):
            inst = src.copy()
            inst.name = "Preview_%s_%s" % (src.name, key)
            bpy.context.collection.objects.link(inst)
            side = 1 if y > 0 else -1
            inst.location = (x, y + side * dy, r)
            inst.rotation_euler = (0, 0, 0 if side > 0 else math.pi)
    for t in templates:
        t.hide_render = True
        t.hide_viewport = True

    # export manifest: every exported mesh with its pivot (Blender coordinates, metres)
    global EXPORT_SET
    EXPORT_SET = {
        "static": [o.name for o in static] + [rear_thr.name, rocket_thr.name, copv.name] + [o.name for o in interior.values() if o.name not in ("Yoke",)],
        "moving": {o.name: list(o.location) for o in [wing, vent, skirt, interior["Yoke"]] + barges + fans},
        "wheels": {o.name: [0, 0, 0] for o in (wf, wr, dfront, cfront, drear, crear)},
        "hardpoints": {k: [v[0], v[1], v[2]] for k, v in corners.items()},
    }

    if DO_VIEWS:
        setup_render(sc, 3440, 1024)
        # side camera matched to Tesla's Pilot photo: 2.251 mm/px, wheelbase midpoint at px 2330.5, ground at px 845
        mmpx = 0.002251
        cx = (2330.5 - 1720) * mmpx
        cz = (845 - 512) * mmpx
        side = camera("CamSide", (cx, 20, cz), (math.pi / 2, 0, math.pi), ortho=3440 * mmpx)
        render(sc, side, os.path.join(OUT, "side_ortho.png"), 3440, 1024)
        front = camera("CamFront", (12.0, 0, 1.55), (math.radians(85.2), 0, math.pi / 2), lens=105)
        render(sc, front, os.path.join(OUT, "front_persp.png"), 2200, 1040)
        # matched to Tesla's Slide-1 front photo (2200x1040) by least squares on 5 features (residual <= 5 px)
        fm = camera("CamFrontMatch", (22.10, 0, 1.79), (math.radians(90 - 3.25), 0, math.pi / 2), lens=8829 * 36 / 2200)
        render(sc, fm, os.path.join(OUT, "front_match.png"), 2200, 1040)
        q34 = camera("Cam34", (7.0, 5.4, 1.45), (math.radians(83), 0, math.radians(128)), lens=55)
        r34 = camera("CamR34", (-6.6, 5.6, 1.7), (math.radians(80), 0, math.radians(-130)), lens=55)
        render(sc, r34, os.path.join(OUT, "rear_three_quarter.png"), 1600, 1000)
        cab = camera("CamCabin", (-0.15, 0.38, 1.02), (math.radians(84), 0, math.radians(-90)), lens=18)
        render(sc, cab, os.path.join(OUT, "cabin.png"), 1600, 1000)
        render(sc, q34, os.path.join(OUT, "three_quarter.png"), 1600, 1000)
        top = camera("CamTop", (0, 0, 20), (0, 0, math.pi / 2), ortho=5.0)
        render(sc, top, os.path.join(OUT, "top_ortho.png"), 1200, 1200)
        rear = camera("CamRear", (-14.0, 0, 1.6), (math.radians(84), 0, -math.pi / 2), lens=110)
        render(sc, rear, os.path.join(OUT, "rear_persp.png"), 1600, 1000)
        # low rear view like the Gravity photo (camera below the wing, sees its underside)
        rlow = camera("CamRearLow", (-13.0, 0, 0.62), (math.radians(91.2), 0, -math.pi / 2), lens=125)
        render(sc, rlow, os.path.join(OUT, "rear_low.png"), 1600, 1000)

    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SCRIPT_DIR, "..", "Roadster.blend"))
    if DO_EXPORT:
        export_fbx()


def export_fbx():
    exp = os.path.join(SCRIPT_DIR, "..", "export")
    os.makedirs(exp, exist_ok=True)
    names = EXPORT_SET["static"] + list(EXPORT_SET["moving"].keys()) + list(EXPORT_SET["wheels"].keys())
    for n in names:
        ob = bpy.data.objects[n]
        ob.hide_viewport = False; ob.hide_render = False
        saved = ob.location.copy(), ob.rotation_euler.copy()
        if n in EXPORT_SET["moving"] or n in EXPORT_SET["wheels"]:
            ob.location = (0, 0, 0)          # pivot at the mesh origin; Unreal places the component
            if n == "Yoke":
                ob.rotation_euler = (0, 0, 0)
        for o in bpy.context.selected_objects:
            o.select_set(False)
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        if not ob.data.uv_layers:
            # carbon weave, suede and paint flakes need UVs: smart-project anything that has none
            bpy.ops.object.mode_set(mode='EDIT')
            bpy.ops.mesh.select_all(action='SELECT')
            bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.01, scale_to_bounds=False)
            bpy.ops.object.mode_set(mode='OBJECT')
        bpy.ops.export_scene.fbx(filepath=os.path.join(exp, "SM_%s.fbx" % n), use_selection=True, apply_scale_options='FBX_SCALE_UNITS',
                                 axis_forward='X', axis_up='Z', object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE',
                                 bake_space_transform=True, add_leaf_bones=False)
        ob.location, ob.rotation_euler = saved
    with open(os.path.join(exp, "manifest.json"), "w") as f:
        json.dump(EXPORT_SET, f, indent=1)
    write_rig_header(EXPORT_SET)
    print("EXPORTED", len(names))


RIG_HEADER = os.path.join(SCRIPT_DIR, "..", "..", "Unreal", "RoadsterSim", "Source", "RoadsterSim", "Public", "RoadsterRig.h")


def write_rig_header(es):
    """Generated C++ constants (Blender frame, metres) so the game always matches the exported model."""
    lines = ["// GENERATED by Blender/scripts/build_roadster.py (export_fbx) - do not edit by hand.",
             "// Blender frame: metres, +X forward, +Y = car's LEFT, +Z up. Convert with B2U() in the pawn.",
             "#pragma once", "", "namespace RoadsterRig", "{",
             "\tstruct FPivot { const char* Name; double X, Y, Z; };"]
    piv = es["moving"]
    lines.append("\tstatic const FPivot Pivots[] = {")
    for n, p in piv.items():
        lines.append('\t\t{ "%s", %.5f, %.5f, %.5f },' % (n, p[0], p[1], p[2]))
    lines.append("\t};")
    lines.append("\tstatic const int NumPivots = %d;" % len(piv))
    nz = es.get("nozzles", {})
    lines.append("\tstruct FNozzle { const char* Name; double X, Y, Z, DX, DY, DZ, ExitDiameter; };")
    lines.append("\tstatic const FNozzle Nozzles[] = {")
    for n, d in nz.items():
        lines.append('\t\t{ "%s", %.5f, %.5f, %.5f, %.4f, %.4f, %.4f, %.4f },' % (n, *d["pos"], *d["dir"], d.get("exit_d", 0.07)))
    if not nz:
        lines.append('\t\t{ "none", 0, 0, 0, 0, 0, 0, 0 },')
    lines.append("\t};")
    lines.append("\tstatic const int NumNozzles = %d;" % len(nz))
    lines.append("\tinline const FPivot* FindPivot(const char* Name)")
    lines.append("\t{")
    lines.append("\t\tfor (const FPivot& P : Pivots) { const char* a = P.Name; const char* b = Name; while (*a && *a == *b) { ++a; ++b; } if (*a == *b) return &P; }")
    lines.append("\t\treturn nullptr;")
    lines.append("\t}")
    lines.append("}")
    with open(RIG_HEADER, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


main()
