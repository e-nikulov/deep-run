from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise RuntimeError(f"{path}: anchor count for {old[:40]!r} = {text.count(old)}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")

def replace_between(path, start, end, replacement):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    i = text.find(start)
    if i < 0:
        raise RuntimeError(f"{path}: start marker missing: {start}")
    j = text.find(end, i)
    if j < 0:
        raise RuntimeError(f"{path}: end marker missing: {end}")
    p.write_text(text[:i] + replacement + text[j:], encoding="utf-8")

replace_once("Engine/Render/ViewPathFog.h",
'''    float fogExtinctionPerMeter = 0.0F;
    std::array<float, 3> fogColorRgb{};
};''',
'''    float fogExtinctionPerMeter = 0.0F;
    std::array<float, 3> fogColorRgb{};
    float atmosphereExtinctionPerMeter = 0.0F;
    std::array<float, 3> atmosphereFogColorRgb{0.390F, 0.550F, 0.720F};
    float cloudCoverFraction = 0.0F;
    float precipitationFraction = 0.0F;
    float sunTransmittance = 1.0F;
    float skyLuminanceMultiplier = 1.0F;
    float horizonHazeFraction = 0.0F;
    float cloudAdvection = 0.0F;
    float atmosphereBoundaryViewportY = 0.0F;
    float cloudPatternOffset = 0.0F;
};''')

replace_once("Engine/Render/ViewPathFog.cpp",
'''    for (const float color : parameters.fogColorRgb)
    {
        if (!std::isfinite(color) || color < 0.0F)
        {
            return std::unexpected("scene presentation fog color must be finite and non-negative");
        }
    }
    return {};
}''',
'''    for (const float color : parameters.fogColorRgb)
    {
        if (!std::isfinite(color) || color < 0.0F)
            return std::unexpected("scene presentation fog color must be finite and non-negative");
    }
    if (!std::isfinite(parameters.atmosphereExtinctionPerMeter) ||
        parameters.atmosphereExtinctionPerMeter < 0.0F)
        return std::unexpected("scene presentation atmosphere extinction must be finite and non-negative");
    for (const float color : parameters.atmosphereFogColorRgb)
    {
        if (!std::isfinite(color) || color < 0.0F)
            return std::unexpected("scene presentation atmosphere color must be finite and non-negative");
    }
    const auto normalized = [](const float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (!normalized(parameters.cloudCoverFraction) ||
        !normalized(parameters.precipitationFraction) ||
        !normalized(parameters.sunTransmittance) ||
        !normalized(parameters.skyLuminanceMultiplier) ||
        !normalized(parameters.horizonHazeFraction) ||
        !std::isfinite(parameters.cloudAdvection) ||
        !normalized(parameters.atmosphereBoundaryViewportY) ||
        !normalized(parameters.cloudPatternOffset))
        return std::unexpected("scene presentation atmosphere controls are invalid");
    return {};
}''')

replace_once("Game/PhysicalPlayground.cpp",
'#include "Game/Environment/ScalableEnvironmentPresentation.h"\n',
'#include "Game/Environment/ScalableEnvironmentPresentation.h"\n#include "Game/Environment/WeatherPresentation.h"\n')

