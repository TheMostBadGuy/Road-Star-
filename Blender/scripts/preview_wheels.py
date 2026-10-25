"""Build the wheels / brakes / suspension module and render verification views (EEVEE).

blender -b -P preview_wheels.py -- [out_dir] [--only name,name] [--samples N] [--save-blend]

Renders (into Blender/renders_wheels by default):
  side_front_match.png       orthographic side view of the front-left wheel at the scale of Tesla's Pilot photo
                             (2.251 mm/px x4), centred on the wheel centre like photo_front_crop.png
  side_front_compare.png     photo crop | render, side by side
  side_rear_match.png / side_rear_compare.png   the same for the rear wheel
  wheel_34_closeup.png       front-left wheel from the front 3/4 (as Tesla's 08-End photo): caliper, disc
                             and gold bell visible through the spoke slots
  tread_closeup.png          rear tyre tread and shoulder
  susp_front_corner.png      front-left corner, wheel hidden: wishbones, upright, track rod, pushrod, rocker,
                             coil-over, brakes, rack
  susp_front_corner_wheel.png  the same with the wheel
  susp_rear_coilover.png     rear-left coil-over / rocker from inboard (compare with Slide 5)
  stats.txt                  triangle counts, materials, clearance checks
"""
import bpy, sys, os, math, importlib
import numpy as np
from mathutils import Vector, Matrix

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import roadster_materials
importlib.reload(roadster_materials)
import roadster_wheels as RW
importlib.reload(RW)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "renders_wheels"))
ONLY = None
SAMPLES = 64
_i = 0
while _i < len(argv):
    a = argv[_i]
    if a == "--only":
        ONLY = set(argv[_i + 1].split(","))
        _i += 2
    elif a == "--samples":
        SAMPLES = int(argv[_i + 1])
        _i += 2
    else:
        if not a.startswith("--"):
            OUT = a
        _i += 1
os.makedirs(OUT, exist_ok=True)
PHOTO = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "Research", "images",
                                      "tesla_official_Roadster-02-Pilot-Desktop.jpg"))
MMPX = 0.002251
# wheel centres in the Pilot photo (pixels), fitted to the aero-insert outer edge (circle fit, +-1 px)
PHOTO_CENTRE = {"Front": (1706.0, 698.5), "Rear": (2970.5, 683.0)}
HALF = 175            # crop half size in photo pixels
UP = 4                # upscale


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    return sc


def setup_eevee(sc):
    sc.render.engine = 'BLENDER_EEVEE'
    ee = sc.eevee
    ee.taa_render_samples = SAMPLES
    for attr, val in (("use_raytracing", True), ("use_shadows", True), ("use_gtao", True)):
        if hasattr(ee, attr):
            setattr(ee, attr, val)
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast' if 'AgX - Medium High Contrast' in [
        i.identifier for i in sc.view_settings.bl_rna.properties['look'].enum_items] else 'None'
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes["Background"]
    # dark studio with a soft vertical gradient (brighter above), like Tesla's studio shots
    tex = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.45
    ramp.color_ramp.elements[0].color = (0.010, 0.010, 0.011, 1)
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = (0.10, 0.10, 0.105, 1)
    nt.links.new(tex.outputs["Generated"], sep.inputs[0])
    nt.links.new(sep.outputs["Z"], ramp.inputs[0])
    nt.links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.0
    sc.world = world


