"""Read-only canonical FBX/proxy audit. Uses the installed pure FBX data parser, never Blender/bpy."""
import sys,types,pathlib,json,math,hashlib,datetime
sys.dont_write_bytecode=True
root=pathlib.Path(r'E:\TeslaRoadsterSim');out=root/'Tests'/'Driveability-20261026';exports=root/'Blender'/'export'
parser_dir=pathlib.Path(r'C:\Program Files\Blender Foundation\Blender 5.1\5.1\scripts\addons_core\io_scene_fbx')
pkg=types.ModuleType('io_scene_fbx');pkg.__path__=[str(parser_dir)];sys.modules['io_scene_fbx']=pkg
from io_scene_fbx import parse_fbx
manifest=json.loads((exports/'manifest.json').read_text(encoding='utf-8-sig'));probes=json.loads((out/'body-probes.json').read_text(encoding='utf-8-sig'))['probes']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def walk(n):
 yield n
 for c in n.elems:yield from walk(c)
def add(a,b):return tuple(a[k]+b[k] for k in range(3))
def rotate(p,axis,degrees):
 norm=math.sqrt(sum(x*x for x in axis));a=tuple(x/norm for x in axis);t=math.radians(degrees);c=math.cos(t);s=math.sin(t);dot=sum(a[k]*p[k] for k in range(3));cross=(a[1]*p[2]-a[2]*p[1],a[2]*p[0]-a[0]*p[2],a[0]*p[1]-a[1]*p[0]);return tuple(p[k]*c+cross[k]*s+a[k]*dot*(1-c) for k in range(3))
static=['Roadster_Body','Side_Details','Splitter','Rear_Diffuser','Winglets']
moving=['Door_L','Door_R','DoorGlass_L','DoorGlass_R','Rear_Wing','Wing_Struts','Barge_L','Barge_R','FrontWing_L','FrontWing_R','Hood_Vent','HeadlightCover_L','HeadlightCover_R','Skirt_L','Skirt_R','Skirt_Front','Skirt_Rear']
meshes={};sources=[];bounds=[]
for name in static+moving:
 path=exports/('SM_'+name+'.fbx')
 if not path.is_file():continue
 before=sha(path);fbx,version=parse_fbx.parse(str(path));after=sha(path)
 if before!=after:raise RuntimeError('Export changed while reading '+name)
 global_props={x.props[0].decode():list(x.props[4:]) for n in fbx.elems if n.id==b'GlobalSettings' for x in walk(n) if x.id==b'P'}
 assert global_props['UpAxis']==[2] and global_props['FrontAxis']==[0] and global_props['FrontAxisSign']==[-1] and global_props['CoordAxis']==[1] and global_props['CoordAxisSign']==[-1],global_props
 scale=global_props['UnitScaleFactor'][0]/100
 geoms=[n for n in walk(fbx) if n.id==b'Geometry'];assert len(geoms)==1
 verts=next(x.props[0] for x in geoms[0].elems if x.id==b'Vertices')
 for n in walk(fbx):
  if n.id==b'Model':
   for p in walk(n):
    if p.id==b'P' and p.props[0] in (b'Lcl Translation',b'Lcl Rotation',b'GeometricTranslation',b'GeometricRotation'):assert all(abs(v)<1e-10 for v in p.props[4:]),(name,p.props)
    if p.id==b'P' and p.props[0] in (b'Lcl Scaling',b'GeometricScaling'):assert all(abs(v-1)<1e-10 for v in p.props[4:]),(name,p.props)
 # Exported X-forward/Z-up FBX baked rotation: file=(Blender.y,-Blender.x,Blender.z).
 points=[(-verts[i+1]*scale,verts[i]*scale,verts[i+2]*scale) for i in range(0,len(verts),3)]
 meshes[name]=points;sources.append({'path':str(path),'sha256':before,'vertices':len(points),'fbx_version':version})
 pivot=manifest['moving'].get(name,[0,0,0]);baseline=[add(p,pivot) for p in points]
 bounds.append({'part':name,'min_blender_car_m':[min(p[k] for p in baseline) for k in range(3)],'max_blender_car_m':[max(p[k] for p in baseline) for k in range(3)]})
