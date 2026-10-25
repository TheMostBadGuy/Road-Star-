"""Refresh exported car and polish the map in one supervised editor session."""
import os
import runpy
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/ProvingGround')
    runpy.run_path(os.path.join(HERE, 'polish_proving_ground.py'), run_name='__main__')
    # This existing importer saves each asset and requests editor shutdown on completion.
    runpy.run_path(os.path.join(HERE, 'reimport_car.py'), run_name='__main__')
except Exception:
    import traceback
    unreal.log_error(traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
    raise
