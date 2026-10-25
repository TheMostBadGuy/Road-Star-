"""Re-import only the car meshes from Blender/export and re-assign materials (fast model iteration)."""
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_project as S   # noqa: E402  (module guards main() behind __main__ check below)

LOG = []
try:
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    files = [S.EXPORT + "/" + f for f in os.listdir(S.EXPORT) if f.endswith(".fbx") and "Terrain" not in f and "Apron" not in f]
    S.import_files(files, "/Game/Roadster/Meshes", S.fbx_options())
    lin = lambda c: tuple((x / 255.0) ** 2.2 for x in c)
    I = {}
    for slot, asset in (("M_Paint_AnoWhite", "MI_Paint_AnoWhite"), ("M_BlackGloss", "MI_BlackGloss"), ("M_Underbody", "MI_Underbody"),
                        ("M_Wheel_Spoke", "MI_WheelSpoke"), ("M_Wheel_AeroDisc", "MI_WheelAeroDisc"), ("M_Titanium", "MI_Titanium"),
                        ("M_Nozzle_Gold", "MI_NozzleGold"), ("M_Plate_Silver", "MI_PlateSilver"), ("M_Rubber", "MI_Rubber"),
                        ("M_Brake_CarbonCeramic", "MI_BrakeCarbonCeramic"), ("M_Interior_Ultrawhite", "MI_Ultrawhite"),
                        ("M_Interior_Microsuede", "MI_Microsuede"), ("M_Interior_Dark", "MI_InteriorDark"), ("M_Aluminum", "MI_Aluminum"),
                        ("M_Light_White", "MI_LightWhite"), ("M_Light_Red", "MI_LightRed"), ("M_Light_Amber", "MI_LightAmber"),
                        ("M_Interior_RedAmbient", "MI_RedAmbient"), ("M_Glass_Gold", "M_Glass"), ("M_Glass_Rear", "MI_GlassRear"),
                        ("M_Carbon", "M_Carbon"), ("M_Tire_Front", "MI_TireFront"), ("M_Tire_Rear", "MI_TireRear"),
                        ("M_Screen_Main", "M_Screen"), ("M_Screen_Forward", "M_Screen")):
        I[slot] = unreal.load_asset("/Game/Materials/" + asset)
    S.assign_materials(I)
finally:
    with open(S.ROOT + "/Unreal/RoadsterSim/Saved/reimport_log.txt", "w") as f:
        f.write("\n".join(S.LOG))
    unreal.SystemLibrary.quit_editor()
