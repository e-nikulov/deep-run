from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "CMakeLists.txt"
text = path.read_text(encoding="utf-8")
old = '''deeprun_set_project_warnings(DeepRunW1SurfaceDynamicsTests)
target_link_libraries(DeepRunW1SurfaceDynamicsTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunW1SurfaceDynamicsTests COMMAND DeepRunW1SurfaceDynamicsTests)

# Core time-domain regression: player time compression changes fixed-work count, never fixed-step duration.
'''
new = '''deeprun_set_project_warnings(DeepRunW1SurfaceDynamicsTests)
target_link_libraries(DeepRunW1SurfaceDynamicsTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunW1SurfaceDynamicsTests COMMAND DeepRunW1SurfaceDynamicsTests)

# W1-D semantic presentation gate: a hydrodynamic slam is a distinct short high-priority haptic event and
# must not alter the accepted continuous engine-vibration mapping.
add_executable(DeepRunW1WaveSlamHapticTests
    Tests/W1WaveSlamHapticTest.cpp
    Game/Haptics/HapticFeedbackSystem.cpp
)
target_compile_features(DeepRunW1WaveSlamHapticTests PRIVATE cxx_std_23)
target_compile_definitions(DeepRunW1WaveSlamHapticTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
deeprun_set_project_warnings(DeepRunW1WaveSlamHapticTests)
target_link_libraries(DeepRunW1WaveSlamHapticTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunW1WaveSlamHapticTests COMMAND DeepRunW1WaveSlamHapticTests)

# Core time-domain regression: player time compression changes fixed-work count, never fixed-step duration.
'''
if text.count(old) != 1:
    raise RuntimeError(f"expected one W1-C target insertion point, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("W1-D haptic acceptance CMake target patch: PASS")