scene_block = '''    const auto waterlineViewportY = ProjectWorldSurfaceToViewportY(*camera, water_->Config().surfaceLevelY);
    if (!waterlineViewportY)
        return std::unexpected("physical playground waterline projection failed: " + waterlineViewportY.error());

    WeatherPresentationParameters weatherPresentation{};
    if (freePresentationCameraFraming_)
    {
        if (!weather_.has_value())
            return std::unexpected("physical playground W1-F weather authority is unavailable");
        weatherPresentation = EvaluateWeatherPresentation(*weather_);
        if (!ValidWeatherPresentationParameters(weatherPresentation))
            return std::unexpected("physical playground W1-F weather presentation is invalid");
    }

    const float cameraDepthMeters = (std::max)(water_->Config().surfaceLevelY - camera->position.y, 0.0F);
    const float deepFogBlend = std::clamp(cameraDepthMeters / M3DeepFogReferenceDepthMeters, 0.0F, 1.0F);
    const auto blendChannel = [deepFogBlend](const float shallow, const float deep) noexcept
    { return shallow + (deep - shallow) * deepFogBlend; };
    const std::array<float, 3> depthAwareFogColorRgb{
        blendChannel(M3FogColorRgb[0], M3DeepFogColorRgb[0]),
        blendChannel(M3FogColorRgb[1], M3DeepFogColorRgb[1]),
        blendChannel(M3FogColorRgb[2], M3DeepFogColorRgb[2])};
    const Render::ScenePresentationParameters scenePresentation{
        .depthLighting = {
            .surfaceLevelYMeters = water_->Config().surfaceLevelY,
            .attenuationPerMeterRgb = M3DepthAttenuationPerMeterRgb,
            .deepAmbientRgb = M3DeepAmbientRgb},
        .cameraPlaneCenterWorldPosition = {camera->position.x, camera->position.y, camera->position.z},
        .cameraViewDirection = {camera->viewDirection.x, camera->viewDirection.y, camera->viewDirection.z},
        .fogExtinctionPerMeter = M3FogExtinctionPerMeter,
        .fogColorRgb = depthAwareFogColorRgb,
        .atmosphereExtinctionPerMeter = weatherPresentation.atmosphereExtinctionPerMeter,
        .atmosphereFogColorRgb = weatherPresentation.atmosphereFogColorRgb,
        .cloudCoverFraction = weatherPresentation.cloudCoverFraction,
        .precipitationFraction = weatherPresentation.precipitationFraction,
        .sunTransmittance = weatherPresentation.sunTransmittance,
        .skyLuminanceMultiplier = weatherPresentation.skyLuminanceMultiplier,
        .horizonHazeFraction = weatherPresentation.horizonHazeFraction,
        .cloudAdvection = weatherPresentation.cloudAdvection,
        .atmosphereBoundaryViewportY = freePresentationCameraFraming_
            ? std::clamp(*waterlineViewportY, 0.0F, 1.0F)
            : 0.0F,
        .cloudPatternOffset = weatherPresentation.cloudPatternOffset};
    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)
        return std::unexpected("physical playground scene presentation configuration failed: " + configured.error());

    // W1-F is presentation-only: legacy benchmark framing remains neutral; free gameplay reconstructs
    // cloud/haze/precipitation from the same WeatherState already used by ocean and sensor composition.

'''
replace_between("Game/PhysicalPlayground.cpp",
"    // M3-C/C.1 authority boundary:",
"    if (freePresentationCameraFraming_)\n    {",
scene_block)

gpu_block = '''struct ScenePresentationConstants final
{
    float surfaceLevelYMeters = 0.0F;
    std::array<float, 3> attenuationPerMeterRgb{};
    std::array<float, 3> deepAmbientRgb{};
    float fogExtinctionPerMeter = 0.0F;
    std::array<float, 3> cameraPlaneCenterWorldPosition{};
    float padding0 = 0.0F;
    std::array<float, 3> cameraViewDirection{};
    float padding1 = 0.0F;
    std::array<float, 3> fogColorRgb{};
    float atmosphereExtinctionPerMeter = 0.0F;
    std::array<float, 3> atmosphereFogColorRgb{};
    float cloudCoverFraction = 0.0F;
    float precipitationFraction = 0.0F;
    float sunTransmittance = 1.0F;
    float skyLuminanceMultiplier = 1.0F;
    float horizonHazeFraction = 0.0F;
    float cloudAdvection = 0.0F;
    float atmosphereBoundaryViewportY = 0.0F;
    float presentationTimeSeconds = 0.0F;
    float cloudPatternOffset = 0.0F;
};

static_assert(sizeof(ScenePresentationConstants) == 128U);
constexpr UINT ScenePresentationConstantBufferBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
constexpr UINT ModelRootSignatureDwordCost =
    sizeof(DrawRootConstants) / sizeof(std::uint32_t) + 2U;
static_assert(ModelRootSignatureDwordCost == 58U);
static_assert(ModelRootSignatureDwordCost < D3D12_MAX_ROOT_COST);

'''
replace_between("Engine/Render/D3D12Renderer.cpp",
"struct ScenePresentationConstants final",
"struct ScenePresentationUpload final",
gpu_block)

replace_between("Engine/Render/D3D12Renderer.cpp",
"        const ScenePresentationConstants constants{",
"        std::memcpy(scenePresentationUploads[frameIndex].mapped, &constants, sizeof(constants));",
'''        const ScenePresentationConstants constants{
            .surfaceLevelYMeters = parameters.depthLighting.surfaceLevelYMeters,
            .attenuationPerMeterRgb = parameters.depthLighting.attenuationPerMeterRgb,
            .deepAmbientRgb = parameters.depthLighting.deepAmbientRgb,
            .fogExtinctionPerMeter = parameters.fogExtinctionPerMeter,
            .cameraPlaneCenterWorldPosition = parameters.cameraPlaneCenterWorldPosition,
            .cameraViewDirection = normalizedViewDirection,
            .fogColorRgb = parameters.fogColorRgb,
            .atmosphereExtinctionPerMeter = parameters.atmosphereExtinctionPerMeter,
            .atmosphereFogColorRgb = parameters.atmosphereFogColorRgb,
            .cloudCoverFraction = parameters.cloudCoverFraction,
            .precipitationFraction = parameters.precipitationFraction,
            .sunTransmittance = parameters.sunTransmittance,
            .skyLuminanceMultiplier = parameters.skyLuminanceMultiplier,
            .horizonHazeFraction = parameters.horizonHazeFraction,
            .cloudAdvection = parameters.cloudAdvection,
            .atmosphereBoundaryViewportY = parameters.atmosphereBoundaryViewportY,
            .presentationTimeSeconds = presentationTimeSeconds,
            .cloudPatternOffset = parameters.cloudPatternOffset};
''')

