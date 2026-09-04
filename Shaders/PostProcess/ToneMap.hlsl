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
    // M3-A's fixed neutral operator preserves the M2 range unchanged and scales only HDR colours whose
    // largest component exceeds display white. It is deterministic and deliberately replaceable by later
    // renderer-owned exposure or artistic controls.
    const float3 nonNegative = max(sceneLinear, 0.0F.xxx);
    const float peak = max(max(nonNegative.r, nonNegative.g), nonNegative.b);
    return nonNegative / max(1.0F, peak);
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

float4 PSMain(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);
    return float4(LinearToSrgb(ToneMapSceneLinear(scene.rgb)), scene.a);
}
