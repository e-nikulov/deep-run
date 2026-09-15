from pathlib import Path

path = Path("Game/Combat/CombatPlaygroundView.h")
text = path.read_text(encoding="utf-8")

old_shader = '        if (const auto initialized = transientVfx.Initialize(renderer, "Shaders"); !initialized)\n'
new_shader = '        const std::filesystem::path runtimeRoot = assets.Root().parent_path();\n        if (const auto initialized = transientVfx.Initialize(renderer, runtimeRoot / "Shaders"); !initialized)\n'
old_config = '        if (const auto authored = Armament::LoadP700LaunchVfxTuning(std::filesystem::path("Config") / "p700_vfx.json"); authored)\n'
new_config = '        if (const auto authored = Armament::LoadP700LaunchVfxTuning(runtimeRoot / "Config" / "p700_vfx.json"); authored)\n'

if text.count(old_shader) != 1:
    raise SystemExit("expected exactly one relative TransientVfx shader-root call")
if text.count(old_config) != 1:
    raise SystemExit("expected exactly one relative P-700 VFX config path")

text = text.replace(old_shader, new_shader)
text = text.replace(old_config, new_config)
path.write_text(text, encoding="utf-8", newline="\n")
