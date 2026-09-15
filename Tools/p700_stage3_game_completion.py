from pathlib import Path
import re


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


exec(compile(read("Tools/p700_stage3_render_completion.py"), "p700_stage3_game_base", "exec"))

# Heat/density distortion and booster reflection remain data-driven P-700 art parameters.
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float gasCavityOpacity = 0.40F;\n\n    float underwaterCoreLengthMeters = 1.8F;",
    "    float gasCavityOpacity = 0.40F;\n    float underwaterDistortionStrengthViewport = 0.00085F;\n\n    float underwaterCoreLengthMeters = 1.8F;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float airbornePlumeLifetimeSeconds = 0.9F;\n\n    std::uint32_t cruiseExhaustCount = 48U;",
    "    float airbornePlumeLifetimeSeconds = 0.9F;\n    float boosterHeatDistortionStrengthViewport = 0.0038F;\n    std::uint32_t boosterSurfaceReflectionCount = 18U;\n    float boosterSurfaceReflectionLifetimeSeconds = 0.62F;\n    float boosterSurfaceReflectionLengthMeters = 14.0F;\n    float boosterSurfaceReflectionWidthMeters = 3.4F;\n    float boosterSurfaceReflectionMaximumAltitudeMeters = 58.0F;\n    float boosterSurfaceReflectionOpacity = 0.24F;\n\n    std::uint32_t cruiseExhaustCount = 48U;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float cruiseExhaustLifetimeSeconds = 0.55F;\n\n    float condensationHumidityThreshold = 0.82F;",
    "    float cruiseExhaustLifetimeSeconds = 0.55F;\n    float cruiseHeatDistortionStrengthViewport = 0.00135F;\n    float heatDistortionRadiusMeters = 6.5F;\n\n    float condensationHumidityThreshold = 0.82F;")

cpp_path = "Game/Weapons/P700LaunchVfx.cpp"
cpp = read(cpp_path)
cpp = cpp.replace(
    "        t.underwaterCoreParticleCount == 0U || t.gasCavityCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||",
    "        t.underwaterCoreParticleCount == 0U || t.gasCavityCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||", 1)
cpp = cpp.replace(
    "        t.airbornePlumeCount == 0U || t.cruiseExhaustCount == 0U || t.maximumTrackedLaunches < 2U)",
    "        t.airbornePlumeCount == 0U || t.boosterSurfaceReflectionCount == 0U ||\n        t.cruiseExhaustCount == 0U || t.maximumTrackedLaunches < 2U)", 1)
cpp = cpp.replace("const std::array<float, 38> scalars{", "const std::array<float, 44> scalars{", 1)
cpp = cpp.replace(
    "        t.airborneEmissiveIntensity, t.airbornePlumeLifetimeSeconds, t.cruiseExhaustLengthMeters,",
    "        t.airborneEmissiveIntensity, t.airbornePlumeLifetimeSeconds,\n        t.boosterSurfaceReflectionLifetimeSeconds, t.boosterSurfaceReflectionLengthMeters,\n        t.boosterSurfaceReflectionWidthMeters, t.boosterSurfaceReflectionMaximumAltitudeMeters,\n        t.cruiseExhaustLengthMeters,", 1)
cpp = cpp.replace(
    "        t.intakeCoverDebrisLifetimeSeconds, t.intakeCoverDebrisSizeMeters};",
    "        t.intakeCoverDebrisLifetimeSeconds, t.intakeCoverDebrisSizeMeters, t.heatDistortionRadiusMeters};", 1)
cpp = cpp.replace(
    "    if (!std::isfinite(t.gasCavityOpacity) || t.gasCavityOpacity < 0.0F || t.gasCavityOpacity > 1.0F ||",
    "    if (!std::isfinite(t.underwaterDistortionStrengthViewport) || t.underwaterDistortionStrengthViewport < 0.0F ||\n        t.underwaterDistortionStrengthViewport > 0.02F ||\n        !std::isfinite(t.boosterHeatDistortionStrengthViewport) || t.boosterHeatDistortionStrengthViewport < 0.0F ||\n        t.boosterHeatDistortionStrengthViewport > 0.02F ||\n        !std::isfinite(t.cruiseHeatDistortionStrengthViewport) || t.cruiseHeatDistortionStrengthViewport < 0.0F ||\n        t.cruiseHeatDistortionStrengthViewport > 0.02F ||\n        !std::isfinite(t.boosterSurfaceReflectionOpacity) || t.boosterSurfaceReflectionOpacity < 0.0F ||\n        t.boosterSurfaceReflectionOpacity > 1.0F ||\n        !std::isfinite(t.gasCavityOpacity) || t.gasCavityOpacity < 0.0F || t.gasCavityOpacity > 1.0F ||", 1)
# Loader.
cpp = cpp.replace(
    "        ReadIfPresent(root, \"gasCavityOpacity\", t.gasCavityOpacity);\n        ReadIfPresent(root, \"underwaterCoreLengthMeters\", t.underwaterCoreLengthMeters);",
    "        ReadIfPresent(root, \"gasCavityOpacity\", t.gasCavityOpacity);\n        ReadIfPresent(root, \"underwaterDistortionStrengthViewport\", t.underwaterDistortionStrengthViewport);\n        ReadIfPresent(root, \"underwaterCoreLengthMeters\", t.underwaterCoreLengthMeters);", 1)
