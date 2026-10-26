import bpy
from collections import Counter
for name in ("Yoke", "Interior"):
    ob = bpy.data.objects.get(name)
    if not ob:
        print("PROBE", name, "missing"); continue
    me = ob.data
    c = Counter(p.material_index for p in me.polygons)
    print("PROBE", name, "slots", [m.name if m else None for m in me.materials])
    print("PROBE", name, "faces per slot", sorted(c.items()))
    # hub region of the yoke: faces near the pivot axis (local y within +-0.06, z within +-0.05)
    if name == "Yoke":
        hub = Counter(p.material_index for p in me.polygons if abs(p.center.y) < 0.05 and abs(p.center.z) < 0.04)
        print("PROBE hub faces per slot", sorted(hub.items()))
