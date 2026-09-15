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
    float4 HorizontalSteepness0;
    float4 HorizontalSteepness1;
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
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, HorizontalSteepness0.x);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, HorizontalSteepness0.y);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, HorizontalSteepness0.z);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave3, HorizontalSteepness0.w);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave4, HorizontalSteepness1.x);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave5, HorizontalSteepness1.y);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave6, HorizontalSteepness1.z);
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
    return float4(lerp(DeepFillColor.rgb, SurfaceTintColor.rgb, narrowSurfaceBand), 1.0F);
}