cpp = cpp.replace(
    "        ReadIfPresent(root, \"airbornePlumeLifetimeSeconds\", t.airbornePlumeLifetimeSeconds);\n        ReadIfPresent(root, \"cruiseExhaustCount\", t.cruiseExhaustCount);",
    "        ReadIfPresent(root, \"airbornePlumeLifetimeSeconds\", t.airbornePlumeLifetimeSeconds);\n        ReadIfPresent(root, \"boosterHeatDistortionStrengthViewport\", t.boosterHeatDistortionStrengthViewport);\n        ReadIfPresent(root, \"boosterSurfaceReflectionCount\", t.boosterSurfaceReflectionCount);\n        ReadIfPresent(root, \"boosterSurfaceReflectionLifetimeSeconds\", t.boosterSurfaceReflectionLifetimeSeconds);\n        ReadIfPresent(root, \"boosterSurfaceReflectionLengthMeters\", t.boosterSurfaceReflectionLengthMeters);\n        ReadIfPresent(root, \"boosterSurfaceReflectionWidthMeters\", t.boosterSurfaceReflectionWidthMeters);\n        ReadIfPresent(root, \"boosterSurfaceReflectionMaximumAltitudeMeters\", t.boosterSurfaceReflectionMaximumAltitudeMeters);\n        ReadIfPresent(root, \"boosterSurfaceReflectionOpacity\", t.boosterSurfaceReflectionOpacity);\n        ReadIfPresent(root, \"cruiseExhaustCount\", t.cruiseExhaustCount);", 1)
cpp = cpp.replace(
    "        ReadIfPresent(root, \"cruiseExhaustLifetimeSeconds\", t.cruiseExhaustLifetimeSeconds);\n        ReadIfPresent(root, \"condensationHumidityThreshold\", t.condensationHumidityThreshold);",
    "        ReadIfPresent(root, \"cruiseExhaustLifetimeSeconds\", t.cruiseExhaustLifetimeSeconds);\n        ReadIfPresent(root, \"cruiseHeatDistortionStrengthViewport\", t.cruiseHeatDistortionStrengthViewport);\n        ReadIfPresent(root, \"heatDistortionRadiusMeters\", t.heatDistortionRadiusMeters);\n        ReadIfPresent(root, \"condensationHumidityThreshold\", t.condensationHumidityThreshold);", 1)
# Add booster reflection after the turbulent plume. It is surface-local and fades with altitude/daylight.
reflection_anchor = '''            plume.linearColor = {1.08F, 0.58F, 0.30F};\n            append(plume);\n        }\n        else if (missile.mainEngineActive &&'''
reflection_block = '''            plume.linearColor = {1.08F, 0.58F, 0.30F};\n            append(plume);\n\n            const float boosterAltitudeMeters = missile.positionMeters.y - missile.surfaceLevelYMeters;\n            if (boosterAltitudeMeters >= 0.0F &&\n                boosterAltitudeMeters <= tuning_.boosterSurfaceReflectionMaximumAltitudeMeters &&\n                lod != P700VfxLod::Lod3Strategic)\n            {\n                auto reflection = emitterBase(Render::TransientVfxPrimitive::SurfaceReflection,\n                    Render::TransientVfxBlendMode::Additive,\n                    ScaledCount(tuning_.boosterSurfaceReflectionCount, lodScale, salvoScale),\n                    std::fmod(phaseAge, tuning_.boosterSurfaceReflectionLifetimeSeconds * 0.9F),\n                    tuning_.boosterSurfaceReflectionLifetimeSeconds);\n                reflection.originWorldMeters = {position[0], missile.surfaceLevelYMeters + 0.045F, position[2]};\n                reflection.directionWorldUnit = direction;\n                reflection.extentMeters = {tuning_.boosterSurfaceReflectionLengthMeters,\n                                           tuning_.boosterSurfaceReflectionWidthMeters,\n                                           tuning_.boosterSurfaceReflectionWidthMeters * 0.5F};\n                reflection.minimumSizeMeters = 0.28F;\n                reflection.maximumSizeMeters = 0.95F;\n                const float altitudeFade = 1.0F - boosterAltitudeMeters /\n                    tuning_.boosterSurfaceReflectionMaximumAltitudeMeters;\n                const float nightGain = 1.0F + 0.65F * (1.0F - environment.daylightFraction);\n                reflection.opacity = std::clamp(\n                    tuning_.boosterSurfaceReflectionOpacity * (0.35F + 0.65F * altitudeFade) * nightGain,\n                    0.0F, 0.60F);\n                reflection.emissiveIntensity = 0.85F;\n                reflection.linearColor = {2.35F, 0.92F, 0.24F};\n                append(reflection);\n            }\n        }\n        else if (missile.mainEngineActive &&'''
if cpp.count(reflection_anchor) != 1:
    raise SystemExit("P700 booster surface-reflection anchor missing")
