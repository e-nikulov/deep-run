from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:140]!r}")
    write(path, text.replace(old, new, 1))


# Stage 2 contains the validated gameplay-observer additions; this slice adds reusable renderer support only.
exec(compile(read("Tools/p700_stage2_completion_v2.py"), "p700_stage3_render_base", "exec"))

replace_once(
    "Engine/Render/TransientVfx.h",
    "    RigidDebris,\n};",
    "    RigidDebris,\n    SurfaceReflection,\n};")

# Two bounded scene-color distortion regions fit in the remaining 32 bytes of the existing 256-byte
# scene-presentation CB. LocalLightControl.y/z/w carry distortion count and restrained lens-moisture controls.
replace_once(
    "Engine/Render/ViewPathFog.h",
    "inline constexpr std::size_t ScenePresentationLocalLightCapacity = 2U;\n\nstruct ScenePresentationLocalLight final",
    "inline constexpr std::size_t ScenePresentationLocalLightCapacity = 2U;\ninline constexpr std::size_t ScenePresentationDistortionCapacity = 2U;\n\nstruct ScenePresentationDistortion final\n{\n    std::array<float, 2> viewportCenter{0.5F, 0.5F};\n    float radiusViewport = 0.0F;\n    float strengthViewport = 0.0F;\n};\n\nstruct ScenePresentationLocalLight final")
replace_once(
    "Engine/Render/ViewPathFog.h",
    "    std::array<ScenePresentationLocalLight, ScenePresentationLocalLightCapacity> localLights{};\n    std::uint32_t activeLocalLightCount = 0U;\n};",
    "    std::array<ScenePresentationLocalLight, ScenePresentationLocalLightCapacity> localLights{};\n    std::uint32_t activeLocalLightCount = 0U;\n    std::array<ScenePresentationDistortion, ScenePresentationDistortionCapacity> distortions{};\n    std::uint32_t activeDistortionCount = 0U;\n    float lensMoistureIntensity = 0.0F;\n    float lensMoistureSeed = 0.0F;\n};")

validation_path = "Engine/Render/ViewPathFog.cpp"
validation = read(validation_path)
old_validation_tail = '''    for (std::size_t index = 0U; index < parameters.activeLocalLightCount; ++index)\n    {\n        const auto& light = parameters.localLights[index];\n        if (!IsFiniteVector(light.worldPositionMeters) || !std::isfinite(light.radiusMeters) ||\n            light.radiusMeters <= 0.0F || light.radiusMeters > 80.0F ||\n            !IsFiniteVector(light.linearColor) || !std::isfinite(light.intensity) ||\n            light.intensity < 0.0F || light.intensity > 12.0F)\n            return std::unexpected("scene presentation local light is outside the bounded range");\n        for (const float channel : light.linearColor)\n            if (channel < 0.0F || channel > 6.0F)\n                return std::unexpected("scene presentation local-light color is outside the bounded range");\n    }\n    return {};'''
new_validation_tail = '''    for (std::size_t index = 0U; index < parameters.activeLocalLightCount; ++index)\n    {\n        const auto& light = parameters.localLights[index];\n        if (!IsFiniteVector(light.worldPositionMeters) || !std::isfinite(light.radiusMeters) ||\n            light.radiusMeters <= 0.0F || light.radiusMeters > 80.0F ||\n            !IsFiniteVector(light.linearColor) || !std::isfinite(light.intensity) ||\n            light.intensity < 0.0F || light.intensity > 12.0F)\n            return std::unexpected("scene presentation local light is outside the bounded range");\n        for (const float channel : light.linearColor)\n            if (channel < 0.0F || channel > 6.0F)\n                return std::unexpected("scene presentation local-light color is outside the bounded range");\n    }\n    if (parameters.activeDistortionCount > ScenePresentationDistortionCapacity)\n        return std::unexpected("scene presentation distortion count exceeds the bounded capacity");\n    for (std::size_t index = 0U; index < parameters.activeDistortionCount; ++index)\n    {\n        const auto& distortion = parameters.distortions[index];\n        if (!std::isfinite(distortion.viewportCenter[0]) || !std::isfinite(distortion.viewportCenter[1]) ||\n            distortion.viewportCenter[0] < -0.25F || distortion.viewportCenter[0] > 1.25F ||\n            distortion.viewportCenter[1] < -0.25F || distortion.viewportCenter[1] > 1.25F ||\n            !std::isfinite(distortion.radiusViewport) || distortion.radiusViewport <= 0.0F ||\n            distortion.radiusViewport > 0.35F || !std::isfinite(distortion.strengthViewport) ||\n            std::abs(distortion.strengthViewport) > 0.02F)\n            return std::unexpected("scene presentation distortion is outside the bounded range");\n    }\n    if (!normalized(parameters.lensMoistureIntensity) || !normalized(parameters.lensMoistureSeed))\n        return std::unexpected("scene presentation lens-moisture controls are invalid");\n    return {};'''
if validation.count(old_validation_tail) != 1:
    raise SystemExit("ViewPathFog distortion validation anchor missing")
