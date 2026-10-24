import bpy, sys, os, bmesh
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roadster_body
bpy.ops.wm.read_factory_settings(use_empty=True)
o = roadster_body.build_body()
me = o.data
print("STATS verts", len(me.vertices), "faces", len(me.polygons), "edges", len(me.edges))
bm = bmesh.new(); bm.from_mesh(me)
nm = [e for e in bm.edges if not e.is_manifold]
print("STATS non-manifold edges", len(nm))
for e in nm[:12]:
    print("  NM", tuple(round(c, 3) for c in e.verts[0].co), tuple(round(c, 3) for c in e.verts[1].co), len(e.link_faces))
xs = sorted({round(v.co.x, 3) for v in bm.verts})
print("STATS stations", len(xs), xs[:5], xs[-5:])
