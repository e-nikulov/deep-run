from pathlib import Path

helper = Path("Tools/p700_final_visual_patch.py").read_text(encoding="utf-8")
old = '''    "#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\"",\n    "#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700LaunchLightingPresentation.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\"",'''
new = '''    "#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700LaunchAudioPresentation.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\"",\n    "#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700LaunchAudioPresentation.h\\"\\n#include \\"Game/Weapons/P700LaunchLightingPresentation.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\"",'''
if old not in helper:
    raise SystemExit("original helper include matcher was not found")
exec(compile(helper.replace(old, new), "p700_final_visual_patch_v2", "exec"))
