import os
import runpy
import unreal

try:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/ProvingGround')
    runpy.run_path(os.path.join(os.path.dirname(__file__), 'import_desert_candidate.py'), run_name='__main__')
finally:
    unreal.SystemLibrary.quit_editor()
