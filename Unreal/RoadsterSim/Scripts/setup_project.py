"""Unreal Editor setup for the Roadster simulator.

Imports meshes / textures / audio from E:/TeslaRoadsterSim, builds every material, assigns them to the
mesh slots (by the Blender material names) and assembles /Game/Maps/ProvingGround.
Run inside the editor:  UnrealEditor.exe RoadsterSim.uproject -ExecutePythonScript=".../setup_project.py"
"""
import os, json
import unreal

ROOT = "E:/TeslaRoadsterSim"
EXPORT = ROOT + "/Blender/export"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
LOG = []


def log(*a):
    s = " ".join(str(x) for x in a)
    LOG.append(s)
    unreal.log("[RoadsterSetup] " + s)


# ----------------------------------------------------------------------------- import
def import_files(files, dest, options=None):
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = f
        t.destination_path = dest
        t.automated = True
        t.replace_existing = True
        t.save = True
        if options is not None:
            t.options = options
        tasks.append(t)
    AT.import_asset_tasks(tasks)
    out = []
    for t in tasks:
        out += list(t.imported_object_paths)
    return out


def fbx_options():
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_textures = False
    o.import_materials = False
    o.import_as_skeletal = False
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    s = o.static_mesh_import_data
    s.combine_meshes = True
    s.generate_lightmap_u_vs = False
    s.auto_generate_collision = False
    s.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    s.convert_scene = True
    s.force_front_x_axis = False
    s.convert_scene_unit = True
    return o