cpp = cpp.replace(reflection_anchor, reflection_block, 1)
write(cpp_path, cpp)

config_path = "Config/p700_vfx.json"
config = read(config_path)
config = config.replace(
    '  "gasCavityOpacity": 0.40,\n\n  "underwaterCoreLengthMeters": 1.8,',
    '  "gasCavityOpacity": 0.40,\n  "underwaterDistortionStrengthViewport": 0.00085,\n\n  "underwaterCoreLengthMeters": 1.8,', 1)
config = config.replace(
    '  "airbornePlumeLifetimeSeconds": 0.9,\n\n  "cruiseExhaustCount": 48,',
    '  "airbornePlumeLifetimeSeconds": 0.9,\n  "boosterHeatDistortionStrengthViewport": 0.0038,\n  "boosterSurfaceReflectionCount": 18,\n  "boosterSurfaceReflectionLifetimeSeconds": 0.62,\n  "boosterSurfaceReflectionLengthMeters": 14.0,\n  "boosterSurfaceReflectionWidthMeters": 3.4,\n  "boosterSurfaceReflectionMaximumAltitudeMeters": 58.0,\n  "boosterSurfaceReflectionOpacity": 0.24,\n\n  "cruiseExhaustCount": 48,', 1)
config = config.replace(
    '  "cruiseExhaustLifetimeSeconds": 0.55,\n\n  "condensationHumidityThreshold": 0.82,',
    '  "cruiseExhaustLifetimeSeconds": 0.55,\n  "cruiseHeatDistortionStrengthViewport": 0.00135,\n  "heatDistortionRadiusMeters": 6.5,\n\n  "condensationHumidityThreshold": 0.82,', 1)
write(config_path, config)

# Pure presentation adapter: world-space missile tail -> bounded screen-space heat/density distortion.
Path("Game/Weapons/P700LaunchDistortionPresentation.h").write_text(r'''#pragma once

#include "Engine/Render/Camera.h"
#include "Engine/Render/ViewPathFog.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Armament
{
[[nodiscard]] inline std::expected<std::vector<Render::ScenePresentationDistortion>, std::string>
BuildP700LaunchDistortions(
    const std::span<const Weapons::P700GranitRuntimeState* const> missiles,
    const Render::OrthographicCamera& camera,
    const P700LaunchVfxTuning& tuning,
    const double simulationTimeSeconds)
{
    if (!Render::IsFinite(camera.viewProjection) || !std::isfinite(camera.width) || camera.width <= 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        return std::unexpected("P-700 distortion presentation received invalid camera/time");

    struct Candidate final { Render::ScenePresentationDistortion distortion{}; float priority = 0.0F; };
    std::vector<Candidate> candidates;
    candidates.reserve(missiles.size());
    for (const Weapons::P700GranitRuntimeState* missile : missiles)
    {
        if (missile == nullptr) continue;
        if (!missile->positionMeters.IsFinite() || !missile->launchForwardUnitVector.IsFinite() ||
            !std::isfinite(missile->headingRadians) || !std::isfinite(missile->phaseStartTimeSeconds) ||
            simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 distortion presentation observed invalid missile state");

        float strength = 0.0F;
        if (missile->phase == Weapons::P700GranitPhase::UnderwaterLaunch && missile->launchBoosterActive)
            strength = tuning.underwaterDistortionStrengthViewport;
        else if (missile->phase == Weapons::P700GranitPhase::WaterExit && missile->launchBoosterActive)
            strength = tuning.boosterHeatDistortionStrengthViewport;
        else if (missile->mainEngineActive &&
                 (missile->phase == Weapons::P700GranitPhase::PostExitTransition ||
                  missile->phase == Weapons::P700GranitPhase::AirborneDeploying ||
                  missile->phase == Weapons::P700GranitPhase::Cruise ||
                  missile->phase == Weapons::P700GranitPhase::Terminal))
            strength = tuning.cruiseHeatDistortionStrengthViewport;
        if (!(strength > 0.0F)) continue;

        const bool launchDirection = missile->phase <= Weapons::P700GranitPhase::WaterExit;
        float dx = launchDirection ? missile->launchForwardUnitVector.x : std::cos(missile->headingRadians);
        float dy = launchDirection ? missile->launchForwardUnitVector.y : std::sin(missile->headingRadians);
        float dz = launchDirection ? missile->launchForwardUnitVector.z : 0.0F;
        const float length = std::hypot(dx, dy, dz);
        if (!std::isfinite(length) || length <= 1.0e-5F) continue;
        dx /= length; dy /= length; dz /= length;
        const Assets::ModelVector3 tail{
            missile->positionMeters.x - dx * 4.75F,
            missile->positionMeters.y - dy * 4.75F,
            missile->positionMeters.z - dz * 4.75F};
        const auto clip = Render::TransformPoint(camera.viewProjection, tail);
        if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) || std::abs(clip[3]) <= 1.0e-5F)
            continue;
        const float ndcX = clip[0] / clip[3];
        const float ndcY = clip[1] / clip[3];
        const float viewportX = ndcX * 0.5F + 0.5F;
        const float viewportY = 0.5F - ndcY * 0.5F;
        if (viewportX < -0.20F || viewportX > 1.20F || viewportY < -0.20F || viewportY > 1.20F)
            continue;

        const float targetDx = missile->positionMeters.x - camera.target.x;
        const float targetDy = missile->positionMeters.y - camera.target.y;
        const float targetDz = missile->positionMeters.z - camera.target.z;
        const float cameraTargetDistance = std::hypot(targetDx, targetDy, targetDz);
        float lodScale = 1.0F;
        if (cameraTargetDistance >= tuning.lodDistancesMeters[2]) continue;
        if (cameraTargetDistance >= tuning.lodDistancesMeters[1]) lodScale = 0.28F;
        else if (cameraTargetDistance >= tuning.lodDistancesMeters[0]) lodScale = 0.58F;
        const float radiusViewport = std::clamp(tuning.heatDistortionRadiusMeters / camera.width, 0.004F, 0.12F);
        Render::ScenePresentationDistortion distortion{
            .viewportCenter = {viewportX, viewportY},
            .radiusViewport = radiusViewport,
            .strengthViewport = strength * lodScale};
        candidates.push_back({.distortion = distortion,
                              .priority = distortion.radiusViewport * std::abs(distortion.strengthViewport)});
    }
    std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; });
    if (candidates.size() > Render::ScenePresentationDistortionCapacity)
        candidates.resize(Render::ScenePresentationDistortionCapacity);
    std::vector<Render::ScenePresentationDistortion> result;
    result.reserve(candidates.size());
    for (const Candidate& candidate : candidates) result.push_back(candidate.distortion);
    return result;
}
} // namespace DeepRun::Game::Armament
''', encoding="utf-8")

