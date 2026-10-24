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


def mat(name, color, rough=0.4, metal=0.0, emit=None, alpha=1.0, coat=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    m.diffuse_color = (*color, alpha)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if coat > 0:
        bsdf.inputs["Coat Weight"].default_value = coat
        bsdf.inputs["Coat Roughness"].default_value = 0.08
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*emit[0], 1)
        bsdf.inputs["Emission Strength"].default_value = emit[1]
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        bsdf.inputs["Transmission Weight"].default_value = 0.0
        m.surface_render_method = 'BLENDED' if hasattr(m, "surface_render_method") else None
    return m


def materials():
    return {
        "Paint": mat("M_Paint_AnoWhite", (0.80, 0.80, 0.78), rough=0.32, coat=0.4),
        "Glass": mat("M_Glass_Gold", (0.16, 0.10, 0.06), rough=0.03, metal=0.3, alpha=0.85),
        "BlackGloss": mat("M_BlackGloss", (0.012, 0.012, 0.014), rough=0.18),
        "Underbody": mat("M_Underbody", (0.02, 0.02, 0.022), rough=0.7),
        "RearGlass": mat("M_Glass_Rear", (0.03, 0.025, 0.02), rough=0.05, metal=0.2),
        "Tire": mat("M_Tire", (0.018, 0.018, 0.018), rough=0.85),
        "Spoke": mat("M_Wheel_Spoke", (0.01, 0.01, 0.012), rough=0.15, metal=0.6),
        "Disc": mat("M_Wheel_AeroDisc", (0.55, 0.56, 0.57), rough=0.35, metal=0.2),
        "Titanium": mat("M_Titanium", (0.62, 0.55, 0.46), rough=0.25, metal=1.0),
        "LightWhite": mat("M_Light_White", (1, 1, 1), emit=((1, 0.97, 0.92), 30)),
        "LightRed": mat("M_Light_Red", (0.5, 0.0, 0.0), emit=((1, 0.02, 0.01), 40)),
        "Amber": mat("M_Light_Amber", (0.6, 0.25, 0.0), emit=((1, 0.4, 0.0), 6)),
        "Carbon": mat("M_Carbon", (0.025, 0.025, 0.028), rough=0.25, coat=1.0),
        "Gold": mat("M_Nozzle_Gold", (0.85, 0.62, 0.32), rough=0.2, metal=1.0),
        "PlateSilver": mat("M_Plate_Silver", (0.70, 0.70, 0.70), rough=0.35, metal=0.9),
        "Rubber": mat("M_Rubber", (0.02, 0.02, 0.02), rough=0.9),
        "BrakeDisc": mat("M_Brake_CarbonCeramic", (0.12, 0.12, 0.125), rough=0.45, metal=0.3),
        "Leather": mat("M_Interior_Ultrawhite", (0.86, 0.86, 0.84), rough=0.55),
        "Suede": mat("M_Interior_Microsuede", (0.10, 0.10, 0.105), rough=0.95),
        "InteriorDark": mat("M_Interior_Dark", (0.03, 0.03, 0.032), rough=0.7),
        "RedAmbient": mat("M_Interior_RedAmbient", (0.4, 0.0, 0.0), emit=((1, 0.03, 0.02), 25)),
        "Aluminum": mat("M_Aluminum", (0.75, 0.75, 0.76), rough=0.25, metal=1.0),
        "Screen": mat("M_Screen_Main", (0.0, 0.0, 0.0), emit=((0.6, 0.02, 0.02), 4)),
        "ScreenSmall": mat("M_Screen_Forward", (0.0, 0.0, 0.0), emit=((0.6, 0.02, 0.02), 4)),
    }


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


def apply_arches(body):
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Mirror")
    for c in arch_cutters():
        b = body.modifiers.new("Arch", 'BOOLEAN')
        b.operation = 'DIFFERENCE'
        b.solver = 'EXACT'
        b.object = c
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.modifier_apply(modifier=b.name)
        bpy.data.objects.remove(c, do_unlink=True)


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
    apply_arches(body)
    smooth(body)
    P = roadster_parts
    static = [body]
    static.append(P.front_lights(M))
    static.append(P.tail_lights(M))
    static.append(P.side_details(M))
    static.append(P.splitter(M))
    pl, pt = P.plate(M)
    static += [pl, pt]
    static.append(P.rear_face(M))
    rear_thr, rocket_thr = P.thrusters(M)
    copv = P.copv(M)
    interior = P.interior(M)
    wing = P.rear_wing(M)
    vent = P.hood_vent(M)
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
        bpy.ops.export_scene.fbx(filepath=os.path.join(exp, "SM_%s.fbx" % n), use_selection=True, apply_scale_options='FBX_SCALE_UNITS',
                                 axis_forward='X', axis_up='Z', object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE',
                                 bake_space_transform=True, add_leaf_bones=False)
        ob.location, ob.rotation_euler = saved
    with open(os.path.join(exp, "manifest.json"), "w") as f:
        json.dump(EXPORT_SET, f, indent=1)
    print("EXPORTED", len(names))


main()
