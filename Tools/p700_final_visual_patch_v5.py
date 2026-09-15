from pathlib import Path

source = Path("Tools/p700_final_visual_patch.py").read_text(encoding="utf-8")
marker = "# Dedicated regression for bounded launch lights."
head, separator, tail = source.partition(marker)
if not separator:
    raise SystemExit("final visual regression marker not found")

old_include = '"#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\""'
new_include = '"#include \\"Game/Weapons/P700LaunchVfx.h\\"\\n#include \\"Game/Weapons/P700LaunchAudioPresentation.h\\"\\n#include \\"Game/Weapons/P700SurfacePresentation.h\\""'
if tail.count(old_include) != 1:
    raise SystemExit(f"expected one test include matcher after marker, found {tail.count(old_include)}")
tail = tail.replace(old_include, new_include, 1)

old_match = '"    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||\\n        !RunSalvoBudgetCheck(1U) ||"'
new_match = '"    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunAudioMappingChecks() ||\\n        !RunSurfaceDisturbanceChecks() ||\\n        !RunSalvoBudgetCheck(1U) ||"'
if tail.count(old_match) != 1:
    raise SystemExit(f"expected one regression-chain matcher after marker, found {tail.count(old_match)}")
tail = tail.replace(old_match, new_match, 1)

old_replacement = '"    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||\\n        !RunLaunchLightingChecks() || !RunSalvoBudgetCheck(1U) ||"'
new_replacement = '"    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunAudioMappingChecks() ||\\n        !RunSurfaceDisturbanceChecks() || !RunLaunchLightingChecks() || !RunSalvoBudgetCheck(1U) ||"'
if tail.count(old_replacement) != 1:
    raise SystemExit(f"expected one lighting regression replacement after marker, found {tail.count(old_replacement)}")
tail = tail.replace(old_replacement, new_replacement, 1)

exec(compile(head + separator + tail, "p700_final_visual_patch_v5", "exec"))
