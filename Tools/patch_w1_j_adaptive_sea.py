from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one occurrence, found {count}")
    return text.replace(old, new, 1)


# Engine/Render/GerstnerSurface.h
path = Path("Engine/Render/GerstnerSurface.h")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "inline constexpr std::size_t GerstnerWaveComponentCapacity = 7U;\ninline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;\n",
    "inline constexpr std::size_t GerstnerWaveComponentCapacity = 7U;\ninline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;\ninline constexpr std::array<std::uint32_t, 4> GerstnerSurfaceHorizontalSampleLods{513U, 1025U, 2049U, 4097U};\ninline constexpr std::uint32_t GerstnerSurfaceMaximumHorizontalSampleCount =\n    GerstnerSurfaceHorizontalSampleLods.back();\n",
    "GerstnerSurface.h LOD constants",
)
text = replace_once(
    text,
    "[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n    const GerstnerSurfacePresentationParameters& parameters,\n    float x,\n    float phaseTimeSeconds);\n",
    "[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n    const GerstnerSurfacePresentationParameters& parameters,\n    float x,\n    float phaseTimeSeconds);\n[[nodiscard]] std::expected<std::uint32_t, std::string> SelectGerstnerSurfaceHorizontalSampleCount(\n    const GerstnerSurfacePresentationParameters& parameters,\n    std::uint32_t viewportWidthPixels,\n    float cameraWidthMeters);\n",
    "GerstnerSurface.h selector declaration",
)
path.write_text(text, encoding="utf-8", newline="\n")

# Engine/Render/GerstnerSurface.cpp
path = Path("Engine/Render/GerstnerSurface.cpp")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "constexpr std::uint32_t MaximumHorizontalSampleCount = 513U;",
    "constexpr std::uint32_t MaximumHorizontalSampleCount = GerstnerSurfaceMaximumHorizontalSampleCount;",
    "GerstnerSurface.cpp max sample count",
)
insert_anchor = "\nstd::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n"
selector = r'''
std::expected<std::uint32_t, std::string> SelectGerstnerSurfaceHorizontalSampleCount(
    const GerstnerSurfacePresentationParameters& parameters,
    const std::uint32_t viewportWidthPixels,
    const float cameraWidthMeters)
{
    if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (viewportWidthPixels == 0U || !std::isfinite(cameraWidthMeters) || cameraWidthMeters <= 0.0F)
    {
        return std::unexpected("Gerstner tessellation selection requires positive finite viewport/camera dimensions");
    }
    if (parameters.activeComponentCount == 0U)
    {
        return GerstnerSurfaceHorizontalSampleLods.front();
    }

    float combinedAmplitudeMeters = 0.0F;
    std::uint32_t wavelengthRequiredSamples = GerstnerSurfaceHorizontalSampleLods.front();
    const float pixelsPerMeter = static_cast<float>(viewportWidthPixels) / cameraWidthMeters;
    for (std::size_t index = 0U; index < parameters.activeComponentCount; ++index)
    {
        const auto& component = parameters.components[index];
        combinedAmplitudeMeters += component.amplitudeMeters;
        const float projectedAmplitudePixels = component.amplitudeMeters * pixelsPerMeter;
        if (projectedAmplitudePixels >= 0.25F)
        {
            constexpr float SamplesPerVisibleWavelength = 20.0F;
            const float required = std::ceil(
                cameraWidthMeters / component.wavelengthMeters * SamplesPerVisibleWavelength) + 1.0F;
            if (std::isfinite(required) && required > 0.0F)
            {
                wavelengthRequiredSamples = (std::max)(
                    wavelengthRequiredSamples,
                    static_cast<std::uint32_t>((std::min)(
                        required, static_cast<float>(GerstnerSurfaceMaximumHorizontalSampleCount))));
            }
        }
    }

    const float targetPixelSpacing = combinedAmplitudeMeters >= 1.0F
        ? 0.60F
        : combinedAmplitudeMeters >= 0.25F
            ? 0.75F
            : 1.00F;
    const float pixelRequired = std::ceil(static_cast<float>(viewportWidthPixels) / targetPixelSpacing) + 1.0F;
    const std::uint32_t pixelRequiredSamples = static_cast<std::uint32_t>((std::min)(
        pixelRequired, static_cast<float>(GerstnerSurfaceMaximumHorizontalSampleCount)));
    const std::uint32_t requiredSamples = (std::max)(pixelRequiredSamples, wavelengthRequiredSamples);
    for (const std::uint32_t lodSamples : GerstnerSurfaceHorizontalSampleLods)
    {
        if (lodSamples >= requiredSamples)
        {
            return lodSamples;
        }
    }
    return GerstnerSurfaceHorizontalSampleLods.back();
}
'''
if insert_anchor not in text:
    raise SystemExit("GerstnerSurface.cpp selector insertion anchor missing")