write(validation_path, validation.replace(old_validation_tail, new_validation_tail, 1))

# Expand the already-aligned 224-byte frame CB to exactly one 256-byte D3D12 constant-buffer allocation.
d3d_path = "Engine/Render/D3D12Renderer.cpp"
d3d = read(d3d_path)
old_constants = '''    std::array<float, 4> localLightColorIntensity1{};\n    std::array<float, 4> localLightControl{};\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 224U);'''
new_constants = '''    std::array<float, 4> localLightColorIntensity1{};\n    std::array<float, 4> localLightControl{};\n    std::array<float, 4> distortion0{};\n    std::array<float, 4> distortion1{};\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 256U);'''
if d3d.count(old_constants) != 1:
    raise SystemExit("D3D scene-presentation constant anchor missing")
d3d = d3d.replace(old_constants, new_constants, 1)
old_init = '''            .localLightColorIntensity1 = {\n                parameters.localLights[1].linearColor[0], parameters.localLights[1].linearColor[1],\n                parameters.localLights[1].linearColor[2], parameters.localLights[1].intensity},\n            .localLightControl = {static_cast<float>(parameters.activeLocalLightCount), 0.0F, 0.0F, 0.0F}};'''
new_init = '''            .localLightColorIntensity1 = {\n                parameters.localLights[1].linearColor[0], parameters.localLights[1].linearColor[1],\n                parameters.localLights[1].linearColor[2], parameters.localLights[1].intensity},\n            .localLightControl = {\n                static_cast<float>(parameters.activeLocalLightCount),\n                static_cast<float>(parameters.activeDistortionCount),\n                parameters.lensMoistureIntensity, parameters.lensMoistureSeed},\n            .distortion0 = {parameters.distortions[0].viewportCenter[0], parameters.distortions[0].viewportCenter[1],\n                            parameters.distortions[0].radiusViewport, parameters.distortions[0].strengthViewport},\n            .distortion1 = {parameters.distortions[1].viewportCenter[0], parameters.distortions[1].viewportCenter[1],\n                            parameters.distortions[1].radiusViewport, parameters.distortions[1].strengthViewport}};'''
if d3d.count(old_init) != 1:
    raise SystemExit("D3D distortion initializer anchor missing")
write(d3d_path, d3d.replace(old_init, new_init, 1))

# Scene-linear screen-space refractive distortion and restrained virtual-lens moisture are applied before both
# SDR tone mapping and HDR scRGB mapping. No bloom/white clipping is baked into the effect.
tone_path = "Shaders/PostProcess/ToneMap.hlsl"
tone = read(tone_path)
replace_cb = '''    float LightningPatternOffset;\n    float ScenePresentationPadding2;\n};'''
cb_new = '''    float LightningPatternOffset;\n    float ScenePresentationPadding2;\n    float4 LocalLightPositionRadius0;\n    float4 LocalLightColorIntensity0;\n    float4 LocalLightPositionRadius1;\n    float4 LocalLightColorIntensity1;\n    float4 LocalLightControl;\n    float4 Distortion0;\n    float4 Distortion1;\n};'''
if tone.count(replace_cb) != 1:
    raise SystemExit("ToneMap CB extension anchor missing")