# Showcase environment now seeds the actual W1 production ocean/weather authority, not only the P-700 emitter.
show_path = "Game/Combat/P700VfxShowcase.h"
show = read(show_path)
if '#include "Simulation/Environment/WeatherSeaState.h"' not in show:
    show = show.replace('#include "Simulation/Weapons/P700Granit.h"\n',
                        '#include "Simulation/Weapons/P700Granit.h"\n#include "Simulation/Environment/WeatherSeaState.h"\n', 1)
weather_fn = r'''
[[nodiscard]] inline Environment::WeatherStateConfig P700VfxShowcaseWeatherConfigFor(
    const P700VfxShowcaseProfile& profile) noexcept
{
    constexpr float RadToDeg = 57.29577951308232F;
    const auto& e = profile.environment;
    const float directionDegrees = e.windDirectionRadians * RadToDeg;
    const float peakPeriod = e.significantWaveHeightMeters > 0.0F
        ? std::clamp(1.5F + e.windSpeedMetersPerSecond * 0.33F, 1.5F, 11.5F) : 0.0F;
    const std::uint8_t beaufort = e.windSpeedMetersPerSecond >= 20.8F ? 9U :
        (e.windSpeedMetersPerSecond >= 17.2F ? 8U :
         (e.windSpeedMetersPerSecond >= 10.8F ? 6U :
          (e.windSpeedMetersPerSecond >= 5.5F ? 4U : (e.windSpeedMetersPerSecond >= 1.6F ? 2U : 1U))));
    return Environment::WeatherStateConfig{
        .beaufortForce = beaufort,
        .windSpeedMetersPerSecond = e.windSpeedMetersPerSecond,
        .windGustSpeedMetersPerSecond = e.windSpeedMetersPerSecond * (e.lightningVisible ? 1.38F : 1.18F),
        .windDirectionDegrees = directionDegrees,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = e.significantWaveHeightMeters,
            .probableMaximumWaveHeightMeters = e.significantWaveHeightMeters * 1.55F,
            .peakPeriodSeconds = peakPeriod,
            .meanDirectionDegrees = directionDegrees,
            .directionalSpreadDegrees = e.lightningVisible ? 34.0F : 44.0F},
        .swell = {},
        .rainRateMillimetersPerHour = e.rainRateMillimetersPerHour,
        .meteorologicalVisibilityMeters = e.rainRateMillimetersPerHour > 0.0F ? 6'000.0F : 100'000.0F,
        .cloudCoverFraction = std::clamp((e.humidityFraction - 0.52F) / 0.48F, 0.0F, e.lightningVisible ? 0.98F : 0.86F),
        .lightningRatePerMinute = e.lightningVisible ? 1.8F : 0.0F,
        .weatherSeed = profile.deterministicSeed};
}
'''
show_anchor = "inline constexpr std::array<P700VfxShowcaseCameraPreset, 6> P700VfxShowcaseCameras"
if show.count(show_anchor) != 1:
    raise SystemExit("P700 showcase weather insertion anchor missing")
show = show.replace(show_anchor, weather_fn + "\n" + show_anchor, 1)
write(show_path, show)

