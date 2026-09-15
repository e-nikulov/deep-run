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

# W1 uses Win32 headers and a typed RgbaColor. Keep the presentation patch compatible with both contracts.
physical_path = Path("Game/PhysicalPlayground.cpp")
physical = physical_path.read_text(encoding="utf-8")
physical = physical.replace(
    "weatherPresentation.skyLuminanceMultiplier *= std::max(0.035F, presentationDaylightFraction_);",
    "weatherPresentation.skyLuminanceMultiplier *= (std::max)(0.035F, presentationDaylightFraction_);", 1)
old_sky = '''            auto skyColor = band.color;\n            for (std::size_t channel = 0U; channel < 3U; ++channel)\n                skyColor[channel] *= 0.035F + 0.965F * presentationDaylightFraction_;'''
new_sky = '''            auto skyColor = band.color;\n            const float skyScale = 0.035F + 0.965F * presentationDaylightFraction_;\n            skyColor.r *= skyScale;\n            skyColor.g *= skyScale;\n            skyColor.b *= skyScale;'''
if physical.count(old_sky) != 1:
    raise SystemExit(f"W1 typed sky color anchor count={physical.count(old_sky)}")
physical = physical.replace(old_sky, new_sky, 1)
old_sun = '''            auto sunColor = strip.color;\n            for (std::size_t channel = 0U; channel < 3U; ++channel)\n                sunColor[channel] *= presentationDaylightFraction_;'''
new_sun = '''            auto sunColor = strip.color;\n            sunColor.r *= presentationDaylightFraction_;\n            sunColor.g *= presentationDaylightFraction_;\n            sunColor.b *= presentationDaylightFraction_;'''
if physical.count(old_sun) != 1:
    raise SystemExit(f"W1 typed sun color anchor count={physical.count(old_sun)}")
physical = physical.replace(old_sun, new_sun, 1)
physical_path.write_text(physical, encoding="utf-8")
print("P-700 stage1 v3 ownership/capture/W1 API fixes applied")
