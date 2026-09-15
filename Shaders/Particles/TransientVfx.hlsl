cbuffer TransientVfxConstants : register(b0)
{
    column_major float4x4 ViewProjection;
    float4 OriginAge;
    float4 DirectionLifetime;
    float4 VelocityTurbulence;
    float4 ExtentKind;
    float4 SizeOpacityEmissive;
    float4 ColorMedium;
    float4 SurfaceAbsorptionR;
    float4 CameraGravity;
    float4 SeedSpawnWind;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    nointerpolation float4 colorOpacity : COLOR0;
};

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float Random01(uint seed)
{
    return (Hash(seed) & 0x00ffffffu) / 16777216.0F;
}

float2 QuadCorner(uint vertexId)
{
    static const float2 corners[6] = {
        float2(-1.0F, -1.0F), float2(-1.0F, 1.0F), float2(1.0F, 1.0F),
        float2(-1.0F, -1.0F), float2(1.0F, 1.0F), float2(1.0F, -1.0F)};
    return corners[vertexId % 6u];
}

PixelInput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint baseSeed = asuint(SeedSpawnWind.x) ^ (instanceId * 0x9e3779b9u);
    const float r0 = Random01(baseSeed + 0u);
    const float r1 = Random01(baseSeed + 1u);
    const float r2 = Random01(baseSeed + 2u);
    const float r3 = Random01(baseSeed + 3u);
    const float r4 = Random01(baseSeed + 4u);
    const float signed0 = r0 * 2.0F - 1.0F;
    const float signed1 = r1 * 2.0F - 1.0F;

    const uint kind = (uint)round(ExtentKind.w);
    const float lifetime = max(DirectionLifetime.w, 0.001F);
    const float age = min(OriginAge.w, lifetime);
    const float delayedAge = max(age - r4 * min(0.22F, lifetime * 0.30F), 0.0F);
    const float normalizedAge = saturate(delayedAge / lifetime);
    const float3 direction = normalize(DirectionLifetime.xyz);
    float3 side = float3(-direction.y, direction.x, 0.0F);
    if (dot(side, side) < 0.001F)
        side = float3(1.0F, 0.0F, 0.0F);
    side = normalize(side);
    const float3 depthSide = normalize(cross(direction, side));
    const float phase = r2 * 6.28318530718F + delayedAge * (3.0F + VelocityTurbulence.w);
    const float spawnRadius = SeedSpawnWind.y;
    const float3 wind = float3(cos(SeedSpawnWind.w), 0.0F, sin(SeedSpawnWind.w)) * SeedSpawnWind.z;

    float3 center = OriginAge.xyz;
    float sizeMeters = lerp(SizeOpacityEmissive.x, SizeOpacityEmissive.y, r3);
    float alphaScale = 1.0F - normalizedAge;

    if (kind <= 2u)
    {
        center -= direction * (r0 * ExtentKind.x);
        center += side * signed1 * (spawnRadius + ExtentKind.y * (0.15F + 0.85F * r3));
        center += depthSide * signed0 * ExtentKind.z;
        const float rise = lerp(0.35F, 1.85F, r2) * delayedAge;
        center.y += rise;
        center += side * sin(phase) * VelocityTurbulence.w * (0.08F + 0.14F * delayedAge);
        sizeMeters *= 1.0F + normalizedAge * (kind == 2u ? 1.10F : 0.45F);
        alphaScale = saturate(1.0F - normalizedAge * 0.82F);
    }
    else if (kind == 3u)
    {
        const float cone = lerp(0.25F, 1.0F, r2);
        float3 velocity = VelocityTurbulence.xyz;
        velocity += side * signed0 * ExtentKind.x * cone;
        velocity.y += ExtentKind.y * (0.35F + 0.90F * r1);
        velocity += depthSide * signed1 * ExtentKind.z;
        velocity += wind * 0.08F;
        center += velocity * delayedAge;
        center.y -= 0.5F * CameraGravity.z * delayedAge * delayedAge;
        alphaScale = saturate(1.0F - normalizedAge * normalizedAge);
    }
    else if (kind == 4u)
    {
        const float expansion = (0.25F + 0.75F * normalizedAge);
        center += side * signed0 * ExtentKind.x * expansion;
        center.y += signed1 * ExtentKind.y * expansion + VelocityTurbulence.y * delayedAge;
        center += depthSide * (r2 * 2.0F - 1.0F) * ExtentKind.z * expansion;
        center += wind * delayedAge * 0.18F;
        sizeMeters *= 1.0F + 1.8F * normalizedAge;
        alphaScale = saturate((1.0F - normalizedAge) * 0.72F);
    }
    else if (kind == 5u)
    {
        const float angle = r0 * 6.28318530718F;
        const float radial = sqrt(r1);
        center.x += cos(angle) * radial * ExtentKind.x;
        center.z += sin(angle) * radial * ExtentKind.z;
        center.y = SurfaceAbsorptionR.x + 0.025F + signed0 * min(0.12F, ExtentKind.y);
        center += wind * delayedAge * 0.025F;
        sizeMeters *= 1.0F + normalizedAge * 0.75F;
        alphaScale = saturate(0.85F - normalizedAge * 0.65F);
    }
    else if (kind == 6u || kind == 7u)
    {
        const float axial = r0 * ExtentKind.x;
        const float radial = (0.10F + axial / max(ExtentKind.x, 0.001F)) * ExtentKind.y;
        center -= direction * axial;
        center += side * signed0 * radial;
        center += depthSide * signed1 * ExtentKind.z;
        center += side * sin(phase * 1.7F) * VelocityTurbulence.w * 0.08F;
        center += wind * delayedAge * 0.04F;
        sizeMeters *= kind == 6u ? lerp(0.65F, 1.1F, r2) : lerp(0.9F, 1.8F, r2);
        alphaScale = kind == 6u ? saturate(1.0F - normalizedAge * 0.35F)
                                : saturate(0.85F - normalizedAge * 0.60F);
    }
    else if (kind == 8u)
    {
        center += side * signed0 * ExtentKind.x;
        center += direction * (signed1 * ExtentKind.z);
        center.y += ExtentKind.y * (0.20F + r2) - 0.5F * CameraGravity.z * delayedAge * delayedAge;
        center += VelocityTurbulence.xyz * delayedAge * (0.15F + 0.35F * r3);
        sizeMeters *= 1.0F + 2.4F * normalizedAge;
        alphaScale = saturate((1.0F - normalizedAge) * 0.88F);
    }
    else if (kind == 9u)
    {
        // Directional breach crown: an elliptical annulus biased in the missile travel direction, with
        // ballistic rise/collapse. It deliberately avoids a symmetric vertical geyser.
        const float angle = r0 * 6.28318530718F;
        const float radial = lerp(0.55F, 1.0F, r1);
        const float forwardBias = 0.35F + 0.65F * saturate(dot(direction, float3(1.0F, 0.0F, 0.0F)) * 0.5F + 0.5F);
        center += side * cos(angle) * radial * ExtentKind.x;
        center += depthSide * sin(angle) * radial * ExtentKind.z;
        center += direction * (signed1 * ExtentKind.x * 0.35F + ExtentKind.x * forwardBias * normalizedAge * 0.42F);
        center.y = SurfaceAbsorptionR.x + ExtentKind.y * (0.18F + 1.05F * r2) * sin(saturate(normalizedAge) * 3.14159265F);
        center.y -= 0.45F * CameraGravity.z * delayedAge * delayedAge;
        center += wind * delayedAge * 0.08F;
        sizeMeters *= lerp(0.75F, 1.65F, normalizedAge);
        alphaScale = saturate((1.0F - normalizedAge) * 0.92F);
    }
    else
    {
        center -= direction * r0 * ExtentKind.x;
        center += side * signed0 * ExtentKind.y;
        center += depthSide * signed1 * ExtentKind.z;
        center += side * sin(phase) * VelocityTurbulence.w * 0.12F;
        center += VelocityTurbulence.xyz * delayedAge * 0.08F;
        alphaScale = saturate(0.65F - normalizedAge * 0.45F);
    }

    const float2 corner = QuadCorner(vertexId);
    float4 clipCenter = mul(ViewProjection, float4(center, 1.0F));
    const float2 ndcDiameter = float2(
        2.0F * sizeMeters / max(CameraGravity.x, 0.001F),
        2.0F * sizeMeters / max(CameraGravity.y, 0.001F));
    clipCenter.xy += corner * ndcDiameter * 0.5F * clipCenter.w;

    const float depthMeters = max(SurfaceAbsorptionR.x - center.y, 0.0F);
    float3 color = ColorMedium.rgb;
    if (ColorMedium.w > 0.5F)
        color *= exp(-SurfaceAbsorptionR.yzw * depthMeters);
    color *= 1.0F + SizeOpacityEmissive.w;

    PixelInput output;
    output.position = clipCenter;
    output.corner = corner;
    output.worldPosition = center;
    output.colorOpacity = float4(color, SizeOpacityEmissive.z * alphaScale);
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    float radial = saturate(1.0F - dot(input.corner, input.corner));
    const uint kind = (uint)round(ExtentKind.w);
    if (kind == 8u || kind == 9u)
        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.35F + input.corner.y * input.corner.y));
    else if (kind == 3u)
        radial = saturate(1.0F - dot(input.corner, input.corner) * 1.35F);
    const float alpha = input.colorOpacity.a * radial * radial;
    clip(alpha - 0.008F);
    return float4(input.colorOpacity.rgb, alpha);
}
