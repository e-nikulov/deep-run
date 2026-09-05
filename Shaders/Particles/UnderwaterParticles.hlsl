cbuffer ParticleDrawConstants : register(b0)
{
    column_major float4x4 ViewProjection;
    float4 ParticleMotion; // presentation time, quad diameter, lateral amplitude, vertical drift.
    float4 ParticleFieldMinimum; // XYZ bounds, lateral angular frequency.
    float4 ParticleFieldMaximum;
};

// This is the same immutable frame-local b1 payload used by opaque models. It intentionally is not a
// second Game upload or separate water-presentation contract.
cbuffer ScenePresentationConstants : register(b1)
{
    float SurfaceLevelYMeters;
    float3 AttenuationPerMeterRgb;
    float3 DeepAmbientRgb;
    float FogExtinctionPerMeter;
    float3 CameraPlaneCenterWorldPosition;
    float ScenePresentationPadding0;
    float3 CameraViewDirection;
    float ScenePresentationPadding1;
    float3 FogColorRgb;
    float ScenePresentationPadding2;
};

struct VertexInput
{
    float3 baseWorldPosition : POSITION;
    float2 quadCorner : TEXCOORD0;
    float phaseRadians : TEXCOORD1;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float2 quadCorner : TEXCOORD1;
};

PixelInput VSMain(VertexInput input)
{
    const float timeSeconds = ParticleMotion.x;
    const float particleDiameter = ParticleMotion.y;
    const float lateralAmplitude = ParticleMotion.z;
    const float verticalDrift = ParticleMotion.w;
    const float verticalSpan = ParticleFieldMaximum.y - ParticleFieldMinimum.y;
    const float verticalOffset = frac(
        (input.baseWorldPosition.y - ParticleFieldMinimum.y + timeSeconds * verticalDrift) / verticalSpan);
    float3 worldPosition = input.baseWorldPosition;
    worldPosition.x += sin(input.phaseRadians + timeSeconds * ParticleFieldMinimum.w) * lateralAmplitude;
    worldPosition.y = ParticleFieldMinimum.y + verticalOffset * verticalSpan;
    worldPosition.xy += input.quadCorner * (particleDiameter * 0.5F);

    PixelInput output;
    output.position = mul(ViewProjection, float4(worldPosition, 1.0F));
    output.worldPosition = worldPosition;
    output.quadCorner = input.quadCorner;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float radialFalloff = saturate(1.0F - dot(input.quadCorner, input.quadCorner));
    const float baseAlpha = radialFalloff * ParticleFieldMaximum.w;
    clip(baseAlpha - 0.01F);

    // M3-D uses a deliberately smaller lighting policy than opaque models, but reads the same surface,
    // attenuation, camera and fog snapshot so deep/distant specks do not remain bright white.
    const float depthMeters = max(SurfaceLevelYMeters - input.worldPosition.y, 0.0F);
    const float3 depthTransmission = exp(-AttenuationPerMeterRgb * depthMeters);
    const float3 particleSurfaceRgb = float3(0.42F, 0.68F, 0.82F);
    const float3 depthLitRgb = particleSurfaceRgb * depthTransmission + DeepAmbientRgb * 0.25F *
                                   (1.0F - depthTransmission);

    const float3 viewDirection = normalize(CameraViewDirection);
    const float rayT = dot(input.worldPosition - CameraPlaneCenterWorldPosition, viewDirection);
    const float3 rayOrigin = input.worldPosition - viewDirection * max(rayT, 0.0F);
    const bool rayOriginBelow = rayOrigin.y < SurfaceLevelYMeters;
    const bool fragmentBelow = input.worldPosition.y < SurfaceLevelYMeters;
    float submergedPathLength = 0.0F;
    if (rayT > 0.0F && rayOriginBelow && fragmentBelow)
    {
        submergedPathLength = rayT;
    }
    else if (rayT > 0.0F && rayOriginBelow != fragmentBelow)
    {
        const float crossingT = saturate(
            (SurfaceLevelYMeters - rayOrigin.y) / (input.worldPosition.y - rayOrigin.y));
        submergedPathLength = rayOriginBelow ? rayT * crossingT : rayT * (1.0F - crossingT);
    }
    const float fogTransmission = exp(-FogExtinctionPerMeter * submergedPathLength);
    const float3 linearRgb = depthLitRgb * fogTransmission + FogColorRgb * (1.0F - fogTransmission);
    return float4(linearRgb, baseAlpha * fogTransmission);
}
