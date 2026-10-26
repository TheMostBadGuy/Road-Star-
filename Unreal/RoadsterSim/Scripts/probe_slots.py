"""Diagnostics: material slot names and the materials assigned to selected car meshes."""
import unreal
out = []
for name in ("SM_Yoke", "SM_Interior", "SM_Seat_Driver", "SM_Wheel_Front"):
    sm = unreal.load_asset("/Game/Roadster/Meshes/" + name)
    if not sm:
        out.append("%s missing" % name)
        continue
    for i, sl in enumerate(sm.get_editor_property("static_materials")):
        mi = sl.get_editor_property("material_interface")
        out.append("%s [%d] slot=%s -> %s" % (name, i, sl.get_editor_property("material_slot_name"), mi.get_path_name() if mi else None))
mi = unreal.load_asset("/Game/Materials/MI_Microsuede")
if mi:
    out.append("MI_Microsuede BaseColor=%s" % unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mi, "BaseColor"))
open("E:/TeslaRoadsterSim/Unreal/RoadsterSim/Saved/probe_slots.txt", "w").write("\n".join(out))
unreal.SystemLibrary.quit_editor()