replace_once("Engine/Render/D3D12Renderer.cpp",
'''        D3D12_ROOT_PARAMETER sceneColorParameter{};
        sceneColorParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sceneColorParameter.DescriptorTable.NumDescriptorRanges = 1;
        sceneColorParameter.DescriptorTable.pDescriptorRanges = &sceneColorRange;
        sceneColorParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;''',
'''        std::array<D3D12_ROOT_PARAMETER, 2> rootParameters{};
        D3D12_ROOT_PARAMETER& sceneColorParameter = rootParameters[0];
        sceneColorParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sceneColorParameter.DescriptorTable.NumDescriptorRanges = 1;
        sceneColorParameter.DescriptorTable.pDescriptorRanges = &sceneColorRange;
        sceneColorParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER& scenePresentationParameter = rootParameters[1];
        scenePresentationParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        scenePresentationParameter.Descriptor.ShaderRegister = 1;
        scenePresentationParameter.Descriptor.RegisterSpace = 0;
        scenePresentationParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;''')

replace_once("Engine/Render/D3D12Renderer.cpp",
'''        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &sceneColorParameter;''',
'''        rootDescription.NumParameters = static_cast<UINT>(rootParameters.size());
        rootDescription.pParameters = rootParameters.data();''')

replace_once("Engine/Render/D3D12Renderer.cpp",
'''        commandList->SetGraphicsRootDescriptorTable(0, sceneColorSrvGpuHandle);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);''',
'''        commandList->SetGraphicsRootDescriptorTable(0, sceneColorSrvGpuHandle);
        commandList->SetGraphicsRootConstantBufferView(
            1, scenePresentationUploads[frameIndex].resource->GetGPUVirtualAddress());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);''')

cbuffer_tail = '''    float3 FogColorRgb;
    float AtmosphereExtinctionPerMeter;
    float3 AtmosphereFogColorRgb;
    float CloudCoverFraction;
    float PrecipitationFraction;
    float SunTransmittance;
    float SkyLuminanceMultiplier;
    float HorizonHazeFraction;
    float CloudAdvection;
    float AtmosphereBoundaryViewportY;
    float PresentationTimeSeconds;
    float CloudPatternOffset;
};'''
for shader in ["Shaders/Model.hlsl", "Shaders/Particles/UnderwaterParticles.hlsl"]:
    replace_once(shader,
'''    float3 FogColorRgb;
    float ScenePresentationPadding2;
};''', cbuffer_tail)

replace_once("Shaders/Model.hlsl",
'''    const float fogTransmission = exp(-FogExtinctionPerMeter * submergedPathLength);
    const float3 linearColor = depthLitColor * fogTransmission + FogColorRgb * (1.0F - fogTransmission);
    return float4(linearColor, BaseColor.a);''',
'''    const float fogTransmission = exp(-FogExtinctionPerMeter * submergedPathLength);
    const float3 waterVeiledColor = depthLitColor * fogTransmission + FogColorRgb * (1.0F - fogTransmission);

    const bool rayOriginAbove = rayOrigin.y >= SurfaceLevelYMeters;
    const bool fragmentAbove = input.worldPosition.y >= SurfaceLevelYMeters;
    float atmosphericPathLength = 0.0F;
    if (rayT > 0.0F && rayOriginAbove && fragmentAbove)
        atmosphericPathLength = rayT;
    else if (rayT > 0.0F && rayOriginAbove != fragmentAbove)
    {
        const float crossingT = saturate(
            (SurfaceLevelYMeters - rayOrigin.y) / (input.worldPosition.y - rayOrigin.y));
        atmosphericPathLength = rayOriginAbove ? rayT * crossingT : rayT * (1.0F - crossingT);
    }
    const float atmosphereTransmission = exp(-AtmosphereExtinctionPerMeter * atmosphericPathLength);
    const float3 linearColor =
        waterVeiledColor * atmosphereTransmission + AtmosphereFogColorRgb * (1.0F - atmosphereTransmission);
    return float4(linearColor, BaseColor.a);''')

