"""Build the Roadster in Blender (background mode).

blender -b -P build_roadster.py -- <out_dir> [--views] [--export] [--candidate]
--candidate saves the blend/FBX/manifest under out_dir and never writes the authoritative blend, exports or C++ rig header.
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
import roadster_interior
importlib.reload(roadster_interior)
import roadster_wheels
importlib.reload(roadster_wheels)
import roadster_coldgas
importlib.reload(roadster_coldgas)
import roadster_transform as RT
importlib.reload(RT)
import json
import roadster_materials
importlib.reload(roadster_materials)
from roadster_materials import mat, materials

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else os.path.join(SCRIPT_DIR, "..", "renders")
DO_VIEWS = "--views" in argv
DO_EXPORT = "--export" in argv
CANDIDATE = "--candidate" in argv
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


def front_lights(M, pockets):
    """Nose LED bar under the hood lip and the jewel headlight modules on the rear walls of the pop-down pockets."""
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
    # the pocket walls/floor are part of the shell (RT.headlight_pockets); the light module faces forward from the
    # rear wall, revealed when the cover's rear edge drops (MotorTrend: "bright jewel-like headlights")
    RT.headlight_modules(bm, pockets, 0, 1, 2)
    bm.normal_update()
    for f in bm.faces:
        if f.normal.x < 0 and f.calc_center_median().x > 1.5:
            f.normal_flip()
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
    # pop-down headlights: the triangular cover panels in the fender inner facets (calibrated front photo) are cut
    # out as moving parts; the shell gets a wedge pocket, deepest under the cover's rear edge
    covers, pockets = RT.headlight_pockets(body, M, HEADLIGHT_TRI)
    for c in arch_cutters():
        b = body.modifiers.new("Arch", 'BOOLEAN')
        b.operation = 'DIFFERENCE'
        b.solver = 'EXACT'
        b.object = c
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.modifier_apply(modifier=b.name)
        bpy.data.objects.remove(c, do_unlink=True)
    return covers, pockets


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
        v.co.z -= 0.075      # duct under the panel: its rear edge tilts down ~5 cm into it when the vent opens
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


def coldgas_shell_openings(body):
    """Open the body skin at the detailed hardware, using its authored positions.

    These are reference-based packaging apertures, not manufacturer CAD. The
    rear mouth follows the detailed frame; the floor opening feeds its ramp.
    Nozzle cutters follow the same exits/axes exported for force and plume use.
    """
    cuts = []
    outline = roadster_coldgas._box_outline()
    bm = bmesh.new()
    rear = [bm.verts.new((-2.24, y, z)) for y, z in outline]
    front = [bm.verts.new((-1.92, y, z)) for y, z in outline]
    bm.faces.new(rear)
    bm.faces.new(list(reversed(front)))
    for i in range(len(outline)):
        j = (i + 1) % len(outline)
        bm.faces.new((rear[i], front[i], front[j], rear[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("FanMouthCut")
    bm.to_mesh(mesh)
    bm.free()
    cut = bpy.data.objects.new("FanMouthCut", mesh)
    bpy.context.collection.objects.link(cut)
    cuts.append(cut)
    # Duct ramp replaces this strip of the original closed underfloor. Keep the
    # lower outer sills and all suspension/body mounts outside the opening.
    bpy.ops.mesh.primitive_cube_add(size=1, location=(-1.78, 0, 0.24))
    intake = bpy.context.active_object
    intake.name = "FanIntakeCut"
    intake.scale = (0.74, 1.18, 0.38)
    cuts.append(intake)
    for name, d in roadster_coldgas.nozzle_manifest().items():
        if name.startswith("Rear_"):
            continue  # rear mouths are inside the already-open fan-box frame
        direction = Vector(d["dir"])
        centre = Vector(d["pos"]) - direction * 0.075
        bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=d["exit_d"] * 0.5 + 0.004,
                                          depth=0.23, location=centre)
        c = bpy.context.active_object
        c.name = "NozzleAperture_" + name
        c.rotation_euler = direction.to_track_quat('Z', 'Y').to_euler()
        cuts.append(c)
    for c in cuts:
        # The FBX material contract contains real material names only. Boolean
        # cutters with no slot otherwise append a None slot to the body mesh.
        c.data.materials.append(body.data.materials[0])
        bpy.context.view_layer.objects.active = body
        modifier = body.modifiers.new("ColdGasAperture", 'BOOLEAN')
        modifier.operation = 'DIFFERENCE'
        modifier.solver = 'EXACT'
        modifier.material_mode = 'INDEX'
        modifier.object = c
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        cut_mesh = c.data
        bpy.data.objects.remove(c, do_unlink=True)
        if cut_mesh.users == 0:
            bpy.data.meshes.remove(cut_mesh)


def rear_fascia(M):
    """Side fascia beside the detailed box, preserving the pawn's asset name.

    The old P.rear_face also supplied a closed box, four shrouds and a nozzle
    rail; those would overlap and obscure roadster_coldgas's detailed parts.
    """
    bm = bmesh.new()
    for s in (-1, 1):
        roadster_parts.box(bm, Vector((roadster_body.TAIL_X - 0.004, s * 0.728, 0.535)),
                           (0.01, 0.164, 0.13), mat=1)
    return roadster_parts.new_obj("Rear_Diffuser", bm, [M["Carbon"], M["BlackGloss"]])


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
    covers, pockets = apply_arches(body, M)
    vent = extract_vent(body, M)
    coldgas_shell_openings(body)
    P = roadster_parts
    static = [body]
    static.append(front_lights(M, pockets))
    static.append(P.tail_lights(M))
    side_details = conform(P.side_details(M), body, 0.0012)
    static.append(side_details)
    static.append(P.splitter(M))
    pl, pt = P.plate(M)
    static += [pl, pt]
    static.append(rear_fascia(M))
    coldgas = roadster_coldgas.build(M)
    fan_data = roadster_coldgas.fan_manifest()
    fans = [coldgas[n] for n in fan_data]
    coldgas_static = [o for n, o in coldgas.items() if n not in fan_data]
    # The detailed modules preserve the names loaded by RoadsterPawn. The legacy
    # P.interior includes wheel-well boxes above the front fenders and is no
    # longer used. All cabin, yoke and display geometry now comes from one source.
    interior = roadster_interior.build(M)
    # the transforming body: doors (+ removable roof glass), tail wing + struts, barge boards, front wings, skirts.
    # Cut from the finished shell, so it must run after every other shell operation and before shading.
    tparts, tstatics, _ = RT.build(body, interior["Interior"], side_details, M)
    tparts.update(covers)
    smooth(body, 24)
    static += tstatics
    # hood vent: hinge on its front edge, the rear edge tilts down into the duct (MotorTrend: "tilts down")
    RT.hinge("Hood_Vent", list(vent.location), (0, 1, 0),
             {"open": RT.open_sign(vent.location, (0, 1, 0), vent.location - Vector((0.30, 0, 0)), (0, 0, -1)) * 9.0})
    # wheels, brakes and the pushrod suspension (all four corners) + export/suspension_rig.json
    wobj = roadster_wheels.build(M, write_rig=not CANDIDATE,
                                 rig_path=os.path.join(OUT, "suspension_rig.json") if CANDIDATE else roadster_wheels.RIG_JSON)
    wf, wr = wobj["Wheel_Front"], wobj["Wheel_Rear"]
    dfront, cfront = wobj["Brake_Front_Disc"], wobj["Brake_Front_Caliper"]
    drear, crear = wobj["Brake_Rear_Disc"], wobj["Brake_Rear_Caliper"]
    susp = [o for n, o in wobj.items() if n.startswith("Susp_")]
    susp_static = [o for o in susp if o.name.endswith("_Mounts") or o.name == "Susp_F_SteeringRack"]
    susp_moving = [o for o in susp if o not in susp_static]

    # preview instances of the wheels / brakes at the four corners (left side = +Y faces outward)
    corners = {"FL": (FA, TRACK_F / 2, R_FRONT, wf, dfront, cfront), "FR": (FA, -TRACK_F / 2, R_FRONT, wf, dfront, cfront),
               "RL": (RA, TRACK_R / 2, R_REAR, wr, drear, crear), "RR": (RA, -TRACK_R / 2, R_REAR, wr, drear, crear)}
    templates = {wf, wr, dfront, cfront, drear, crear}
    for key, (x, y, r, w, d, c) in corners.items():
        # Detailed discs/calipers already contain their axial offset from the
        # wheel centre. The runtime pawn must use Centre, with no legacy -7 cm In.
        for src in (w, d, c):
            inst = src.copy()
            inst.name = "Preview_%s_%s" % (src.name, key)
            bpy.context.collection.objects.link(inst)
            side = 1 if y > 0 else -1
            inst.location = (x, y, r)
            inst.rotation_euler = (0, 0, 0 if side > 0 else math.pi)
    for t in templates:
        t.hide_render = True
        t.hide_viewport = True

    # export manifest: every exported mesh with its pivot (Blender coordinates, metres)
    global EXPORT_SET
    EXPORT_SET = {
        "static": [o.name for o in static] + [o.name for o in coldgas_static] + [o.name for o in interior.values() if o.name not in ("Yoke",)]
                  + [o.name for o in susp_static],
        "moving": {o.name: list(o.location) for o in [vent, interior["Yoke"]] + list(tparts.values()) + fans + susp_moving},
        "hinges": RT.HINGES,
        "wing": RT.WING,
        "skirts": RT.SKIRTS,
        "wheels": {o.name: [0, 0, 0] for o in (wf, wr, dfront, cfront, drear, crear)},
        "hardpoints": {k: [v[0], v[1], v[2]] for k, v in corners.items()},
        "nozzles": roadster_coldgas.nozzle_manifest(),
        "fans": fan_data,
        "geometry_sources": {"interior": "roadster_interior.build", "wheels": "roadster_wheels.build (wheels, brakes, Susp_* rig)",
                             "coldgas": "roadster_coldgas.build (all returned meshes; no legacy fan box, rotor, vessel or nozzles)"},
        "export_contract": {
            "static": "car-space evaluated geometry with identity object transform; UE component transform stays identity",
            "moving": "pivot-local evaluated geometry; translation removed; existing rest rotations baked except Yoke",
            "Yoke": "unrotated local geometry; UE applies base pitch -24deg then local X steering at manifest pivot",
            "wheels": "wheel-centred LEFT-side templates; right side uses runtime 180deg flip; discs/calipers already contain inboard offset",
            "screen_uvs": "preserved authored 0..1 UVs; normals face driver; no smart projection on screens",
            "nozzles": "car-space exit points in metres; dir is unit exhaust/plume direction; force on car is -dir; Blender +Y is LEFT",
            "fans": "35deg rest pitch baked into pivot-local mesh; animate about fans[name].axis in car frame; no additional rest pitch",
            "shell_openings": "reference-based fan mouth, underfloor intake and lift/yaw apertures; not manufacturer CAD",
            "transforming_body": "Door_*/DoorGlass_*/HeadlightCover_*/Barge_*/FrontWing_*/Hood_Vent rotate about hinges[name] "
                                 "(pivot = mesh origin, unit axis, signed degrees per state, Blender right-hand rule). "
                                 "Rear_Wing: origin on the pitch axis; translate by wing.lift * path(s) then pitch about +Y. "
                                 "Wing_Struts follow the translation only. Skirt_*: translate along skirts.axis.",
        },
    }
    validate_cabin_contract(interior)
    validate_coldgas_contract(coldgas)

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

    blend_path = os.path.join(OUT, "RoadsterCandidate.blend") if CANDIDATE else os.path.join(SCRIPT_DIR, "..", "Roadster.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    if DO_EXPORT:
        export_fbx()


def export_fbx():
    exp = os.path.join(OUT, "export") if CANDIDATE else os.path.join(SCRIPT_DIR, "..", "export")
    os.makedirs(exp, exist_ok=True)
    names = EXPORT_SET["static"] + list(EXPORT_SET["moving"].keys()) + list(EXPORT_SET["wheels"].keys())
    for n in names:
        ob = bpy.data.objects[n]
        # Export evaluated copies so authored transforms, visibility and geometry
        # remain intact even on failure. Explicit car-space baking removes any
        # dependency on the FBX importer's TransformVertexToAbsolute default.
        hidden = ob.hide_viewport
        ob.hide_viewport = False
        bpy.context.view_layer.update()
        depsgraph = bpy.context.evaluated_depsgraph_get()
        evaluated = ob.evaluated_get(depsgraph)
        mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
        bake = ob.matrix_world.copy()
        if n in EXPORT_SET["moving"] or n in EXPORT_SET["wheels"]:
            bake.translation = (0, 0, 0)
            if n == "Yoke":
                bake = Matrix.Diagonal((*ob.scale, 1.0))
        mesh.transform(bake)
        export_ob = bpy.data.objects.new("Export_" + n, mesh)
        bpy.context.collection.objects.link(export_ob)
        EXPORT_SET.setdefault("material_slots", {})[n] = [m.name for m in mesh.materials]
        for o in bpy.context.selected_objects:
            o.select_set(False)
        export_ob.select_set(True)
        bpy.context.view_layer.objects.active = export_ob
        if not mesh.uv_layers:
            # carbon weave, suede and paint flakes need UVs: smart-project anything that has none
            bpy.ops.object.mode_set(mode='EDIT')
            bpy.ops.mesh.select_all(action='SELECT')
            bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.01, scale_to_bounds=False)
            bpy.ops.object.mode_set(mode='OBJECT')
        try:
            bpy.ops.export_scene.fbx(filepath=os.path.join(exp, "SM_%s.fbx" % n), use_selection=True, apply_scale_options='FBX_SCALE_UNITS',
                                     axis_forward='X', axis_up='Z', object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE',
                                     bake_space_transform=True, add_leaf_bones=False)
        finally:
            bpy.data.objects.remove(export_ob, do_unlink=True)
            bpy.data.meshes.remove(mesh)
            ob.hide_viewport = hidden
    with open(os.path.join(exp, "manifest.json"), "w") as f:
        json.dump(EXPORT_SET, f, indent=1)
    write_rig_header(EXPORT_SET, os.path.join(exp, "RoadsterRig.h") if CANDIDATE else RIG_HEADER)
    print("EXPORTED", len(names))


RIG_HEADER = os.path.join(SCRIPT_DIR, "..", "..", "Unreal", "RoadsterSim", "Source", "RoadsterSim", "Public", "RoadsterRig.h")


def write_rig_header(es, path=None):
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
    lines.append("\t// D is exhaust/plume direction. Vehicle force is -D. All points are nozzle EXIT centres.")
    lines.append("\tstruct FNozzle { const char* Name; double X, Y, Z, DX, DY, DZ, ExitDiameter, ThroatDiameter, FeedLineDiameter; };")
    lines.append("\tstatic const FNozzle Nozzles[] = {")
    for n, d in nz.items():
        lines.append('\t\t{ "%s", %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f },' %
                     (n, *d["pos"], *d["dir"], d["exit_d"], d["throat_d"], d["feed_line_d"]))
    if not nz:
        lines.append('\t\t{ "none", 0, 0, 0, 0, 0, 0, 0, 0, 0 },')
    lines.append("\t};")
    lines.append("\tstatic const int NumNozzles = %d;" % len(nz))
    fans = es.get("fans", {})
    lines.append("\t// Rest pitch is BAKED in each exported fan. Rotate around A (car frame); E is airflow exhaust.")
    lines.append("\tstruct FFan { const char* Name; double X, Y, Z, AX, AY, AZ, EX, EY, EZ, PitchDegrees, TipDiameter; int Blades; };")
    lines.append("\tstatic const FFan Fans[] = {")
    for n, d in fans.items():
        lines.append('\t\t{ "%s", %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.4f, %.6f, %d },' %
                     (n, *d["pivot"], *d["axis"], *d["exhaust"], d["pitch_deg"], d["tip_d"], d["blades"]))
    if not fans:
        lines.append('\t\t{ "none", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },')
    lines.append("\t};")
    lines.append("\tstatic const int NumFans = %d;" % len(fans))
    # transforming body: hinge axes (Blender frame; positive angle = right-hand rule about A)
    hinges = {k: v for k, v in es.get("hinges", {}).items() if v}
    lines.append("\t// Moving body panels. Mesh origin = P (a point on the hinge axis). Angles are signed degrees about A")
    lines.append("\t// (Blender right-hand rule): Open = fully open / deployed; Mid = front wings' High Downforce; Max = travel limit.")
    lines.append("\tstruct FHinge { const char* Name; double PX, PY, PZ, AX, AY, AZ, Open, Mid, Max; };")
    lines.append("\tstatic const FHinge Hinges[] = {")
    for n, h in hinges.items():
        t = h["travel"]
        opn = t.get("open", t.get("high_downforce", 0.0))
        mid = t.get("high_downforce", opn)
        mx = t.get("max", opn)
        lines.append('\t\t{ "%s", %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.4f, %.4f, %.4f },' % (n, *h["pivot"], *h["axis"], opn, mid, mx))
    if not hinges:
        lines.append('\t\t{ "none", 0, 0, 0, 0, 1, 0, 0, 0, 0 },')
    lines.append("\t};")
    lines.append("\tstatic const int NumHinges = %d;" % len(hinges))
    w = es.get("wing", {})
    if w:
        lines.append("\t// Rear wing: stowed = mesh at the pivot; deployed = pivot + Lift * path(s); pitch about +Y (trailing edge up).")
        lines.append("\tstatic const double WingPivot[3] = { %.6f, %.6f, %.6f };" % tuple(w["pivot"]))
        lines.append("\tstatic const double WingLift[3] = { %.6f, %.6f, %.6f };" % tuple(w["lift"]))
        p = w["pitch_deg"]
        lines.append("\tstatic const double WingPitchLowDrag = %.3f, WingPitchHighDownforce = %.3f, WingPitchAirbrake = %.3f;"
                     % (p["low_drag"], p["high_downforce"], p["airbrake"]))
        t = w["times"]
        lines.append("\tstatic const double WingDeploySeconds = %.3f, WingPitchSeconds = %.3f, WingAirbrakeSeconds = %.3f;"
                     % (t["deploy_s"], t["pitch_s"], t["airbrake_s"]))
    sk = es.get("skirts", {})
    if sk:
        lines.append("\tstatic const double SkirtHeight = %.4f, SkirtReach = %.4f;   // metres; skirts drop along -Z" % (sk["height"], sk["reach"]))
    lines.append("\tinline const FHinge* FindHinge(const char* Name)")
    lines.append("\t{")
    lines.append("\t\tfor (const FHinge& P : Hinges) { const char* a = P.Name; const char* b = Name; while (*a && *a == *b) { ++a; ++b; } if (*a == *b) return &P; }")
    lines.append("\t\treturn nullptr;")
    lines.append("\t}")
    lines.append("\tinline const FPivot* FindPivot(const char* Name)")
    lines.append("\t{")
    lines.append("\t\tfor (const FPivot& P : Pivots) { const char* a = P.Name; const char* b = Name; while (*a && *a == *b) { ++a; ++b; } if (*a == *b) return &P; }")
    lines.append("\t\treturn nullptr;")
    lines.append("\t}")
    for typename, array, label in (("FNozzle", "Nozzles", "Nozzle"), ("FFan", "Fans", "Fan")):
        lines.append("\tinline const %s* Find%s(const char* Name)" % (typename, label))
        lines.append("\t{")
        lines.append("\t\tfor (const %s& P : %s) { const char* a = P.Name; const char* b = Name; while (*a && *a == *b) { ++a; ++b; } if (*a == *b) return &P; }" % (typename, array))
        lines.append("\t\treturn nullptr;")
        lines.append("\t}")
    lines.append("}")
    with open(path or RIG_HEADER, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def validate_coldgas_contract(parts):
    """Validate the complete hardware and metadata handoff before writing assets."""
    expected = {"Fan_Box", "Thrusters_Rear", "Thrusters_Rocket", "COPV_Rocket", "COPV_Trunk", "ColdGas_Plumbing"}
    expected.update("Fan_%d" % i for i in range(4))
    if set(parts) != expected:
        raise RuntimeError("Detailed cold-gas part contract differs: " + str(set(parts) ^ expected))
    names = EXPORT_SET["static"] + list(EXPORT_SET["moving"]) + list(EXPORT_SET["wheels"])
    if len(names) != len(set(names)) or not expected.issubset(names):
        raise RuntimeError("Cold-gas export contains duplicate or missing parts")
    bpy.context.view_layer.update()
    for name, d in EXPORT_SET["fans"].items():
        ob = parts[name]
        axis = (ob.matrix_world.to_3x3() @ Vector((1, 0, 0))).normalized()
        if (Vector(d["pivot"]) - ob.location).length > 1e-5 or (Vector(d["axis"]) - axis).length > 1e-4:
            raise RuntimeError("Fan metadata differs from authored pose: " + name)
        # Preserve the full authored vector, avoiding accumulated spin drift from
        # the detailed module's five-decimal display-oriented manifest values.
        d["axis"] = list(axis)
        d["exhaust"] = list(-axis)
    if len(EXPORT_SET["nozzles"]) != 10:
        raise RuntimeError("Expected four rear, four lift and two yaw exits")
    for name, d in EXPORT_SET["nozzles"].items():
        if abs(Vector(d["dir"]).length - 1) > 1e-6 or not 0 < d["throat_d"] < d["exit_d"]:
            raise RuntimeError("Invalid nozzle metadata: " + name)
        # Match the actual yaw lathe origin without the source API's 0.1 mm
        # presentation rounding. The aperture and force/plume centre then agree.
        if name.startswith("Yaw_"):
            d["pos"][1] = math.copysign(roadster_coldgas.yaw_exit_y()[0] - 0.002, d["pos"][1])
    EXPORT_SET["coldgas_contract"] = {"parts": sorted(expected), "nozzle_count": 10, "fan_count": 4,
                                       "legacy_coldgas_geometry": False, "fan_rest_pitch_baked": True}


def validate_cabin_contract(parts):
    """Fail early on missing legacy names, flipped displays or invalid yoke pose."""
    bpy.context.view_layer.update()
    required = {"Interior", "Seat_Driver", "Seat_Passenger", "Yoke", "Screen_Bezel", "Screen_Display", "Forward_Display_A", "Forward_Display_B"}
    if set(parts) != required:
        raise RuntimeError("Detailed cabin names differ from the pawn contract: " + str(set(parts) ^ required))
    screens = {}
    for name in ("Screen_Display", "Forward_Display_A", "Forward_Display_B"):
        ob = parts[name]
        if len(ob.data.polygons) != 1 or not ob.data.uv_layers:
            raise RuntimeError("Display must retain one authored UV quad: " + name)
        normal = (ob.matrix_world.to_3x3() @ ob.data.polygons[0].normal).normalized()
        toward_eye = (roadster_interior.EYE - ob.matrix_world.translation).normalized()
        score = float(normal.dot(toward_eye))
        if score < 0.5:
            raise RuntimeError("Display normal faces away from driver: %s (%f)" % (name, score))
        screens[name] = {"normal_dot_toward_eye": score, "location": list(ob.location), "rotation_radians": list(ob.rotation_euler),
                         "material_slots": [m.name for m in ob.data.materials]}
    EXPORT_SET["cabin_contract"] = {"screens": screens, "yoke_pivot": list(parts["Yoke"].location), "yoke_preview_pitch_degrees": math.degrees(parts["Yoke"].rotation_euler.y)}


if __name__ == "__main__":
    main()