# Physical playground accepts one optional authored WeatherStateConfig at scenario creation. It still constructs the
# same production spectrum/WaterBody and therefore does not create a second weather or ocean system.
header_path = "Game/PhysicalPlayground.h"
header = read(header_path)
old_init_decl = '''        bool verifyDistinctUploads,\n        float initialSubmarineDepthMeters = 100.0F,\n        std::optional<std::uint8_t> weatherBeaufortForce = std::nullopt);'''
new_init_decl = '''        bool verifyDistinctUploads,\n        float initialSubmarineDepthMeters = 100.0F,\n        std::optional<std::uint8_t> weatherBeaufortForce = std::nullopt,\n        std::optional<Environment::WeatherStateConfig> weatherOverride = std::nullopt);'''
if header.count(old_init_decl) != 1:
    raise SystemExit("PhysicalPlayground weather override declaration anchor missing")
header = header.replace(old_init_decl, new_init_decl, 1)
old_render_decl = '''        double presentationTimeSeconds,\n        std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances = {},\n        std::span<const Render::ScenePresentationLocalLight> localLights = {}) const;'''
new_render_decl = '''        double presentationTimeSeconds,\n        std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances = {},\n        std::span<const Render::ScenePresentationLocalLight> localLights = {},\n        std::span<const Render::ScenePresentationDistortion> distortions = {},\n        float lensMoistureIntensity = 0.0F,\n        float lensMoistureSeed = 0.0F) const;'''
if header.count(old_render_decl) != 1:
    raise SystemExit("PhysicalPlayground distortion Render declaration anchor missing")
header = header.replace(old_render_decl, new_render_decl, 1)
write(header_path, header)

physical_path = "Game/PhysicalPlayground.cpp"
physical = read(physical_path)
old_init_def = '''    const bool verifyDistinctUploads,\n    const float initialSubmarineDepthMeters,\n    const std::optional<std::uint8_t> weatherBeaufortForce)\n{'''
new_init_def = '''    const bool verifyDistinctUploads,\n    const float initialSubmarineDepthMeters,\n    const std::optional<std::uint8_t> weatherBeaufortForce,\n    const std::optional<Environment::WeatherStateConfig> weatherOverride)\n{'''
if physical.count(old_init_def) != 1:
    raise SystemExit("PhysicalPlayground weather override definition anchor missing")
physical = physical.replace(old_init_def, new_init_def, 1)
old_weather_create = '''    const auto weather = weatherBeaufortForce.has_value()\n        ? Environment::WeatherState::FullyDevelopedBeaufort(\n              *weatherBeaufortForce, 18.0F, 0x5731495F56495355ULL)\n        : Environment::WeatherState::Create(Environment::WeatherStateConfig{'''
new_weather_create = '''    const auto weather = weatherOverride.has_value()\n        ? Environment::WeatherState::Create(*weatherOverride)\n        : weatherBeaufortForce.has_value()\n            ? Environment::WeatherState::FullyDevelopedBeaufort(\n                  *weatherBeaufortForce, 18.0F, 0x5731495F56495355ULL)\n            : Environment::WeatherState::Create(Environment::WeatherStateConfig{'''
if physical.count(old_weather_create) != 1:
    raise SystemExit("PhysicalPlayground weather creation anchor missing")
physical = physical.replace(old_weather_create, new_weather_create, 1)
# The aggregate close gains one indentation level but no semantic change; ?: grammar needs one extra close only if
# the existing Create() expression has exactly the same close. It already ends with '});', valid for the nested conditional.
old_render_def = '''    const double presentationTimeSeconds,\n    const std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances,\n    const std::span<const Render::ScenePresentationLocalLight> localLights) const\n{'''
new_render_def = '''    const double presentationTimeSeconds,\n    const std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances,\n    const std::span<const Render::ScenePresentationLocalLight> localLights,\n    const std::span<const Render::ScenePresentationDistortion> distortions,\n    const float lensMoistureIntensity,\n    const float lensMoistureSeed) const\n{'''
if physical.count(old_render_def) != 1:
    raise SystemExit("PhysicalPlayground distortion Render definition anchor missing")
physical = physical.replace(old_render_def, new_render_def, 1)
scene_anchor = '''    scenePresentation.activeLocalLightCount = static_cast<std::uint32_t>(localLights.size());\n    for (std::size_t index = 0U; index < localLights.size(); ++index)\n        scenePresentation.localLights[index] = localLights[index];\n    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)'''
scene_new = '''    scenePresentation.activeLocalLightCount = static_cast<std::uint32_t>(localLights.size());\n    for (std::size_t index = 0U; index < localLights.size(); ++index)\n        scenePresentation.localLights[index] = localLights[index];\n    if (distortions.size() > Render::ScenePresentationDistortionCapacity)\n        return std::unexpected("physical playground distortion presentation exceeds the bounded capacity");\n    scenePresentation.activeDistortionCount = static_cast<std::uint32_t>(distortions.size());\n    for (std::size_t index = 0U; index < distortions.size(); ++index)\n        scenePresentation.distortions[index] = distortions[index];\n    scenePresentation.lensMoistureIntensity = lensMoistureIntensity;\n    scenePresentation.lensMoistureSeed = lensMoistureSeed;\n    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)'''
if physical.count(scene_anchor) != 1:
    raise SystemExit("PhysicalPlayground scene distortion assignment anchor missing")
physical = physical.replace(scene_anchor, scene_new, 1)
write(physical_path, physical)