replace_once("Shaders/PostProcess/ToneMap.hlsl",
"SamplerState SceneColorSampler : register(s0);\n",
'''SamplerState SceneColorSampler : register(s0);

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
    float AtmosphereExtinctionPerMeter;
    float3 AtmosphereFogColorRgb;
    float CloudCoverFraction;
    float PrecipitationFraction;
    float SunTransmittance;
    float SkyLuminanceMultiplier;
    float HorizonHazeFraction;
    float CloudAdvection;
    float AtmosphereBoundaryViewportY;
    float PresentationTimeSeconds;
    float CloudPatternOffset;
};
''')

weather_sky = r'''float HashAtmosphere21(const float2 p)
{
    return frac(sin(dot(p, float2(127.1F, 311.7F))) * 43758.5453123F);
}

float AtmosphereValueNoise(const float2 p)
{
    const float2 c = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0F - 2.0F * f);
    return lerp(
        lerp(HashAtmosphere21(c), HashAtmosphere21(c + float2(1, 0)), u.x),
        lerp(HashAtmosphere21(c + float2(0, 1)), HashAtmosphere21(c + float2(1, 1)), u.x), u.y);
}

float AtmosphereFbm(float2 p)
{
    float sum = 0.0F;
    float amplitude = 0.55F;
    [unroll] for (int octave = 0; octave < 4; ++octave)
    {
        sum += AtmosphereValueNoise(p) * amplitude;
        p = p * 2.03F + float2(17.1F, 9.2F);
        amplitude *= 0.48F;
    }
    return sum;
}

float3 EvaluateProceduralDaySkySun(const float2 uv, const float skyBottom)
{
    const float safeSkyBottom = max(skyBottom, 1.0e-4F);
    const float skyT = saturate(uv.y / safeSkyBottom);
    const float smoothSkyT = skyT * skyT * (3.0F - 2.0F * skyT);
    const float3 zenithColor = float3(0.030F, 0.120F, 0.380F);
    const float3 midSkyColor = float3(0.095F, 0.285F, 0.610F);
    const float3 horizonColor = float3(0.260F, 0.470F, 0.700F);
    float3 sky = lerp(zenithColor, midSkyColor, smoothstep(0.0F, 0.72F, smoothSkyT));
    const float horizonBlend = smoothstep(0.62F, 1.0F, skyT);
    sky = lerp(sky, horizonColor, horizonBlend * horizonBlend);
    const float horizonHaze = pow(saturate((skyT - 0.72F) / 0.28F), 2.0F);
    sky = lerp(sky, AtmosphereFogColorRgb, HorizonHazeFraction * horizonHaze);
    sky *= SkyLuminanceMultiplier;

    if (CloudCoverFraction > 1.0e-4F)
    {
        const float drift = PresentationTimeSeconds * (0.006F + abs(CloudAdvection) * 0.010F);
        float2 cloudUv = float2(uv.x * 5.4F, skyT * 2.7F) +
            float2(CloudPatternOffset * 31.7F, CloudPatternOffset * 17.3F);
        cloudUv.x += drift * (CloudAdvection >= 0.0F ? 1.0F : -1.0F);
        const float density = AtmosphereFbm(cloudUv);
        const float threshold = lerp(0.86F, 0.36F, CloudCoverFraction);
        const float cloud = smoothstep(threshold, threshold + 0.18F, density);
        const float lowerDeck = smoothstep(0.50F, 1.0F, skyT) * CloudCoverFraction;
        const float cloudMass = saturate(max(cloud, lowerDeck * 0.42F));
        const float3 brightCloud = float3(0.88F, 0.90F, 0.92F) * (0.72F + 0.28F * (1.0F - skyT));
        const float3 stormCloud = float3(0.25F, 0.29F, 0.33F);
        const float stormTint = saturate(PrecipitationFraction * 0.72F + CloudCoverFraction * 0.20F);
        sky = lerp(sky, lerp(brightCloud, stormCloud, stormTint),
                   cloudMass * (0.35F + 0.55F * CloudCoverFraction));
    }

    const float aspectRatio =
        max(abs(ddy(uv.y)), 1.0e-7F) / max(abs(ddx(uv.x)), 1.0e-7F);
    const float radiusY = min(0.018F, safeSkyBottom * 0.15F);
    const float2 sunCenter = float2(0.70F, safeSkyBottom * 0.38F);
    const float2 sunDelta = float2((uv.x - sunCenter.x) * aspectRatio, uv.y - sunCenter.y) /
                            max(radiusY, 1.0e-5F);
    const float radialDistance = length(sunDelta);
    const float edgeAa = max(fwidth(radialDistance) * 0.85F, 0.0015F);
    const float diskCoverage = 1.0F - smoothstep(1.0F - edgeAa, 1.0F + edgeAa, radialDistance);
    const float interior = saturate(1.0F - radialDistance);
    const float3 diskRadiance =
        lerp(float3(11.0F, 7.2F, 3.6F), float3(30.0F, 27.0F, 20.0F),
             pow(interior, 0.32F)) * SunTransmittance;
    const float outsideDisk = max(radialDistance - 1.0F, 0.0F);
    const float corona = exp2(-outsideDisk * 1.55F) * (1.0F - diskCoverage) *
                         saturate(1.0F - outsideDisk / 7.0F);
    const float broadGlow = exp2(-radialDistance * radialDistance * 0.22F);
    sky += float3(1.00F, 0.72F, 0.38F) *
           (0.75F * corona + 0.28F * broadGlow) * SunTransmittance;
    const float solarPath =
        exp2(-abs((uv.x - sunCenter.x) * aspectRatio) / max(radiusY, 1.0e-5F) * 0.45F) * horizonHaze;
    sky += float3(0.35F, 0.22F, 0.10F) * (0.16F * solarPath * SunTransmittance);
    return sky + diskRadiance * diskCoverage;
}

float3 ApplyPrecipitationPresentation(const float3 sceneLinear, const float2 uv)
{
    if (PrecipitationFraction <= 1.0e-4F ||
        AtmosphereBoundaryViewportY <= 1.0e-4F ||
        uv.y > AtmosphereBoundaryViewportY)
        return sceneLinear;

    const float normalizedY = uv.y / max(AtmosphereBoundaryViewportY, 1.0e-4F);
    const float2 rainUv = float2(
        uv.x * 150.0F + normalizedY * CloudAdvection * 27.0F,
        normalizedY * 95.0F + PresentationTimeSeconds * (17.0F + 19.0F * PrecipitationFraction));
    const float2 cell = floor(rainUv);
    const float2 local = frac(rainUv);
    const float random = HashAtmosphere21(cell + CloudPatternOffset * 97.0F);
    const float streak = smoothstep(0.045F, 0.0F, abs(local.x - random)) *
        smoothstep(0.92F, 0.18F, local.y) *
        step(0.42F - 0.30F * PrecipitationFraction, random);
    const float mist = PrecipitationFraction * (0.035F + 0.075F * normalizedY);
    float3 result = lerp(sceneLinear, AtmosphereFogColorRgb, mist);
    result += float3(0.32F, 0.37F, 0.42F) * streak * (0.16F * PrecipitationFraction);
    return result;
}

'''
replace_between("Shaders/PostProcess/ToneMap.hlsl",
"float3 EvaluateProceduralDaySkySun",
"float4 ResolveSceneLinear",
weather_sky)

