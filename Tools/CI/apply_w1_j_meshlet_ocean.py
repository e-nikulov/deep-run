from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one anchor, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    text = read(path)
    start_index = text.find(start)
    if start_index < 0:
        raise RuntimeError(f"{path}: start anchor not found: {start!r}")
    end_index = text.find(end, start_index)
    if end_index < 0:
        raise RuntimeError(f"{path}: end anchor not found: {end!r}")
    write(path, text[:start_index] + replacement + text[end_index:])


# -----------------------------------------------------------------------------
# Renderer-neutral meshlet dispatch policy.
# -----------------------------------------------------------------------------
replace_once(
    "Engine/Render/GerstnerSurface.h",
    "inline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;\n",
    "inline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;\n"
    "inline constexpr std::uint32_t GerstnerMeshletCellCapacity = 31U;\n"
    "inline constexpr std::uint32_t GerstnerMeshletMinimumCellCount = 256U;\n"
    "inline constexpr std::uint32_t GerstnerMeshletMaximumCellCount = 8192U;\n"
    "inline constexpr std::uint32_t GerstnerMeshletVisibleSamplesPerWavelength = 16U;\n",
)

replace_once(
    "Engine/Render/GerstnerSurface.h",
    "struct GerstnerSurfaceDrawStats final\n"
    "{\n"
    "    std::uint32_t vertexCount = 0U;\n"
    "    std::uint32_t indexCount = 0U;\n"
    "    std::uint32_t drawCalls = 0U;\n"
    "};\n",
    "struct GerstnerMeshletDispatchPlan final\n"
    "{\n"
    "    std::uint32_t cellCount = 0U;\n"
    "    std::uint32_t meshletCount = 0U;\n"
    "    std::uint32_t emittedVertexCount = 0U;\n"
    "    std::uint32_t emittedPrimitiveCount = 0U;\n"
    "};\n\n"
    "struct GerstnerSurfaceDrawStats final\n"
    "{\n"
    "    std::uint32_t vertexCount = 0U;\n"
    "    std::uint32_t indexCount = 0U;\n"
    "    std::uint32_t drawCalls = 0U;\n"
    "    std::uint32_t meshletCount = 0U;\n"
    "    bool meshShaderPath = false;\n"
    "};\n",
)

replace_once(
    "Engine/Render/GerstnerSurface.h",
    "[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n",
    "[[nodiscard]] std::expected<GerstnerMeshletDispatchPlan, std::string> BuildGerstnerMeshletDispatchPlan(\n"
    "    const GerstnerSurfacePresentationParameters& parameters,\n"
    "    float cameraHorizontalSpanMeters,\n"
    "    std::uint32_t viewportWidthPixels);\n"
    "[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n",
)

replace_once(
    "Engine/Render/GerstnerSurface.cpp",
    "#include <cmath>\n",
    "#include <algorithm>\n#include <cmath>\n",
)

meshlet_plan_cpp = r'''std::expected<GerstnerMeshletDispatchPlan, std::string> BuildGerstnerMeshletDispatchPlan(
    const GerstnerSurfacePresentationParameters& parameters,
    const float cameraHorizontalSpanMeters,
    const std::uint32_t viewportWidthPixels)
{
    if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(cameraHorizontalSpanMeters) || cameraHorizontalSpanMeters <= 0.0F || viewportWidthPixels == 0U)
    {
        return std::unexpected("Gerstner meshlet dispatch requires a finite positive camera span and viewport width");
    }

    float combinedAmplitudeMeters = 0.0F;
    for (std::size_t index = 0U; index < parameters.activeComponentCount; ++index)
    {
        combinedAmplitudeMeters += parameters.components[index].amplitudeMeters;
    }

    float targetPixelsPerCell = 1.25F;
    if (combinedAmplitudeMeters >= 2.0F)
    {
        targetPixelsPerCell = 0.625F;
    }
    else if (combinedAmplitudeMeters >= 1.0F)
    {
        targetPixelsPerCell = 0.8F;
    }
    else if (combinedAmplitudeMeters >= 0.35F)
    {
        targetPixelsPerCell = 1.0F;
    }

    std::uint32_t requiredCells = GerstnerMeshletMinimumCellCount;
    if (parameters.activeComponentCount > 0U)
    {
        const double screenRequired = std::ceil(
            static_cast<double>(viewportWidthPixels) / static_cast<double>(targetPixelsPerCell));
        requiredCells = static_cast<std::uint32_t>((std::min)(
            screenRequired, static_cast<double>(GerstnerMeshletMaximumCellCount)));

        const double pixelsPerMeter =
            static_cast<double>(viewportWidthPixels) / static_cast<double>(cameraHorizontalSpanMeters);
        for (std::size_t index = 0U; index < parameters.activeComponentCount; ++index)
        {
            const GerstnerWaveComponent& component = parameters.components[index];
            const double projectedAmplitudePixels = static_cast<double>(component.amplitudeMeters) * pixelsPerMeter;
            if (projectedAmplitudePixels < 0.25)
            {
                continue;
            }
            const double spectralRequired = std::ceil(
                static_cast<double>(cameraHorizontalSpanMeters) /
                static_cast<double>(component.wavelengthMeters) *
                static_cast<double>(GerstnerMeshletVisibleSamplesPerWavelength));
            const std::uint32_t boundedSpectralRequired = static_cast<std::uint32_t>((std::min)(
                spectralRequired, static_cast<double>(GerstnerMeshletMaximumCellCount)));
            requiredCells = (std::max)(requiredCells, boundedSpectralRequired);
        }
    }

    const std::uint32_t cellCount = std::clamp(
        requiredCells, GerstnerMeshletMinimumCellCount, GerstnerMeshletMaximumCellCount);
    const std::uint32_t meshletCount =
        (cellCount + GerstnerMeshletCellCapacity - 1U) / GerstnerMeshletCellCapacity;

    // Adjacent meshlets intentionally duplicate their shared boundary sample. This avoids cross-meshlet
    // index dependencies while keeping each workgroup bounded to 64 output vertices / 62 triangles.
    const std::uint32_t emittedVertexCount = 2U * (cellCount + meshletCount);
    const std::uint32_t emittedPrimitiveCount = 2U * cellCount;
    return GerstnerMeshletDispatchPlan{
        .cellCount = cellCount,
        .meshletCount = meshletCount,
        .emittedVertexCount = emittedVertexCount,
        .emittedPrimitiveCount = emittedPrimitiveCount};
}

'''
replace_once(
    "Engine/Render/GerstnerSurface.cpp",
    "std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n",
    meshlet_plan_cpp + "std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(\n",
)

