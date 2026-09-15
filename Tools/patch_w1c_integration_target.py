from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "CMakeLists.txt"
text = path.read_text(encoding="utf-8")
old = '''deeprun_set_project_warnings(DeepRunM4AcousticTests)
target_link_libraries(DeepRunM4AcousticTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunM4AcousticTests COMMAND DeepRunM4AcousticTests)

# Core time-domain regression: player time compression changes fixed-work count, never fixed-step duration.
'''
new = '''deeprun_set_project_warnings(DeepRunM4AcousticTests)
target_link_libraries(DeepRunM4AcousticTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunM4AcousticTests COMMAND DeepRunM4AcousticTests)

# W1-C headless integration gate: long surface body consumes the authoritative wave elevation through
# vertical point hydrostatics and must exhibit bounded Jolt heave/pitch without disturbing flat-water equilibrium.
add_executable(DeepRunW1SurfaceDynamicsTests
    Tests/W1SurfaceDynamicsIntegrationTest.cpp
)
target_compile_features(DeepRunW1SurfaceDynamicsTests PRIVATE cxx_std_23)
target_compile_definitions(DeepRunW1SurfaceDynamicsTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
deeprun_set_project_warnings(DeepRunW1SurfaceDynamicsTests)
target_link_libraries(DeepRunW1SurfaceDynamicsTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunW1SurfaceDynamicsTests COMMAND DeepRunW1SurfaceDynamicsTests)

# Core time-domain regression: player time compression changes fixed-work count, never fixed-step duration.
'''
if text.count(old) != 1:
    raise RuntimeError(f"expected one M4 target insertion point, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("W1-C integration CMake target patch: PASS")
