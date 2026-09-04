Texture2D<float4> SceneColorHDR : register(t0);
SamplerState SceneColorSampler : register(s0);

struct FullscreenPixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FullscreenPixelInput VSMain(const uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0F, -1.0F),
        float2(-1.0F, 3.0F),
        float2(3.0F, -1.0F)};

    FullscreenPixelInput output;
    output.position = float4(positions[vertexId], 0.0F, 1.0F);
    output.uv = float2(positions[vertexId].x * 0.5F + 0.5F, -positions[vertexId].y * 0.5F + 0.5F);
    return output;
}

float3 ToneMapSceneLinear(const float3 sceneLinear)
{
    // M3-A uses fixed neutral exposure (1.0) with a per-channel Reinhard shoulder. This is deterministic,
    // monotonic, and bounded for finite non-negative input. Unlike peak normalization, it retains distinct
    // HDR intensities and does not reduce a channel merely because another becomes brighter. It remains
    // temporary renderer-owned presentation tuning policy.
    const float3 nonNegative = max(sceneLinear, 0.0F.xxx);
    return nonNegative / (1.0F.xxx + nonNegative);
}

float3 LinearToSrgb(const float3 linearColor)
{
    const float3 clamped = saturate(linearColor);
    const float3 low = clamped * 12.92F;
    const float3 high = 1.055F * pow(clamped, 1.0F / 2.4F) - 0.055F;
    return float3(
        clamped.r <= 0.0031308F ? low.r : high.r,
        clamped.g <= 0.0031308F ? low.g : high.g,
        clamped.b <= 0.0031308F ? low.b : high.b);
}

// M3-A.1 fixed presentation policy: scene-linear 1.0 is 80 nits / scRGB 1.0. The 1,000-nit engineering
// ceiling is a deterministic temporary shoulder, not display calibration or HDR10 metadata.
static const float HdrReferenceWhiteNits = 80.0F;
static const float ScRgbNominalWhiteNits = 80.0F;
static const float HdrOutputPeakNits = 1000.0F;

float3 MapSceneLinearToHdrScRgb(const float3 sceneLinear)
{
    const float3 nonNegative = max(sceneLinear, 0.0F.xxx);
    const float referenceWhiteScale = HdrReferenceWhiteNits / ScRgbNominalWhiteNits;
    const float peakScRgb = HdrOutputPeakNits / ScRgbNominalWhiteNits;
    // Maps scene 1.0 exactly to reference white, remains monotonic, and approaches the bounded peak without
    // applying an sRGB transfer. The denominator form remains finite for every finite non-negative float.
    return referenceWhiteScale * peakScRgb *
           (1.0F.xxx - (peakScRgb - 1.0F) / (nonNegative + (peakScRgb - 1.0F)));
}

float4 PSMain(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);
    return float4(LinearToSrgb(ToneMapSceneLinear(scene.rgb)), scene.a);
}

float4 PSHdrScRgb(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);
    return float4(MapSceneLinearToHdrScRgb(scene.rgb), scene.a);
}
