from pathlib import Path

source = Path("Tools/p700_stage2_completion.py").read_text(encoding="utf-8")
exec(compile(source, "p700_stage2_completion_v2_base", "exec"))

path = Path("Game/Weapons/P700LaunchVfx.cpp")
text = path.read_text(encoding="utf-8")
old = "const std::array<float, 37> scalars{"
new = "const std::array<float, 38> scalars{"
if text.count(old) != 1:
    raise SystemExit(f"stage2 scalar cardinality anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("P-700 stage2 v2 scalar cardinality fix applied")
