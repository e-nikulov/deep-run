from pathlib import Path

source = Path("Tools/p700_stage1_completion_v2.py").read_text(encoding="utf-8")
exec(compile(source, "p700_stage1_completion_v3_base", "exec"))

path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")
old_move = '''                        const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(\n'''
new_move = '''                        auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(\n'''
if text.count(old_move) != 1:
    raise SystemExit(f"move-only combat create anchor count={text.count(old_move)}")
text = text.replace(old_move, new_move, 1)
old_assignment = '''                        combatPlayground = *combat;\n                    }\n                    combatPlayground = std::move(*combat);'''
new_assignment = '''                        combatPlayground = std::move(*combat);\n                    }'''
if text.count(old_assignment) != 1:
    raise SystemExit(f"merged combat ownership anchor count={text.count(old_assignment)}")
text = text.replace(old_assignment, new_assignment, 1)
old_capture = '''             &loggedHapticSubmissionFailure, &loggedFirstAcousticObservation, &loggedConfirmedAcousticTrack,\n             &loggedCombatRuntime, &loggedCombatImpact](const float fixedDeltaSeconds)'''
new_capture = '''             &loggedHapticSubmissionFailure, &loggedFirstAcousticObservation, &loggedConfirmedAcousticTrack,\n             &loggedCombatRuntime, &loggedCombatImpact, &p700ShowcaseProfile](const float fixedDeltaSeconds)'''
if text.count(old_capture) != 1:
    raise SystemExit(f"fixed-update showcase capture anchor count={text.count(old_capture)}")
text = text.replace(old_capture, new_capture, 1)
path.write_text(text, encoding="utf-8")
print("P-700 stage1 v3 ownership/capture fixes applied")
