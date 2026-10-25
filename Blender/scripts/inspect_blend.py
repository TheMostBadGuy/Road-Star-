import bpy, sys
from mathutils import Vector
print('INSPECT file', bpy.data.filepath)
print('INSPECT texts', [t.name for t in bpy.data.texts])
print('INSPECT images', [(i.name, tuple(i.size)) for i in bpy.data.images])
print('INSPECT materials', [m.name for m in bpy.data.materials])
for o in bpy.data.objects:
    line = 'INSPECT OBJ %s type=%s' % (o.name, o.type)
    if o.type == 'MESH':
        bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
        mn = [min(v[i] for v in bb) for i in range(3)]; mx = [max(v[i] for v in bb) for i in range(3)]
        line += ' verts=%d faces=%d mats=%s bbox=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f) uv=%d' % (len(o.data.vertices), len(o.data.polygons), [m.name for m in o.data.materials if m], *mn, *mx, len(o.data.uv_layers))
    print(line)
