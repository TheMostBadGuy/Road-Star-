"""Add a restrained test-facility paddock to the existing ProvingGround map.

Run inside this project's Unreal 5.7 editor with ProvingGround open and PIE stopped.
Does not rebuild the map, car, track or existing materials. Only its own tagged
actors are replaced. The two existing material usage flags are repaired in place.
Original map/material packages are saved and copied to E: before modifications.

API signatures checked against the installed UE5.7 headers: EditorActorSubsystem,
LevelEditorSubsystem, EditorAssetLibrary, MaterialEditingLibrary,
SubobjectDataSubsystem, SubobjectDataBlueprintFunctionLibrary and
InstancedStaticMeshComponent. Actual engine execution/visual review is still
required. The parent process owns editor startup/shutdown; this script never quits.
"""
from pathlib import Path
from datetime import datetime, timezone
import json
import shutil
import traceback
import hashlib

import unreal

PROJECT = Path("E:/TeslaRoadsterSim/Unreal/RoadsterSim")
CONTENT = PROJECT / "Content"
TAG = "RoadsterPolish20261025"
# Parent's baseline screenshot showed the existing 10-lux daylight blowing out
# emissive headlights. Extended luminance range is enabled in DefaultEngine.ini.
# Leave False to preserve the original lighting, or restore the backed-up map.
PHYSICAL_DAYLIGHT = True
MATERIAL_DIR = "/Game/Environment/RoadsterPolish20261025/Materials"
MAP_PACKAGE = "/Game/Maps/ProvingGround"
STAMP = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ")
BACKUP = Path("E:/TeslaRoadsterSim/Backups") / ("VisualPolish-" + STAMP)
LOG_PATH = PROJECT / "Saved" / "Polish" / ("proving_ground_" + STAMP + ".json")
LOG = {
    "started_utc": STAMP, "status": "started", "backup_root": str(BACKUP),
    "map": MAP_PACKAGE, "tag": TAG, "backed_up_files": [],
    "material_usage_changes": [], "materials_created": [], "materials_reused": [],
    "removed_own_actors": [], "created_actors": [], "instance_groups": [],
    "lighting_changes": [], "warnings": [],
    "placement": {
        "units": "meters in script; centimeters in Unreal",
        "minimum_north_clearance_m": 100.0,
        "all_new_geometry_collision": "disabled",
        "basis": "setup_project.py apron spans y +/-350m; terrain basin is flat within1500m. TrackActor.cpp starts each preset in +X at y=0. Paddock and decoration remain south of y=-100m near the start.",
        "custom_track_note": "No arbitrary custom layout can be guaranteed visually clear of scenery. All added geometry has collision disabled, so it cannot block the vehicle physics. Existing runtime track geometry is untouched."
    },
}
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ASSETS = unreal.AssetToolsHelpers.get_asset_tools()


def say(message):
    unreal.log("[RoadsterPolish] " + message)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_log():
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    LOG_PATH.write_text(json.dumps(LOG, indent=2), encoding="utf-8")