def add_area(name, loc, target, energy, size, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.energy = energy
    ld.size = size
    ld.color = color
    ob = bpy.data.objects.new(name, ld)
    bpy.context.collection.objects.link(ob)
    ob.location = loc
    d = (Vector(target) - Vector(loc)).normalized()
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return ob


def camera(name, loc, target=None, rot=None, lens=50, ortho=None):
    cd = bpy.data.cameras.new(name)
    if ortho:
        cd.type = 'ORTHO'
        cd.ortho_scale = ortho
    else:
        cd.lens = lens
    cd.clip_start = 0.01
    ob = bpy.data.objects.new(name, cd)
    bpy.context.collection.objects.link(ob)
    ob.location = loc
    if target is not None:
        d = (Vector(target) - Vector(loc)).normalized()
        ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    elif rot is not None:
        ob.rotation_euler = rot
    return ob


def render(sc, cam, name, w, h):
    if ONLY and name not in ONLY:
        return None
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.resolution_percentage = 100
    path = os.path.join(OUT, name + ".png")
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("RENDERED", path)
    return path


def place(src, name, loc, mirror_y=False, yaw180=False):
    ob = src.copy()
    ob.name = name
    bpy.context.collection.objects.link(ob)
    ob.location = loc
    if yaw180:
        ob.rotation_euler = (0, 0, math.pi)
    if mirror_y:
        ob.scale = (1, -1, 1)
    return ob


def ground():
    me = bpy.data.meshes.new("Ground")
    s = 20
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new("Ground", me)
    bpy.context.collection.objects.link(ob)
    m = roadster_materials.mat("M_Preview_Floor", (0.05, 0.05, 0.052), rough=0.6)
    me.materials.append(m)
    return ob


def fender_proxy(objs_out, M):
    """Stand-in for the body around each wheel arch (white shell above the wheel, side wall with the arch cut)
    so the wheel interior is shaded the way the real body shades it. Preview only."""
    import bmesh
    paint = M["Paint"]
    for key, g, xa in (("Front", RW.geom("Front"), RW.FA), ("Rear", RW.geom("Rear"), RW.RA)):
        for side in (1, -1):
            bm = bmesh.new()
            c = Vector((xa, 0, g["R"]))
            ra, rb = g["R"] + 0.024, 0.80
            y0, y1 = side * 0.50, side * 1.0
            n = 40
            ring = []
            for i in range(n + 1):
                a = math.radians(-12 + (192 * i / n))
                d = Vector((-math.cos(a), 0, math.sin(a)))
                ring.append((d, a))
            vi, vo = [], []
            for d, a in ring:
                vi.append((bm.verts.new(c + d * ra + Vector((0, y0, 0))), bm.verts.new(c + d * ra + Vector((0, y1, 0)))))
                vo.append(bm.verts.new(c + d * rb + Vector((0, y1, 0))))
            for i in range(n):
                bm.faces.new((vi[i][0], vi[i + 1][0], vi[i + 1][1], vi[i][1]))      # arch liner
                bm.faces.new((vi[i][1], vi[i + 1][1], vo[i + 1], vo[i]))            # body side around the arch
            me = bpy.data.meshes.new("Fender")
            bm.to_mesh(me)
            bm.free()
            me.materials.append(paint)
            ob = bpy.data.objects.new("Prev_Fender_%s_%d" % (key, side), me)
            bpy.context.collection.objects.link(ob)
            objs_out.append(ob)


def photo_crop(key):
    """Crop of the Pilot photo around the wheel centre, upscaled x4 (Blender image API resampling)."""
    img = bpy.data.images.load(PHOTO, check_existing=True)
    W_, H_ = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(H_, W_, 4)      # bottom-up rows
    cx, cy = PHOTO_CENTRE[key]
    x0, y0 = int(round(cx - HALF)), int(round(cy - HALF))
    rows = slice(H_ - (y0 + 2 * HALF), H_ - y0)
    crop = px[rows, x0:x0 + 2 * HALF, :].copy()
    ci = bpy.data.images.new("crop_" + key, 2 * HALF, 2 * HALF, alpha=False)
    ci.pixels[:] = crop.ravel()
    ci.scale(2 * HALF * UP, 2 * HALF * UP)
    out = np.array(ci.pixels[:], dtype=np.float32).reshape(2 * HALF * UP, 2 * HALF * UP, 4)
    return out, img.colorspace_settings.name


def compare(key, render_path, name):
    if render_path is None or not os.path.exists(render_path):
        return
    crop, _ = photo_crop(key)
    r = bpy.data.images.load(render_path)
    rw, rh = r.size
    rp = np.array(r.pixels[:], dtype=np.float32).reshape(rh, rw, 4)
    h = crop.shape[0]
    gap = 12
    out = np.ones((h, crop.shape[1] + gap + rw, 4), dtype=np.float32)
    out[:, :crop.shape[1]] = crop
    out[:, crop.shape[1] + gap:] = rp[:h]
    oi = bpy.data.images.new("cmp_" + key, out.shape[1], out.shape[0], alpha=False)
    oi.pixels[:] = out.ravel()
    oi.filepath_raw = os.path.join(OUT, name + ".png")
    oi.file_format = 'PNG'
    oi.save()
    cpath = os.path.join(OUT, "photo_%s_crop.png" % key.lower())
    ci = bpy.data.images.new("photo_" + key, crop.shape[1], crop.shape[0], alpha=False)
    ci.pixels[:] = crop.ravel()
    ci.filepath_raw = cpath
    ci.file_format = 'PNG'
    ci.save()
    print("COMPARE", oi.filepath_raw)


def clearance_report(objs, lines):
    """Suspension parts vs the tyre volume (static pose) and vs the body envelope (z top, half width)."""
    try:
        import roadster_body as RB
    except Exception:
        RB = None
    for corner in ("FL", "FR", "RL", "RR"):
        front = corner[0] == "F"
        g = RW.geom("Front" if front else "Rear")
        side = 1 if corner[1] == "L" else -1
        WC = Vector((RW.FA if front else RW.RA, side * RW.CORNER_Y[corner[0]], g["R"]))
        worst_tyre = None
        hits = 0
        top = 0.0
        width = 0.0
        for n, ob in objs.items():
            if not n.startswith("Susp_" + corner):
                continue
            mw = ob.matrix_world
            for v in ob.data.vertices:
                p = mw @ v.co
                yl = (p.y - WC.y) * side
                rr = math.hypot(p.x - WC.x, p.z - WC.z)
                if abs(yl) < g["hw"] + 0.004 and g["Rr"] - 0.012 < rr < g["R"] + 0.005:
                    hits += 1
                if abs(yl) < g["hwr"] + 0.004 and rr > g["Rr"] - 0.0095 - 0.003:
                    hits += 1
                top = max(top, p.z)
                width = max(width, abs(p.y))
        env = ""
        if RB:
            x = WC.x
            try:
                hood = min(RB.row_at(r, x)[1] for r in (7, 8, 9))
                env = " body-top-at-axle(rows7-9 min z)=%.3f" % hood
            except Exception:
                pass
        lines.append("%s: verts inside tyre/rim volume=%d  max z=%.3f  max |y|=%.3f%s" % (corner, hits, top, width, env))


def main():
    sc = reset()
    setup_eevee(sc)
    M = roadster_materials.materials()
    objs = RW.build(M)
    gF, gR = RW.geom("Front"), RW.geom("Rear")
    # stats
    lines = ["object, triangles, materials"]
    tot_w, tot_b, tot_s = {}, {}, 0
    for n, ob in sorted(objs.items()):
        t = RW.triangle_count(ob)
        lines.append("%-28s %7d  %s" % (n, t, ",".join(m.name for m in ob.data.materials)))
        if n.startswith("Susp_"):
            tot_s += t
    lines.append("suspension total %d" % tot_s)
    for key in ("Front", "Rear"):
        lines.append("brake set %s: %d" % (key, RW.triangle_count(objs["Brake_%s_Disc" % key]) +
                                            RW.triangle_count(objs["Brake_%s_Caliper" % key])))
    clearance_report(objs, lines)
    with open(os.path.join(OUT, "stats.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))

    # corner instances of the wheel-type templates (left: as built; right: wheel yawed 180 deg like the game,
    # disc + caliper mirrored so the calipers stay trailing and meet the mirrored uprights)
    inst = {}
    for key, g, xa in (("Front", gF, RW.FA), ("Rear", gR, RW.RA)):
        y = RW.CORNER_Y[key[0]]
        for side, tag in ((1, "L"), (-1, "R")):
            loc = (xa, side * y, g["R"])
            c = key[0] + tag
            inst["W" + c] = place(objs["Wheel_" + key], "Prev_Wheel_" + c, loc, yaw180=side < 0)
            inst["D" + c] = place(objs["Brake_%s_Disc" % key], "Prev_Disc_" + c, loc, mirror_y=side < 0)
            inst["C" + c] = place(objs["Brake_%s_Caliper" % key], "Prev_Caliper_" + c, loc, mirror_y=side < 0)
    for key in ("Front", "Rear"):
        for n in ("Wheel_" + key, "Brake_%s_Disc" % key, "Brake_%s_Caliper" % key):
            objs[n].hide_render = True
    gnd = ground()
    fenders = []
    fender_proxy(fenders, M)

    # ---------------------------------------------------------------- 1. side matches (studio key from above)
    key_l = add_area("Key", (0.0, 2.2, 4.0), (0.0, 0.9, 0.3), 750, 5.0)
    fill = add_area("Fill", (0.0, 6.0, 0.7), (0.0, 0.0, 0.4), 25, 4.0)
    kick = add_area("Kick", (2.5, 3.0, 0.15), (1.4, 0.9, 0.3), 60, 2.0)
    for key, g, xa, cname in (("Front", gF, RW.FA, "side_front"), ("Rear", gR, RW.RA, "side_rear")):
        y = RW.CORNER_Y[key[0]]
        cam = camera("Cam_" + cname, (xa, 8.0, g["R"]), rot=(math.pi / 2, 0, math.pi), ortho=2 * HALF * MMPX)
        p = render(sc, cam, cname + "_match", 2 * HALF * UP, 2 * HALF * UP)
        compare(key, p, cname + "_compare")

    for f in fenders:
        f.hide_render = True
    # ---------------------------------------------------------------- 2. 3/4 close-up (as 08-End)
    WCf = Vector((RW.FA, RW.CORNER_Y["F"], gF["R"]))
    cam = camera("Cam_34", WCf + Vector((1.25, 1.05, 0.30)), WCf + Vector((0.0, 0.03, 0.0)), lens=70)
    render(sc, cam, "wheel_34_closeup", 1600, 1200)

    # ---------------------------------------------------------------- 3. tread close-up (rear tyre)
    WCr = Vector((RW.RA, RW.CORNER_Y["R"], gR["R"]))
    tgt = WCr + Vector((-0.20, 0.0, 0.26))
    cam = camera("Cam_tread", WCr + Vector((-0.95, 0.42, 0.78)), tgt, lens=60)
    render(sc, cam, "tread_closeup", 1600, 1000)

    # ---------------------------------------------------------------- 4. suspension views
    for n in ("WFL", "WFR", "WRL", "WRR"):
        inst[n].hide_render = True
    for f in fenders:
        f.hide_render = True
    key_l.data.energy = 2400
    gnd.hide_render = False
    cam = camera("Cam_susp", Vector((RW.FA + 0.95, 1.45, 1.05)), Vector((RW.FA, 0.55, 0.38)), lens=35)
    render(sc, cam, "susp_front_corner", 1600, 1100)
    cam2 = camera("Cam_susp_rear_in", Vector((RW.FA - 0.85, -0.25, 0.95)), Vector((RW.FA, 0.45, 0.42)), lens=30)
    render(sc, cam2, "susp_front_corner_from_inside", 1600, 1100)
    cam3 = camera("Cam_rear_coil", Vector((RW.RA + 0.55, -0.15, 0.80)), Vector((RW.RA + 0.06, 0.35, 0.60)), lens=40)
    render(sc, cam3, "susp_rear_coilover", 1600, 900)
    for n in ("WFL",):
        inst[n].hide_render = False
    render(sc, cam, "susp_front_corner_wheel", 1600, 1100)
    if "--save-blend" in sys.argv:
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "preview_wheels.blend"))


main()