for entry in ["PSMain(FullscreenPixelInput input)", "PSMainSdr10(FullscreenPixelInput input)",
              "PSHdrScRgb(FullscreenPixelInput input)"]:
    start = f"float4 {entry} : SV_TARGET\n{{\n    const float4 scene = ResolveSceneLinear(input);\n"
    if "Hdr" in entry:
        old = start + "    const float3 scRgb = MapSceneLinearToHdrScRgb(scene.rgb);"
        new = start + "    const float3 weatherPresented = ApplyPrecipitationPresentation(scene.rgb, input.uv);\n    const float3 scRgb = MapSceneLinearToHdrScRgb(weatherPresented);"
    else:
        old = start + "    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(scene.rgb));"
        new = start + "    const float3 weatherPresented = ApplyPrecipitationPresentation(scene.rgb, input.uv);\n    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(weatherPresented));"
    replace_once("Shaders/PostProcess/ToneMap.hlsl", old, new)

replace_once("Tests/M4EnvironmentChecks.h",
'#include "Tests/W1WeatherSensorCouplingChecks.h"\n',
'#include "Tests/W1WeatherSensorCouplingChecks.h"\n#include "Tests/W1WeatherPresentationChecks.h"\n')
replace_once("Tests/M4EnvironmentChecks.h",
'           RunW1SurfaceImpactChecks() && RunW1WeatherSensorCouplingChecks();',
'           RunW1SurfaceImpactChecks() && RunW1WeatherSensorCouplingChecks() &&\n'
'           RunW1WeatherPresentationChecks();')

print("W1-F patch applied")
