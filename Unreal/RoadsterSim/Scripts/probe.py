"""Diagnostics: list the ProvingGround actors with their key lighting / post-process settings."""
import unreal
out = []
try:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/ProvingGround")
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        line = "%s  %s  loc=%s rot=%s" % (a.get_actor_label(), a.get_class().get_name(), a.get_actor_location(), a.get_actor_rotation())
        for cls in (unreal.DirectionalLightComponent, unreal.SkyLightComponent):
            c = a.get_component_by_class(cls)
            if c:
                line += "  intensity=%s visible=%s" % (c.get_editor_property("intensity"), c.get_editor_property("visible"))
        if isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property("settings")
            for k in ("auto_exposure_method", "auto_exposure_min_brightness", "auto_exposure_max_brightness", "auto_exposure_bias"):
                line += "  %s=%s/%s" % (k, s.get_editor_property(k), s.get_editor_property("override_" + k))
        out.append(line)
except Exception as e:
    out.append("ERROR %r" % e)
open("E:/TeslaRoadsterSim/Unreal/RoadsterSim/Saved/probe.txt", "w").write("\n".join(out))
unreal.SystemLibrary.quit_editor()
