"""Read-only: object transforms of the moving panels, and stray vertices measured in the closed pose (mesh local space
placed at the object's location without rotation). Usage: blender -b <file.blend> -P inspect_pose.py -- <out.json>"""
import json
import sys

import bpy

X_MAX, Y_MAX, Z_MIN, Z_MAX = 3.0, 1.6, -0.3, 2.0
out = []
for name in ("Door_L", "Door_R", "DoorGlass_L", "DoorGlass_R", "Rear_Wing", "Barge_L", "Barge_R",
             "HeadlightCover_L", "HeadlightCover_R", "Hood_Vent"):
    ob = bpy.data.objects.get(name)
    if not ob:
        continue
    loc = ob.location
    far = 0
    worst = 0.0
    for v in ob.data.vertices:
        p = v.co + loc
        if abs(p.x) > X_MAX or abs(p.y) > Y_MAX or p.z < Z_MIN or p.z > Z_MAX:
            far += 1
            worst = max(worst, p.length)
    out.append(dict(name=name, location=[round(c, 4) for c in loc],
                    rotation=[round(c, 4) for c in ob.rotation_euler], mode=ob.rotation_mode,
                    parent=ob.parent.name if ob.parent else None,
                    anim=bool(ob.animation_data and ob.animation_data.action),
                    closed_strays=far, closed_worst=round(worst, 2)))
with open(sys.argv[-1], 'w') as f:
    json.dump(out, f, indent=1)