def import_textures():
    files = [ROOT + "/Textures/" + f for f in os.listdir(ROOT + "/Textures") if f.endswith(".png")]
    import_files(files, "/Game/Textures")
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        tex = unreal.load_asset("/Game/Textures/" + name)
        if not tex:
            continue
        if name.endswith("_N"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif name.endswith("_R") or name in ("T_Noise",) or name.startswith("T_TireText"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            tex.set_editor_property("srgb", False)
        EAL.save_loaded_asset(tex)
    log("textures", len(files))


def import_audio():
    files = [ROOT + "/Audio/" + f for f in os.listdir(ROOT + "/Audio") if f.endswith(".wav")]
    import_files(files, "/Game/Audio")
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        sw = unreal.load_asset("/Game/Audio/" + name)
        if sw:
            sw.set_editor_property("looping", True)
            EAL.save_loaded_asset(sw)
    log("audio", len(files))


def import_meshes():
    files = [EXPORT + "/" + f for f in os.listdir(EXPORT) if f.endswith(".fbx")]
    car = [f for f in files if "Terrain" not in f and "Apron" not in f]
    env = [f for f in files if f not in car]
    import_files(car, "/Game/Roadster/Meshes", fbx_options())
    import_files(env, "/Game/Environment", fbx_options())
    log("meshes", len(car), "env", len(env))


# ----------------------------------------------------------------------------- material helpers
def fresh_material(name, path="/Game/Materials"):
    full = path + "/" + name
    if EAL.does_asset_exist(full):
        EAL.delete_asset(full)
    return AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())


def E(m, cls, x=-400, y=0, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(m, name, v, x=-400, y=0):
    return E(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def vec(m, name, rgb, x=-400, y=0):
    return E(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def const(m, v, x=-400, y=0):
    return E(m, unreal.MaterialExpressionConstant, x, y, r=v)


def const3(m, rgb, x=-400, y=0):
    return E(m, unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def tex(m, path, x=-700, y=0, sampler=None, param=None):
    cls = unreal.MaterialExpressionTextureSampleParameter2D if param else unreal.MaterialExpressionTextureSample
    e = E(m, cls, x, y)
    if param:
        e.set_editor_property("parameter_name", param)
    e.set_editor_property("texture", unreal.load_asset(path))
    if sampler is not None:
        e.set_editor_property("sampler_type", sampler)
    return e


def link(a, b, inp="", out=""):
    MEL.connect_material_expressions(a, out, b, inp)


def prop(e, p, out=""):
    MEL.connect_material_property(e, out, p)


def op(m, cls, a, b, x=-300, y=0):
    e = E(m, cls, x, y)
    link(a, e, "A"); link(b, e, "B")
    return e


def mul(m, a, b, x=-300, y=0): return op(m, unreal.MaterialExpressionMultiply, a, b, x, y)
def add(m, a, b, x=-300, y=0): return op(m, unreal.MaterialExpressionAdd, a, b, x, y)
def sub(m, a, b, x=-300, y=0): return op(m, unreal.MaterialExpressionSubtract, a, b, x, y)


def lerp(m, a, b, alpha, x=-200, y=0):
    e = E(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, e, "A"); link(b, e, "B"); link(alpha, e, "Alpha")
    return e


def mask(m, a, r=False, g=False, b=False, aa=False, x=-500, y=0):
    e = E(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=aa)
    link(a, e)
    return e


def fn1(m, cls, a, x=-300, y=0, **props):
    e = E(m, cls, x, y, **props)
    link(a, e)
    return e


def uv(m, idx=0, tile=1.0, x=-900, y=0):
    return E(m, unreal.MaterialExpressionTextureCoordinate, x, y, coordinate_index=idx, u_tiling=tile, v_tiling=tile)


def finish(m):
    # Keep these persisted so runtime/cooking does not substitute the default material.
    if m.get_name() == "M_Terrain":
        m.set_editor_property("used_with_nanite", True)
    if m.get_name() == "M_Track_Support":
        m.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


def instance(name, parent, scalars=None, vectors=None, textures=None, path="/Game/Materials"):
    full = path + "/" + name
    if EAL.does_asset_exist(full):
        EAL.delete_asset(full)
    mi = AT.create_asset(name, path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    for k, v in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(mi, k, unreal.load_asset(v))
    EAL.save_loaded_asset(mi)
    return mi


MP = unreal.MaterialProperty
SAMP_N = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
SAMP_G = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE


# ----------------------------------------------------------------------------- materials
def build_master():
    """Clear-coat master: colour, metal, roughness, clear coat, detail normal and emissive."""
    m = fresh_material("M_Master")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    m.set_editor_property("use_material_attributes", True)
    mma = E(m, unreal.MaterialExpressionMakeMaterialAttributes, -200, 0)
    prop(mma, MP.MP_MATERIAL_ATTRIBUTES)
    link(vec(m, "BaseColor", (0.5, 0.5, 0.5), -600, -300), mma, "BaseColor")
    link(scalar(m, "Metallic", 0.0, -600, -200), mma, "Metallic")
    link(scalar(m, "Roughness", 0.4, -600, -120), mma, "Roughness")
    link(scalar(m, "Specular", 0.5, -600, -40), mma, "Specular")
    link(scalar(m, "ClearCoat", 0.0, -600, 40), mma, "ClearCoat")
    link(scalar(m, "ClearCoatRoughness", 0.1, -600, 120), mma, "ClearCoatRoughness")
    em = mul(m, vec(m, "Emissive", (0, 0, 0), -800, 220), scalar(m, "Intensity", 0.0, -800, 320), -500, 260)
    link(em, mma, "EmissiveColor")
    # detail normal blended towards flat by NormalStrength
    n = tex(m, "/Game/Textures/T_Rubber_N", -1000, 450, SAMP_N, "NormalTex")
    link(uv(m, 0, 1.0, -1300, 450), n, "UVs")
    tile = scalar(m, "NormalTiling", 1.0, -1300, 560)
    uvs = mul(m, uv(m, 0, 1.0, -1500, 450), tile, -1200, 500)
    link(uvs, n, "UVs")
    flat = const3(m, (0, 0, 1), -900, 600)
    link(lerp(m, flat, n, scalar(m, "NormalStrength", 0.0, -900, 680), -600, 520), mma, "Normal")
    finish(m)
    return m


def build_glass():
    m = fresh_material("M_Glass")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property("two_sided", True)
    prop(vec(m, "Tint", (0.42, 0.25, 0.09), -600, -200), MP.MP_BASE_COLOR)
    prop(scalar(m, "Metallic", 0.55, -600, -100), MP.MP_METALLIC)
    prop(const(m, 0.04, -600, 0), MP.MP_ROUGHNESS)
    prop(const(m, 1.0, -600, 60), MP.MP_SPECULAR)
    # more opaque at grazing angles (fresnel), like the gold canopy in Tesla's photos
    fr = E(m, unreal.MaterialExpressionFresnel, -800, 200, exponent=3.0, base_reflect_fraction=0.08)
    op_ = lerp(m, scalar(m, "Opacity", 0.45, -800, 300), const(m, 0.92, -800, 380), fr, -500, 250)
    prop(op_, MP.MP_OPACITY)
    finish(m)
    return m


def build_carbon():
    m = fresh_material("M_Carbon")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    t = scalar(m, "Tiling", 6.0, -1300, 0)
    uvs = mul(m, uv(m, 0, 1.0, -1500, -100), t, -1100, 0)
    d = tex(m, "/Game/Textures/T_Carbon_D", -800, -200)
    n = tex(m, "/Game/Textures/T_Carbon_N", -800, 200, SAMP_N)
    link(uvs, d, "UVs"); link(uvs, n, "UVs")
    m.set_editor_property("use_material_attributes", True)
    mma = E(m, unreal.MaterialExpressionMakeMaterialAttributes, -200, 0)
    prop(mma, MP.MP_MATERIAL_ATTRIBUTES)
    link(d, mma, "BaseColor", "RGB")
    link(n, mma, "Normal", "RGB")
    link(const(m, 0.35, -500, 0), mma, "Roughness")
    link(const(m, 0.3, -500, 60), mma, "Metallic")
    link(const(m, 1.0, -500, 120), mma, "ClearCoat")
    link(const(m, 0.05, -500, 180), mma, "ClearCoatRoughness")
    finish(m)
    return m


def build_tire():
    """Rubber with a raised, lighter 'P ZERO TROFEO RS' sidewall band (tire UVs: u around, v across the profile)."""
    m = fresh_material("M_Tire")
    coords = uv(m, 0, 1.0, -1600, 0)
    u = mask(m, coords, r=True, x=-1400, y=-80)
    v = mask(m, coords, g=True, x=-1400, y=60)
    # outer sidewall band v in [0.83, 0.97] -> text v 0..1
    tv = op(m, unreal.MaterialExpressionDivide, sub(m, v, const(m, 0.83, -1300, 140), -1200, 100), const(m, 0.14, -1200, 180), -1050, 120)
    tu = mul(m, u, const(m, 2.0, -1300, -140), -1200, -100)
    ap = E(m, unreal.MaterialExpressionAppendVector, -900, 0)
    link(tu, ap, "A"); link(tv, ap, "B")
    txt = tex(m, "/Game/Textures/T_TireText_Front", -700, 0, SAMP_G, "TextTex")
    link(ap, txt, "UVs")
    inband = mul(m, fn1(m, unreal.MaterialExpressionSaturate, mul(m, tv, const(m, 1e3, -900, 220), -800, 200), -700, 200),
                 fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, const(m, 1.0, -1000, 300), tv, -900, 300), const(m, 1e3, -900, 360), -800, 320), -700, 320), -600, 260)
    letters = mul(m, txt, inband, -450, 100)
    col = lerp(m, const3(m, (0.018, 0.018, 0.019), -400, -200), const3(m, (0.32, 0.32, 0.32), -400, -120), letters, -250, -150)
    prop(col, MP.MP_BASE_COLOR)
    prop(lerp(m, const(m, 0.88, -400, 300), const(m, 0.6, -400, 360), letters, -250, 330), MP.MP_ROUGHNESS)
    n = tex(m, "/Game/Textures/T_Rubber_N", -700, 500, SAMP_N)
    link(mul(m, coords, const(m, 18.0, -1000, 560), -850, 520), n, "UVs")
    prop(n, MP.MP_NORMAL, "RGB")
    finish(m)
    return m


def build_screen():
    m = fresh_material("M_Screen")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    t = tex(m, "/Engine/EngineResources/Black", -700, 0, None, "ScreenTex")
    # Maintain a legible cockpit display through daylight/tunnel eye adaptation.
    color = mul(m, t, scalar(m, "Brightness", 0.9, -700, 200), -400, 50)
    adaptive = E(m, unreal.MaterialExpressionEyeAdaptationInverse, -150, 50)
    inputs = MEL.get_material_expression_input_names(adaptive)
    light_pin = next((p for p in inputs if "light" in p.lower()), inputs[0])
    if not MEL.connect_material_expressions(color, "", adaptive, light_pin):
        raise RuntimeError("Could not connect adaptive screen input: " + str(inputs))
    prop(adaptive, MP.MP_EMISSIVE_COLOR, "")
    finish(m)
    return m


def build_plume():
    """Cold nitrogen jet: white condensation, noisy, fading along the cone (engine cone UVs: v along)."""
    m = fresh_material("M_Plume")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    coords = uv(m, 0, 1.0, -1300, 0)
    pan = E(m, unreal.MaterialExpressionPanner, -1100, 0, speed_x=0.0, speed_y=-4.0)
    link(coords, pan, "Coordinate")
    n = tex(m, "/Game/Textures/T_Noise", -900, 0, SAMP_G)
    link(pan, n, "UVs")
    v = mask(m, coords, g=True, x=-1100, y=200)
    fade = fn1(m, unreal.MaterialExpressionOneMinus, v, -900, 200)
    fr = E(m, unreal.MaterialExpressionFresnel, -900, 320, exponent=1.5)
    core = fn1(m, unreal.MaterialExpressionOneMinus, fr, -750, 320)
    opac = mul(m, mul(m, mul(m, n, fade, -700, 100), core, -550, 150), const(m, 0.9, -550, 250), -400, 150)
    prop(opac, MP.MP_OPACITY)
    prop(mul(m, const3(m, (0.9, 0.95, 1.0), -600, -150), const(m, 2.5, -600, -80), -400, -120), MP.MP_EMISSIVE_COLOR)
    finish(m)
    return m


def build_xray():
    """Engineering X-ray view: the body becomes a faint, edge-lit translucent ghost so the cold-gas system,
    fans and chassis inside are visible. Colour and strength are parameters for the pawn."""
    m = fresh_material("M_XRay")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    fr = E(m, unreal.MaterialExpressionFresnel, -900, 150, exponent=2.2, base_reflect_fraction=0.02)
    col = vec(m, "Tint", (0.35, 0.75, 1.0), -900, -100)
    strength = scalar(m, "Strength", 1.0, -900, 300)
    edge = mul(m, fr, strength, -700, 200)
    prop(mul(m, col, mul(m, edge, const(m, 2.0, -700, 320), -550, 250), -400, 0), MP.MP_EMISSIVE_COLOR)
    prop(add(m, mul(m, edge, const(m, 0.55, -550, 380), -400, 300), const(m, 0.03, -550, 450), -300, 350), MP.MP_OPACITY)
    finish(m)
    return m


def build_smoke():
    """Smoke / vapour puffs on instanced spheres: per-instance alpha in custom data 0, soft edges."""
    m = fresh_material("M_Smoke")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    alpha = E(m, unreal.MaterialExpressionPerInstanceCustomData, -900, 0, data_index=0)
    fr = E(m, unreal.MaterialExpressionFresnel, -900, 150, exponent=1.2)
    soft = fn1(m, unreal.MaterialExpressionOneMinus, fr, -750, 150)
    wp = E(m, unreal.MaterialExpressionWorldPosition, -1300, 300)
    wuv = mul(m, mask(m, wp, r=True, g=True, x=-1150, y=300), const(m, 0.004, -1150, 400), -1000, 330)
    pan = E(m, unreal.MaterialExpressionPanner, -900, 330, speed_x=0.05, speed_y=0.08)
    link(wuv, pan, "Coordinate")
    n = tex(m, "/Game/Textures/T_Noise", -700, 330, SAMP_G)
    link(pan, n, "UVs")
    op_ = mul(m, mul(m, alpha, mul(m, soft, soft, -600, 150), -450, 80), n, -300, 150)
    prop(op_, MP.MP_OPACITY)
    prop(const3(m, (0.82, 0.82, 0.84), -400, -150), MP.MP_BASE_COLOR)
    finish(m)
    return m


def build_track_materials():
    # asphalt with lane markings from UV1 (u = metres along, v = 0..1 across)
    m = fresh_material("M_Track_Asphalt")
    c0 = uv(m, 0, 1.0, -1700, -200)
    d = tex(m, "/Game/Textures/T_Asphalt_D", -1300, -300); link(c0, d, "UVs")
    n = tex(m, "/Game/Textures/T_Asphalt_N", -1300, 0, SAMP_N); link(c0, n, "UVs")
    r = tex(m, "/Game/Textures/T_Asphalt_R", -1300, 250, SAMP_G); link(c0, r, "UVs")
    c1 = uv(m, 1, 1.0, -1700, 500)
    u1 = mask(m, c1, r=True, x=-1500, y=450)
    v1 = mask(m, c1, g=True, x=-1500, y=560)
    # edge lines: v < 0.015 or v > 0.985
    dv = fn1(m, unreal.MaterialExpressionAbs, sub(m, v1, const(m, 0.5, -1400, 640), -1300, 600), -1200, 600)
    edge = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, dv, const(m, 0.478, -1200, 700), -1100, 650), const(m, 400.0, -1100, 740), -1000, 680), -900, 680)
    # dashed centre line: |v-0.5| < 0.005 and frac(u/12) < 0.5
    cl = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, const(m, 0.0055, -1200, 800), dv, -1100, 800), const(m, 400.0, -1100, 880), -1000, 840), -900, 840)
    dash = fn1(m, unreal.MaterialExpressionFrac, mul(m, u1, const(m, 1.0 / 12.0, -1400, 960), -1300, 940), -1150, 940)
    dashm = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, const(m, 0.5, -1100, 1020), dash, -1000, 980), const(m, 200.0, -1000, 1060), -900, 1000), -800, 1000)
    lines = fn1(m, unreal.MaterialExpressionSaturate, add(m, edge, mul(m, cl, dashm, -750, 900), -650, 800), -550, 800)
    base = mul(m, d, const(m, 1.0, -1100, -300), -1000, -300)
    col = lerp(m, base, const3(m, (0.78, 0.78, 0.76), -800, -100), lines, -600, -200)
    prop(col, MP.MP_BASE_COLOR)
    prop(n, MP.MP_NORMAL, "RGB")
    prop(lerp(m, r, const(m, 0.45, -800, 300), lines, -600, 250), MP.MP_ROUGHNESS)
    finish(m)

    # curbs: red / white blocks every 2 m (UV0.u = s/8)
    m = fresh_material("M_Track_Curb")
    c0 = uv(m, 0, 1.0, -1300, 0)
    u0 = mask(m, c0, r=True, x=-1100, y=0)
    f = fn1(m, unreal.MaterialExpressionFrac, mul(m, u0, const(m, 2.0, -1000, 80), -900, 40), -800, 40)
    sel = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, f, const(m, 0.5, -700, 120), -650, 80), const(m, 60.0, -650, 160), -550, 100), -450, 100)
    prop(lerp(m, const3(m, (0.55, 0.02, 0.02), -500, -150), const3(m, (0.8, 0.8, 0.8), -500, -60), sel, -300, -100), MP.MP_BASE_COLOR)
    prop(const(m, 0.55, -400, 250), MP.MP_ROUGHNESS)
    finish(m)

    for name, tint, rough in (("M_Track_Barrier", (0.85, 0.85, 0.85), 0.75), ("M_Track_Tube", (0.55, 0.56, 0.6), 0.6), ("M_Track_Support", (0.75, 0.74, 0.72), 0.8)):
        m = fresh_material(name)
        c0 = uv(m, 0, 1.0, -1300, 0)
        d = tex(m, "/Game/Textures/T_Concrete_D", -900, -150); link(c0, d, "UVs")
        n = tex(m, "/Game/Textures/T_Concrete_N", -900, 150, SAMP_N); link(c0, n, "UVs")
        prop(mul(m, d, const3(m, tint, -900, -300), -600, -200), MP.MP_BASE_COLOR)
        prop(n, MP.MP_NORMAL, "RGB")
        prop(const(m, rough, -500, 100), MP.MP_ROUGHNESS)
        finish(m)

    m = fresh_material("M_Track_LED")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    prop(const3(m, (40.0, 44.0, 50.0), -400, 0), MP.MP_EMISSIVE_COLOR)
    finish(m)

    m = fresh_material("M_Track_StartLine")
    c0 = uv(m, 0, 1.0, -1300, 0)
    fl = fn1(m, unreal.MaterialExpressionFloor, c0, -1100, 0)
    s = add(m, mask(m, fl, r=True, x=-950, y=-60), mask(m, fl, g=True, x=-950, y=60), -800, 0)
    chk = fn1(m, unreal.MaterialExpressionFrac, mul(m, s, const(m, 0.5, -800, 120), -700, 60), -600, 60)
    sel = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, chk, const(m, 0.25, -600, 160), -500, 120), const(m, 100.0, -500, 200), -400, 140), -300, 140)
    prop(lerp(m, const3(m, (0.02, 0.02, 0.02), -400, -150), const3(m, (0.85, 0.85, 0.85), -400, -60), sel, -200, -100), MP.MP_BASE_COLOR)
    prop(const(m, 0.5, -300, 250), MP.MP_ROUGHNESS)
    finish(m)