tone = tone.replace(replace_cb, cb_new, 1)
old_resolve = '''float4 ResolveSceneLinear(const FullscreenPixelInput input)\n{\n    // Tone mapping is 1:1 with SceneColorHDR, so Load avoids filtering the sky sentinel across the waterline.\n    const int2 pixel = int2(input.position.xy);\n    const float4 scene = SceneColorHDR.Load(int3(pixel, 0));\n    if (IsProceduralSkyMarker(scene))\n    {\n        return float4(EvaluateProceduralDaySkySun(input.uv, scene.a), 1.0F);\n    }\n    return float4(scene.rgb, 1.0F);\n}\n\nfloat3 ResolveWeatherPresentation(const float3 sceneLinear, const float2 uv)\n{\n    const float3 precipitated = ApplyPrecipitationPresentation(sceneLinear, uv);\n    return ApplyLightningPresentation(precipitated, uv);\n}'''
new_resolve = r'''float2 ApplySceneDistortion(float2 uv)
{
    float2 offset = float2(0.0F, 0.0F);
    const uint activeCount = min((uint)round(LocalLightControl.y), 2U);
    const float4 distortions[2] = {Distortion0, Distortion1};
    [unroll] for (uint index = 0U; index < 2U; ++index)
    {
        if (index >= activeCount) break;
        const float4 d = distortions[index];
        const float2 delta = uv - d.xy;
        const float distance01 = length(delta) / max(d.z, 1.0e-5F);
        const float envelope = pow(saturate(1.0F - distance01), 2.2F);
        const float2 radial = normalize(delta + float2(1.0e-5F, 0.0F));
        const float phase = PresentationTimeSeconds * (24.0F + 7.0F * index) + distance01 * 18.0F + index * 2.17F;
        offset += radial * (d.w * envelope * sin(phase));
    }

    // WATERLINE_MONEY_SHOT only: three sparse deterministic optical droplets. This remains intentionally mild;
    // it refracts a few pixels and never becomes a persistent GoPro-style overlay.
    const float moisture = saturate(LocalLightControl.z);
    if (moisture > 1.0e-4F)
    {
        const float seed = LocalLightControl.w;
        [unroll] for (uint index = 0U; index < 3U; ++index)
        {
            const float fi = (float)index;
            const float2 center = float2(
                frac(sin((seed + 0.17F + fi * 1.31F) * 37.11F) * 43758.5453F) * 0.72F + 0.14F,
                frac(sin((seed + 0.83F + fi * 2.17F) * 53.73F) * 24634.6345F) * 0.55F + 0.12F);
            const float radius = 0.015F + 0.011F * frac(sin(seed * 19.3F + fi * 7.7F) * 9531.17F);
            const float2 delta = uv - center;
            const float distance01 = length(delta) / max(radius, 1.0e-5F);
            const float lens = pow(saturate(1.0F - distance01), 2.0F);
            offset -= delta * (0.075F * moisture * lens);
        }
    }
    return saturate(uv + offset);
}

float LensMoistureCoverage(const float2 uv)
{
    const float moisture = saturate(LocalLightControl.z);
    if (moisture <= 1.0e-4F) return 0.0F;
    const float seed = LocalLightControl.w;
    float coverage = 0.0F;
    [unroll] for (uint index = 0U; index < 3U; ++index)
    {
        const float fi = (float)index;
        const float2 center = float2(
            frac(sin((seed + 0.17F + fi * 1.31F) * 37.11F) * 43758.5453F) * 0.72F + 0.14F,
            frac(sin((seed + 0.83F + fi * 2.17F) * 53.73F) * 24634.6345F) * 0.55F + 0.12F);
        const float radius = 0.015F + 0.011F * frac(sin(seed * 19.3F + fi * 7.7F) * 9531.17F);
        const float distance01 = length(uv - center) / max(radius, 1.0e-5F);
        const float rim = smoothstep(0.68F, 0.90F, distance01) * (1.0F - smoothstep(0.90F, 1.05F, distance01));
        coverage = max(coverage, rim * moisture);
    }
    return coverage;
}

float4 ResolveSceneLinear(const FullscreenPixelInput input)
{
    uint width = 0U;
    uint height = 0U;
    SceneColorHDR.GetDimensions(width, height);
    const float2 sampleUv = ApplySceneDistortion(input.uv);
    const float2 pixelFloat = sampleUv * float2((float)width, (float)height);
    const int2 pixel = int2(
        clamp((int)pixelFloat.x, 0, (int)max(width, 1U) - 1),
        clamp((int)pixelFloat.y, 0, (int)max(height, 1U) - 1));
    const float4 scene = SceneColorHDR.Load(int3(pixel, 0));
    if (IsProceduralSkyMarker(scene))
        return float4(EvaluateProceduralDaySkySun(sampleUv, scene.a), 1.0F);
    return float4(scene.rgb, 1.0F);
}

float3 ResolveWeatherPresentation(const float3 sceneLinear, const float2 uv)
{
    const float3 precipitated = ApplyPrecipitationPresentation(sceneLinear, uv);
    float3 result = ApplyLightningPresentation(precipitated, uv);
    const float lensRim = LensMoistureCoverage(uv);
    result += float3(0.055F, 0.065F, 0.075F) * lensRim;
    return result;
}'''
if tone.count(old_resolve) != 1:
    raise SystemExit("ToneMap scene distortion anchor missing")
