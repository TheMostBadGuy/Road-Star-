import bpy, math, sys, os
from mathutils import Vector
out = sys.argv[sys.argv.index('--') + 1]
os.makedirs(out, exist_ok=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_cavity = True
sc.render.film_transparent = True
sc.render.resolution_x, sc.render.resolution_y = 1600, 900
ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
def cam(name, loc, target, lens=50, ortho=None):
    d = bpy.data.cameras.new(name)
    if ortho: d.type = 'ORTHO'; d.ortho_scale = ortho
    else: d.lens = lens
    c = bpy.data.objects.new(name, d); sc.collection.objects.link(c)
    c.location = loc
    c.rotation_euler = (Vector(loc) - Vector(target)).to_track_quat('Z', 'Y').to_euler()
    return c
views = {'px': (4, 0, 0), 'nx': (-4, 0, 0), 'py': (0, 4, 0), 'ny': (0, -4, 0), 'pz': (0, 0, 4), 'q34': (2.4, 2.4, 1.2)}
for n, loc in views.items():
    c = cam(n, loc, (0, 0, 0), ortho=2.1 if n != 'q34' else None, lens=60)
    if n == 'pz': c.rotation_euler = (0, 0, 0)
    sc.camera = c
    sc.render.filepath = os.path.join(out, n + '.png')
    bpy.ops.render.render(write_still=True)
