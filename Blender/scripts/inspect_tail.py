"""Read-only: tail profile (height and surface normal by downward ray casts) on the stowed car, to place the rear-wing
trailing-edge cut. Usage: blender -b <debug.blend from debug_inset.py> -P inspect_tail.py -- <out.json>"""
import json
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

dg = bpy.context.evaluated_depsgraph_get()
trees = []
for name in ("Roadster_Body", "Rear_Wing_preInset"):
    ob = bpy.data.objects.get(name)
    if ob is None:
        continue
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    me.transform(ob.matrix_world)
    trees.append(BVHTree.FromPolygons([v.co.copy() for v in me.vertices], [tuple(p.vertices) for p in me.polygons]))
    ev.to_mesh_clear()

rows = []
for y in (0.0, 0.3, 0.55, 0.75, 0.85):
    prof = []
    x = -1.90
    while x >= -2.13:
        best = None
        for t in trees:
            hit = t.ray_cast(Vector((x, y, 2.0)), Vector((0, 0, -1)), 2.0)
            if hit[0] is not None and (best is None or hit[0].z > best[0].z):
                best = hit
        if best:
            prof.append([round(x, 3), round(best[0].z, 4), [round(c, 3) for c in best[1]]])
        x -= 0.005
    rows.append({"y": y, "profile": prof})
with open(sys.argv[-1], "w") as f:
    json.dump(rows, f, indent=0)