tone = tone.replace(old_resolve, new_resolve, 1)
write(tone_path, tone)

# Let volumetric transient sprites receive one strongest bounded launch light. The generic particle root signature
# remains below D3D12's 64-DWORD limit (52 -> 60 DWORD); Game still owns the semantic light source.
replace_once(
    "Engine/Render/TransientVfx.h",
    '#include "Engine/Render/DepthLighting.h"\n',
    '#include "Engine/Render/DepthLighting.h"\n#include "Engine/Render/ViewPathFog.h"\n')
replace_once(
    "Engine/Render/TransientVfx.h",
    "        const OrthographicCamera& camera,\n        const DepthLightingParameters& depthLighting);",
    "        const OrthographicCamera& camera,\n        const DepthLightingParameters& depthLighting,\n        std::span<const ScenePresentationLocalLight> localLights = {});")

vfx_cpp_path = "Engine/Render/TransientVfx.cpp"
vfx_cpp = read(vfx_cpp_path)
old_root = '''    std::array<float, 4> cameraGravity{};\n    std::array<float, 4> seedSpawnWind{};\n};\nstatic_assert(sizeof(TransientVfxDrawConstants) == 52U * sizeof(std::uint32_t));\nstatic_assert(52U < D3D12_MAX_ROOT_COST);'''
new_root = '''    std::array<float, 4> cameraGravity{};\n    std::array<float, 4> seedSpawnWind{};\n    std::array<float, 4> localLightPositionRadius{};\n    std::array<float, 4> localLightColorIntensity{};\n};\nstatic_assert(sizeof(TransientVfxDrawConstants) == 60U * sizeof(std::uint32_t));\nstatic_assert(60U < D3D12_MAX_ROOT_COST);'''
if vfx_cpp.count(old_root) != 1:
    raise SystemExit("TransientVfx root-constant extension anchor missing")
vfx_cpp = vfx_cpp.replace(old_root, new_root, 1)
old_draw_sig = '''        const std::span<const TransientVfxEmitter> emitters,\n        const OrthographicCamera& camera,\n        const DepthLightingParameters& depthLighting)\n    {'''
new_draw_sig = '''        const std::span<const TransientVfxEmitter> emitters,\n        const OrthographicCamera& camera,\n        const DepthLightingParameters& depthLighting,\n        const std::span<const ScenePresentationLocalLight> localLights)\n    {'''
if vfx_cpp.count(old_draw_sig) != 1:
    raise SystemExit("TransientVfx Impl::Draw signature anchor missing")
vfx_cpp = vfx_cpp.replace(old_draw_sig, new_draw_sig, 1)
validation_anchor = '''        if (const auto valid = ValidateTransientVfxBatch(emitters); !valid)\n            return std::unexpected(valid.error());\n\n        auto* commandList = renderer.CommandList();'''
validation_new = '''        if (const auto valid = ValidateTransientVfxBatch(emitters); !valid)\n            return std::unexpected(valid.error());\n        if (localLights.size() > 1U)\n            return std::unexpected("transient VFX supports one strongest bounded local light");\n        if (!localLights.empty())\n        {\n            const auto& light = localLights.front();\n            if (!Finite3(light.worldPositionMeters) || !Finite3(light.linearColor) ||\n                !std::isfinite(light.radiusMeters) || light.radiusMeters <= 0.0F || light.radiusMeters > 80.0F ||\n                !std::isfinite(light.intensity) || light.intensity < 0.0F || light.intensity > 12.0F)\n                return std::unexpected("transient VFX local light is outside the bounded range");\n        }\n\n        auto* commandList = renderer.CommandList();'''
if vfx_cpp.count(validation_anchor) != 1:
    raise SystemExit("TransientVfx local-light validation anchor missing")