# Combat view exposes distortion composition and lets the transparent VFX pass receive the strongest launch light.
view_path = "Game/Combat/CombatPlaygroundView.h"
view = read(view_path)
if '#include "Game/Weapons/P700LaunchDistortionPresentation.h"' not in view:
    view = view.replace('#include "Game/Weapons/P700LaunchLightingPresentation.h"\n',
                        '#include "Game/Weapons/P700LaunchLightingPresentation.h"\n#include "Game/Weapons/P700LaunchDistortionPresentation.h"\n', 1)
insert_after_lights = '''    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationLocalLight>, std::string>\n    BuildP700LocalLights(const CombatPlaygroundRuntime& runtime, const double simulationTimeSeconds) const\n    {\n        std::vector<const Weapons::P700GranitRuntimeState*> missiles;\n        missiles.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());\n        if (const auto& leader = runtime.PlayerP700(); leader) missiles.push_back(&*leader);\n        for (const auto& wingman : runtime.PlayerP700Wingmen()) missiles.push_back(&wingman);\n        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles()) missiles.push_back(&ripple);\n        return Armament::BuildP700LaunchLocalLights(missiles, simulationTimeSeconds);\n    }\n'''
distortion_method = insert_after_lights + '''\n    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationDistortion>, std::string>\n    BuildP700Distortions(\n        const CombatPlaygroundRuntime& runtime,\n        const Render::OrthographicCamera& camera,\n        const double simulationTimeSeconds) const\n    {\n        std::vector<const Weapons::P700GranitRuntimeState*> missiles;\n        missiles.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());\n        if (const auto& leader = runtime.PlayerP700(); leader) missiles.push_back(&*leader);\n        for (const auto& wingman : runtime.PlayerP700Wingmen()) missiles.push_back(&wingman);\n        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles()) missiles.push_back(&ripple);\n        return Armament::BuildP700LaunchDistortions(missiles, camera, p700VfxSystem_.Tuning(), simulationTimeSeconds);\n    }\n'''
if view.count(insert_after_lights) != 1:
    raise SystemExit("Combat view distortion method insertion anchor missing")
view = view.replace(insert_after_lights, distortion_method, 1)
old_draw = '''        const auto p700VfxStats = p700TransientVfx_.Draw(\n            renderer,\n            std::span<const Render::TransientVfxEmitter>(p700VfxFrame->emitters.data(), p700VfxFrame->emitters.size()),\n            camera,\n            vfxDepthLighting);'''
new_draw = '''        const auto launchLights = Armament::BuildP700LaunchLocalLights(liveP700, simulationTimeSeconds);\n        if (!launchLights)\n            return std::unexpected("P-700 transient VFX local-light composition failed: " + launchLights.error());\n        const std::size_t vfxLightCount = (std::min)(std::size_t{1U}, launchLights->size());\n        const auto p700VfxStats = p700TransientVfx_.Draw(\n            renderer,\n            std::span<const Render::TransientVfxEmitter>(p700VfxFrame->emitters.data(), p700VfxFrame->emitters.size()),\n            camera,\n            vfxDepthLighting,\n            std::span<const Render::ScenePresentationLocalLight>(launchLights->data(), vfxLightCount));'''
if view.count(old_draw) != 1:
    raise SystemExit("Combat view VFX local-light draw anchor missing")
view = view.replace(old_draw, new_draw, 1)
write(view_path, view)

# Composition forwarding.
comp_path = "Game/Combat/CombatPlaygroundWindowedComposition.h"
comp = read(comp_path)
comp_anchor = '''    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationLocalLight>, std::string>\n    BuildP700LocalLights(const double simulationTimeSeconds) const\n    {\n        if (!runtime_.has_value()) return std::unexpected("P-700 runtime is unavailable");\n        return view_.BuildP700LocalLights(*runtime_, simulationTimeSeconds);\n    }\n'''
comp_new = comp_anchor + '''\n    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationDistortion>, std::string>\n    BuildP700Distortions(const Render::OrthographicCamera& camera, const double simulationTimeSeconds) const\n    {\n        if (!runtime_.has_value()) return std::unexpected("P-700 runtime is unavailable");\n        return view_.BuildP700Distortions(*runtime_, camera, simulationTimeSeconds);\n    }\n'''
if comp.count(comp_anchor) != 1:
    raise SystemExit("Combat composition distortion forwarding anchor missing")
write(comp_path, comp.replace(comp_anchor, comp_new, 1))

# Main: same showcase profile seeds W1 ocean at initialization; current presentation camera drives distortion;
# WATERLINE_MONEY_SHOT alone receives sparse lens moisture.
main_path = "DeepRun/Main.cpp"
main = read(main_path)
old_init_capture = '''            [&options, &weatherVisualBeaufortForce, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,\n             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,\n             &inputState, &engineServices](DeepRun::Core::Engine& engine)'''
new_init_capture = '''            [&options, &weatherVisualBeaufortForce, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,\n             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,\n             &inputState, &engineServices, &p700ShowcaseProfile](DeepRun::Core::Engine& engine)'''
if main.count(old_init_capture) != 1:
    raise SystemExit("Main showcase weather init-capture anchor missing")
