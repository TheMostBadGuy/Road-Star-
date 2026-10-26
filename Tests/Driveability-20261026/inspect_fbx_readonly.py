import sys,types,pathlib,json
sys.dont_write_bytecode=True
pkg=types.ModuleType('io_scene_fbx');pkg.__path__=[r'C:\Program Files\Blender Foundation\Blender 5.1\5.1\scripts\addons_core\io_scene_fbx'];sys.modules['io_scene_fbx']=pkg
from io_scene_fbx import parse_fbx
root,version=parse_fbx.parse(r'E:\TeslaRoadsterSim\Blender\export\SM_Roadster_Body.fbx')
def walk(node):
    yield node
    for child in node.elems:yield from walk(child)
for node in root.elems:
    if node.id==b'GlobalSettings':
        print('GLOBAL',[(x.props[0].decode(),list(x.props[4:])) for x in walk(node) if x.id==b'P'])
for node in walk(root):
    if node.id==b'Geometry':
        verts=next((c.props[0] for c in node.elems if c.id==b'Vertices'),None)
        if verts is not None:print('GEOMETRY',node.props[:2],len(verts)//3,[min(verts[k::3]) for k in range(3)],[max(verts[k::3]) for k in range(3)])
    if node.id==b'Model':print('MODEL',node.props,[(x.props[0].decode(),list(x.props[4:])) for x in walk(node) if x.id==b'P' and (b'Lcl' in x.props[0] or b'Geometric' in x.props[0] or b'Rotation' in x.props[0])])
