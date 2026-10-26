"""Import specific car meshes from Blender/export (names in env ROADSTER_PARTS, comma separated) and report results.
Run inside the editor:  UnrealEditor.exe RoadsterSim.uproject -ExecutePythonScript=".../import_parts.py"
"""
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_project as S   # noqa: E402

out = []
try:
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    names = [n.strip() for n in os.environ.get("ROADSTER_PARTS", "").split(",") if n.strip()]
    files = [S.EXPORT + "/SM_%s.fbx" % n for n in names]
    for f in files:
        out.append("file %s exists=%s size=%d" % (f, os.path.exists(f), os.path.getsize(f) if os.path.exists(f) else -1))
    imported = S.import_files(files, "/Game/Roadster/Meshes", S.fbx_options())
    out.append("imported: " + ", ".join(str(p) for p in imported))
    for n in names:
        a = unreal.load_asset("/Game/Roadster/Meshes/SM_" + n)
        out.append("%s -> %s" % (n, "OK" if a else "MISSING"))
except Exception as e:
    out.append("ERROR %r" % e)
finally:
    with open(S.ROOT + "/Unreal/RoadsterSim/Saved/import_parts_log.txt", "w") as f:
        f.write("\n".join(out + S.LOG))
    unreal.SystemLibrary.quit_editor()