def build_environment_materials():
    # desert terrain: world-aligned sand, rock on slopes, large-scale tint variation
    m = fresh_material("M_Terrain")
    wp = E(m, unreal.MaterialExpressionWorldPosition, -1800, 0)
    xy = mask(m, wp, r=True, g=True, x=-1600, y=0)
    uvs = mul(m, xy, const(m, 1.0 / 900.0, -1600, 100), -1450, 50)
    macro = mul(m, xy, const(m, 1.0 / 25000.0, -1600, 250), -1450, 250)
    sd = tex(m, "/Game/Textures/T_Sand_D", -1200, -250); link(uvs, sd, "UVs")
    sn = tex(m, "/Game/Textures/T_Sand_N", -1200, 300, SAMP_N); link(uvs, sn, "UVs")
    mv = tex(m, "/Game/Textures/T_Noise", -1200, 600, SAMP_G); link(macro, mv, "UVs")
    rt = tex(m, "/Game/Textures/T_Concrete_D", -1200, -50)
    link(mul(m, xy, const(m, 1.0 / 1500.0, -1500, -50), -1350, -60), rt, "UVs")
    rock = mul(m, rt, const3(m, (0.62, 0.48, 0.38), -1200, 80), -1000, 0)
    nrm = E(m, unreal.MaterialExpressionVertexNormalWS, -1300, 800)
    slope = fn1(m, unreal.MaterialExpressionSaturate, mul(m, sub(m, const(m, 0.92, -1200, 900), mask(m, nrm, b=True, x=-1150, y=800), -1050, 850), const(m, 6.0, -1050, 950), -950, 900), -850, 900)
    sand = mul(m, sd, lerp(m, const(m, 0.82, -1000, 500), const(m, 1.12, -1000, 560), mv, -850, 520), -700, -150)
    prop(lerp(m, sand, rock, slope, -500, -100), MP.MP_BASE_COLOR)
    prop(sn, MP.MP_NORMAL, "RGB")
    prop(const(m, 0.92, -500, 200), MP.MP_ROUGHNESS)
    finish(m)

    # apron: world-aligned asphalt, slightly lighter / dustier
    m = fresh_material("M_Apron")
    wp = E(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    uvs = mul(m, mask(m, wp, r=True, g=True, x=-1300, y=0), const(m, 1.0 / 800.0, -1300, 100), -1150, 50)
    d = tex(m, "/Game/Textures/T_Asphalt_D", -900, -200); link(uvs, d, "UVs")
    n = tex(m, "/Game/Textures/T_Asphalt_N", -900, 100, SAMP_N); link(uvs, n, "UVs")
    r = tex(m, "/Game/Textures/T_Asphalt_R", -900, 350, SAMP_G); link(uvs, r, "UVs")
    prop(mul(m, d, const3(m, (1.15, 1.1, 1.05), -900, -350), -600, -250), MP.MP_BASE_COLOR)
    prop(n, MP.MP_NORMAL, "RGB")
    prop(r, MP.MP_ROUGHNESS)
    finish(m)


def build_all_materials():
    master = build_master()
    glass = build_glass()
    carbon = build_carbon()
    tire = build_tire()
    build_screen(); build_plume(); build_smoke(); build_xray()
    build_track_materials(); build_environment_materials()
    lin = lambda c: tuple((x / 255.0) ** 2.2 for x in c)   # sRGB -> linear
    I = {}
    I["M_Paint_AnoWhite"] = instance("MI_Paint_AnoWhite", master, {"Roughness": 0.36, "ClearCoat": 0.55, "ClearCoatRoughness": 0.28, "Specular": 0.5}, {"BaseColor": lin((214, 213, 208))})
    I["M_BlackGloss"] = instance("MI_BlackGloss", master, {"Roughness": 0.12, "ClearCoat": 1.0, "ClearCoatRoughness": 0.03}, {"BaseColor": (0.008, 0.008, 0.009)})
    I["M_Underbody"] = instance("MI_Underbody", master, {"Roughness": 0.75}, {"BaseColor": (0.015, 0.015, 0.016)})
    I["M_Wheel_Spoke"] = instance("MI_WheelSpoke", master, {"Roughness": 0.18, "Metallic": 0.6, "ClearCoat": 1.0, "ClearCoatRoughness": 0.05}, {"BaseColor": (0.012, 0.012, 0.014)})
    I["M_Wheel_AeroDisc"] = instance("MI_WheelAeroDisc", master, {"Roughness": 0.38, "Metallic": 0.25}, {"BaseColor": lin((168, 170, 174))})
    I["M_Titanium"] = instance("MI_Titanium", master, {"Roughness": 0.22, "Metallic": 1.0}, {"BaseColor": lin((180, 150, 120))})
    I["M_Nozzle_Gold"] = instance("MI_NozzleGold", master, {"Roughness": 0.16, "Metallic": 1.0}, {"BaseColor": lin((226, 176, 110))})
    I["M_Plate_Silver"] = instance("MI_PlateSilver", master, {"Roughness": 0.3, "Metallic": 0.9}, {"BaseColor": lin((200, 200, 202))})
    I["M_Rubber"] = instance("MI_Rubber", master, {"Roughness": 0.9}, {"BaseColor": (0.012, 0.012, 0.012)})
    I["M_Brake_CarbonCeramic"] = instance("MI_BrakeCarbonCeramic", master, {"Roughness": 0.5, "Metallic": 0.4}, {"BaseColor": (0.07, 0.07, 0.075)})
    I["M_Interior_Ultrawhite"] = instance("MI_Ultrawhite", master, {"Roughness": 0.5, "NormalStrength": 0.3, "NormalTiling": 30.0}, {"BaseColor": lin((236, 236, 232))})
    I["M_Interior_Microsuede"] = instance("MI_Microsuede", master, {"Roughness": 0.95, "NormalStrength": 0.6, "NormalTiling": 40.0}, {"BaseColor": lin((70, 70, 74))})
    I["M_Interior_Dark"] = instance("MI_InteriorDark", master, {"Roughness": 0.7}, {"BaseColor": (0.02, 0.02, 0.022)})
    I["M_Aluminum"] = instance("MI_Aluminum", master, {"Roughness": 0.2, "Metallic": 1.0}, {"BaseColor": lin((205, 206, 210))})
    I["M_Light_White"] = instance("MI_LightWhite", master, {"Intensity": 80.0, "Roughness": 0.1}, {"BaseColor": (1, 1, 1), "Emissive": (1.0, 0.96, 0.9)})
    I["M_Light_Red"] = instance("MI_LightRed", master, {"Intensity": 60.0, "Roughness": 0.1}, {"BaseColor": (0.5, 0.0, 0.0), "Emissive": (1.0, 0.015, 0.008)})
    I["M_Light_Amber"] = instance("MI_LightAmber", master, {"Intensity": 12.0}, {"BaseColor": (0.6, 0.25, 0.0), "Emissive": (1.0, 0.35, 0.0)})
    I["M_Interior_RedAmbient"] = instance("MI_RedAmbient", master, {"Intensity": 25.0}, {"BaseColor": (0.3, 0.0, 0.0), "Emissive": (1.0, 0.03, 0.02)})
    # "Gold Glazed" canopy: from outside it reads as dark, reflective bronze (Tesla photos), not clear glass
    I["M_Glass_Gold"] = instance("MI_GlassGold", glass, {"Opacity": 0.86, "Metallic": 0.72}, {"Tint": (0.20, 0.12, 0.045)})
    I["M_Glass_Rear"] = instance("MI_GlassRear", glass, {"Opacity": 0.88}, {"Tint": (0.05, 0.035, 0.02)})
    I["M_Carbon"] = carbon
    # tyres: the detailed P Zero Trofeo R meshes carry their lettering as geometry (own slot), so the
    # rubber is plain with a fine detail normal (the old texture-text material would print it twice)
    rubber = instance("MI_TireRubber", master, {"Roughness": 0.86, "NormalStrength": 0.35, "NormalTiling": 40.0}, {"BaseColor": (0.011, 0.011, 0.012)})
    I["M_Tire_Front"] = rubber
    I["M_Tire_Rear"] = rubber
    I["M_Tire_Lettering"] = instance("MI_TireLettering", master, {"Roughness": 0.7}, {"BaseColor": lin((222, 222, 220))})
    # brakes and suspension (roadster_wheels)
    I["M_Caliper_Gold"] = instance("MI_CaliperGold", master, {"Roughness": 0.22, "Metallic": 0.75, "ClearCoat": 1.0, "ClearCoatRoughness": 0.04}, {"BaseColor": lin((214, 162, 78))})
    I["M_Susp_Forged"] = instance("MI_SuspForged", master, {"Roughness": 0.42, "Metallic": 1.0}, {"BaseColor": lin((118, 120, 124))})
    I["M_Chrome"] = instance("MI_Chrome", master, {"Roughness": 0.05, "Metallic": 1.0}, {"BaseColor": lin((238, 238, 242))})
    I["M_Stainless"] = instance("MI_Stainless", master, {"Roughness": 0.22, "Metallic": 1.0}, {"BaseColor": lin((172, 178, 184))})
    I["M_COPV_Filament"] = carbon
    # cabin details (roadster_interior)
    I["M_Interior_UltrawhitePerf"] = instance("MI_UltrawhitePerf", master, {"Roughness": 0.55, "NormalStrength": 1.0, "NormalTiling": 15.625},
                                              {"BaseColor": lin((236, 236, 232))}, {"NormalTex": "/Game/Textures/T_SeatPerf_N"})
    I["M_Interior_PerfDot"] = instance("MI_PerfDot", master, {"Roughness": 0.8}, {"BaseColor": lin((112, 112, 114))})
    I["M_Interior_Stitch"] = instance("MI_Stitch", master, {"Roughness": 0.75}, {"BaseColor": lin((150, 150, 155))})
    I["M_Interior_Webbing"] = instance("MI_Webbing", master, {"Roughness": 0.85, "NormalStrength": 0.4, "NormalTiling": 60.0}, {"BaseColor": (0.012, 0.012, 0.013)})
    I["M_Interior_Glyph"] = instance("MI_Glyph", master, {"Intensity": 3.0, "Roughness": 0.4}, {"BaseColor": (0.9, 0.9, 0.9), "Emissive": (1.0, 1.0, 1.0)})
    screen = unreal.load_asset("/Game/Materials/M_Screen")
    I["M_Screen_Main"] = screen
    I["M_Screen_Forward"] = screen
    log("materials built", len(I))
    return I


def assign_materials(I):
    count = 0
    for path in EAL.list_assets("/Game/Roadster/Meshes", recursive=False):
        sm = unreal.load_asset(path)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        mats = sm.get_editor_property("static_materials")
        for i, sl in enumerate(mats):
            slot = str(sl.get_editor_property("material_slot_name"))
            key = slot
            if slot == "M_Tire":
                key = "M_Tire_Rear" if "Rear" in sm.get_name() else "M_Tire_Front"
            mat = I.get(key)
            if mat is None:
                log("no material for slot", slot, "in", sm.get_name())
                continue
            sm.set_material(i, mat)
            count += 1
        EAL.save_loaded_asset(sm)
    for name, mat in (("SM_Terrain", "/Game/Materials/M_Terrain"), ("SM_Apron", "/Game/Materials/M_Apron")):
        sm = unreal.load_asset("/Game/Environment/" + name)
        if sm:
            sm.set_material(0, unreal.load_asset(mat))
            bs = sm.get_editor_property("body_setup")
            if bs:
                bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            ns = sm.get_editor_property("nanite_settings")
            ns.enabled = name == "SM_Terrain"
            sm.set_editor_property("nanite_settings", ns)
            EAL.save_loaded_asset(sm)
    log("slots assigned", count)


# ----------------------------------------------------------------------------- level
def build_level():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if EAL.does_asset_exist("/Game/Maps/ProvingGround"):
        # reuse the map: never delete a loaded level asset (crashes the editor); clear its actors instead
        les.load_level("/Game/Maps/ProvingGround")
        for a in eas.get_all_level_actors():
            # keep only the world settings and the builder brush (volumes are Brush subclasses: they go)
            if not isinstance(a, unreal.WorldSettings) and type(a) is not unreal.Brush:
                eas.destroy_actor(a)
    else:
        les.new_level("/Game/Maps/ProvingGround")
    V, R = unreal.Vector, unreal.Rotator

    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, V(0, 0, 2000), R(roll=0, pitch=-24, yaw=-35))
    sun.set_actor_label("Sun")
    lc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property("intensity", 10.0)
    lc.set_editor_property("light_color", unreal.Color(255, 236, 214, 255))
    lc.set_editor_property("atmosphere_sun_light", True)
    lc.set_editor_property("light_source_angle", 0.6)
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

    eas.spawn_actor_from_class(unreal.SkyAtmosphere, V(0, 0, 0)).set_actor_label("SkyAtmosphere")
    sky = eas.spawn_actor_from_class(unreal.SkyLight, V(0, 0, 500))
    sky.set_actor_label("SkyLight")
    sc = sky.get_component_by_class(unreal.SkyLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("real_time_capture", True)
    cl = eas.spawn_actor_from_class(unreal.VolumetricCloud, V(0, 0, 0))
    cl.set_actor_label("Clouds")
    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, V(0, 0, 0))
    fog.set_actor_label("Fog")
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", 0.0015)
    fc.set_editor_property("fog_height_falloff", 0.12)
    for k in ("enable_volumetric_fog", "volumetric_fog"):
        try:
            fc.set_editor_property(k, True); break
        except Exception:
            pass

    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, V(0, 0, 0))
    ppv.set_actor_label("PostProcess")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("bloom_intensity", 0.55), ("auto_exposure_bias", 0.3), ("lumen_final_gather_quality", 2.0),
                 ("lumen_reflection_quality", 2.0), ("lumen_scene_lighting_quality", 1.5), ("vignette_intensity", 0.3),
                 ("film_grain_intensity", 0.06), ("scene_fringe_intensity", 0.15), ("ambient_occlusion_intensity", 0.6),
                 ("lens_flare_intensity", 0.0), ("motion_blur_amount", 0.35),
                 # exposure range (EV100) around the open-air level of this scene (sun 10 lux -> about EV 2.5):
                 # the camera cannot adapt so far into a dark tunnel that the portals blow out
                 ("auto_exposure_min_brightness", 0.5), ("auto_exposure_max_brightness", 4.5),
                 ("auto_exposure_speed_up", 2.5), ("auto_exposure_speed_down", 1.5)):
        try:
            s.set_editor_property("override_" + k, True)
            s.set_editor_property(k, v)
        except Exception as e:
            log("pp", k, e)
    ppv.set_editor_property("settings", s)

    terrain = unreal.load_asset("/Game/Environment/SM_Terrain")
    if terrain:
        a = eas.spawn_actor_from_object(terrain, V(0, 0, 0))
        a.set_actor_label("Terrain")
    apron = unreal.load_asset("/Game/Environment/SM_Apron")
    if apron:
        a = eas.spawn_actor_from_object(apron, V(0, 0, 1.0))
        a.set_actor_label("Apron")

    # the track is not saved into the map: RoadsterGameMode spawns it at runtime (keeps the .umap small)
    ps =eas.spawn_actor_from_class(unreal.PlayerStart, V(0, 0, 100))

    world = unreal.EditorLevelLibrary.get_editor_world()
    ws = world.get_world_settings()
    ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/RoadsterSim.RoadsterGameMode"))
    les.save_current_level()
    log("level saved")


def main():
    steps = os.environ.get("ROADSTER_STEPS", "import,materials,level").split(",")
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    if "import" in steps:
        import_textures()
        import_audio()
        import_meshes()
    if "materials" in steps:
        I = build_all_materials()
        assign_materials(I)
    if "level" in steps:
        build_level()
    with open(ROOT + "/Unreal/RoadsterSim/Saved/setup_log.txt", "w") as f:
        f.write("\n".join(LOG))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        import traceback
        with open(ROOT + "/Unreal/RoadsterSim/Saved/setup_log.txt", "w") as f:
            f.write("\n".join(LOG) + "\nFAILED\n" + traceback.format_exc())
        raise
    finally:
        # Let startup and outstanding asset work return to the editor before teardown.
        import time
        _quit_at = time.monotonic() + 12.0
        def _settled_exit(delta):
            if time.monotonic() >= _quit_at:
                unreal.unregister_slate_post_tick_callback(_quit_handle)
                unreal.SystemLibrary.quit_editor()
        _quit_handle = unreal.register_slate_post_tick_callback(_settled_exit)
