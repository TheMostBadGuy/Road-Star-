"""Read-only close-up renders of the stowed rear wing and its bay: everything, without the wing, and the wing alone.
Usage: blender -b <file.blend> -P inspect_wing.py -- <out_dir>"""
import os
import sys

import bpy
from mathutils import Vector

out = sys.argv[-1]
sc = bpy.context.scene
sc.render.resolution_x, sc.render.resolution_y = 1400, 1000
sc.render.resolution_percentage = 100


def cam(name, loc, target, lens):
    c = bpy.data.cameras.new(name)
    c.lens = lens
    o = bpy.data.objects.new(name, c)
    sc.collection.objects.link(o)
    o.location = Vector(loc)
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    return o


def shot(camera, path, hide=(), only=None):
    saved = {}
    for ob in sc.objects:
        if ob.type != 'MESH':
            continue
        saved[ob.name] = ob.hide_render
        if only is not None:
            ob.hide_render = ob.name not in only
        elif ob.name in hide:
            ob.hide_render = True
    sc.camera = camera
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    for n, h in saved.items():
        sc.objects[n].hide_render = h


wing = ("Rear_Wing", "Wing_Struts")
views = {
    "rear_high": cam("CamWingRearHigh", (-4.2, 1.6, 2.2), (-1.85, 0.0, 0.85), 45),
    "side_corner": cam("CamWingCorner", (-2.6, 2.1, 1.35), (-1.85, 0.75, 0.85), 50),
}
for tag, c in views.items():
    shot(c, os.path.join(out, "wing_%s_all.png" % tag))
    shot(c, os.path.join(out, "wing_%s_nowing.png" % tag), hide=wing)
    shot(c, os.path.join(out, "wing_%s_wingonly.png" % tag), only=wing)
