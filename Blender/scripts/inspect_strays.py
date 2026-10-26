"""Read-only check for stray vertices far outside the car's envelope (base mesh and evaluated mesh).
Usage: blender -b Roadster.blend -P inspect_strays.py -- <out.json>
"""
import json
import sys

import bpy

# Generous envelope around the car (Blender frame, metres): length ~4.5 m, width ~2 m, height ~1.3 m.
X_MAX, Y_MAX, Z_MIN, Z_MAX = 3.0, 1.6, -0.3, 2.0


def strays(mesh, mw):
    far = []
    for v in mesh.vertices:
        p = mw @ v.co
        if abs(p.x) > X_MAX or abs(p.y) > Y_MAX or p.z < Z_MIN or p.z > Z_MAX:
            far.append((v.index, round(p.length, 2), [round(c, 3) for c in p]))
    return far


out = []
dg = bpy.context.evaluated_depsgraph_get()
for ob in bpy.data.objects:
    if ob.type != 'MESH':
        continue
    mw = ob.matrix_world
    base = strays(ob.data, mw)
    ev = ob.evaluated_get(dg)
    em = ev.to_mesh()
    evald = strays(em, mw)
    nev = len(em.vertices)
    ev.to_mesh_clear()
    if base or evald:
        out.append(dict(
            name=ob.name, hidden=ob.hide_render, verts=len(ob.data.vertices), eval_verts=nev,
            modifiers=[m.type for m in ob.modifiers],
            stray_base=len(base), max_base=max((d for _, d, _ in base), default=0),
            stray_eval=len(evald), max_eval=max((d for _, d, _ in evald), default=0),
            sample=(base or evald)[:4]))

with open(sys.argv[-1], 'w') as f:
    json.dump(out, f, indent=1)
