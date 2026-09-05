cbuffer DrawConstants : register(b0)
{
    column_major float4x4 Model;
    float4 NormalRow0;
    float4 NormalRow1;
    float4 NormalRow2;
    column_major float4x4 ViewProjection;
    float4 BaseColor;
    float Metallic;
    float Roughness;
    float2 MaterialPadding;
    float3 LightDirection;
    float Ambient;
};

cbuffer UnderwaterDepthLightingConstants : register(b1)
{
    float SurfaceLevelYMeters;
    float3 AttenuationPerMeterRgb;
    float3 DeepAmbientRgb;
    float UnderwaterLightingPadding;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float worldY : TEXCOORD0;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    const float4 worldPosition = mul(Model, float4(input.position, 1.0F));
    output.position = mul(ViewProjection, worldPosition);
    output.worldY = worldPosition.y;
    output.normal = normalize(float3(
        dot(NormalRow0.xyz, input.normal),
        dot(NormalRow1.xyz, input.normal),
        dot(NormalRow2.xyz, input.normal)));
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float3 normal = normalize(input.normal);
    const float3 toLight = normalize(-LightDirection);
    const float diffuseAmount = saturate(dot(normal, toLight));
    const float directDiffuseScale = (1.0F - Ambient) * diffuseAmount * (1.0F - Metallic * 0.25F);

    const float3 viewDirection = float3(0.0F, 0.0F, 1.0F);
    const float3 halfVector = normalize(toLight + viewDirection);
    const float specularPower = lerp(64.0F, 4.0F, saturate(Roughness));
    const float specularAmount = pow(saturate(dot(normal, halfVector)), specularPower);
    const float3 specularColor = lerp(0.04F.xxx, BaseColor.rgb, saturate(Metallic));

    // M3-C depth lighting uses only actual world Y and the Game-supplied authoritative water surface.
    // It is independent of camera distance and operates in scene-linear space before tone mapping/output.
    const float depthMeters = max(SurfaceLevelYMeters - input.worldY, 0.0F);
    const float3 transmission = exp(-AttenuationPerMeterRgb * depthMeters);
    const float3 surfaceLit = BaseColor.rgb * (Ambient + directDiffuseScale) +
                              specularColor * specularAmount * 0.35F;
    // The deep ambient floor is material-modulated, so black material remains black while deep terrain
    // retains a restrained, wavelength-shifted silhouette without any view-distance fog.
    const float3 deepAmbient = BaseColor.rgb * DeepAmbientRgb * (1.0F - transmission);
    const float3 linearColor = surfaceLit * transmission + deepAmbient;
    return float4(linearColor, BaseColor.a);
}
