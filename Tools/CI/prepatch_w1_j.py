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

# The validation runner token cannot update workflow files. Keep the product patch self-contained; the
# permanent visual-capture workflow is updated separately through the GitHub API after validation.
visual_block = '''replace_once(\n    ".github/workflows/w1-sea-state-visual-capture.yml",\n    "      - Engine/Render/D3D12Renderer.cpp\\n",\n    "      - Engine/Render/D3D12Renderer.cpp\\n      - CMakeLists.txt\\n      - Tests/W1MeshletOceanTest.cpp\\n",\n)\n'''
if text.count(visual_block) != 1:
    raise RuntimeError("unable to locate W1-J visual-capture path patch")
text = text.replace(visual_block, "", 1)

# Engine/Render is intentionally renderer-generic. Product/log vocabulary there must describe only the
# Gerstner presentation surface, never Game/Simulation concepts such as ocean/water.
renderer_semantic_rewrites = {
    "W1-J mesh shader ocean unavailable; retaining indexed Gerstner compatibility path":
        "W1-J mesh shader surface unavailable; retaining indexed Gerstner compatibility path",
    "D3D12 Mesh Shader Tier 1 path available for W1-J ocean geometry":
        "D3D12 Mesh Shader Tier 1 path available for W1-J Gerstner geometry",
    "W1-J meshlet ocean pipeline created (31 cells / 64 vertices / 62 triangles per meshlet)":
        "W1-J meshlet Gerstner pipeline created (31 cells / 64 vertices / 62 triangles per meshlet)",
    "W1-J meshlet ocean configured: procedural geometry, persistent ocean VB/IB=0, dispatches=1":
        "W1-J meshlet Gerstner surface configured: procedural geometry, persistent surface VB/IB=0, dispatches=1",
    "W1-J indexed compatibility ocean configured: vertices=":
        "W1-J indexed Gerstner compatibility configured: vertices=",
    "visible world span instead and generates every ocean vertex procedurally around absolute world X.":
        "visible world span instead and generates every surface vertex procedurally around absolute world X.",
}
for old, new in renderer_semantic_rewrites.items():
    if text.count(old) != 1:
        raise RuntimeError(f"expected one renderer semantic phrase, found {text.count(old)}: {old}")
    text = text.replace(old, new, 1)

patch.write_text(text, encoding="utf-8", newline="\n")
Path(__file__).unlink()
print("W1-J patch anchors, workflow permissions and generic renderer semantics normalized")
