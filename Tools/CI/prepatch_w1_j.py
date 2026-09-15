from pathlib import Path

root = Path(__file__).resolve().parents[2]
patch = root / "Tools/CI/apply_w1_j_meshlet_ocean.py"
text = patch.read_text(encoding="utf-8")

# The CMake shader list contains the same PS -> ToneMap pair in both DeepRunShaders dependencies and
# runtime staging. Convert the two sequential replace_once calls into one explicit two-occurrence update.
start_marker = '''replace_once(\n    "CMakeLists.txt",\n    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\\n'\n'''
end_marker = '''replace_once(\n    "CMakeLists.txt",\n    "enable_testing()\\n\\nadd_executable(DeepRunTests\\n",\n'''
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("unable to locate duplicated CMake shader staging patch")
replacement = '''cmake = read("CMakeLists.txt")\nshader_staging_old = (\n    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\\n'\n    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\\n'\n)\nshader_staging_new = (\n    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\\n'\n    '        "${DEEPRUN_GERSTNER_SURFACE_MESH_SHADER}"\\n'\n    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\\n'\n)\nif cmake.count(shader_staging_old) != 2:\n    raise RuntimeError(f"CMakeLists.txt: expected two Gerstner shader staging anchors, found {cmake.count(shader_staging_old)}")\nwrite("CMakeLists.txt", cmake.replace(shader_staging_old, shader_staging_new, 2))\n\n'''
text = text[:start] + replacement + text[end:]

# W1-I did not watch D3D12Renderer.cpp yet. W1-J must add it rather than expecting it to exist already.
old_visual_block = '''replace_once(\n    ".github/workflows/w1-sea-state-visual-capture.yml",\n    "      - Engine/Render/D3D12Renderer.cpp\\n",\n    "      - Engine/Render/D3D12Renderer.cpp\\n      - CMakeLists.txt\\n      - Tests/W1MeshletOceanTest.cpp\\n",\n)\n'''
new_visual_block = '''replace_once(\n    ".github/workflows/w1-sea-state-visual-capture.yml",\n    "      - Engine/Render/GerstnerSurface.cpp\\n",\n    "      - Engine/Render/D3D12Renderer.cpp\\n      - Engine/Render/GerstnerSurface.cpp\\n      - CMakeLists.txt\\n      - Tests/W1MeshletOceanTest.cpp\\n",\n)\n'''
if text.count(old_visual_block) != 1:
    raise RuntimeError("unable to locate W1-J visual-capture path patch")
text = text.replace(old_visual_block, new_visual_block, 1)

patch.write_text(text, encoding="utf-8", newline="\n")
Path(__file__).unlink()
print("W1-J patch anchors normalized")
