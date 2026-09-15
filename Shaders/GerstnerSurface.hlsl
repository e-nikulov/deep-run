cbuffer GerstnerDrawConstants : register(b0)
{
    column_major float4x4 ViewProjection;
    float4 ReferenceLevelAndTime;
    float4 Wave0;
    float4 Wave1;
    float4 Wave2;
    float4 Wave3;
    float4 Wave4;
    float4 Wave5;
    float4 Wave6;
    uint4 PackedPresentation;
    float4 Disturbance0;
    float4 Disturbance1;
    float4 Disturbance2;
};

struct VSInput
{
    float2 basePosition : POSITION;
    float surfaceWeight : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float surfaceWeight : TEXCOORD0;
};

float UnpackNormalizedByte(const uint packed, const uint shift)
{
    return (float)((packed >> shift) & 0xffU) / 255.0F;
}

float3 UnpackRgb10(const uint packed)
{
    return float3(
        (float)(packed & 0x3ffU),
        (float)((packed >> 10U) & 0x3ffU),
        (float)((packed >> 20U) & 0x3ffU)) / 1023.0F;
}

float2 EvaluateComponent(const float baseX, const float timeSeconds, const float4 wave, const float steepness)
{
    const float waveNumber = 6.28318530718F / wave.y;
    const float theta = waveNumber * baseX - wave.z * timeSeconds + wave.w;
    return float2(steepness * wave.x * cos(theta), wave.x * sin(theta));
}

float EvaluateTransientDisturbance(const float baseX, const float4 disturbance)
{
    if (disturbance.y <= 0.0F || abs(disturbance.z) <= 1.0e-5F)
        return 0.0F;
    const float u = abs(baseX - disturbance.x) / disturbance.y;
    if (u >= 1.0F)
        return 0.0F;
    const float compactEnvelope = pow(saturate(1.0F - u * u), max(disturbance.w, 1.0F));
    return disturbance.z * cos(3.14159265359F * u) * compactEnvelope;
}

VSOutput VSMain(const VSInput input)
{
    const float timeSeconds = ReferenceLevelAndTime.y;
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    const uint activeComponentCount = (PackedPresentation.y >> 24U) & 0xffU;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        if (activeComponentCount > 0U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, UnpackNormalizedByte(PackedPresentation.x, 0U));
        if (activeComponentCount > 1U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, UnpackNormalizedByte(PackedPresentation.x, 8U));
        if (activeComponentCount > 2U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, UnpackNormalizedByte(PackedPresentation.x, 16U));
        if (activeComponentCount > 3U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave3, UnpackNormalizedByte(PackedPresentation.x, 24U));
        if (activeComponentCount > 4U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave4, UnpackNormalizedByte(PackedPresentation.y, 0U));
        if (activeComponentCount > 5U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave5, UnpackNormalizedByte(PackedPresentation.y, 8U));
        if (activeComponentCount > 6U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave6, UnpackNormalizedByte(PackedPresentation.y, 16U));
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance0);
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance1);
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance2);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = baseWorldX + displacement.x * surfaceWeight;
    const float worldY = input.basePosition.y + displacement.y * surfaceWeight;
    VSOutput output;
    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;
}

float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    const float3 deepFillColor = UnpackRgb10(PackedPresentation.z);
    const float3 surfaceTintColor = UnpackRgb10(PackedPresentation.w);
    return float4(lerp(deepFillColor, surfaceTintColor, narrowSurfaceBand), 1.0F);
}