# -----------------------------------------------------------------------------
# Mesh shader: procedural 1D ocean meshlets. Existing VS/PS remain fallback.
# -----------------------------------------------------------------------------
shader_path = "Shaders/GerstnerSurface.hlsl"
shader = read(shader_path)
old_evaluate_to_vs = r'''float2 EvaluateComponent(const float baseX, const float timeSeconds, const float4 wave, const float steepness)
{
    const float waveNumber = 6.28318530718F / wave.y;
    const float theta = waveNumber * baseX - wave.z * timeSeconds + wave.w;
    return float2(steepness * wave.x * cos(theta), wave.x * sin(theta));
}

VSOutput VSMain(const VSInput input)
{
    const float timeSeconds = ReferenceLevelAndTime.y;
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    const uint activeComponentCount = (uint)HorizontalSteepness1.w;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        if (activeComponentCount > 0U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, HorizontalSteepness0.x);
        if (activeComponentCount > 1U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, HorizontalSteepness0.y);
        if (activeComponentCount > 2U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, HorizontalSteepness0.z);
        if (activeComponentCount > 3U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave3, HorizontalSteepness0.w);
        if (activeComponentCount > 4U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave4, HorizontalSteepness1.x);
        if (activeComponentCount > 5U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave5, HorizontalSteepness1.y);
        if (activeComponentCount > 6U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave6, HorizontalSteepness1.z);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = baseWorldX + displacement.x * surfaceWeight;
    const float worldY = input.basePosition.y + displacement.y * surfaceWeight;
    VSOutput output;
    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;
}
'''
new_evaluate_to_vs = r'''float2 EvaluateComponent(const float baseX, const float timeSeconds, const float4 wave, const float steepness)
{
    const float waveNumber = 6.28318530718F / wave.y;
    const float theta = waveNumber * baseX - wave.z * timeSeconds + wave.w;
    return float2(steepness * wave.x * cos(theta), wave.x * sin(theta));
}

float2 EvaluateSurfaceDisplacement(const float baseWorldX, const float timeSeconds, const uint activeComponentCount)
{
    float2 displacement = float2(0.0F, 0.0F);
    if (activeComponentCount > 0U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, HorizontalSteepness0.x);
    if (activeComponentCount > 1U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, HorizontalSteepness0.y);
    if (activeComponentCount > 2U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, HorizontalSteepness0.z);
    if (activeComponentCount > 3U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave3, HorizontalSteepness0.w);
    if (activeComponentCount > 4U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave4, HorizontalSteepness1.x);
    if (activeComponentCount > 5U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave5, HorizontalSteepness1.y);
    if (activeComponentCount > 6U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave6, HorizontalSteepness1.z);
    return displacement;
}

VSOutput VSMain(const VSInput input)
{
    const float timeSeconds = ReferenceLevelAndTime.y;
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    const uint activeComponentCount = (uint)HorizontalSteepness1.w;
    const float2 displacement = input.surfaceWeight > 0.5F
        ? EvaluateSurfaceDisplacement(baseWorldX, timeSeconds, activeComponentCount)
        : float2(0.0F, 0.0F);

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = baseWorldX + displacement.x * surfaceWeight;
    const float worldY = input.basePosition.y + displacement.y * surfaceWeight;
    VSOutput output;
    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;
}

static const uint GerstnerMeshletCellCapacity = 31U;

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MSMain(
    const uint3 groupId : SV_GroupID,
    const uint threadIndex : SV_GroupIndex,
    out vertices VSOutput outputVertices[64],
    out indices uint3 outputTriangles[62])
{
    // W1-J mesh path repurposes otherwise-unused alpha lanes as renderer-private constants:
    // ReferenceLevelAndTime.w = visible world span, DeepFillColor.w = bottom fill Y,
    // SurfaceTintColor.w = total horizontal cell count. The compatibility VS still receives
    // its historical horizontal scale and both pixel-shader alpha lanes remain unused.
    const uint totalCellCount = max(1U, (uint)(SurfaceTintColor.w + 0.5F));
    const uint firstCell = groupId.x * GerstnerMeshletCellCapacity;
    const uint localCellCount = min(GerstnerMeshletCellCapacity, totalCellCount - firstCell);
    const uint localSampleCount = localCellCount + 1U;
    SetMeshOutputCounts(localSampleCount * 2U, localCellCount * 2U);

    if (threadIndex < localSampleCount)
    {
        const uint sampleIndex = firstCell + threadIndex;
        const float normalizedX = (float)sampleIndex / (float)totalCellCount;
        const float baseWorldX = ReferenceLevelAndTime.z + (normalizedX - 0.5F) * ReferenceLevelAndTime.w;
        const uint activeComponentCount = (uint)HorizontalSteepness1.w;
        const float2 displacement = EvaluateSurfaceDisplacement(
            baseWorldX, ReferenceLevelAndTime.y, activeComponentCount);

        const uint surfaceVertex = threadIndex * 2U;
        const uint bottomVertex = surfaceVertex + 1U;
        outputVertices[surfaceVertex].position = mul(
            ViewProjection,
            float4(baseWorldX + displacement.x, ReferenceLevelAndTime.x + displacement.y, 0.0F, 1.0F));
        outputVertices[surfaceVertex].surfaceWeight = 1.0F;
        outputVertices[bottomVertex].position = mul(
            ViewProjection,
            float4(baseWorldX, DeepFillColor.w, 0.0F, 1.0F));
        outputVertices[bottomVertex].surfaceWeight = 0.0F;
    }

    if (threadIndex < localCellCount)
    {
        const uint upperLeft = threadIndex * 2U;
        const uint lowerLeft = upperLeft + 1U;
        const uint upperRight = upperLeft + 2U;
        const uint lowerRight = upperLeft + 3U;
        outputTriangles[threadIndex * 2U] = uint3(upperLeft, lowerLeft, upperRight);
        outputTriangles[threadIndex * 2U + 1U] = uint3(upperRight, lowerLeft, lowerRight);
    }
}
'''
if shader.count(old_evaluate_to_vs) != 1:
    raise RuntimeError("Shaders/GerstnerSurface.hlsl: VS anchor does not match clean W1-I")
