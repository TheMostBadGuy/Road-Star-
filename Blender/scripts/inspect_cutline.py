"""Read-only: distance of the open-border vertices of the left door skin and of the body opening to the door's
side-view cut planes. A clean cut puts every border vertex on a plane (distance ~0).
Usage: blender -b <file.blend> -P inspect_cutline.py -- <out.json>"""
import json
import os
import sys

import bmesh
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roadster_transform as RT  # noqa: E402

planes = RT.door_cut_planes()


def border_report(ob, region):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    loc = ob.location
    dists = []
    for e in bm.edges:
        if not e.is_boundary:
            continue
        for v in e.verts:
            p = v.co + loc
            if not region(p):
                continue
            d = min(abs((p - co).dot(no)) for co, no in planes)
            dists.append(d)
    bm.free()
    dists.sort()
    n = len(dists)
    return dict(object=ob.name, border_verts=n,
                median_mm=round(1000 * dists[n // 2], 2) if n else None,
                p90_mm=round(1000 * dists[int(n * 0.9)], 2) if n else None,
                max_mm=round(1000 * dists[-1], 2) if n else None,
                over_2mm=sum(1 for d in dists if d > 0.002))


front = lambda p: p.y > 0.4 and 0.70 < p.x < 0.95 and 0.25 < p.z < 0.80
rear = lambda p: p.y > 0.4 and -0.55 < p.x < -0.15 and 0.25 < p.z < 1.0
out = []
for name in ("Door_L", "Body"):
    ob = bpy.data.objects.get(name)
    if ob is None:
        out.append(dict(object=name, missing=True))
        continue
    out.append(dict(region="front", **border_report(ob, front)))
    out.append(dict(region="rear", **border_report(ob, rear)))
out.append(dict(objects=[o.name for o in bpy.data.objects if o.type == 'MESH' and 'ody' in o.name]))
with open(sys.argv[-1], 'w') as f:
    json.dump(out, f, indent=1)
