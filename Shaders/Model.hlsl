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

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    const float4 worldPosition = mul(Model, float4(input.position, 1.0F));
    output.position = mul(ViewProjection, worldPosition);
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
    const float diffuseScale = Ambient + (1.0F - Ambient) * diffuseAmount * (1.0F - Metallic * 0.25F);

    const float3 viewDirection = float3(0.0F, 0.0F, 1.0F);
    const float3 halfVector = normalize(toLight + viewDirection);
    const float specularPower = lerp(64.0F, 4.0F, saturate(Roughness));
    const float specularAmount = pow(saturate(dot(normal, halfVector)), specularPower);
    const float3 specularColor = lerp(0.04F.xxx, BaseColor.rgb, saturate(Metallic));

    const float3 linearColor = BaseColor.rgb * diffuseScale + specularColor * specularAmount * 0.35F;
    return float4(linearColor, BaseColor.a);
}