# Current body export sanity: matches authored length/width and car-space identity, not centimetres or a 90deg car yaw.
body=next(x for x in bounds if x['part']=='Roadster_Body');assert 4<body['max_blender_car_m'][0]-body['min_blender_car_m'][0]<5;assert 1.8<body['max_blender_car_m'][1]-body['min_blender_car_m'][1]<2.2
normals=[(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]+[tuple(v/math.sqrt(2) for v in n) for n in ((1,0,1),(-1,0,1),(0,1,1),(0,-1,1),(1,1,0),(1,-1,0),(-1,1,0),(-1,-1,0))]
states=[]
for state,pitch,deploy,doors in [('stowed',0,0,False),('wing_low_drag',5.5,1,False),('wing_high_downforce',19,1,False),('doors_open',0,0,True)]:
 supports=[(-1e30,None) for _ in normals];largest=(-1,None);count=0
 for name,points in meshes.items():
  pivot=manifest['moving'].get(name,[0,0,0]);axis=None;angle=0
  if name=='Rear_Wing':axis=[0,1,0];angle=pitch;pivot=add(pivot,tuple(v*deploy for v in manifest['wing']['lift']))
  elif name=='Wing_Struts':pivot=add(pivot,tuple(v*deploy for v in manifest['wing']['lift']))
  elif doors and name.startswith('Door'):axis=manifest['hinges'][name]['axis'];angle=manifest['hinges'][name]['travel']['open']
  for index,p in enumerate(points):
   if axis:p=rotate(p,axis,angle)
   p=add(p,pivot);p=(p[0],-p[1],p[2]);count+=1
   for k,n in enumerate(normals):
    support=sum(p[j]*n[j] for j in range(3))
    if support>supports[k][0]:supports[k]=(support,{'part':name,'vertex':index,'point_physics_m':p})
   # A gap in the union proves a small local world feature can touch visible geometry without any shell probe.
   gap=min(math.sqrt(sum((p[k]-q['p'][k])**2 for k in range(3)))-q['r'] for q in probes)
   if gap>largest[0]:largest=(gap,{'part':name,'vertex':index,'point_physics_m':p})
 planes=[]
 for k,n in enumerate(normals):
  collision=max(sum(q['p'][j]*n[j] for j in range(3))+q['r'] for q in probes)
  planes.append({'normal':n,'visible_support_m':supports[k][0],'probe_support_m':collision,'visible_beyond_probe_m':supports[k][0]-collision,'witness':supports[k][1]})
 result={'state':state,'evaluated_vertices':count,'planes':planes,'largest_local_feature_gap_m':largest[0],'local_feature_witness':largest[1]};states.append(result)
 z=next(x for x in planes if x['normal']==(0,0,1));print(state,'vertices',count,'visible_top',z['visible_support_m'],'probe_top',z['probe_support_m'],'ceiling_gap_m',z['visible_beyond_probe_m'],'local_gap_m',largest[0],largest[1]['part'])
report={'captured_at':datetime.datetime.now().astimezone().isoformat(),'head':'935bbac482b4077121013f9e05d5ea42e37d86fd','method':'Read current FBX vertex arrays with installed pure io_scene_fbx parser; no Blender process, bpy, model edits or engine. Match manifest pivot/wing/door transforms and physics Y-right frame. Other moving parts are stowed. Compare visible support and union to actual compiled16bodyprobes; exclude tyres/suspension/internalparts.','manifest_sha256':sha(exports/'manifest.json'),'rig_sha256':sha(root/'Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterRig.h'),'physics_sha256':sha(root/'Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterPhysics.h'),'parser_sha256':sha(parser_dir/'parse_fbx.py'),'sources':sources,'bounds':bounds,'states':states,'limits':'Geometric representation audit only; exact live component transforms/import settings and UWorld tests remain required. Positive plane gap proves these proxies cannot protect all sampled visible vertices in that pose. Union gap identifies a finite local-feature counterexample, not a manufacturer collision hull.'}
(out/'visible-envelope-audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