def backup_file(source):
    source = Path(source)
    require(source.is_file(), "Cannot back up missing file: " + str(source))
    destination = BACKUP / "Content" / source.relative_to(CONTENT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    require(destination.stat().st_size == source.stat().st_size,
            "Backup size differs: " + str(source))
    with source.open("rb") as original, destination.open("rb") as copied:
        source_hash = hashlib.file_digest(original, "sha256").hexdigest()
        require(hashlib.file_digest(copied, "sha256").hexdigest() == source_hash, "Backup hash differs: " + str(source))
    LOG["backed_up_files"].append({"source": str(source), "backup": str(destination), "bytes": source.stat().st_size, "sha256": source_hash})


def backup_package(package_name):
    stem = CONTENT / package_name.removeprefix("/Game/")
    candidates = [stem.with_suffix(s) for s in (".umap", ".uasset", ".uexp", ".ubulk")]
    existing = [p for p in candidates if p.is_file()]
    require(existing, "Package has no on-disk recovery file: " + package_name)
    for source in existing:
        backup_file(source)


def save_asset(asset):
    require(EAL.save_loaded_asset(asset, only_if_is_dirty=False),
            "Failed saving asset: " + asset.get_path_name())


def preflight_and_backup():
    require(Path(unreal.Paths.project_dir()).resolve() == PROJECT.resolve(),
            "This script must run in E:/TeslaRoadsterSim/Unreal/RoadsterSim.")
    require(not LES.is_in_play_in_editor(), "Stop Play-in-Editor before polishing the map.")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world is not None and world.get_path_name().split(".")[0] == MAP_PACKAGE,
            "Open /Game/Maps/ProvingGround before running this script; no other map will be changed.")
    require(LES.save_current_level(), "Could not save the current map for its recovery copy.")
    BACKUP.mkdir(parents=True, exist_ok=False)
    backup_package(MAP_PACKAGE)
    for package in ("/Game/Materials/M_Terrain", "/Game/Materials/M_Track_Support"):
        asset = unreal.load_asset(package)
        require(isinstance(asset, unreal.Material), "Required material missing: " + package)
        save_asset(asset)
        backup_package(package)
    # On repeat runs also preserve this script's own material packages.
    own_directory = CONTENT / "Environment" / TAG
    if own_directory.exists():
        for source in own_directory.rglob("*"):
            if source.is_file() and source.suffix in (".uasset", ".uexp", ".ubulk"):
                backup_file(source)
    # Support external actors if the map is converted to external-actor storage later.
    for kind in ("__ExternalActors__", "__ExternalObjects__"):
        external = CONTENT / kind / "Maps" / "ProvingGround"
        if external.exists():
            for source in external.rglob("*"):
                if source.is_file():
                    backup_file(source)
    LOG["status"] = "backup_complete"
    write_log()


def fix_usage(package, property_name):
    material = unreal.load_asset(package)
    previous = bool(material.get_editor_property(property_name))
    if not previous:
        material.set_editor_property(property_name, True)
        MEL.recompile_material(material)
        save_asset(material)
    require(bool(material.get_editor_property(property_name)),
            "Material usage flag did not persist in memory: " + package)
    LOG["material_usage_changes"].append({"asset": package, "property": property_name, "before": previous, "after": True})


def adjust_daylight():
    if not PHYSICAL_DAYLIGHT:
        LOG["lighting_changes"].append({"enabled": False})
        return
    actors = EAS.get_all_level_actors()
    suns = [a for a in actors if isinstance(a, unreal.DirectionalLight) and a.get_actor_label() == "Sun"]
    postprocess = [a for a in actors if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label() == "PostProcess"]
    require(len(suns) == 1 and len(postprocess) == 1,
            "Physical daylight expects one existing Sun and one existing PostProcess volume; no lights will be created.")
    sun_component = suns[0].get_component_by_class(unreal.DirectionalLightComponent)
    previous = float(sun_component.get_editor_property("intensity"))
    sun_component.set_editor_property("intensity", 70000.0)
    LOG["lighting_changes"].append({"actor": suns[0].get_path_name(), "property": "intensity_lux", "before": previous, "after": 70000.0})
    settings = postprocess[0].get_editor_property("settings")
    for key, value in (("auto_exposure_min_brightness", 3.0), ("auto_exposure_max_brightness", 16.0),
                       ("auto_exposure_speed_up", 3.0), ("auto_exposure_speed_down", 4.0),
                       ("film_grain_intensity", 0.0), ("scene_fringe_intensity", 0.0)):
        before = float(settings.get_editor_property(key))
        before_override = bool(settings.get_editor_property("override_" + key))
        settings.set_editor_property("override_" + key, True)
        settings.set_editor_property(key, value)
        LOG["lighting_changes"].append({"actor": postprocess[0].get_path_name(), "property": key,
                                        "before": before, "before_override": before_override, "after": value, "after_override": True})
    postprocess[0].set_editor_property("settings", settings)
    LOG["warnings"].append("Physical daylight is an explicit visual-review candidate: 70000 lux and EV100 adaptation3..16, including tunnel adaptation. Inspect exterior and cockpit before acceptance; restore the map recovery copy to revert. All other sun, post-process, skylight, fog, bloom and emissive settings were preserved.")


def color_linear(rgb):
    def channel(x):
        x = x / 255.0
        return x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4
    return unreal.LinearColor(*(channel(c) for c in rgb), 1.0)


