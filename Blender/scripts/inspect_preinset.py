"""Read-only: render a panel's pre-inset copy (from a debug_inset.py build) with its wireframe, from behind and above.
Usage: blender -b <debug.blend> -P inspect_preinset.py -- <panel name> <out_dir>"""
import os
import sys

import bpy
from mathutils import Vector

name, out = sys.argv[-2], sys.argv[-1]
sc = bpy.context.scene
sc.render.resolution_x, sc.render.resolution_y = 1600, 1000
pre = bpy.data.objects[name + "_preInset"]
for ob in sc.objects:
    if ob.type == 'MESH':
        ob.hide_render = ob is not pre
pre.hide_render = False
wire = pre.copy()
wire.data = pre.data.copy()
sc.collection.objects.link(wire)
m = wire.modifiers.new("Wire", 'WIREFRAME')
m.thickness = 0.0012
m.use_replace = True
black = bpy.data.materials.new("WireRed")
black.diffuse_color = (1, 0, 0, 1)
black.use_nodes = True
bsdf = black.node_tree.nodes.get("Principled BSDF")
if bsdf:
    bsdf.inputs["Base Color"].default_value = (1, 0, 0, 1)
    bsdf.inputs["Emission Color"].default_value = (1, 0, 0, 1)
    bsdf.inputs["Emission Strength"].default_value = 2.0
wire.data.materials.clear()
wire.data.materials.append(black)
m.material_offset = 0
wire.hide_render = False

border = [e for e in pre.data.edges if False]
for tag, loc, target in (("rear", (-3.6, 0.0, 1.25), (-2.0, 0.0, 0.88)), ("corner", (-2.5, 1.3, 1.2), (-2.0, 0.7, 0.85))):
    c = bpy.data.cameras.new("Cam" + tag)
    c.lens = 50
    o = bpy.data.objects.new("Cam" + tag, c)
    sc.collection.objects.link(o)
    o.location = Vector(loc)
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = o
    sc.render.filepath = os.path.join(out, "%s_preinset_%s.png" % (name, tag))
    bpy.ops.render.render(write_still=True)
