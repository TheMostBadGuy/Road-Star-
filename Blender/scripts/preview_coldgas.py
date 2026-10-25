"""Build the cold-gas / fan-box module and render inspection views.

blender -b -P preview_coldgas.py -- [out_dir] [--eevee] [--only name,name] [--blend]
Renders into Blender/renders_coldgas by default.
"""
import bpy, bmesh, math, sys, os, importlib, json
from mathutils import Vector, Matrix

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import roadster_materials; importlib.reload(roadster_materials)
import roadster_body as RB; importlib.reload(RB)
import roadster_coldgas as CG; importlib.reload(CG)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
pos = [a for i, a in enumerate(argv) if not a.startswith("--") and not (i > 0 and argv[i - 1] == "--only")]
OUT = pos[0] if pos else os.path.join(SCRIPT_DIR, "..", "renders_coldgas")
EEVEE = "--eevee" in argv
ONLY = None
for i, a in enumerate(argv):
    if a == "--only" and i + 1 < len(argv):
        ONLY = set(argv[i + 1].split(","))
os.makedirs(OUT, exist_ok=True)

FA, RA = RB.WB / 2, -RB.WB / 2


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    return sc


def body_context(M):
    """Faint body envelope with the black lower rear face and the rear floor cut away (the fan box sits there)."""
    body = RB.build_body("Body_Envelope")
    mir = body.modifiers.new("Mirror", 'MIRROR'); mir.use_axis = (False, True, False); mir.use_clip = True
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Mirror")
    bm = bmesh.new(); bm.from_mesh(body.data)
    kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        if c.x < -2.08 and c.z < 0.645 and abs(c.y) < 0.86:
            kill.append(f)
        elif c.x < -1.44 and c.z < 0.40 and abs(c.y) < 0.80:
            kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context='FACES')
    bm.to_mesh(body.data); bm.free()
    m = roadster_materials.mat("M_Preview_Body", (0.80, 0.80, 0.78), rough=0.4)
    body.data.materials.clear(); body.data.materials.append(m)
    return body


def wheels(M):
    obs = []
    for (x, r, w, y) in ((FA, 0.3425, 0.295, 0.85), (RA, 0.3672, 0.335, 0.84)):
        for s in (1, -1):
            bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=r, depth=w, location=(x, s * y, r), rotation=(math.pi / 2, 0, 0))
            ob = bpy.context.active_object
            ob.data.materials.append(M["Tire"])
            obs.append(ob)
    return obs


def setup(sc):
    if EEVEE:
        for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT'):
            try:
                sc.render.engine = eng
                break
            except TypeError:
                continue
        world = bpy.data.worlds.new("W"); world.use_nodes = True
        world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.05, 0.05, 0.055, 1)
        world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
        sc.world = world
        for (loc, rot, e, size) in (((-6, 3, 4), (math.radians(55), 0, math.radians(-120)), 1500, 4),
                                    ((-5, -4, 2), (math.radians(75), 0, math.radians(-50)), 900, 3),
                                    ((2, 0, 6), (0, 0, 0), 800, 6)):
            ld = bpy.data.lights.new("A", 'AREA'); ld.energy = e; ld.size = size
            lo = bpy.data.objects.new("A", ld); lo.location = loc; lo.rotation_euler = rot
            sc.collection.objects.link(lo)
        try:
            sc.eevee.taa_render_samples = 64
        except Exception:
            pass
    else:
        sc.render.engine = 'BLENDER_WORKBENCH'
        sh = sc.display.shading
        sh.light = 'STUDIO'; sh.color_type = 'MATERIAL'
        sh.show_object_outline = False
        sh.show_cavity = True; sh.cavity_type = 'BOTH'
        sh.cavity_ridge_factor = 1.0; sh.cavity_valley_factor = 1.0
        sh.show_specular_highlight = True
        sc.display.render_aa = '16'
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'
    if not EEVEE:
        world = bpy.data.worlds.new("W"); world.use_nodes = True
        world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.20, 0.21, 0.23, 1)
        sc.world = world
        sc.display.shading.background_type = 'VIEWPORT'
        sc.display.shading.background_color = (0.16, 0.17, 0.19)


def cam(name, loc, rot, ortho=None, lens=50):
    cd = bpy.data.cameras.new(name)
    if ortho:
        cd.type = 'ORTHO'; cd.ortho_scale = ortho
    else:
        cd.lens = lens
    cd.clip_start = 0.01; cd.clip_end = 100
    ob = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = loc; ob.rotation_euler = rot
    return ob


def look_at(ob, target):
    d = Vector(target) - ob.location
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def render(sc, c, name, w, h, hide=(), show_only=None):
    if ONLY and name not in ONLY:
        return
    saved = {}
    for o in sc.objects:
        if o.type not in ('MESH',):
            continue
        vis = True
        if show_only is not None:
            vis = o.name in show_only
        if o.name in hide:
            vis = False
        saved[o.name] = o.hide_render
        o.hide_render = not vis
    sc.camera = c
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.filepath = os.path.join(OUT, name + (".eevee" if EEVEE else "") + ".png")
    bpy.ops.render.render(write_still=True)
    for n, v in saved.items():
        sc.objects[n].hide_render = v
    print("RENDERED", sc.render.filepath)


