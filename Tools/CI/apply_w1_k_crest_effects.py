from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


replace_once(
    "Engine/Render/GerstnerSurface.cpp",
    '''    if (!IsFiniteColor(parameters.deepFillRgb) || !IsFiniteColor(parameters.surfaceTintRgb))
    {
        return std::unexpected("Gerstner surface colors must be finite SDR-normalized RGB values");
    }

    const bool legacyContract''',
    '''    if (!IsFiniteColor(parameters.deepFillRgb) || !IsFiniteColor(parameters.surfaceTintRgb))
    {
        return std::unexpected("Gerstner surface colors must be finite SDR-normalized RGB values");
    }
    if (!std::isfinite(parameters.crestWhiteningStrength) ||
        parameters.crestWhiteningStrength < 0.0F || parameters.crestWhiteningStrength > 1.0F)
    {
        return std::unexpected("Gerstner crest whitening strength must be finite and normalized");
    }

    const bool legacyContract''')

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    '''                parameters.components[4].horizontalSteepness,
                parameters.components[5].horizontalSteepness,
                parameters.components[6].horizontalSteepness,
                static_cast<float>(parameters.activeComponentCount)},''',
    '''                parameters.components[4].horizontalSteepness,
                parameters.components[5].horizontalSteepness,
                parameters.components[6].horizontalSteepness,
                // Preserve the 64-DWORD root-signature ceiling: integer part transports active component count;
                // the fractional half-range transports one normalized generic crest highlight control.
                static_cast<float>(parameters.activeComponentCount) +
                    parameters.crestWhiteningStrength * 0.5F},''')

shader_path = "Shaders/GerstnerSurface.hlsl"
shader = read(shader_path)
shader = shader.replace(
    '''struct VSOutput
{
    float4 position : SV_Position;
    float surfaceWeight : TEXCOORD0;
};''',
    '''struct VSOutput
{
    float4 position : SV_Position;
    float surfaceWeight : TEXCOORD0;
    float crestSignal : TEXCOORD1;
};''', 1)
shader = shader.replace(
    '''float2 EvaluateSurfaceDisplacement(const float baseWorldX, const float timeSeconds, const uint activeComponentCount)
{
    float2 displacement = float2(0.0F, 0.0F);''',
    '''float ActiveCombinedAmplitude(const uint activeComponentCount)
{
    float amplitude = 0.0F;
    if (activeComponentCount > 0U) amplitude += Wave0.x;
    if (activeComponentCount > 1U) amplitude += Wave1.x;
    if (activeComponentCount > 2U) amplitude += Wave2.x;
    if (activeComponentCount > 3U) amplitude += Wave3.x;
    if (activeComponentCount > 4U) amplitude += Wave4.x;
    if (activeComponentCount > 5U) amplitude += Wave5.x;
    if (activeComponentCount > 6U) amplitude += Wave6.x;
    return amplitude;
}

float CrestSignal(const float verticalDisplacement, const uint activeComponentCount)
{
    const float amplitude = max(ActiveCombinedAmplitude(activeComponentCount), 1.0e-4F);
    return smoothstep(0.30F, 0.82F, verticalDisplacement / amplitude);
}

float2 EvaluateSurfaceDisplacement(const float baseWorldX, const float timeSeconds, const uint activeComponentCount)
{
    float2 displacement = float2(0.0F, 0.0F);''', 1)
shader = shader.replace(
    '''    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;''',
    '''    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    output.crestSignal = surfaceWeight > 0.5F ? CrestSignal(displacement.y, activeComponentCount) : 0.0F;
    return output;''', 1)
shader = shader.replace(
    '''        outputVertices[surfaceVertex].surfaceWeight = 1.0F;
        outputVertices[bottomVertex].position = mul(
            ViewProjection,
            float4(baseWorldX, DeepFillColor.w, 0.0F, 1.0F));
        outputVertices[bottomVertex].surfaceWeight = 0.0F;''',
    '''        outputVertices[surfaceVertex].surfaceWeight = 1.0F;
        outputVertices[surfaceVertex].crestSignal = CrestSignal(displacement.y, activeComponentCount);
        outputVertices[bottomVertex].position = mul(
            ViewProjection,
            float4(baseWorldX, DeepFillColor.w, 0.0F, 1.0F));
        outputVertices[bottomVertex].surfaceWeight = 0.0F;
        outputVertices[bottomVertex].crestSignal = 0.0F;''', 1)
shader = shader.replace(
    '''float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    return float4(lerp(DeepFillColor.rgb, SurfaceTintColor.rgb, narrowSurfaceBand), 1.0F);
}''',
    '''float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    float3 color = lerp(DeepFillColor.rgb, SurfaceTintColor.rgb, narrowSurfaceBand);

    // The integer part remains active component count; W1-K.1 packs normalized crest strength into half
    // of the fractional range. A deterministic sub-pixel breakup avoids a continuous glowing white ribbon.
    const float crestWhiteningStrength = saturate(frac(HorizontalSteepness1.w) * 2.0F);
    const float breakup = 0.66F + 0.34F * sin(input.position.x * 0.173F + ReferenceLevelAndTime.y * 2.1F);
    const float whiten = saturate(input.crestSignal * crestWhiteningStrength * breakup) * narrowSurfaceBand;
    const float3 crestColor = float3(0.82F, 0.86F, 0.88F);
    color = lerp(color, crestColor, whiten);
    return float4(color, 1.0F);
}''', 1)
if shader == read(shader_path):
    raise RuntimeError("Gerstner shader patch produced no change")
write(shader_path, shader)

replace_once(
    "Tests/W1MeshletOceanTest.cpp",
    '''    if (!Require(!BuildGerstnerMeshletDispatchPlan(rough, 0.0F, 2560U).has_value(),
                 "zero camera span must be rejected") ||
        !Require(!BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 0U).has_value(),
                 "zero viewport width must be rejected"))
    {
        return 1;
    }

    std::cout''',
    '''    if (!Require(!BuildGerstnerMeshletDispatchPlan(rough, 0.0F, 2560U).has_value(),
                 "zero camera span must be rejected") ||
        !Require(!BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 0U).has_value(),
                 "zero viewport width must be rejected"))
    {
        return 1;
    }

    GerstnerSurfacePresentationParameters invalidCrest = rough;
    invalidCrest.crestWhiteningStrength = 1.01F;
    if (!Require(!ValidateGerstnerSurfacePresentationParameters(invalidCrest).has_value(),
                 "crest whitening above normalized range must be rejected"))
    {
        return 1;
    }
    rough.crestWhiteningStrength = 0.82F;
    if (!Require(ValidateGerstnerSurfacePresentationParameters(rough).has_value(),
                 "normalized crest whitening must remain a valid renderer-neutral surface snapshot"))
    {
        return 1;
    }

    std::cout''')

print("W1-K.1 crest effects patch applied")
