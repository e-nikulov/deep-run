from pathlib import Path

source = Path("Tools/p700_final_visual_patch.py").read_text(encoding="utf-8")
marker = "# Dedicated regression for bounded launch lights."
head, separator, tail = source.partition(marker)
if not separator:
    raise SystemExit("final visual regression marker not found")
old = '"#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\""'
new = '"#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700LaunchAudioPresentation.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\""'
if tail.count(old) != 1:
    raise SystemExit(f"expected one test include matcher after marker, found {tail.count(old)}")
tail = tail.replace(old, new, 1)
exec(compile(head + separator + tail, "p700_final_visual_patch_v3", "exec"))