def material(name, rgb, roughness, metallic=0.0):
    path = MATERIAL_DIR + "/" + name
    existing = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if existing:
        require(isinstance(existing, unreal.Material), "Unexpected asset type at " + path)
        LOG["materials_reused"].append(path)
        return existing
    result = ASSETS.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    require(result is not None, "Could not create " + path)
    base = MEL.create_material_expression(result, unreal.MaterialExpressionConstant3Vector, -450, 0)
    base.set_editor_property("constant", color_linear(rgb))
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for index, (value, output) in enumerate(((roughness, unreal.MaterialProperty.MP_ROUGHNESS), (metallic, unreal.MaterialProperty.MP_METALLIC))):
        scalar = MEL.create_material_expression(result, unreal.MaterialExpressionConstant, -450, 160 + index * 140)
        scalar.set_editor_property("r", value)
        MEL.connect_material_property(scalar, "", output)
    # Reuse the project's asphalt detail on this horizontal service apron only.
    # No texture imports or project-wide material rebuilding are performed.
    if name.startswith("M_PG_ServiceAsphalt"):
        textures = [unreal.load_asset("/Game/Textures/" + key) for key in ("T_Asphalt_D", "T_Asphalt_N", "T_Asphalt_R")]
        if all(textures):
            world = MEL.create_material_expression(result, unreal.MaterialExpressionWorldPosition, -1200, 0)
            xy = MEL.create_material_expression(result, unreal.MaterialExpressionComponentMask, -1020, 0)
            xy.set_editor_property("r", True)
            xy.set_editor_property("g", True)
            MEL.connect_material_expressions(world, "", xy, "")
            factor = MEL.create_material_expression(result, unreal.MaterialExpressionConstant, -1020, 150)
            factor.set_editor_property("r", 1.0 / 800.0)
            tile = MEL.create_material_expression(result, unreal.MaterialExpressionMultiply, -840, 0)
            MEL.connect_material_expressions(xy, "", tile, "A")
            MEL.connect_material_expressions(factor, "", tile, "B")
            for index, (texture, output) in enumerate(zip(textures, (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_NORMAL, unreal.MaterialProperty.MP_ROUGHNESS))):
                sample = MEL.create_material_expression(result, unreal.MaterialExpressionTextureSample, -650, index * 200)
                sample.set_editor_property("texture", texture)
                if index == 1:
                    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
                elif index == 2:
                    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
                MEL.connect_material_expressions(tile, "", sample, "UVs")
                MEL.connect_material_property(sample, "RGB" if index < 2 else "R", output)
        else:
            LOG["warnings"].append("Existing asphalt detail textures unavailable; service apron uses its constant rough asphalt material.")
    result.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(result)
    save_asset(result)
    LOG["materials_created"].append(path)
    return result


def meters(values):
    return unreal.Vector(*(float(v) * 100.0 for v in values))


def tag_actor(actor, label):
    actor.set_actor_label(label)
    actor.set_editor_property("tags", [unreal.Name(TAG)])
    actor.set_folder_path(unreal.Name("RoadsterPolish20261025/Paddock"))
    actor.set_actor_enable_collision(False)
    LOG["created_actors"].append({"label": label, "path": actor.get_path_name()})


def cube(label, location, dimensions, surface, yaw=0.0):
    # Every architectural primitive remains entirely south of y=-100m.
    require(location[1] + dimensions[1] / 2 < -100.0, "Geometry exceeds safe south zone: " + label)
    actor = EAS.spawn_actor_from_class(unreal.StaticMeshActor, meters(location), unreal.Rotator(yaw=yaw))
    require(actor is not None, "Could not spawn " + label)
    tag_actor(actor, label)
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    require(component.set_static_mesh(CUBE_MESH), "Could not assign cube mesh: " + label)
    component.set_material(0, surface)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_scale3d(unreal.Vector(*dimensions))
    return actor