vfx_cpp = vfx_cpp.replace(validation_anchor, validation_new, 1)
old_constants_init = '''                .cameraGravity = {camera.width, camera.height, emitter.gravityMetersPerSecondSquared, 0.0F},\n                .seedSpawnWind = {std::bit_cast<float>(emitter.seed), emitter.spawnRadiusMeters, emitter.windSpeedMetersPerSecond, emitter.windDirectionRadians}};'''
new_constants_init = '''                .cameraGravity = {camera.width, camera.height, emitter.gravityMetersPerSecondSquared, 0.0F},\n                .seedSpawnWind = {std::bit_cast<float>(emitter.seed), emitter.spawnRadiusMeters, emitter.windSpeedMetersPerSecond, emitter.windDirectionRadians},\n                .localLightPositionRadius = localLights.empty()\n                    ? std::array<float, 4>{}\n                    : std::array<float, 4>{localLights.front().worldPositionMeters[0], localLights.front().worldPositionMeters[1],\n                                           localLights.front().worldPositionMeters[2], localLights.front().radiusMeters},\n                .localLightColorIntensity = localLights.empty()\n                    ? std::array<float, 4>{}\n                    : std::array<float, 4>{localLights.front().linearColor[0], localLights.front().linearColor[1],\n                                           localLights.front().linearColor[2], localLights.front().intensity}};'''
if vfx_cpp.count(old_constants_init) != 1:
    raise SystemExit("TransientVfx local-light constants anchor missing")
vfx_cpp = vfx_cpp.replace(old_constants_init, new_constants_init, 1)
old_public_sig = '''    const std::span<const TransientVfxEmitter> emitters,\n    const OrthographicCamera& camera,\n    const DepthLightingParameters& depthLighting)\n{\n    if (impl_ == nullptr)\n        return std::unexpected("transient VFX renderer implementation is unavailable");\n    return impl_->Draw(renderer, emitters, camera, depthLighting);\n}'''
new_public_sig = '''    const std::span<const TransientVfxEmitter> emitters,\n    const OrthographicCamera& camera,\n    const DepthLightingParameters& depthLighting,\n    const std::span<const ScenePresentationLocalLight> localLights)\n{\n    if (impl_ == nullptr)\n        return std::unexpected("transient VFX renderer implementation is unavailable");\n    return impl_->Draw(renderer, emitters, camera, depthLighting, localLights);\n}'''
if vfx_cpp.count(old_public_sig) != 1:
    raise SystemExit("TransientVfx public Draw signature anchor missing")
vfx_cpp = vfx_cpp.replace(old_public_sig, new_public_sig, 1)
write(vfx_cpp_path, vfx_cpp)

particle_path = "Shaders/Particles/TransientVfx.hlsl"
particle = read(particle_path)
replace_particle_cb = '''    float4 CameraGravity;\n    float4 SeedSpawnWind;\n};'''
particle_cb_new = '''    float4 CameraGravity;\n    float4 SeedSpawnWind;\n    float4 LocalLightPositionRadius;\n    float4 LocalLightColorIntensity;\n};'''
if particle.count(replace_particle_cb) != 1:
    raise SystemExit("TransientVfx HLSL local-light CB anchor missing")