text = text.replace(insert_anchor, "\n" + selector + insert_anchor, 1)
path.write_text(text, encoding="utf-8", newline="\n")

# Game/WaterPresentation.cpp
path = Path("Game/WaterPresentation.cpp")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    ".horizontalSampleCount = 257U,",
    ".horizontalSampleCount = Render::GerstnerSurfaceHorizontalSampleLods.front(),",
    "WaterPresentation.cpp base sample count",
)
path.write_text(text, encoding="utf-8", newline="\n")

# Engine/Render/D3D12Renderer.cpp
path = Path("Engine/Render/D3D12Renderer.cpp")
text = path.read_text(encoding="utf-8")
old_struct = '''struct GpuGerstnerSurface final
{
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    GerstnerSurfacePresentationParameters parameters{};
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
};'''
new_struct = '''struct GpuGerstnerSurfaceLod final
{
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    std::uint32_t horizontalSampleCount = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
};

struct GpuGerstnerSurface final
{
    std::array<GpuGerstnerSurfaceLod, GerstnerSurfaceHorizontalSampleLods.size()> lods{};
    GerstnerSurfacePresentationParameters parameters{};
};'''
text = replace_once(text, old_struct, new_struct, "D3D12Renderer.cpp GPU surface structs")
text = replace_once(
    text,
    "        if (gerstnerSurface.vertexBuffer != nullptr)\n        {\n            return std::unexpected(\"Gerstner surface is already configured for this renderer\");\n        }",
    "        if (gerstnerSurface.lods.front().vertexBuffer != nullptr)\n        {\n            return std::unexpected(\"Gerstner surface is already configured for this renderer\");\n        }",
    "D3D12Renderer.cpp already configured check",
)
start = text.index("        const auto mesh = GenerateGerstnerSurfaceBaseMesh(parameters);")
end_marker = "            gerstnerSurface = std::move(surface);\n"
end = text.index(end_marker, start) + len(end_marker)
replacement = r'''        try
        {
            const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            const auto createUploadBuffer = [&](const void* source, const std::uint64_t byteSize, const char* name)
                -> ComPtr<ID3D12Resource>
            {
                ComPtr<ID3D12Resource> buffer;
                const D3D12_RESOURCE_DESC description = BufferDescription(byteSize);
                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &uploadHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &description,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&buffer)),
                    name);
                void* mapped = nullptr;
                ThrowIfFailed(buffer->Map(0, nullptr, &mapped), "Map Gerstner surface upload buffer");
                std::memcpy(mapped, source, static_cast<std::size_t>(byteSize));
                buffer->Unmap(0, nullptr);
                return buffer;
            };

            GpuGerstnerSurface surface;
            surface.parameters = parameters;
            for (std::size_t lodIndex = 0U; lodIndex < GerstnerSurfaceHorizontalSampleLods.size(); ++lodIndex)
            {
                GerstnerSurfacePresentationParameters lodParameters = parameters;
                lodParameters.horizontalSampleCount = GerstnerSurfaceHorizontalSampleLods[lodIndex];
                const auto mesh = GenerateGerstnerSurfaceBaseMesh(lodParameters);
                if (!mesh)
                {
                    return std::unexpected(mesh.error());
                }

                std::vector<GerstnerSurfaceVertex> vertices;
                vertices.reserve(mesh->vertices.size());
                for (const GerstnerSurfaceBaseVertex& vertex : mesh->vertices)
                {
                    vertices.push_back({.basePosition = {vertex.x, vertex.y}, .surfaceWeight = vertex.surfaceWeight});
                }

                GpuGerstnerSurfaceLod& lod = surface.lods[lodIndex];
                lod.vertexBuffer = createUploadBuffer(
                    vertices.data(),
                    static_cast<std::uint64_t>(vertices.size()) * sizeof(GerstnerSurfaceVertex),
                    "Create Gerstner surface vertex buffer");
                lod.indexBuffer = createUploadBuffer(
                    mesh->indices.data(),
                    static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
                    "Create Gerstner surface index buffer");
                lod.vertexView.BufferLocation = lod.vertexBuffer->GetGPUVirtualAddress();
                lod.vertexView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(GerstnerSurfaceVertex));
                lod.vertexView.StrideInBytes = sizeof(GerstnerSurfaceVertex);
                lod.indexView.BufferLocation = lod.indexBuffer->GetGPUVirtualAddress();
                lod.indexView.SizeInBytes = static_cast<UINT>(mesh->indices.size() * sizeof(std::uint32_t));
                lod.indexView.Format = DXGI_FORMAT_R32_UINT;
                lod.horizontalSampleCount = lodParameters.horizontalSampleCount;
                lod.vertexCount = static_cast<std::uint32_t>(vertices.size());
                lod.indexCount = static_cast<std::uint32_t>(mesh->indices.size());
            }
#if defined(DEEPRUN_DEBUG)
            if (ValidateDebugMessages("Gerstner surface creation") != 0)
            {
                return std::unexpected("D3D12 validation reported a Gerstner surface warning or error");
            }
#endif
            gerstnerSurface = std::move(surface);
'''
text = text[:start] + replacement + text[end:]
old_log = '''            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-B spectral surface configured: vertices=" + std::to_string(gerstnerSurface.vertexCount) +
                    ", indices=" + std::to_string(gerstnerSurface.indexCount) + ", draw calls=1");'''
