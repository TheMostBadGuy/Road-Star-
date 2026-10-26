"""Read-only: faces of the pre-inset rear wing (debug_inset.py build) near/behind the trailing-edge crease, binned by x,
with their normals, plus which body objects own geometry behind the crease. Usage: blender -b <debug.blend> -P
inspect_wingfaces.py -- <out.json>"""
import json
import sys

import bpy

pre = bpy.data.objects["Rear_Wing_preInset"]
loc = pre.location
bins = {}
for p in pre.data.polygons:
    c = p.center + loc
    if c.x > -2.06:
        continue
    key = round(c.x, 2)
    b = bins.setdefault(key, {"n": 0, "nz_min": 9, "nz_max": -9, "nx_min": 9, "z_min": 9, "z_max": -9, "area": 0.0})
    b["n"] += 1
    b["nz_min"] = round(min(b["nz_min"], p.normal.z), 3)
    b["nz_max"] = round(max(b["nz_max"], p.normal.z), 3)
    b["nx_min"] = round(min(b["nx_min"], p.normal.x), 3)
    b["z_min"] = round(min(b["z_min"], c.z), 3)
    b["z_max"] = round(max(b["z_max"], c.z), 3)
    b["area"] = round(b["area"] + p.area * 1e4, 2)          # cm^2
owners = {}
for ob in bpy.data.objects:
    if ob.type != 'MESH' or ob.name.endswith("_preInset"):
        continue
    n = 0
    for v in ob.data.vertices:
        w = ob.matrix_world @ v.co
        if w.x < -2.099 and w.z > 0.90 and abs(w.y) < 0.9:
            n += 1
    if n:
        owners[ob.name] = n
with open(sys.argv[-1], "w") as f:
    json.dump({"wing_bins": dict(sorted(bins.items())), "behind_crease_owners": owners,
               "wing_faces": len(pre.data.polygons)}, f, indent=1)
