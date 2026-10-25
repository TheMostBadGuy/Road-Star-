"""Import an additive attributed DEM/PBR candidate, then swap only Terrain's mesh.

Requires ProvingGround open in this project with PIE stopped. Never imports over
existing environment assets, changes lighting, moves the Terrain actor, or quits
the editor. Parent must inspect rendering/collision after running.
"""
from pathlib import Path
from datetime import datetime, timezone
import json, shutil, hashlib, traceback
import unreal

ROOT=Path('E:/TeslaRoadsterSim/WorldAssets/DesertCandidate-20261025')
PROJECT=Path('E:/TeslaRoadsterSim/Unreal/RoadsterSim')
DEST='/Game/Environment/DesertCandidate20261025'
STAMP=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
BACKUP=Path('E:/TeslaRoadsterSim/Backups')/('DesertCandidate-'+STAMP)
LOG={'status':'started','timestamp_utc':STAMP,'backup':str(BACKUP),'imports':[],'changes':[]}
EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary
AT=unreal.AssetToolsHelpers.get_asset_tools()
LES=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def require(value,message):
    if not value:raise RuntimeError(message)

def save(asset):require(EAL.save_loaded_asset(asset,only_if_is_dirty=False),'Save failed '+asset.get_path_name())

def backup():
    require(Path(unreal.Paths.project_dir()).resolve()==PROJECT.resolve(),'Wrong project')
    require(not LES.is_in_play_in_editor(),'Stop PIE first')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.')[0]=='/Game/Maps/ProvingGround','Open ProvingGround first')
    require(LES.save_current_level(),'Initial map save failed')
    BACKUP.mkdir(parents=True,exist_ok=False)
    files=list((PROJECT/'Content'/'Maps').glob('ProvingGround.*'))
    require(files,'Map recovery package missing')
    # Also preserve external-actor packages if the map is converted in the future.
    for kind in ['__ExternalActors__','__ExternalObjects__']:
        folder=PROJECT/'Content'/kind/'Maps'/'ProvingGround'
        if folder.exists():files.extend(p for p in folder.rglob('*') if p.is_file())
    LOG['backed_up_files']=[]
    for source in files:
        target=BACKUP/'Content'/source.relative_to(PROJECT/'Content');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        source_hash=hashlib.file_digest(source.open('rb'),'sha256').hexdigest()
        require(hashlib.file_digest(target.open('rb'),'sha256').hexdigest()==source_hash,'Backup hash mismatch')
        LOG['backed_up_files'].append({'path':str(source),'sha256':source_hash})

def import_one(path,folder,options=None):
    name=Path(path).stem;target=folder+'/'+name
    if EAL.does_asset_exist(target):return unreal.load_asset(target)
    task=unreal.AssetImportTask();task.filename=str(path);task.destination_path=folder;task.automated=True;task.replace_existing=False;task.save=True
    if options is not None:task.options=options
    AT.import_asset_tasks([task]);LOG['imports'].extend(list(task.imported_object_paths))
    asset=unreal.load_asset(target)
    require(asset is not None,'Import missing '+target)
    return asset

def expression(material,cls,**properties):
    node=MEL.create_material_expression(material,cls)
    require(node is not None,'Cannot create expression '+str(cls))
    for key,value in properties.items():node.set_editor_property(key,value)
    return node

def link(a,b,pin='',output=''):
    require(MEL.connect_material_expressions(a,output,b,pin),'Material connection failed: '+pin+' / '+output)

def constant(material,value):return expression(material,unreal.MaterialExpressionConstant,r=float(value))

def vector(material,value):return expression(material,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*value,1.0))

def op(material,cls,a,b):
    node=expression(material,cls);link(a,node,'A');link(b,node,'B');return node

def mix(material,a,b,alpha):
    node=expression(material,unreal.MaterialExpressionLinearInterpolate);link(a,node,'A','XYZ Texture');link(b,node,'B','XYZ Texture');link(alpha,node,'Alpha');return node

def world_aligned(material,texture,size_m,normal=False):
    path='/Engine/Functions/Engine_MaterialFunctions01/Texturing/'+('WorldAlignedNormal' if normal else 'WorldAlignedTexture')
    function=unreal.load_asset(path);require(function is not None,'Missing engine function '+path)
    call=expression(material,unreal.MaterialExpressionMaterialFunctionCall)
    require(call.set_material_function(function),'Cannot bind engine material function')
    obj=expression(material,unreal.MaterialExpressionTextureObject,texture=texture)
    if normal:obj.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif '_rough_' in texture.get_name():obj.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    size=vector(material,(size_m*100,)*3)
    link(obj,call,'TextureObject');link(size,call,'TextureSize')
    return call

