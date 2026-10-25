"""Repair the imported candidate and service apron, save, then let startup settle before exit."""
import os
import runpy
import time
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Maps/ProvingGround')
try:
    runpy.run_path(os.path.join(HERE, 'import_desert_candidate.py'), run_name='__main__')
    helpers = runpy.run_path(os.path.join(HERE, 'polish_proving_ground.py'), run_name='world_material_helpers')
    new_material = helpers['material']('M_PG_ServiceAsphalt_Linear', (45,46,45), .92)
    old_material = unreal.load_asset('/Game/Environment/RoadsterPolish20261025/Materials/M_PG_ServiceAsphalt')
    changed = 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            for index in range(component.get_num_materials()):
                if component.get_material(index) == old_material:
                    component.set_material(index, new_material)
                    changed += 1
    assert les.save_current_level(), 'Could not save repaired materials'
    unreal.log('WORLD_MATERIAL_REPAIR: changed %d apron slots' % changed)
finally:
    # Returning from startup allows pending editor UI/asset work to finish before shutdown.
    _quit_at = time.monotonic() + 15
    def _settled_exit(delta):
        if time.monotonic() >= _quit_at:
            unreal.unregister_slate_post_tick_callback(_quit_handle)
            unreal.SystemLibrary.quit_editor()
    _quit_handle = unreal.register_slate_post_tick_callback(_settled_exit)
