"""Read-only close-up renders of the left door shut lines, with and without the door, to find what draws the black
teeth. Usage: blender -b <file.blend> -P inspect_shutline.py -- <out_dir>"""
import math
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


front = cam("CamDoorFront", (1.55, 2.3, 0.95), (0.80, 0.93, 0.62), 60)
rear = cam("CamDoorRear", (-0.55, 2.4, 0.95), (-0.30, 0.95, 0.55), 60)
for c, tag in ((front, "front"), (rear, "rear")):
    shot(c, os.path.join(out, "door_%s_all.png" % tag))
    shot(c, os.path.join(out, "door_%s_nodoor.png" % tag), hide=("Door_L", "DoorGlass_L"))
    shot(c, os.path.join(out, "door_%s_dooronly.png" % tag), only=("Door_L", "DoorGlass_L"))