write(shader_path, shader.replace(old_evaluate_to_vs, new_evaluate_to_vs, 1))

# -----------------------------------------------------------------------------
# DXC build/staging for Shader Model 6.5 mesh shader.
# -----------------------------------------------------------------------------
replace_once(
    "CMakeLists.txt",
    'set(DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/GerstnerSurfacePS.cso")\n',
    'set(DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/GerstnerSurfacePS.cso")\n'
    'set(DEEPRUN_GERSTNER_SURFACE_MESH_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/GerstnerSurfaceMS.cso")\n',
)

replace_once(
    "CMakeLists.txt",
    'add_custom_command(\n'
    '    OUTPUT "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
    'add_custom_command(\n'
    '    OUTPUT "${DEEPRUN_GERSTNER_SURFACE_MESH_SHADER}"\n'
    '    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"\n'
    '    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"\n'
    '        -T ms_6_5 -E MSMain -Fo "${DEEPRUN_GERSTNER_SURFACE_MESH_SHADER}"\n'
    '        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"\n'
    '    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"\n'
    '    VERBATIM\n'
    ')\n'
    'add_custom_command(\n'
    '    OUTPUT "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
)

replace_once(
    "CMakeLists.txt",
    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\n'
    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\n'
    '        "${DEEPRUN_GERSTNER_SURFACE_MESH_SHADER}"\n'
    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
)

# There are two occurrences of the PS->ToneMap sequence: dependency list and post-build copy. The previous
# replacement consumes the first. Replace the remaining staging occurrence now.
replace_once(
    "CMakeLists.txt",
    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\n'
    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
    '        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"\n'
    '        "${DEEPRUN_GERSTNER_SURFACE_MESH_SHADER}"\n'
    '        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"\n',
)

replace_once(
    "CMakeLists.txt",
    "enable_testing()\n\nadd_executable(DeepRunTests\n",
    "enable_testing()\n\n"
    "add_executable(W1MeshletOceanTests Tests/W1MeshletOceanTest.cpp)\n"
    "target_compile_features(W1MeshletOceanTests PRIVATE cxx_std_23)\n"
    "deeprun_set_project_warnings(W1MeshletOceanTests)\n"
    "target_link_libraries(W1MeshletOceanTests PRIVATE DeepRunEngine)\n"
    "add_test(NAME W1MeshletOcean COMMAND W1MeshletOceanTests)\n\n"
    "add_executable(DeepRunTests\n",
)

