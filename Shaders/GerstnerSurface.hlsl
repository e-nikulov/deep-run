cbuffer GerstnerDrawConstants : register(b0)
{
    column_major float4x4 ViewProjection;
    float4 ReferenceLevelAndTime;
    float4 Wave0;
    float4 Wave1;
    float4 Wave2;
    float4 HorizontalSteepness;
    float4 DeepFillColor;
    float4 SurfaceTintColor;
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

float2 EvaluateComponent(const float baseX, const float timeSeconds, const float4 wave, const float steepness)
{
    const float waveNumber = 6.28318530718F / wave.y;
    const float theta = waveNumber * baseX - wave.z * timeSeconds + wave.w;
    return float2(steepness * wave.x * cos(theta), wave.x * sin(theta));
}

VSOutput VSMain(const VSInput input)
{
    const float timeSeconds = ReferenceLevelAndTime.y;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave0, HorizontalSteepness.x);
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave1, HorizontalSteepness.y);
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave2, HorizontalSteepness.z);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = input.basePosition.x + displacement.x * surfaceWeight;
    const float worldY = input.basePosition.y + displacement.y * surfaceWeight;
    VSOutput output;
    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;
}

float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    return float4(lerp(DeepFillColor.rgb, SurfaceTintColor.rgb, narrowSurfaceBand), 1.0F);
}
