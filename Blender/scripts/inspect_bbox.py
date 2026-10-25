import bpy
from mathutils import Vector
for o in bpy.data.objects:
    if o.type != 'MESH' or o.hide_render: continue
    bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
    mn = Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
    mx = Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
    print('OBJ %-22s x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f tris %d' % (o.name, mn.x, mx.x, mn.y, mx.y, mn.z, mx.z, sum(len(p.vertices) - 2 for p in o.data.polygons)))
