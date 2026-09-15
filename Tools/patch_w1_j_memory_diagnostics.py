from pathlib import Path

path = Path("Engine/Render/D3D12Renderer.cpp")
text = path.read_text(encoding="utf-8")
old = '''    count(impl_->gerstnerSurface.vertexBuffer.Get(), true);
    count(impl_->gerstnerSurface.indexBuffer.Get(), true);
'''
new = '''    for (const auto& lod : impl_->gerstnerSurface.lods)
    {
        count(lod.vertexBuffer.Get(), true);
        count(lod.indexBuffer.Get(), true);
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one legacy Gerstner memory diagnostics block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("W1-J Gerstner memory diagnostics updated for all immutable LODs")
