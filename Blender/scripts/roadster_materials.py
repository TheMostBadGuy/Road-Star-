"""Shared Blender materials for the Roadster (names are matched to Unreal material slots by setup_project.py)."""
import bpy


def mat(name, color, rough=0.4, metal=0.0, emit=None, alpha=1.0, coat=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    m.diffuse_color = (*color, alpha)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if coat > 0:
        bsdf.inputs["Coat Weight"].default_value = coat
        bsdf.inputs["Coat Roughness"].default_value = 0.08
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*emit[0], 1)
        bsdf.inputs["Emission Strength"].default_value = emit[1]
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        bsdf.inputs["Transmission Weight"].default_value = 0.0
        m.surface_render_method = 'BLENDED' if hasattr(m, "surface_render_method") else None
    return m


def materials():
    return {
        "Paint": mat("M_Paint_AnoWhite", (0.80, 0.80, 0.78), rough=0.32, coat=0.4),
        "Glass": mat("M_Glass_Gold", (0.16, 0.10, 0.06), rough=0.03, metal=0.3, alpha=0.85),
        "BlackGloss": mat("M_BlackGloss", (0.012, 0.012, 0.014), rough=0.18),
        "Underbody": mat("M_Underbody", (0.02, 0.02, 0.022), rough=0.7),
        "RearGlass": mat("M_Glass_Rear", (0.03, 0.025, 0.02), rough=0.05, metal=0.2),
        "Tire": mat("M_Tire", (0.018, 0.018, 0.018), rough=0.85),
        "Spoke": mat("M_Wheel_Spoke", (0.01, 0.01, 0.012), rough=0.15, metal=0.6),
        "Disc": mat("M_Wheel_AeroDisc", (0.55, 0.56, 0.57), rough=0.35, metal=0.2),
        "Titanium": mat("M_Titanium", (0.62, 0.55, 0.46), rough=0.25, metal=1.0),
        "LightWhite": mat("M_Light_White", (1, 1, 1), emit=((1, 0.97, 0.92), 30)),
        "LightRed": mat("M_Light_Red", (0.5, 0.0, 0.0), emit=((1, 0.02, 0.01), 40)),
        "Amber": mat("M_Light_Amber", (0.6, 0.25, 0.0), emit=((1, 0.4, 0.0), 6)),
        "Carbon": mat("M_Carbon", (0.025, 0.025, 0.028), rough=0.25, coat=1.0),
        "Gold": mat("M_Nozzle_Gold", (0.85, 0.62, 0.32), rough=0.2, metal=1.0),
        "PlateSilver": mat("M_Plate_Silver", (0.70, 0.70, 0.70), rough=0.35, metal=0.9),
        "Rubber": mat("M_Rubber", (0.02, 0.02, 0.02), rough=0.9),
        "BrakeDisc": mat("M_Brake_CarbonCeramic", (0.12, 0.12, 0.125), rough=0.45, metal=0.3),
        "Leather": mat("M_Interior_Ultrawhite", (0.86, 0.86, 0.84), rough=0.55),
        "Suede": mat("M_Interior_Microsuede", (0.10, 0.10, 0.105), rough=0.95),
        "InteriorDark": mat("M_Interior_Dark", (0.03, 0.03, 0.032), rough=0.7),
        "RedAmbient": mat("M_Interior_RedAmbient", (0.4, 0.0, 0.0), emit=((1, 0.03, 0.02), 25)),
        "Aluminum": mat("M_Aluminum", (0.75, 0.75, 0.76), rough=0.25, metal=1.0),
        "Screen": mat("M_Screen_Main", (0.0, 0.0, 0.0), emit=((0.6, 0.02, 0.02), 4)),
        "ScreenSmall": mat("M_Screen_Forward", (0.0, 0.0, 0.0), emit=((0.6, 0.02, 0.02), 4)),
    }
