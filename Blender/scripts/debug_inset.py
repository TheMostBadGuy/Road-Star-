"""Debug run of the car build: keeps a copy of every panel just before inset_border (object '<name>_preInset') and logs
the centres of faces the inset flips. Usage: blender -b -P debug_inset.py -- <out_dir> --candidate"""
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_roadster as B  # noqa: E402  (main() does not run on import)

RT = B.RT
_inset = RT.inset_border


def logged(ob, d):
    pre = ob.copy()
    pre.data = ob.data.copy()
    pre.name = ob.name + "_preInset"
    ob.users_collection[0].objects.link(pre)
    pre.hide_render = True
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    before = [f.normal.copy() for f in bm.faces]
    bm.free()
    _inset(ob, d)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    if len(bm.faces) == len(before):
        bad = [f.calc_center_median() for f, n in zip(bm.faces, before) if f.normal.dot(n) < 0]
        for c in bad[:60]:
            print("FLIP %s (%.3f, %.3f, %.3f)" % (ob.name, c.x + ob.location.x, c.y + ob.location.y, c.z + ob.location.z))
    else:
        print("FLIP %s: face count changed by the weld; centres not matched" % ob.name)
    bm.free()


RT.inset_border = logged
B.main()