def make_material(textures):
    path=DEST+'/M_DesertDEM_PBR_v2'
    if EAL.does_asset_exist(path):return unreal.load_asset(path)
    material=AT.create_asset('M_DesertDEM_PBR_v2',DEST,unreal.Material,unreal.MaterialFactoryNew())
    require(material is not None,'Cannot create candidate material')
    material.set_editor_property('used_with_nanite',True)
    normal=expression(material,unreal.MaterialExpressionVertexNormalWS)
    nz=expression(material,unreal.MaterialExpressionComponentMask,r=False,g=False,b=True,a=False);link(normal,nz)
    slope=op(material,unreal.MaterialExpressionSubtract,constant(material,.94),nz)
    slope=op(material,unreal.MaterialExpressionMultiply,slope,constant(material,3.5))
    clamp=expression(material,unreal.MaterialExpressionSaturate);link(slope,clamp)
    # Native WorldAligned functions provide triplanar projection and correctly
    # oriented normal maps. Do not use worldXY-only UVs on cliff faces.
    samplers={}
    for asset,size in [('sandy_gravel_02',4.0),('aerial_rocks_02',8.0)]:
        samplers[asset]={kind:world_aligned(material,textures[asset][kind],size,kind=='nor_dx') for kind in ['Diffuse','nor_dx','Rough']}
    for kind,output in [('Diffuse',unreal.MaterialProperty.MP_BASE_COLOR),('nor_dx',unreal.MaterialProperty.MP_NORMAL),('Rough',unreal.MaterialProperty.MP_ROUGHNESS)]:
        blended=mix(material,samplers['sandy_gravel_02'][kind],samplers['aerial_rocks_02'][kind],clamp)
        if kind=='Diffuse':
            # A second broad-scale scanned-rock layer varies hue/brightness over
            # mountain faces and breaks a uniform near-detail texture repetition.
            macro=world_aligned(material,textures['aerial_rocks_02']['Diffuse'],180.0)
            luminance=expression(material,unreal.MaterialExpressionComponentMask,r=True,g=False,b=False,a=False);link(macro,luminance,'','XYZ Texture')
            broad=op(material,unreal.MaterialExpressionMultiply,luminance,constant(material,.35))
            broad=op(material,unreal.MaterialExpressionAdd,broad,constant(material,.78))
            blended=op(material,unreal.MaterialExpressionMultiply,blended,broad)
        require(MEL.connect_material_property(blended,'',output),'Cannot connect material property')
    MEL.layout_material_expressions(material);MEL.recompile_material(material);save(material)
    return material

def main():
    backup()
    manifest=json.loads((ROOT/'source_manifest.json').read_text())
    textures={}
    for asset,info in manifest['textures'].items():
        textures[asset]={}
        for kind,item in info['maps'].items():
            tex=import_one(ROOT/item['path'],DEST+'/Textures')
            if kind=='nor_dx':
                tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP);tex.set_editor_property('srgb',False)
            elif kind=='Rough':
                tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_GRAYSCALE);tex.set_editor_property('srgb',False)
            else:tex.set_editor_property('srgb',True)
            save(tex);textures[asset][kind]=tex
    material=make_material(textures)
    unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX False')
    options=unreal.FbxImportUI();options.import_mesh=True;options.import_textures=False;options.import_materials=False;options.import_as_skeletal=False;options.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
    data=options.static_mesh_import_data;data.combine_meshes=True;data.generate_lightmap_u_vs=False;data.auto_generate_collision=False;data.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;data.convert_scene=True;data.force_front_x_axis=False;data.convert_scene_unit=True
    mesh=import_one(ROOT/'SM_DesertTerrain.fbx',DEST,options)
    require(isinstance(mesh,unreal.StaticMesh),'Wrong terrain asset type')
    mesh.set_material(0,material)
    nanite=mesh.get_editor_property('nanite_settings');nanite.enabled=True;mesh.set_editor_property('nanite_settings',nanite)
    body=mesh.get_editor_property('body_setup')
    require(body is not None,'Terrain import has no collision body')
    body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    save(mesh)
    terrain=[a for a in EAS.get_all_level_actors() if a.get_actor_label()=='Terrain']
    require(len(terrain)==1,'Expected exactly one existing Terrain actor')
    component=terrain[0].get_component_by_class(unreal.StaticMeshComponent)
    require(component is not None,'Terrain actor has no StaticMeshComponent')
    previous=component.get_editor_property('static_mesh')
    if previous != mesh:
        require(component.set_static_mesh(mesh),'Terrain mesh swap failed')
    component.set_material(0,material)
    require(LES.save_current_level(),'Final map save failed')
    LOG['changes'].append({'actor':terrain[0].get_path_name(),'previous_mesh':previous.get_path_name() if previous else None,'new_mesh':mesh.get_path_name(),'transform':'unchanged'})
    LOG['status']='saved';LOG['terrain_stats']=manifest['terrain'];LOG['attribution']=manifest['attribution']
    unreal.log('[DesertCandidate] Saved candidate terrain; inspect rendering and collision before acceptance. Backup '+str(BACKUP))

if __name__=='__main__':
    try:main()
    except Exception:
        LOG['status']='failed';LOG['error']=traceback.format_exc();raise
    finally:
        (PROJECT/'Saved'/'Polish').mkdir(parents=True,exist_ok=True)
        (PROJECT/'Saved'/'Polish'/('desert_candidate_'+STAMP+'.json')).write_text(json.dumps(LOG,indent=2))