def instance_group(label, surface, placements, mesh=None):
    """Create a persistent editor ISM component via the supported subsystem.

    Actor.AddComponentByClass is ScriptNoExport in UE5.7, so it is deliberately
    avoided. SubobjectDataSubsystem creates, owns and registers the component.
    """
    actor = EAS.spawn_actor_from_class(unreal.Actor, unreal.Vector())
    require(actor is not None, "Could not spawn instance group: " + label)
    tag_actor(actor, label)
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_instance(actor)
    require(bool(handles), "No actor handle for component creation: " + label)
    params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.InstancedStaticMeshComponent)
    handle, reason = subsystem.add_new_subobject(params)
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object(data)
    require(isinstance(component, unreal.InstancedStaticMeshComponent), "Cannot create persistent ISM: " + str(reason))
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    require(component.set_static_mesh(mesh or CUBE_MESH), "Cannot set ISM mesh: " + label)
    component.set_material(0, surface)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    transforms = []
    for position, dimensions in placements:
        require(position[1] + dimensions[1] / 2 < -100.0, "Instance exceeds safe south zone: " + label)
        transforms.append(unreal.Transform(location=meters(position), scale=unreal.Vector(*dimensions)))
    component.add_instances(transforms, False, True, False)
    require(component.get_instance_count() == len(transforms), "Wrong instance count: " + label)
    LOG["instance_groups"].append({"label": label, "instances": len(transforms), "component": component.get_path_name()})
    return component


def text_sign(label, text, location, size=0.85):
    require(location[1] < -100.0, "Sign exceeds safe south zone.")
    actor = EAS.spawn_actor_from_class(unreal.TextRenderActor, meters(location), unreal.Rotator(yaw=90.0))
    require(actor is not None, "Could not create sign: " + label)
    tag_actor(actor, label)
    component = actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(text)
    component.set_world_size(size * 100)
    component.set_text_render_color(unreal.Color(220, 225, 225, 255))
    component.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)


def build_paddock(palette):
    concrete, steel, glass, white, red, asphalt = (palette[k] for k in ("concrete", "steel", "glass", "white", "red", "asphalt"))
    # Low podium, two-storey control suite at west end, six engineering bays east.
    cube("PG Paddock Concrete Podium", (103, -169, .13), (210, 110, .24), concrete)
    cube("PG Service Apron", (104, -145, .28), (202, 56, .10), asphalt)
    cube("PG Operations Foundation", (28, -180, .65), (42, 26, 1.0), concrete)
    cube("PG Operations Ground Floor", (28, -180, 3.1), (40, 24, 5.0), steel)
    cube("PG Operations Upper Slab", (28, -180, 5.7), (44, 28, .6), concrete)
    cube("PG Operations Upper Rear", (28, -187, 8.4), (41, 8, 5.0), concrete)
    cube("PG Operations Roof", (28, -180, 11.3), (45, 29, .65), steel)
    cube("PG Operations North Glazing", (28, -167.75, 8.4), (40, .12, 4.8), glass)
    cube("PG Operations West Glazing", (7.6, -176, 8.4), (.12, 15.5, 4.8), glass)
    cube("PG Operations East Glazing", (48.4, -176, 8.4), (.12, 15.5, 4.8), glass)
    for x in (8, 16, 24, 32, 40, 48):
        cube("PG Control Suite Mullion %.0f" % x, (x, -167.6, 8.4), (.13, .28, 4.85), steel)
    cube("PG Control Suite Red Fascia", (28, -165.4, 11.13), (43.5, .12, .12), red)
    text_sign("PG Dynamics Lab Sign", "ROADSTER  |  VEHICLE DYNAMICS", (28, -165.28, 10.35), .68)
    # Long garage block: an architectural shell with inset dark shutters and reveals.
    cube("PG Engineering Bays Rear Shell", (124, -187, 3.8), (140, 8, 7.1), concrete)
    cube("PG Engineering Bays Roof", (124, -176, 7.5), (143, 29, .6), steel)
    cube("PG Engineering Bays Front Lintel", (124, -162.9, 6.75), (141, 1.1, 1.0), concrete)
    for number, x in enumerate((64, 88, 112, 136, 160, 184), 1):
        cube("PG Bay %02d Roller Door" % number, (x, -166.9, 3.35), (20.5, .16, 5.8), steel)
        cube("PG Bay %02d Transom" % number, (x, -166.74, 5.4), (19.8, .10, .55), glass)
        cube("PG Bay %02d Number Panel" % number, (x, -162.24, 6.75), (3.2, .12, .7), steel)
        text_sign("PG Bay %02d Label" % number, "%02d" % number, (x, -162.14, 6.57), .48)
    for x in (52, 76, 100, 124, 148, 172, 196):
        cube("PG Garage Concrete Pier %.0f" % x, (x, -166, 3.7), (.8, 2.2, 7.0), concrete)
    # Open pit canopy projects toward the start strip, leaving a wide empty buffer.
    cube("PG Pit Canopy Roof", (110, -139.5, 6.0), (170, 18, .34), steel)
    cube("PG Pit Canopy Soffit", (110, -139.5, 5.78), (167, 17, .10), white)
    cube("PG Pit Canopy Front Edge", (110, -130.44, 5.95), (170, .12, .26), concrete)
    for x in (28, 60, 92, 124, 156, 192):
        cube("PG Pit Canopy Column %.0f" % x, (x, -145.8, 3.05), (.35, .35, 5.8), steel)
    # All repeated dressing uses ISM, avoiding hundreds of separate level actors.
    bay_lines = []
    for x in range(28, 197, 24):
        bay_lines.append(((float(x), -145.5, .345), (.12, 23, .015)))
    for x in range(22, 198, 8):
        bay_lines.append(((float(x), -120, .345), (3.5, .14, .015)))
    instance_group("PG Instanced Pit and Parking Markings", white, bay_lines)
    joints = []
    for x in range(0, 209, 8):
        joints.append(((float(x), -202, .255), (.025, 34, .012)))
    for y in (-190, -198, -206, -214):
        joints.append(((103, y, .255), (209, .025, .012)))
    instance_group("PG Instanced Concrete Expansion Joints", steel, joints)
    curbs = []
    for x in range(-2, 209, 3):
        curbs.append(((float(x), -224, .38), (2.9, .4, .5)))
    for y in range(-221, -113, 3):
        for x in (-2, 208):
            curbs.append(((x, float(y), .38), (.4, 2.9, .5)))
    instance_group("PG Instanced Paddock Curb Modules", concrete, curbs)
    # Wheel stops and perimeter bollards give human scale without roadside clutter.
    stops = [((float(x), -214, .39), (1.8, .2, .22)) for x in range(10, 202, 6)]
    instance_group("PG Instanced Parking Wheel Stops", concrete, stops)
    posts = [((float(x), -112, .82), (.14, .14, 1.0)) for x in range(6, 205, 8)]
    instance_group("PG Instanced Paddock Bollards", steel, posts, CYLINDER_MESH)


