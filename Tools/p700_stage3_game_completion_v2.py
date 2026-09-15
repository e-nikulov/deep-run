from pathlib import Path

source = Path("Tools/p700_stage3_game_completion.py").read_text(encoding="utf-8")
old = '''        if (!runtime_.has_value()) return std::unexpected(\"P-700 runtime is unavailable\");\\n        return view_.BuildP700LocalLights(*runtime_, simulationTimeSeconds);'''
new = '''        if (!runtime_.has_value()) return std::vector<Render::ScenePresentationLocalLight>{};\\n        return view_.BuildP700LocalLights(*runtime_, simulationTimeSeconds);'''
if source.count(old) != 1:
    raise SystemExit(f"stage3 composition source anchor count={source.count(old)}")
source = source.replace(old, new, 1)
exec(compile(source, "p700_stage3_game_completion_v2", "exec"))
