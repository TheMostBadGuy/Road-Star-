"""Debug run of the car build that reports, after every transform-panel bisect, how many faces in the cut box still
straddle the cut plane (a working bisect leaves none). Usage: blender -b -P debug_bisect.py -- <out_dir> --candidate"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_roadster as B  # noqa: E402  (main() does not run on import)

RT = B.RT
_bisect = RT.bisect


def checked(bm, co, no, box):
    _bisect(bm, co, no, box)
    (x0, x1), (y0, y1), (z0, z1) = box
    n = RT.V(no).normalized()
    c0 = RT.V(co)
    total = crossing = 0
    for f in bm.faces:
        c = f.calc_center_median()
        if not (x0 <= c.x <= x1 and y0 <= c.y <= y1 and z0 <= c.z <= z1):
            continue
        total += 1
        ds = [(v.co - c0).dot(n) for v in f.verts]
        if min(ds) < -1e-5 and max(ds) > 1e-5:
            crossing += 1
    print("BISECTCHECK co=(%.3f,%.3f,%.3f) no=(%.3f,%.3f,%.3f) faces=%d crossing=%d"
          % (c0.x, c0.y, c0.z, n.x, n.y, n.z, total, crossing))


RT.bisect = checked
B.main()
