"""Permit eye adaptation from daylight into the physically lit tunnel."""
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Maps/ProvingGround')
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, unreal.PostProcessVolume):
        settings = actor.get_editor_property('settings')
        for key, value in [('auto_exposure_min_brightness', 3.0), ('auto_exposure_max_brightness', 16.0),
                           ('auto_exposure_speed_up', 3.0), ('auto_exposure_speed_down', 4.0)]:
            settings.set_editor_property('override_' + key, True)
            settings.set_editor_property(key, value)
        actor.set_editor_property('settings', settings)
assert les.save_current_level(), 'Exposure map save failed'
unreal.SystemLibrary.quit_editor()