particle = particle.replace(replace_particle_cb, particle_cb_new, 1)
# Surface reflection is a thin elongated additive patch anchored to the water level.
reflection_anchor = '''    else if (kind == 14u)\n    {\n        // Lightweight rigid debris presentation: ballistic translation plus visual tumble. It has no collision\n        // or gameplay authority and is intended for small detached panels/covers.\n        float3 velocity = VelocityTurbulence.xyz;\n        velocity += side * signed0 * ExtentKind.y;\n        velocity += depthSide * signed1 * ExtentKind.z;\n        center += velocity * delayedAge;\n        center.y -= 0.5F * CameraGravity.z * delayedAge * delayedAge;\n        sizeMeters *= lerp(0.80F, 1.12F, r2);\n        alphaScale = saturate(1.0F - normalizedAge * normalizedAge);\n    }\n    else'''
reflection_new = '''    else if (kind == 14u)\n    {\n        // Lightweight rigid debris presentation: ballistic translation plus visual tumble. It has no collision\n        // or gameplay authority and is intended for small detached panels/covers.\n        float3 velocity = VelocityTurbulence.xyz;\n        velocity += side * signed0 * ExtentKind.y;\n        velocity += depthSide * signed1 * ExtentKind.z;\n        center += velocity * delayedAge;\n        center.y -= 0.5F * CameraGravity.z * delayedAge * delayedAge;\n        sizeMeters *= lerp(0.80F, 1.12F, r2);\n        alphaScale = saturate(1.0F - normalizedAge * normalizedAge);\n    }\n    else if (kind == 15u)\n    {\n        const float axial = signed0 * ExtentKind.x * 0.5F;\n        center += direction * axial;\n        center += side * signed1 * ExtentKind.y * 0.5F;\n        center += depthSide * (r2 * 2.0F - 1.0F) * ExtentKind.z * 0.15F;\n        center.y = SurfaceAbsorptionR.x + 0.045F;\n        center += wind * delayedAge * 0.012F;\n        sizeMeters *= lerp(0.65F, 1.25F, r3);\n        alphaScale = saturate((1.0F - normalizedAge) * (0.55F + 0.35F * r4));\n    }\n    else'''
if particle.count(reflection_anchor) != 1:
    raise SystemExit("TransientVfx HLSL surface-reflection branch anchor missing")
particle = particle.replace(reflection_anchor, reflection_new, 1)
old_lighting = '''    float3 color = ColorMedium.rgb;\n    if (ColorMedium.w > 0.5F)\n        color *= exp(-SurfaceAbsorptionR.yzw * depthMeters);\n    color *= 1.0F + SizeOpacityEmissive.w;'''
new_lighting = '''    float3 color = ColorMedium.rgb;\n    if (ColorMedium.w > 0.5F)\n        color *= exp(-SurfaceAbsorptionR.yzw * depthMeters);\n    color *= 1.0F + SizeOpacityEmissive.w;\n\n    if (kind != 13u && LocalLightPositionRadius.w > 0.0F && LocalLightColorIntensity.w > 0.0F)\n    {\n        const float3 toLight = LocalLightPositionRadius.xyz - center;\n        const float lightDistance = length(toLight);\n        const bool sameMedium =\n            (center.y - SurfaceAbsorptionR.x) * (LocalLightPositionRadius.y - SurfaceAbsorptionR.x) >= 0.0F;\n        const float radialLight = sameMedium\n            ? pow(saturate(1.0F - lightDistance / LocalLightPositionRadius.w), 2.0F) : 0.0F;\n        float3 lightTransmission = 1.0F.xxx;\n        if (center.y < SurfaceAbsorptionR.x && LocalLightPositionRadius.y < SurfaceAbsorptionR.x)\n            lightTransmission = exp(-SurfaceAbsorptionR.yzw * lightDistance);\n        const float scattering = (kind <= 5u || kind == 8u || kind == 9u || kind == 11u || kind == 12u)\n            ? 0.26F : 0.10F;\n        color += LocalLightColorIntensity.rgb * LocalLightColorIntensity.w * radialLight * lightTransmission * scattering;\n    }'''
if particle.count(old_lighting) != 1:
    raise SystemExit("TransientVfx HLSL sprite-lighting anchor missing")
particle = particle.replace(old_lighting, new_lighting, 1)
old_radial = '''    else if (kind == 14u)\n        radial = saturate(1.0F - max(abs(input.corner.x), abs(input.corner.y) * 1.8F));\n    else if (kind == 3u)'''
new_radial = '''    else if (kind == 14u)\n        radial = saturate(1.0F - max(abs(input.corner.x), abs(input.corner.y) * 1.8F));\n    else if (kind == 15u)\n        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.12F + input.corner.y * input.corner.y * 2.4F));\n    else if (kind == 3u)'''
if particle.count(old_radial) != 1:
    raise SystemExit("TransientVfx HLSL reflection radial anchor missing")
particle = particle.replace(old_radial, new_radial, 1)
write(particle_path, particle)

print("P-700 stage3 reusable render completion patch applied")
