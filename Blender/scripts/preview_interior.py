"""Build the Roadster interior (with the body shell around it) and render check views.

blender -b -P preview_interior.py -- [out_dir] [--quick] [--only cam1,cam2] [--workbench] [--nobody] [--save]
Views (out_dir, default Blender/renders_interior):
  slide4.png       camera matched to Tesla's official interior photo (03-Carousel-Slide-4)
  driver_pov.png   the Unreal cockpit camera: eye (-0.18, 0.38, 1.00), pitch -6 deg, 80 deg horizontal FOV
  seat_close.png   driver seat close-up (bucket shape, seams, perforation clusters, belt)
  yoke_close.png   yoke close-up (rim, hub + emblem, spokes, buttons, BOOST / DRS paddles)
  top_cutaway.png, side_cutaway.png   layout checks with the shell hidden (Workbench)
  backfaces.png    driver POV with back-face culling (any hole = a face pointing the wrong way)
The screens show UI frames from Research/images/ui_frames in the preview only, to check UV orientation.
"""
import bpy, sys, os, math, importlib
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import roadster_body as RB
import roadster_materials
import roadster_interior as RI
for m in (RB, roadster_materials, RI):
    importlib.reload(m)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
flags = {a for a in argv if a.startswith("--")}
pos = [a for a in argv if not a.startswith("--")]
ONLY = None
if "--only" in argv:
    ONLY = set(argv[argv.index("--only") + 1].split(","))
    pos = [a for a in pos if a != argv[argv.index("--only") + 1]]
OUT = pos[0] if pos else os.path.normpath(os.path.join(HERE, "..", "renders_interior"))
QUICK = "--quick" in flags
os.makedirs(OUT, exist_ok=True)
REF = os.path.normpath(os.path.join(HERE, "..", "..", "Research", "images"))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    return sc


def build_body(M):
    body = RB.build_body("Roadster_Body")
    for key in RB.MAT_NAMES:
        body.data.materials.append(M[key])
    mir = body.modifiers.new("Mirror", 'MIRROR')
    mir.use_axis = (False, True, False)
    mir.use_clip = True
    mir.merge_threshold = 0.002
    for f in body.data.polygons:
        f.use_smooth = False
    return body