# -----------------------------------------------------------------------------
# Existing D3D12 renderer: feature detection + mesh PSO + DispatchMesh fallback split.
# -----------------------------------------------------------------------------
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "struct GpuGerstnerSurface final\n"
    "{\n"
    "    ComPtr<ID3D12Resource> vertexBuffer;\n"
    "    ComPtr<ID3D12Resource> indexBuffer;\n"
    "    D3D12_VERTEX_BUFFER_VIEW vertexView{};\n"
    "    D3D12_INDEX_BUFFER_VIEW indexView{};\n"
    "    GerstnerSurfacePresentationParameters parameters{};\n"
    "    std::uint32_t vertexCount = 0U;\n"
    "    std::uint32_t indexCount = 0U;\n"
    "};\n",
    "struct GpuGerstnerSurface final\n"
    "{\n"
    "    ComPtr<ID3D12Resource> vertexBuffer;\n"
    "    ComPtr<ID3D12Resource> indexBuffer;\n"
    "    D3D12_VERTEX_BUFFER_VIEW vertexView{};\n"
    "    D3D12_INDEX_BUFFER_VIEW indexView{};\n"
    "    GerstnerSurfacePresentationParameters parameters{};\n"
    "    std::uint32_t vertexCount = 0U;\n"
    "    std::uint32_t indexCount = 0U;\n"
    "    bool configured = false;\n"
    "};\n\n"
    "template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T>\n"
    "struct alignas(void*) PipelineStateStreamSubobject final\n"
    "{\n"
    "    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;\n"
    "    T data{};\n"
    "};\n\n"
    "struct GerstnerMeshPipelineStateStream final\n"
    "{\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> rootSignature;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> meshShader;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> pixelShader;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> sampleMask;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depthStencil;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> primitiveTopology;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> renderTargetFormats;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depthStencilFormat;\n"
    "    PipelineStateStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> sampleDescription;\n"
    "};\n",
)

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    '            ThrowIfFailed(commandList->Close(), "Close initial command list");\n'
    '            CreateScenePresentationUploads();\n',
    '            ThrowIfFailed(commandList->Close(), "Close initial command list");\n'
    '            DetectMeshShaderSupport();\n'
    '            CreateScenePresentationUploads();\n',
)

new_pipeline_function = r'''    void DetectMeshShaderSupport()
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        const HRESULT optionsResult = device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
        if (FAILED(optionsResult) || options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
        {
            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J mesh shader ocean unavailable; retaining indexed Gerstner compatibility path");
            return;
        }

        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_5};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
            shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5)
        {
            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J mesh shader tier is present but Shader Model 6.5 is unavailable; retaining compatibility path");
            return;
        }

        ComPtr<ID3D12GraphicsCommandList6> commandList6;
        if (FAILED(commandList.As(&commandList6)))
        {
            logger.Warning(
                Diagnostics::LogCategory::Render,
                "W1-J mesh shader tier is present but ID3D12GraphicsCommandList6 is unavailable");
            return;
        }

        meshShaderTier = options7.MeshShaderTier;
        meshCommandList = std::move(commandList6);
        meshShaderSupported = true;
        logger.Info(
            Diagnostics::LogCategory::Render,
            "D3D12 Mesh Shader Tier 1 path available for W1-J ocean geometry");
    }

    void CreateGerstnerSurfacePipeline(const std::filesystem::path& shaderRoot)
    {
        const std::vector<std::byte> vertexShader = ReadBinaryFile(shaderRoot / "GerstnerSurfaceVS.cso");
        const std::vector<std::byte> pixelShader = ReadBinaryFile(shaderRoot / "GerstnerSurfacePS.cso");

        D3D12_ROOT_PARAMETER rootParameter{};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameter.Constants.ShaderRegister = 0;
        rootParameter.Constants.RegisterSpace = 0;
        rootParameter.Constants.Num32BitValues = sizeof(GerstnerDrawConstants) / sizeof(std::uint32_t);
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1U;
        rootDescription.pParameters = &rootParameter;
        // One root signature intentionally serves both paths. IA access is harmless to the mesh path and
        // preserves the WARP / pre-mesh-shader compatibility renderer without parallel presentation state.
        rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> serializedRoot;
        ComPtr<ID3DBlob> rootErrors;
        const HRESULT serializeResult = D3D12SerializeRootSignature(
            &rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRoot,
            &rootErrors);
        if (FAILED(serializeResult))
        {
            const std::string detail = rootErrors != nullptr
                                           ? std::string(
                                                 static_cast<const char*>(rootErrors->GetBufferPointer()),
                                                 rootErrors->GetBufferSize())
                                           : "unknown root-signature error";
            throw std::runtime_error("Serialize Gerstner surface root signature failed: " + detail);
        }
        ThrowIfFailed(
            device->CreateRootSignature(
                0,
                serializedRoot->GetBufferPointer(),
                serializedRoot->GetBufferSize(),
                IID_PPV_ARGS(&gerstnerSurfaceRootSignature)),
            "Create Gerstner surface root signature");

        const std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}}};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = gerstnerSurfaceRootSignature.Get();
        pipeline.VS = {vertexShader.data(), vertexShader.size()};
        pipeline.PS = {pixelShader.data(), pixelShader.size()};
        pipeline.BlendState.AlphaToCoverageEnable = FALSE;
        pipeline.BlendState.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC& targetBlend = pipeline.BlendState.RenderTarget[0];
        targetBlend.BlendEnable = FALSE;
        targetBlend.LogicOpEnable = FALSE;
        targetBlend.SrcBlend = D3D12_BLEND_ONE;
        targetBlend.DestBlend = D3D12_BLEND_ZERO;
        targetBlend.BlendOp = D3D12_BLEND_OP_ADD;
        targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        targetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        targetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        targetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = std::numeric_limits<UINT>::max();
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.FrontCounterClockwise = TRUE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = FALSE;
        pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.StencilEnable = FALSE;
        pipeline.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
        pipeline.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = SceneColorFormat;
        pipeline.DSVFormat = DepthFormat;
        pipeline.SampleDesc.Count = 1;
        ThrowIfFailed(
            device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&gerstnerSurfacePipeline)),
            "Create Gerstner surface graphics pipeline");

        if (!meshShaderSupported)
        {
            return;
        }

        try
        {
            const std::vector<std::byte> meshShader = ReadBinaryFile(shaderRoot / "GerstnerSurfaceMS.cso");
            ComPtr<ID3D12Device2> meshDevice;
            ThrowIfFailed(device.As(&meshDevice), "Query ID3D12Device2 for Gerstner mesh shader");

            GerstnerMeshPipelineStateStream meshPipeline{};
            meshPipeline.rootSignature.data = gerstnerSurfaceRootSignature.Get();
            meshPipeline.meshShader.data = {meshShader.data(), meshShader.size()};
            meshPipeline.pixelShader.data = {pixelShader.data(), pixelShader.size()};
            meshPipeline.blend.data = pipeline.BlendState;
            meshPipeline.sampleMask.data = pipeline.SampleMask;
            meshPipeline.rasterizer.data = pipeline.RasterizerState;
            meshPipeline.depthStencil.data = pipeline.DepthStencilState;
            meshPipeline.primitiveTopology.data = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            meshPipeline.renderTargetFormats.data.NumRenderTargets = 1U;
            meshPipeline.renderTargetFormats.data.RTFormats[0] = SceneColorFormat;
            meshPipeline.depthStencilFormat.data = DepthFormat;
            meshPipeline.sampleDescription.data.Count = 1U;
            meshPipeline.sampleDescription.data.Quality = 0U;

            const D3D12_PIPELINE_STATE_STREAM_DESC streamDescription{
                sizeof(meshPipeline), &meshPipeline};
            ThrowIfFailed(
                meshDevice->CreatePipelineState(&streamDescription, IID_PPV_ARGS(&gerstnerSurfaceMeshPipeline)),
                "Create Gerstner mesh shader pipeline");
            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J meshlet ocean pipeline created (31 cells / 64 vertices / 62 triangles per meshlet)");
        }
        catch (const std::exception& exception)
        {
            gerstnerSurfaceMeshPipeline.Reset();
            meshCommandList.Reset();
            meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            meshShaderSupported = false;
            logger.Warning(
                Diagnostics::LogCategory::Render,
                std::string("W1-J mesh pipeline creation failed; using indexed compatibility path: ") + exception.what());
        }
    }

'''
replace_between(
    "Engine/Render/D3D12Renderer.cpp",
    "    void CreateGerstnerSurfacePipeline(const std::filesystem::path& shaderRoot)\n",
    "    void CreateOutputPipeline(const std::filesystem::path& shaderRoot)\n",
    new_pipeline_function,
)