new_log = '''            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J adaptive spectral surface configured: lod samples=513/1025/2049/4097, max vertices=" +
                    std::to_string(gerstnerSurface.lods.back().vertexCount) + ", max indices=" +
                    std::to_string(gerstnerSurface.lods.back().indexCount) + ", draw calls=1");'''
text = replace_once(text, old_log, new_log, "D3D12Renderer.cpp configuration log")
text = replace_once(
    text,
    "            gerstnerSurface.vertexBuffer == nullptr || gerstnerSurface.indexBuffer == nullptr)\n        {",
    "            gerstnerSurface.lods.front().vertexBuffer == nullptr ||\n            gerstnerSurface.lods.front().indexBuffer == nullptr)\n        {",
    "D3D12Renderer.cpp draw readiness",
)
anchor = '''        const GerstnerSurfacePresentationParameters& parameters = gerstnerSurface.parameters;
        const auto asConstants = [](const GerstnerWaveComponent& component)
'''
selection = '''        const GerstnerSurfacePresentationParameters& parameters = gerstnerSurface.parameters;
        const auto selectedSampleCount = SelectGerstnerSurfaceHorizontalSampleCount(parameters, width, camera.width);
        if (!selectedSampleCount)
        {
            return std::unexpected(selectedSampleCount.error());
        }
        const auto selectedLod = std::ranges::find_if(
            gerstnerSurface.lods,
            [&selectedSampleCount](const GpuGerstnerSurfaceLod& lod)
            {
                return lod.horizontalSampleCount == *selectedSampleCount;
            });
        if (selectedLod == gerstnerSurface.lods.end())
        {
            return std::unexpected("Gerstner adaptive tessellation selected an unavailable LOD");
        }
        const auto asConstants = [](const GerstnerWaveComponent& component)
'''
text = replace_once(text, anchor, selection, "D3D12Renderer.cpp adaptive selection")
text = replace_once(
    text,
    "        commandList->IASetVertexBuffers(0, 1, &gerstnerSurface.vertexView);\n        commandList->IASetIndexBuffer(&gerstnerSurface.indexView);\n        commandList->DrawIndexedInstanced(gerstnerSurface.indexCount, 1, 0, 0, 0);\n        return GerstnerSurfaceDrawStats{\n            .vertexCount = gerstnerSurface.vertexCount,\n            .indexCount = gerstnerSurface.indexCount,\n            .drawCalls = 1U};",
    "        commandList->IASetVertexBuffers(0, 1, &selectedLod->vertexView);\n        commandList->IASetIndexBuffer(&selectedLod->indexView);\n        commandList->DrawIndexedInstanced(selectedLod->indexCount, 1, 0, 0, 0);\n        return GerstnerSurfaceDrawStats{\n            .vertexCount = selectedLod->vertexCount,\n            .indexCount = selectedLod->indexCount,\n            .drawCalls = 1U};",
    "D3D12Renderer.cpp selected LOD draw",
)
text = replace_once(
    text,
    "    return impl_->gerstnerSurfacePipeline != nullptr && impl_->gerstnerSurfaceRootSignature != nullptr &&\n           impl_->gerstnerSurface.vertexBuffer != nullptr && impl_->gerstnerSurface.indexBuffer != nullptr;",
    "    return impl_->gerstnerSurfacePipeline != nullptr && impl_->gerstnerSurfaceRootSignature != nullptr &&\n           impl_->gerstnerSurface.lods.front().vertexBuffer != nullptr &&\n           impl_->gerstnerSurface.lods.front().indexBuffer != nullptr &&\n           impl_->gerstnerSurface.lods.back().vertexBuffer != nullptr &&\n           impl_->gerstnerSurface.lods.back().indexBuffer != nullptr;",
    "D3D12Renderer.cpp IsGerstnerSurfaceReady",
)
path.write_text(text, encoding="utf-8", newline="\n")

