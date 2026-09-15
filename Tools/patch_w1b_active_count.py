from pathlib import Path

path = Path(__file__).resolve().parents[1] / "Engine" / "Render" / "D3D12Renderer.cpp"
text = path.read_text(encoding="utf-8")
old = """            .horizontalSteepness1 = {\n                parameters.components[4].horizontalSteepness,\n                parameters.components[5].horizontalSteepness,\n                parameters.components[6].horizontalSteepness,\n                0.0F},\n"""
new = """            .horizontalSteepness1 = {\n                parameters.components[4].horizontalSteepness,\n                parameters.components[5].horizontalSteepness,\n                parameters.components[6].horizontalSteepness,\n                static_cast<float>(parameters.activeComponentCount)},\n"""
if text.count(old) != 1:
    raise RuntimeError(f"expected exactly one active-count slot, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("W1-B active component root-constant patch: PASS")