new_configure = r'''    std::expected<void, std::string> ConfigureGerstnerSurface(
        const GerstnerSurfacePresentationParameters& parameters)
    {
        if (!initialized || device == nullptr)
        {
            return std::unexpected("Gerstner surface requires an initialized renderer");
        }
        if (frameOpen)
        {
            return std::unexpected("Gerstner surface configuration is not valid during a frame");
        }
        if (gerstnerSurface.configured)
        {
            return std::unexpected("Gerstner surface is already configured for this renderer");
        }
        if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
        {
            return std::unexpected(valid.error());
        }

        if (meshShaderSupported && meshCommandList != nullptr && gerstnerSurfaceMeshPipeline != nullptr)
        {
            gerstnerSurface.parameters = parameters;
            gerstnerSurface.configured = true;
            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J meshlet ocean configured: procedural geometry, persistent ocean VB/IB=0, dispatches=1");
            return {};
        }

        const auto mesh = GenerateGerstnerSurfaceBaseMesh(parameters);
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

        try
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
            surface.vertexBuffer = createUploadBuffer(
                vertices.data(),
                static_cast<std::uint64_t>(vertices.size()) * sizeof(GerstnerSurfaceVertex),
                "Create Gerstner surface vertex buffer");
            surface.indexBuffer = createUploadBuffer(
                mesh->indices.data(),
                static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
                "Create Gerstner surface index buffer");
            surface.vertexView.BufferLocation = surface.vertexBuffer->GetGPUVirtualAddress();
            surface.vertexView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(GerstnerSurfaceVertex));
            surface.vertexView.StrideInBytes = sizeof(GerstnerSurfaceVertex);
            surface.indexView.BufferLocation = surface.indexBuffer->GetGPUVirtualAddress();
            surface.indexView.SizeInBytes = static_cast<UINT>(mesh->indices.size() * sizeof(std::uint32_t));
            surface.indexView.Format = DXGI_FORMAT_R32_UINT;
            surface.parameters = parameters;
            surface.vertexCount = static_cast<std::uint32_t>(vertices.size());
            surface.indexCount = static_cast<std::uint32_t>(mesh->indices.size());
            surface.configured = true;
#if defined(DEEPRUN_DEBUG)
            if (ValidateDebugMessages("Gerstner surface creation") != 0)
            {
                return std::unexpected("D3D12 validation reported a Gerstner surface warning or error");
            }
#endif
            gerstnerSurface = std::move(surface);
            logger.Info(
                Diagnostics::LogCategory::Render,
                "W1-J indexed compatibility ocean configured: vertices=" +
                    std::to_string(gerstnerSurface.vertexCount) + ", indices=" +
                    std::to_string(gerstnerSurface.indexCount) + ", draw calls=1");
            return {};
        }
        catch (const std::exception& exception)
        {
            logger.Error(Diagnostics::LogCategory::Render, exception.what());
            return std::unexpected(exception.what());
        }
    }

'''
replace_between(
    "Engine/Render/D3D12Renderer.cpp",
    "    std::expected<void, std::string> ConfigureGerstnerSurface(\n",
    "    std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(\n",
    new_configure,
)