# Tests/TestMain.cpp
path = Path("Tests/TestMain.cpp")
text = path.read_text(encoding="utf-8")
test_anchor = "\nbool D2SurfaceProjectionInsideViewport()\n"
test_code = r'''
bool W1JAdaptiveGerstnerTessellationPolicy()
{
    DeepRun::Render::GerstnerSurfacePresentationParameters flat{
        .minimumX = -340.0F,
        .maximumX = 340.0F,
        .referenceLevelY = 0.0F,
        .bottomFillY = -600.0F,
        .horizontalSampleCount = DeepRun::Render::GerstnerSurfaceHorizontalSampleLods.front(),
        .activeComponentCount = 0U,
        .deepFillRgb = {0.003F, 0.04F, 0.12F},
        .surfaceTintRgb = {0.0065F, 0.075F, 0.18F}};
    const auto flatSelection = DeepRun::Render::SelectGerstnerSurfaceHorizontalSampleCount(flat, 1028U, 600.0F);
    if (!flatSelection || *flatSelection != 513U)
        return false;

    auto rough = flat;
    rough.activeComponentCount = 1U;
    rough.components[0] = DeepRun::Render::GerstnerWaveComponent{
        .amplitudeMeters = 2.0F,
        .wavelengthMeters = 40.0F,
        .angularFrequencyRadiansPerSecond = 0.7F,
        .phaseOffsetRadians = 0.0F,
        .horizontalSteepness = 0.2F};
    const auto acceptanceSelection = DeepRun::Render::SelectGerstnerSurfaceHorizontalSampleCount(
        rough, 1028U, 600.0F);
    const auto targetSelection = DeepRun::Render::SelectGerstnerSurfaceHorizontalSampleCount(
        rough, 2560U, 600.0F);
    if (!acceptanceSelection || *acceptanceSelection != 2049U ||
        !targetSelection || *targetSelection != 4097U)
        return false;

    rough.horizontalSampleCount = DeepRun::Render::GerstnerSurfaceMaximumHorizontalSampleCount;
    const auto maximumMesh = DeepRun::Render::GenerateGerstnerSurfaceBaseMesh(rough);
    if (!maximumMesh || maximumMesh->vertices.size() != 8194U || maximumMesh->indices.size() != 24576U)
        return false;

    return !DeepRun::Render::SelectGerstnerSurfaceHorizontalSampleCount(rough, 0U, 600.0F) &&
           !DeepRun::Render::SelectGerstnerSurfaceHorizontalSampleCount(rough, 2560U, 0.0F);
}
'''
if test_anchor not in text:
    raise SystemExit("TestMain W1-J insertion anchor missing")
text = text.replace(test_anchor, "\n" + test_code + test_anchor, 1)
list_anchor = '        {"D2 surface projection inside viewport", D2SurfaceProjectionInsideViewport},\n'
text = replace_once(
    text,
    list_anchor,
    '        {"W1-J adaptive Gerstner tessellation policy", W1JAdaptiveGerstnerTessellationPolicy},\n' + list_anchor,
    "TestMain W1-J test registration",
)
path.write_text(text, encoding="utf-8", newline="\n")

print("W1-J adaptive sea tessellation patch applied")
