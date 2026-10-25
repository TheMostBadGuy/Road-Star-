"""Desert proving-ground terrain: 12 km square, flat 3 km basin at z=0, ridged mountains around it.
blender -b -P build_terrain.py -- <export_dir>
"""
import bpy, bmesh, math, sys, os, random

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else os.path.join(os.path.dirname(__file__), "..", "export")
bpy.ops.wm.read_factory_settings(use_empty=True)
random.seed(11)

SIZE, N = 12000.0, 300
FLAT_R, RISE_R = 1500.0, 3600.0

# value-noise lattice
G = 64
lat = [[random.random() for _ in range(G + 1)] for _ in range(G + 1)]


def vnoise(x, y):
    x %= G; y %= G
    xi, yi = int(x), int(y)
    xf, yf = x - xi, y - yi
    xf = xf * xf * (3 - 2 * xf); yf = yf * yf * (3 - 2 * yf)
    a, b = lat[xi][yi], lat[xi + 1][yi]
    c, d = lat[xi][yi + 1], lat[xi + 1][yi + 1]
    return (a * (1 - xf) + b * xf) * (1 - yf) + (c * (1 - xf) + d * xf) * yf


def ridged(x, y):
    h, amp, f = 0.0, 1.0, 1.0
    for o in range(6):
        n = 1.0 - abs(vnoise(x * f, y * f) * 2 - 1)
        h += n * n * amp
        amp *= 0.5; f *= 2.03
    return h


def height(x, y):
    r = math.hypot(x, y)
    if r < FLAT_R:
        return 0.0
    t = min(1.0, (r - FLAT_R) / (RISE_R - FLAT_R))
    t = t * t * (3 - 2 * t)
    base = ridged(x / 900.0 + 10, y / 900.0 + 10)
    dunes = 6.0 * math.sin(x / 140.0 + vnoise(x / 400, y / 400) * 6) * min(1.0, (r - FLAT_R) / 200)
    return t * (120 + 520 * base) + dunes * (1 - t)


bm = bmesh.new()
verts = []
for j in range(N + 1):
    row = []
    for i in range(N + 1):
        x = -SIZE / 2 + SIZE * i / N
        y = -SIZE / 2 + SIZE * j / N
        row.append(bm.verts.new((x, y, height(x, y) - 0.03)))
    verts.append(row)
for j in range(N):
    for i in range(N):
        bm.faces.new((verts[j][i], verts[j][i + 1], verts[j + 1][i + 1], verts[j + 1][i]))
me = bpy.data.meshes.new("Terrain")
bm.to_mesh(me); bm.free()
ob = bpy.data.objects.new("Terrain", me)
bpy.context.collection.objects.link(ob)
for p in me.polygons:
    p.use_smooth = True
m = bpy.data.materials.new("M_Terrain"); ob.data.materials.append(m)

# asphalt apron around the start / drag area (slightly above the ground)
bpy.ops.mesh.primitive_plane_add(size=1, location=(150, 0, 0.0))
ap = bpy.context.active_object
ap.name = "Apron"
ap.scale = (1400, 700, 1)
bpy.ops.object.transform_apply(scale=True)
m2 = bpy.data.materials.new("M_Apron"); ap.data.materials.append(m2)

os.makedirs(OUT, exist_ok=True)
for o, name in ((ob, "SM_Terrain"), (ap, "SM_Apron")):
    for x in bpy.context.selected_objects:
        x.select_set(False)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, name + ".fbx"), use_selection=True, apply_scale_options='FBX_SCALE_UNITS',
                             axis_forward='X', axis_up='Z', object_types={'MESH'}, mesh_smooth_type='FACE', bake_space_transform=True)
print("TERRAIN DONE")