new_draw = r'''    std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(
        const OrthographicCamera& camera, const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
            simulationTimeSeconds > (std::numeric_limits<float>::max)())
        {
            return std::unexpected("Gerstner phase time must be finite, non-negative and representable");
        }
        if (!frameOpen)
        {
            return std::unexpected("Gerstner surface draw is only valid between BeginFrame and EndFrame");
        }
        const bool meshPath = meshShaderSupported && meshCommandList != nullptr &&
                              gerstnerSurfaceMeshPipeline != nullptr;
        const bool indexedPath = gerstnerSurfacePipeline != nullptr &&
                                 gerstnerSurface.vertexBuffer != nullptr && gerstnerSurface.indexBuffer != nullptr;
        if (!gerstnerSurface.configured || gerstnerSurfaceRootSignature == nullptr || (!meshPath && !indexedPath))
        {
            return std::unexpected("Gerstner surface pipeline or geometry is not ready");
        }
        if (!IsFinite(camera.viewProjection) || !std::isfinite(camera.width) || !std::isfinite(camera.height) ||
            camera.width <= 0.0F || camera.height <= 0.0F)
        {
            return std::unexpected("Gerstner surface draw received invalid camera projection data");
        }

        const GerstnerSurfacePresentationParameters& parameters = gerstnerSurface.parameters;
        const auto asConstants = [](const GerstnerWaveComponent& component)
        {
            return std::array<float, 4>{
                component.amplitudeMeters,
                component.wavelengthMeters,
                component.angularFrequencyRadiansPerSecond,
                component.phaseOffsetRadians};
        };
        const float authoredSpanMeters = parameters.maximumX - parameters.minimumX;
        const float horizontalScale = (std::max)(1.0F, camera.width * 1.05F / authoredSpanMeters);
        const float renderSpanMeters = authoredSpanMeters * horizontalScale;

        GerstnerMeshletDispatchPlan dispatchPlan{};
        if (meshPath)
        {
            const auto plan = BuildGerstnerMeshletDispatchPlan(parameters, camera.width, width);
            if (!plan)
            {
                return std::unexpected(plan.error());
            }
            dispatchPlan = *plan;
        }

        const GerstnerDrawConstants constants{
            .viewProjection = camera.viewProjection.values,
            // The compatibility VS receives historical horizontalScale in .w. The mesh shader receives
            // visible world span instead and generates every ocean vertex procedurally around absolute world X.
            .referenceLevelAndTime = {
                parameters.referenceLevelY,
                static_cast<float>(simulationTimeSeconds),
                camera.target.x,
                meshPath ? renderSpanMeters : horizontalScale},
            .wave0 = asConstants(parameters.components[0]),
            .wave1 = asConstants(parameters.components[1]),
            .wave2 = asConstants(parameters.components[2]),
            .wave3 = asConstants(parameters.components[3]),
            .wave4 = asConstants(parameters.components[4]),
            .wave5 = asConstants(parameters.components[5]),
            .wave6 = asConstants(parameters.components[6]),
            .horizontalSteepness0 = {
                parameters.components[0].horizontalSteepness,
                parameters.components[1].horizontalSteepness,
                parameters.components[2].horizontalSteepness,
                parameters.components[3].horizontalSteepness},
            .horizontalSteepness1 = {
                parameters.components[4].horizontalSteepness,
                parameters.components[5].horizontalSteepness,
                parameters.components[6].horizontalSteepness,
                static_cast<float>(parameters.activeComponentCount)},
            // Alpha is not consumed by PSMain. On the mesh path it transports renderer-private procedural data.
            .deepFillColor = {
                parameters.deepFillRgb[0], parameters.deepFillRgb[1], parameters.deepFillRgb[2],
                meshPath ? parameters.bottomFillY : 1.0F},
            .surfaceTintColor = {
                parameters.surfaceTintRgb[0], parameters.surfaceTintRgb[1], parameters.surfaceTintRgb[2],
                meshPath ? static_cast<float>(dispatchPlan.cellCount) : 1.0F}};
        commandList->SetGraphicsRootSignature(gerstnerSurfaceRootSignature.Get());
        commandList->SetGraphicsRoot32BitConstants(
            0,
            sizeof(GerstnerDrawConstants) / sizeof(std::uint32_t),
            &constants,
            0);

        if (meshPath)
        {
            commandList->SetPipelineState(gerstnerSurfaceMeshPipeline.Get());
            meshCommandList->DispatchMesh(dispatchPlan.meshletCount, 1U, 1U);
            return GerstnerSurfaceDrawStats{
                .vertexCount = dispatchPlan.emittedVertexCount,
                .indexCount = dispatchPlan.emittedPrimitiveCount * 3U,
                .drawCalls = 1U,
                .meshletCount = dispatchPlan.meshletCount,
                .meshShaderPath = true};
        }

        commandList->SetPipelineState(gerstnerSurfacePipeline.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &gerstnerSurface.vertexView);
        commandList->IASetIndexBuffer(&gerstnerSurface.indexView);
        commandList->DrawIndexedInstanced(gerstnerSurface.indexCount, 1, 0, 0, 0);
        return GerstnerSurfaceDrawStats{
            .vertexCount = gerstnerSurface.vertexCount,
            .indexCount = gerstnerSurface.indexCount,
            .drawCalls = 1U,
            .meshletCount = 0U,
            .meshShaderPath = false};
    }

'''
replace_between(
    "Engine/Render/D3D12Renderer.cpp",
    "    std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(\n",
    "    void SetPresentationTime(const float elapsedSeconds) noexcept\n",
    new_draw,
)

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "        gerstnerSurfacePipeline.Reset();\n"
    "        gerstnerSurfaceRootSignature.Reset();\n",
    "        gerstnerSurfaceMeshPipeline.Reset();\n"
    "        meshCommandList.Reset();\n"
    "        meshShaderSupported = false;\n"
    "        meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;\n"
    "        gerstnerSurfacePipeline.Reset();\n"
    "        gerstnerSurfaceRootSignature.Reset();\n",
)

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "    ComPtr<ID3D12RootSignature> gerstnerSurfaceRootSignature;\n"
    "    ComPtr<ID3D12PipelineState> gerstnerSurfacePipeline;\n",
    "    ComPtr<ID3D12RootSignature> gerstnerSurfaceRootSignature;\n"
    "    ComPtr<ID3D12PipelineState> gerstnerSurfacePipeline;\n"
    "    ComPtr<ID3D12PipelineState> gerstnerSurfaceMeshPipeline;\n"
    "    ComPtr<ID3D12GraphicsCommandList6> meshCommandList;\n"
    "    D3D12_MESH_SHADER_TIER meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;\n"
    "    bool meshShaderSupported = false;\n",
)

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "bool D3D12Renderer::IsGerstnerSurfaceReady() const noexcept\n"
    "{\n"
    "    return impl_->gerstnerSurfacePipeline != nullptr && impl_->gerstnerSurfaceRootSignature != nullptr &&\n"
    "           impl_->gerstnerSurface.vertexBuffer != nullptr && impl_->gerstnerSurface.indexBuffer != nullptr;\n"
    "}\n",
    "bool D3D12Renderer::IsGerstnerSurfaceReady() const noexcept\n"
    "{\n"
    "    const bool meshPath = impl_->meshShaderSupported && impl_->meshCommandList != nullptr &&\n"
    "                          impl_->gerstnerSurfaceMeshPipeline != nullptr;\n"
    "    const bool indexedPath = impl_->gerstnerSurfacePipeline != nullptr &&\n"
    "                             impl_->gerstnerSurface.vertexBuffer != nullptr &&\n"
    "                             impl_->gerstnerSurface.indexBuffer != nullptr;\n"
    "    return impl_->gerstnerSurface.configured && impl_->gerstnerSurfaceRootSignature != nullptr &&\n"
    "           (meshPath || indexedPath);\n"
    "}\n",
)