def main():
    sc = reset()
    M = roadster_materials.materials()
    parts = CG.build(M)
    # stats
    tot = 0
    info = {}
    for n, ob in parts.items():
        t = CG.tri_count(ob)
        tot += t
        info[n] = dict(tris=t, origin=[round(v, 4) for v in ob.location], mats=[m.name for m in ob.data.materials],
                       uv=bool(ob.data.uv_layers))
    print("PARTS", json.dumps(info, indent=1))
    print("TOTAL_TRIS", tot)
    print("COPV_Rocket volume L", round(CG.copv_volume(CG.COPV_R) * 1000, 1), " COPV_Trunk volume L", round(CG.copv_volume(CG.COPV_T) * 1000, 1))
    for r in CG.thruster_table():
        print("NOZZLE", json.dumps(r))
    # sanity: non-manifold edges / zero-area faces per object
    for n, ob in parts.items():
        bm = bmesh.new(); bm.from_mesh(ob.data)
        nm = sum(1 for e in bm.edges if not e.is_manifold)
        za = sum(1 for f in bm.faces if f.calc_area() < 1e-10)
        # signed volume per connected island: negative = inverted normals
        seen, inv, isl = set(), 0, 0
        for f0 in bm.faces:
            if f0.index in seen:
                continue
            stack, vol = [f0], 0.0
            seen.add(f0.index)
            while stack:
                f = stack.pop()
                vs = [v.co for v in f.verts]
                for k in range(1, len(vs) - 1):
                    vol += vs[0].dot(vs[k].cross(vs[k + 1])) / 6.0
                for e in f.edges:
                    for g in e.link_faces:
                        if g.index not in seen:
                            seen.add(g.index); stack.append(g)
            isl += 1
            if vol < 0:
                inv += 1
        bm.free()
        print("CHECK", n, "nonmanifold_edges", nm, "zero_area_faces", za, "islands", isl, "inverted_islands", inv)
    with open(os.path.join(OUT, "coldgas_stats.json"), "w") as f:
        json.dump(dict(parts=info, total_tris=tot, nozzles=CG.thruster_table(),
                       copv_L=dict(rocket=CG.copv_volume(CG.COPV_R) * 1000, trunk=CG.copv_volume(CG.COPV_T) * 1000)), f, indent=1)

    body = body_context(M)
    whl = wheels(M)
    setup(sc)
    names_parts = set(parts.keys())
    ctx = names_parts | {body.name} | {w.name for w in whl}
    nobody = names_parts | {w.name for w in whl}

    # 1. rear view matched to the Gravity image (lower car): ortho, 1.56 mm/px, centre (y 0, z 0.628)
    rear = cam("CamRear", (-12.0, 0.0, 0.628), (math.pi / 2, 0, -math.pi / 2), ortho=1460 * 0.00156)
    render(sc, rear, "rear_match_body", 1460, 1000, show_only=ctx)
    render(sc, rear, "rear_match_parts", 1460, 1000, show_only=nobody)
    # 2. side view (left), whole car
    side = cam("CamSide", (0.0, 12.0, 0.55), (math.pi / 2, 0, math.pi), ortho=5.0)
    render(sc, side, "side_parts", 2000, 700, show_only=nobody)
    render(sc, side, "side_body", 2000, 700, show_only=ctx)
    # 3. 3/4 close-up of the rear nozzles and valves
    q = cam("CamQ34", (-2.95, 0.55, 0.95), (0, 0, 0), lens=55)
    look_at(q, (-2.03, 0.10, 0.52))
    render(sc, q, "q34_nozzles", 1600, 1000, show_only=names_parts - {"COPV_Rocket", "COPV_Trunk"})
    q2 = cam("CamQ34b", (-3.05, 1.55, 0.55), (0, 0, 0), lens=50)
    look_at(q2, (-2.0, 0.66, 0.38))
    render(sc, q2, "q34_corner", 1400, 1000, show_only=names_parts)
    # 4. top cutaway of the plumbing
    top = cam("CamTop", (-0.20, 0.0, 10.0), (0, 0, 0), ortho=4.9)
    render(sc, top, "top_plumbing", 2000, 900, show_only=names_parts | {w.name for w in whl})
    topr = cam("CamTopR", (-1.55, 0.0, 10.0), (0, 0, 0), ortho=2.0)
    render(sc, topr, "top_rear_detail", 1600, 1600, show_only=names_parts)
    # 5. details
    fx = cam("CamFan", (-3.25, -0.62, 0.78), (0, 0, 0), lens=60)
    look_at(fx, (-2.02, -0.28, 0.42))
    render(sc, fx, "fan_detail", 1400, 1000, show_only={"Fan_Box", "Fan_0", "Fan_1", "Fan_2", "Fan_3", "Thrusters_Rear"})
    lf = cam("CamLiftF", (1.45, 1.60, 0.35), (0, 0, 0), lens=60)
    look_at(lf, (0.97, 0.85, 0.27))
    render(sc, lf, "lift_front_yaw", 1400, 1000, show_only={"Thrusters_Rocket"})
    yw = cam("CamYaw", (2.45, 1.45, 0.45), (0, 0, 0), lens=60)
    look_at(yw, (1.95, 0.80, 0.24))
    render(sc, yw, "yaw_detail", 1400, 1000, show_only={"Thrusters_Rocket"})
    cv = cam("CamCOPV", (1.5, -2.6, 1.6), (0, 0, 0), lens=40)
    look_at(cv, (-0.45, -0.20, 0.55))
    render(sc, cv, "copv_overview", 1600, 1000, show_only={"COPV_Rocket", "COPV_Trunk", "ColdGas_Plumbing", "Thrusters_Rocket", "Thrusters_Rear", "Fan_Box"})
    pl = cam("CamPlumb", (-1.05, -1.05, 1.25), (0, 0, 0), lens=40)
    look_at(pl, (-1.62, 0.05, 0.60))
    render(sc, pl, "plumbing_detail", 1600, 1000, show_only={"ColdGas_Plumbing", "Thrusters_Rear", "COPV_Trunk", "Thrusters_Rocket", "COPV_Rocket"})
    if "--blend" in argv:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "coldgas_preview.blend"))


main()