main = main.replace(old_init_capture, new_init_capture, 1)
old_init_call = '''                const auto initialized = playground.Initialize(\n                    engine.Assets(), *physics, *renderer, options.smokeTest || options.p700SmokeTest,\n                    initialDepthMeters, weatherVisualBeaufortForce);'''
new_init_call = '''                const std::optional<DeepRun::Environment::WeatherStateConfig> p700ShowcaseWeather =\n                    options.p700SmokeTest\n                        ? std::optional<DeepRun::Environment::WeatherStateConfig>{\n                              DeepRun::Game::Combat::P700VfxShowcaseWeatherConfigFor(p700ShowcaseProfile)}\n                        : std::nullopt;\n                const auto initialized = playground.Initialize(\n                    engine.Assets(), *physics, *renderer, options.smokeTest || options.p700SmokeTest,\n                    initialDepthMeters, weatherVisualBeaufortForce, p700ShowcaseWeather);'''
if main.count(old_init_call) != 1:
    raise SystemExit("Main showcase W1 weather initialization anchor missing")
main = main.replace(old_init_call, new_init_call, 1)
old_offset = '''                const auto p700CameraOffset = p700CameraImpulse.Evaluate(presentationTimeSeconds);\n\n                if (combatPlayground.has_value()'''
new_offset = '''                const auto p700CameraOffset = p700CameraImpulse.Evaluate(presentationTimeSeconds);\n                float p700LensMoistureIntensity = 0.0F;\n                const float p700LensMoistureSeed = static_cast<float>(\n                    p700ShowcaseProfile.deterministicSeed & 0xFFFFU) / 65535.0F;\n\n                if (combatPlayground.has_value()'''
if main.count(old_offset) != 1:
    raise SystemExit("Main lens-moisture frame variable anchor missing")
main = main.replace(old_offset, new_offset, 1)
selection_anchor = '''                            const auto selection = DeepRun::Game::Combat::EvaluateP700VfxShowcaseCamera(\n                                missile, simulationTimeSeconds);\n                            targetOffsetXMeters += missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x +'''
selection_new = '''                            const auto selection = DeepRun::Game::Combat::EvaluateP700VfxShowcaseCamera(\n                                missile, simulationTimeSeconds);\n                            if (selection.preset != nullptr && selection.preset->waterlineLensDroplets)\n                                p700LensMoistureIntensity = 0.16F;\n                            targetOffsetXMeters += missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x +'''
if main.count(selection_anchor) != 1:
    raise SystemExit("Main waterline lens-moisture selection anchor missing")
main = main.replace(selection_anchor, selection_new, 1)
old_vectors = '''                std::vector<DeepRun::Render::GerstnerSurfaceTransientDisturbance> p700SurfaceDisturbances;\n                std::vector<DeepRun::Render::ScenePresentationLocalLight> p700LocalLights;\n                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())'''
new_vectors = '''                std::vector<DeepRun::Render::GerstnerSurfaceTransientDisturbance> p700SurfaceDisturbances;\n                std::vector<DeepRun::Render::ScenePresentationLocalLight> p700LocalLights;\n                std::vector<DeepRun::Render::ScenePresentationDistortion> p700Distortions;\n                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())'''
if main.count(old_vectors) != 1:
    raise SystemExit("Main distortion vector anchor missing")
main = main.replace(old_vectors, new_vectors, 1)
old_lights_done = '''                    p700LocalLights = *localLights;\n                }\n                const auto rendered = playground.Render('''
new_lights_done = '''                    p700LocalLights = *localLights;\n                    const auto presentationCamera = playground.BuildPresentationCamera(renderer, presentationTimeSeconds);\n                    if (!presentationCamera)\n                    {\n                        std::cerr << "[Game][ERROR] P-700 distortion camera composition failed: "\n                                  << presentationCamera.error() << '\\n';\n                        return false;\n                    }\n                    const auto distortions = combatPlayground->BuildP700Distortions(\n                        *presentationCamera, simulationTimeSeconds);\n                    if (!distortions)\n                    {\n                        std::cerr << "[Game][ERROR] P-700 heat/density distortion composition failed: "\n                                  << distortions.error() << '\\n';\n                        return false;\n                    }\n                    p700Distortions = *distortions;\n                }\n                const auto rendered = playground.Render('''
if main.count(old_lights_done) != 1:
    raise SystemExit("Main distortion composition anchor missing")
main = main.replace(old_lights_done, new_lights_done, 1)
old_render_tail = '''                    std::span<const DeepRun::Render::ScenePresentationLocalLight>(\n                        p700LocalLights.data(), p700LocalLights.size()));'''
new_render_tail = '''                    std::span<const DeepRun::Render::ScenePresentationLocalLight>(\n                        p700LocalLights.data(), p700LocalLights.size()),\n                    std::span<const DeepRun::Render::ScenePresentationDistortion>(\n                        p700Distortions.data(), p700Distortions.size()),\n                    p700LensMoistureIntensity, p700LensMoistureSeed);'''
if main.count(old_render_tail) != 1:
    raise SystemExit("Main distortion Render forwarding anchor missing")