# -----------------------------------------------------------------------------
# Focused regression executable: exact meshlet counts / bounds independent of GPU availability.
# -----------------------------------------------------------------------------
test_source = r'''#include "Engine/Render/GerstnerSurface.h"

#include <cstdint>
#include <iostream>

namespace
{
using namespace DeepRun::Render;

GerstnerSurfacePresentationParameters MakeBaseSurface()
{
    return GerstnerSurfacePresentationParameters{
        .minimumX = -300.0F,
        .maximumX = 300.0F,
        .referenceLevelY = 0.0F,
        .bottomFillY = -600.0F,
        .horizontalSampleCount = 257U,
        .activeComponentCount = 0U,
        .deepFillRgb = {0.02F, 0.075F, 0.12F},
        .surfaceTintRgb = {0.0065F, 0.075F, 0.18F}};
}

bool Require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "W1-J meshlet ocean regression failed: " << message << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    using namespace DeepRun::Render;

    GerstnerSurfacePresentationParameters flat = MakeBaseSurface();
    const auto flatPlan = BuildGerstnerMeshletDispatchPlan(flat, 600.0F, 2560U);
    if (!Require(flatPlan.has_value(), "flat dispatch plan rejected") ||
        !Require(flatPlan->cellCount == 256U, "flat water must keep the minimum cell budget") ||
        !Require(flatPlan->meshletCount == 9U, "flat water meshlet count changed") ||
        !Require(flatPlan->emittedVertexCount == 530U, "flat emitted vertex count changed") ||
        !Require(flatPlan->emittedPrimitiveCount == 512U, "flat primitive count changed"))
    {
        return 1;
    }

    GerstnerSurfacePresentationParameters rough = MakeBaseSurface();
    rough.activeComponentCount = 1U;
    rough.components[0] = GerstnerWaveComponent{
        .amplitudeMeters = 2.5F,
        .wavelengthMeters = 45.0F,
        .angularFrequencyRadiansPerSecond = 0.8F,
        .phaseOffsetRadians = 0.2F,
        .horizontalSteepness = 0.05F};

    const auto targetPlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 2560U);
    if (!Require(targetPlan.has_value(), "target-resolution rough plan rejected") ||
        !Require(targetPlan->cellCount == 4096U, "2560 px rough sea must select 4096 procedural cells") ||
        !Require(targetPlan->meshletCount == 133U, "4096-cell sea must dispatch 133 meshlets") ||
        !Require(targetPlan->emittedVertexCount == 8458U, "4096-cell emitted vertex count changed") ||
        !Require(targetPlan->emittedPrimitiveCount == 8192U, "4096-cell primitive count changed"))
    {
        return 1;
    }

    const auto acceptancePlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 1028U);
    if (!Require(acceptancePlan.has_value(), "visual-acceptance rough plan rejected") ||
        !Require(acceptancePlan->cellCount == 1645U, "1028 px rough sea density changed") ||
        !Require(acceptancePlan->meshletCount == 54U, "1028 px rough sea meshlet count changed") ||
        !Require(acceptancePlan->emittedVertexCount == 3398U, "1028 px emitted vertex count changed") ||
        !Require(acceptancePlan->emittedPrimitiveCount == 3290U, "1028 px primitive count changed"))
    {
        return 1;
    }

    const auto clampedPlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 20000U);
    if (!Require(clampedPlan.has_value(), "maximum-density plan rejected") ||
        !Require(clampedPlan->cellCount == GerstnerMeshletMaximumCellCount, "maximum cell clamp changed") ||
        !Require(clampedPlan->meshletCount == 265U, "maximum meshlet count changed") ||
        !Require(clampedPlan->emittedVertexCount == 16914U, "maximum emitted vertex count changed") ||
        !Require(clampedPlan->emittedPrimitiveCount == 16384U, "maximum primitive count changed"))
    {
        return 1;
    }

    if (!Require(!BuildGerstnerMeshletDispatchPlan(rough, 0.0F, 2560U).has_value(),
                 "zero camera span must be rejected") ||
        !Require(!BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 0U).has_value(),
                 "zero viewport width must be rejected"))
    {
        return 1;
    }

    std::cout << "W1-J meshlet ocean regression PASS\n";
    return 0;
}
'''
write("Tests/W1MeshletOceanTest.cpp", test_source)