def main():
    global CUBE_MESH, CYLINDER_MESH
    preflight_and_backup()
    CUBE_MESH = unreal.load_asset("/Engine/BasicShapes/Cube")
    CYLINDER_MESH = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    require(CUBE_MESH is not None and CYLINDER_MESH is not None, "Engine primitive meshes are missing.")
    fix_usage("/Game/Materials/M_Terrain", "used_with_nanite")
    fix_usage("/Game/Materials/M_Track_Support", "used_with_instanced_static_meshes")
    adjust_daylight()
    palette = {
        "concrete": material("M_PG_Concrete", (156, 153, 145), .82),
        "steel": material("M_PG_AnthraciteSteel", (48, 53, 57), .38, .8),
        "glass": material("M_PG_OpaqueFacadeGlass", (37, 53, 63), .13, .65),
        "white": material("M_PG_PaintedWhite", (201, 204, 199), .68),
        "red": material("M_PG_AccentRed", (155, 18, 18), .42),
        "asphalt": material("M_PG_ServiceAsphalt", (45, 46, 45), .92),
    }
    # Delete only this script's own previous actors after a complete disk backup.
    for actor in EAS.get_all_level_actors():
        if TAG in [str(t) for t in actor.get_editor_property("tags")]:
            label = actor.get_actor_label()
            require(EAS.destroy_actor(actor), "Cannot remove previous own actor: " + label)
            LOG["removed_own_actors"].append(label)
    build_paddock(palette)
    require(LES.save_current_level(), "Could not save the polished level.")
    LOG["status"] = "saved"
    LOG["completed_utc"] = datetime.now(timezone.utc).isoformat()
    LOG["warnings"].append("Material graph compilation and map save are not rendered visual or packaged-game validation; parent must inspect the result.")
    say("Saved paddock: %d actors, %d instances. Recovery: %s" % (len(LOG["created_actors"]), sum(x["instances"] for x in LOG["instance_groups"]), BACKUP))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        LOG["status"] = "failed"
        LOG["error"] = traceback.format_exc()
        unreal.log_error("[RoadsterPolish] Failed; inspect JSON log and recovery copy. " + LOG["error"])
        raise
    finally:
        write_log()
