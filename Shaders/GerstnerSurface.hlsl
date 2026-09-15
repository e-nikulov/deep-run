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
    // xyz = steepness for waves 4..6; w = active component count (1..7).
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

float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    return float4(lerp(DeepFillColor.rgb, SurfaceTintColor.rgb, narrowSurfaceBand), 1.0F);
}