# -----------------------------------------------------------------------------
# Architecture note and permanent visual capture routing.
# -----------------------------------------------------------------------------
doc = r'''# W1-J — Meshlet Ocean Surface / Smooth Silhouette

## Goal

Remove visible polygonal stepping from the W1 production ocean without introducing hardware tessellation,
a second wave model, CPU wave simulation, per-frame mesh rebuilding, or four prebuilt indexed LOD meshes.

The authority path remains exactly:

`WeatherState -> ProductionOceanSpectrum -> WaterBody -> Gerstner presentation -> D3D12`

Only the renderer-owned geometry submission changes.

## Production geometry path

On adapters exposing `D3D12_FEATURE_D3D12_OPTIONS7::MeshShaderTier` and Shader Model 6.5, the renderer uses
a procedural mesh-shader path. `GerstnerSurfaceMS.cso` receives the same seven-wave draw constants as the
compatibility vertex shader. No persistent ocean vertex or index buffer is allocated.

One meshlet covers at most 31 horizontal cells. It emits at most 64 vertices and 62 triangles. Shared boundary
samples are duplicated between adjacent meshlets so no meshlet references another meshlet's output.

At the 2560 px project target a rough sea selects 4096 horizontal cells and dispatches 133 meshlets in one
`DispatchMesh(133, 1, 1)` call. Wave phase still uses absolute world X, so camera movement never owns wave state.

## Compatibility path

Adapters without Mesh Shader Tier support, WARP, or environments that cannot expose
`ID3D12GraphicsCommandList6` retain the existing indexed Gerstner path. This is a compatibility fallback only;
it is not a second ocean model and does not change simulation or weather authority.

## Density policy

The procedural cell budget is bounded to 256..8192 cells. Flat water uses the minimum. Active waves combine a
screen-space target with spectral evidence for wavelengths that project to a visible vertical amplitude. Rough
water targets approximately 0.625 px per horizontal cell; sub-pixel spectral components cannot force max density.

No vertex/index buffer is rebuilt per frame. Geometry density is transported only as draw constants and mesh
workgroup count.

## Acceptance gates

- clean Debug and Release configure/build/test;
- focused W1-J regression validates 256/4096/8192-cell budgets and exact meshlet/output counts;
- mesh-capable hardware logs the mesh shader path and keeps persistent ocean VB/IB at zero;
- unsupported hardware reaches the indexed compatibility path without renderer failure;
- one ocean submission per frame on either path;
- B0/B1/B2/B8 production captures remain successful;
- B8 no longer exposes obvious triangular/polyline stepping at the production target resolution;
- no HS/DS hardware tessellation and no parallel wave authority are introduced.
'''
write("docs/development/w1-j-meshlet-ocean.md", doc)

replace_once(
    ".github/workflows/w1-sea-state-visual-capture.yml",
    "      - feature/w1-i-sea-state-visual-acceptance\n",
    "      - feature/w1-i-sea-state-visual-acceptance\n      - feature/w1-j-meshlet-ocean\n",
)
replace_once(
    ".github/workflows/w1-sea-state-visual-capture.yml",
    "      - Engine/Render/D3D12Renderer.cpp\n",
    "      - Engine/Render/D3D12Renderer.cpp\n      - CMakeLists.txt\n      - Tests/W1MeshletOceanTest.cpp\n",
)

# The patch workflow is intentionally self-deleting: the resulting branch contains only product code/tests/docs.
for temporary in (
    ROOT / "Tools/CI/apply_w1_j_meshlet_ocean.py",
    ROOT / ".github/workflows/w1-j-meshlet-ocean-patch.yml",
):
    temporary.unlink(missing_ok=True)

print("W1-J meshlet ocean patch applied")
