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
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPosition : TEXCOORD0;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    const float4 worldPosition = mul(Model, float4(input.position, 1.0F));
    output.position = mul(ViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
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
    const float depthMeters = max(SurfaceLevelYMeters - input.worldPosition.y, 0.0F);
    const float3 transmission = exp(-AttenuationPerMeterRgb * depthMeters);
    const float3 surfaceLit = BaseColor.rgb * (Ambient + directDiffuseScale) +
                              specularColor * specularAmount * 0.35F;
    // The deep ambient floor is material-modulated, so black material remains black while deep terrain
    // retains a restrained, wavelength-shifted silhouette without any view-distance fog.
    const float3 deepAmbient = BaseColor.rgb * DeepAmbientRgb * (1.0F - transmission);
    const float3 depthLitColor = surfaceLit * transmission + deepAmbient;

    // M3-C.1 view-path extinction is distinct from M3-C depth lighting. Reconstruct the orthographic ray
    // origin R on the camera plane, then use only R -> fragment below the Game-derived surface plane.
    const float3 orthographicViewDirection = normalize(CameraViewDirection);
    const float rayT = dot(input.worldPosition - CameraPlaneCenterWorldPosition, orthographicViewDirection);
    const float3 rayOrigin = input.worldPosition - orthographicViewDirection * max(rayT, 0.0F);
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
    const float3 linearColor = depthLitColor * fogTransmission + FogColorRgb * (1.0F - fogTransmission);
    return float4(linearColor, BaseColor.a);
}