main = main.replace(old_render_tail, new_render_tail, 1)
write(main_path, main)

# Regression: explicit distortion, real W1 showcase weather, reflection, local-light/lens bounds.
test_path = "Tests/P700LaunchVfxTest.cpp"
test = read(test_path)
if '#include "Game/Weapons/P700LaunchDistortionPresentation.h"' not in test:
    test = test.replace('#include "Game/Weapons/P700LaunchLightingPresentation.h"\n',
                        '#include "Game/Weapons/P700LaunchLightingPresentation.h"\n#include "Game/Weapons/P700LaunchDistortionPresentation.h"\n', 1)
test = test.replace(
    '!HasPrimitive(*breach, TransientVfxPrimitive::Foam) ||\n        !HasAudio(*breach, P700LaunchAudioEvent::SurfaceBreach)',
    '!HasPrimitive(*breach, TransientVfxPrimitive::Foam) ||\n        !HasPrimitive(*breach, TransientVfxPrimitive::SurfaceReflection) ||\n        !HasAudio(*breach, P700LaunchAudioEvent::SurfaceBreach)', 1)
showcase_anchor = '''[[nodiscard]] bool RunShowcaseChecks()\n{'''
distortion_test = r'''[[nodiscard]] bool RunDistortionAndOpticsChecks()
{
    auto camera = TestCamera();
    auto tuning = DeepRun::Game::Armament::DefaultP700LaunchVfxTuning();
    auto missile = MakeMissile(909U, P700GranitPhase::WaterExit, 0.0F, 0.10F, 1.0);
    missile.launchBoosterActive = true;
    const P700GranitRuntimeState* pointer = &missile;
    const auto booster = DeepRun::Game::Armament::BuildP700LaunchDistortions(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, tuning, 1.10);
    if (!booster || booster->size() != 1U || booster->front().strengthViewport <= 0.0F)
    {
        std::cerr << "P-700 booster heat distortion did not produce one bounded screen-space region\n";
        return false;
    }
    missile.phase = P700GranitPhase::Cruise;
    missile.phaseStartTimeSeconds = 1.20;
    missile.positionMeters = {.x = 0.0F, .y = 0.15F, .z = 0.0F};
    missile.launchBoosterActive = false;
    missile.mainEngineActive = true;
    const auto cruise = DeepRun::Game::Armament::BuildP700LaunchDistortions(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, tuning, 1.25);
    if (!cruise || cruise->size() != 1U ||
        !(cruise->front().strengthViewport < booster->front().strengthViewport))
    {
        std::cerr << "P-700 turbojet heat haze did not become subtler than booster distortion\n";
        return false;
    }
    DeepRun::Render::ScenePresentationParameters parameters{};
    parameters.cameraViewDirection = {0.0F, 0.0F, -1.0F};
    parameters.activeDistortionCount = 1U;
    parameters.distortions[0] = booster->front();
    parameters.lensMoistureIntensity = 0.16F;
    parameters.lensMoistureSeed = 0.42F;
    if (!DeepRun::Render::ValidateScenePresentationParameters(parameters))
    {
        std::cerr << "P-700 distortion/lens optics did not satisfy generic scene-presentation bounds\n";
        return false;
    }
    return true;
}

'''
if test.count(showcase_anchor) != 1:
    raise SystemExit("P700 distortion regression insertion anchor missing")
test = test.replace(showcase_anchor, distortion_test + showcase_anchor, 1)
# Showcase test proves environment parameters are accepted by the actual WeatherState authority.
old_showcase_condition = '''    const auto night = P700VfxShowcaseProfileFor(P700VfxShowcaseEnvironment::Night);\n    if (calm.deterministicSeed == storm.deterministicSeed || calm.nominalDurationSeconds < 8.0F ||'''
new_showcase_condition = '''    const auto night = P700VfxShowcaseProfileFor(P700VfxShowcaseEnvironment::Night);\n    const auto calmWeather = DeepRun::Environment::WeatherState::Create(P700VfxShowcaseWeatherConfigFor(calm));\n    const auto stormWeather = DeepRun::Environment::WeatherState::Create(P700VfxShowcaseWeatherConfigFor(storm));\n    if (!calmWeather || !stormWeather || !stormWeather->HasRain() || !stormWeather->HasLightning() ||\n        stormWeather->CombinedSignificantWaveHeightMeters() <= calmWeather->CombinedSignificantWaveHeightMeters() ||\n        calm.deterministicSeed == storm.deterministicSeed || calm.nominalDurationSeconds < 8.0F ||'''
if test.count(old_showcase_condition) != 1:
    raise SystemExit("P700 showcase W1 weather regression anchor missing")
test = test.replace(old_showcase_condition, new_showcase_condition, 1)
test = test.replace(
    '!RunSurfaceDisturbanceChecks() ||\n        !RunLaunchLightingChecks()',
    '!RunSurfaceDisturbanceChecks() || !RunLaunchLightingChecks() || !RunDistortionAndOpticsChecks()', 1)
write(test_path, test)

print("P-700 stage3 game/weather/distortion integration patch applied")
