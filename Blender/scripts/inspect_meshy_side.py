import bpy, bmesh, math, sys, os
from mathutils import Vector, Matrix
out = sys.argv[sys.argv.index('--') + 1]
ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
me = ob.data
# wheel centres: tire tread is the lowest geometry; find the two lowest clusters along x
vs = [ob.matrix_world @ v.co for v in me.vertices]
zmin = min(v.z for v in vs)
low = [v for v in vs if v.z < zmin + 0.004]
xs = sorted(v.x for v in low)
mid = 0.5 * (xs[0] + xs[-1])
fr = [x for x in xs if x < mid]; re = [x for x in xs if x >= mid]
cf = 0.5 * (min(fr) + max(fr)); cr = 0.5 * (min(re) + max(re))
print('MESHY contact patch centres x', cf, cr, 'zmin', zmin)
wb = abs(cr - cf)
s = 2.825 / wb
# front is at -X in this file: rotate 180 about Z so the nose points +X, scale, axle midpoint to origin, ground to z=0
xm = 0.5 * (cf + cr)
ob.matrix_world = Matrix.Diagonal((s, s, s, 1)) @ Matrix.Rotation(math.pi, 4, 'Z') @ Matrix.Translation((-xm, 0, -zmin))
bpy.context.view_layer.update()
vs = [ob.matrix_world @ v.co for v in me.vertices]
print('MESHY scale %.4f  length %.3f (x %.3f..%.3f)  width %.3f  height %.3f' % (s, max(v.x for v in vs) - min(v.x for v in vs), min(v.x for v in vs), max(v.x for v in vs), max(v.y for v in vs) - min(v.y for v in vs), max(v.z for v in vs)))
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'; sc.display.shading.color_type = 'TEXTURE'
sc.render.film_transparent = True
mmpx = 0.002251
cx = (2330.5 - 1720) * mmpx; cz = (845 - 512) * mmpx
d = bpy.data.cameras.new('side'); d.type = 'ORTHO'; d.ortho_scale = 3440 * mmpx
c = bpy.data.objects.new('side', d); sc.collection.objects.link(c)
c.location = (cx, 20, cz); c.rotation_euler = (math.pi / 2, 0, math.pi)
sc.camera = c
sc.render.resolution_x, sc.render.resolution_y = 3440, 1024
sc.render.filepath = os.path.join(out, 'meshy_side_ortho.png')
bpy.ops.render.render(write_still=True)