def preview_screen_material(name, image_path, strength=3.0):
    """Emissive preview material showing a UI frame (checks the 0..1 UV orientation of the screens)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = bpy.data.images.load(image_path, check_existing=True)
    em.inputs["Strength"].default_value = strength
    nt.links.new(tx.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m


def look_at(cam, target, roll=0.0):
    d = (Vector(target) - cam.location).normalized()
    q = d.to_track_quat('-Z', 'Y')
    cam.rotation_euler = q.to_euler()
    if roll:
        cam.rotation_euler.rotate_axis('Z', roll)


def camera(name, loc, rot=None, target=None, lens=35.0, fov=None, shift=(0.0, 0.0), sensor=36.0, clip=0.02):
    cd = bpy.data.cameras.new(name)
    cd.sensor_width = sensor
    cd.sensor_fit = 'HORIZONTAL'
    cd.clip_start = clip
    if fov:
        cd.angle = math.radians(fov)
    else:
        cd.lens = lens
    cd.shift_x, cd.shift_y = shift
    cam = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = loc
    if rot is not None:
        cam.rotation_euler = rot
    elif target is not None:
        look_at(cam, target)
    return cam


def lights():
    sc = bpy.context.scene
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.43, 0.45, 1)
    bg.inputs["Strength"].default_value = 1.4
    sc.world = world
    def area(name, loc, target, energy, size, color=(1, 1, 1)):
        ld = bpy.data.lights.new(name, 'AREA')
        ld.energy = energy
        ld.size = size
        ld.color = color
        ob = bpy.data.objects.new(name, ld)
        sc.collection.objects.link(ob)
        ob.location = loc
        look_at(ob, target)
        return ob
    area("SkyFront", (3.2, 0.0, 2.2), (0.6, 0.0, 0.8), 1600, 3.0)          # bright overcast through the windshield
    area("CabinTop", (-0.15, 0.0, 1.06), (-0.1, 0.0, 0.2), 55, 0.7)        # soft studio light over the seats
    area("RearFill", (-1.2, 0.0, 1.0), (0.8, 0.0, 0.6), 35, 0.6)


def setup_render(sc, workbench=False):
    if workbench:
        sc.render.engine = 'BLENDER_WORKBENCH'
        sh = sc.display.shading
        sh.light = 'STUDIO'
        sh.color_type = 'MATERIAL'
        sh.show_cavity = True
        sh.cavity_type = 'BOTH'
        sh.show_object_outline = False
        sh.show_specular_highlight = True
    else:
        sc.render.engine = 'BLENDER_EEVEE'
        ee = sc.eevee
        ee.taa_render_samples = 24 if QUICK else 96
        ee.use_raytracing = True
        ee.ray_tracing_options.resolution_scale = '2' if QUICK else '1'
        ee.use_shadows = True
        ee.use_fast_gi = True
        ee.fast_gi_method = 'GLOBAL_ILLUMINATION'
    sc.view_settings.view_transform = 'AgX'
    try:
        sc.view_settings.look = 'AgX - Medium High Contrast'
    except TypeError:
        pass
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'


def render(sc, cam, name, w, h, exposure=0.0):
    if ONLY and name not in ONLY:
        return
    s = 0.5 if QUICK else 1.0
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = int(w * s), int(h * s)
    sc.view_settings.exposure = exposure
    sc.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("RENDERED", sc.render.filepath)


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def main():
    sc = reset()
    M = roadster_materials.materials()
    for k in ("Glass", "RearGlass"):
        g = M[k]
        g.blend_method = 'BLEND' if hasattr(g, "blend_method") else None
        bsdf = g.node_tree.nodes.get("Principled BSDF")
        bsdf.inputs["Alpha"].default_value = 0.12
        if hasattr(g, "use_transparent_shadow"):
            g.use_transparent_shadow = True
    body = None if "--nobody" in flags else build_body(M)
    parts = RI.build(M)

    # report: triangles, materials, skin clearance
    total = 0
    for nm, ob in parts.items():
        t = tri_count(ob)
        total += t
        print("PART %-18s tris %7d  slots %s  loc %s rot %s" % (nm, t, [m.name for m in ob.data.materials],
              tuple(round(v, 4) for v in ob.location), tuple(round(math.degrees(v), 2) for v in ob.rotation_euler)))
    print("TOTAL tris", total)
    bpy.context.view_layer.update()
    for nm, (worst, bad, wp) in RI.clearance_report(parts.values()).items():
        print("CLEARANCE %-18s worst %+.4f m  verts<3mm %5d  at %s" % (nm, worst, bad, wp))
    for nm in ("Screen_Display", "Forward_Display_A", "Forward_Display_B"):
        ob = parts[nm]
        uv = ob.data.uv_layers[0].data
        mw = ob.matrix_world
        for poly in ob.data.polygons:
            for li in poly.loop_indices:
                v = ob.data.vertices[ob.data.loops[li].vertex_index].co
                print("UV %s %s -> world %s" % (nm, tuple(round(c, 3) for c in uv[li].uv), tuple(round(c, 3) for c in (mw @ v))))

    # preview-only screen content (checks the UV orientation)
    ui = os.path.join(REF, "ui_frames")
    parts["Screen_Display"].data.materials[0] = preview_screen_material("PV_Screen", os.path.join(ui, "ui_t20_trackmode_dashboard.jpg"), 2.5)
    parts["Forward_Display_A"].data.materials[0] = preview_screen_material("PV_FwdA", os.path.join(ui, "ui_t14_trackmode_dashboard_alt.jpg"), 2.5)
    parts["Forward_Display_B"].data.materials[0] = preview_screen_material("PV_FwdB", os.path.join(ui, "ui_t28_vehicle_dynamics_balance.jpg"), 2.5)

    lights()
    wb = "--workbench" in flags
    setup_render(sc, wb)
    # Slide-4 camera: centreline, level with vertical lens shift (verticals stay vertical in the photo), ~16 mm.
    s4 = camera("Cam_Slide4", (-0.50, 0.012, 0.865), rot=(math.radians(90), 0, math.radians(-90)), lens=16.0, shift=(0.0, -0.075))
    pov = camera("Cam_DriverPOV", tuple(RI.EYE), rot=(math.radians(90 - 6), 0, math.radians(-90)), fov=80.0)
    seat = camera("Cam_Seat", (0.62, -0.02, 0.98), target=(-0.08, 0.38, 0.47), lens=30.0)
    yoke = camera("Cam_Yoke", (0.12, 0.30, 0.96), target=(0.50, 0.38, 0.78), lens=42.0)
    render(sc, s4, "slide4", 2200, 1040, exposure=0.0)
    render(sc, pov, "driver_pov", 1920, 1080)
    render(sc, seat, "seat_close", 1600, 1000)
    render(sc, yoke, "yoke_close", 1600, 1000)

    # layout checks without the shell
    if body:
        body.hide_render = True
    setup_render(sc, True)
    top = camera("Cam_Top", (0.30, 0.0, 4.0), rot=(0, 0, math.radians(90)), lens=50)
    top.data.type = 'ORTHO'; top.data.ortho_scale = 2.4
    top.data.clip_start = 4.0 - 1.02                      # cut away the headliner
    side = camera("Cam_Side", (0.30, 4.0, 0.62), rot=(math.radians(90), 0, math.radians(180)), lens=50)
    side.data.type = 'ORTHO'; side.data.ortho_scale = 2.4
    side.data.clip_start = 4.0 - 0.70                     # cut away the left door trim
    hide = [o for o in parts.values() if o.name == "Interior"]
    render(sc, top, "top_cutaway", 1600, 1000)
    render(sc, side, "side_cutaway", 1600, 1000)
    if body:
        body.hide_render = False
    sc.display.shading.show_backface_culling = True
    render(sc, pov, "backfaces", 1600, 900)
    sc.display.shading.show_backface_culling = False
    if "--save" in flags:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "interior_preview.blend"))


main()
