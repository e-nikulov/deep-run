from pathlib import Path

root = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


header = root / "Game" / "PhysicalPlayground.h"
replace_once(
    header,
    """    Marine::BuoyancyComponent buoyancy_;\n    Marine::BuoyancyComponent surfaceFloatBuoyancy_;\n    // Pre-reserved at initialization; CalculateWaveSurface reuses this storage in every fixed tick.\n    Marine::BuoyancyResult surfaceFloatBuoyancyResult_;\n""",
    """    Marine::BuoyancyComponent buoyancy_;\n    Marine::BuoyancyComponent surfaceFloatBuoyancy_;\n    // W1-C and M3-F reuse caller-owned result storage in every fixed tick; neither surface calculation allocates.\n    Marine::BuoyancyResult surfaceVesselBuoyancyResult_;\n    Marine::BuoyancyResult surfaceFloatBuoyancyResult_;\n""",
)

cpp = root / "Game" / "PhysicalPlayground.cpp"
replace_once(
    cpp,
    """    buoyancy_ = std::move(buoyancy);\n    surfaceFloatBuoyancy_ = surfaceFloatBuoyancy;\n    surfaceFloatBuoyancyResult_.points.reserve(surfaceFloatBuoyancy_.points.size());\n""",
    """    buoyancy_ = std::move(buoyancy);\n    surfaceVesselBuoyancyResult_.points.reserve(buoyancy_.points.size());\n    surfaceFloatBuoyancy_ = surfaceFloatBuoyancy;\n    surfaceFloatBuoyancyResult_.points.reserve(surfaceFloatBuoyancy_.points.size());\n""",
)
replace_once(
    cpp,
    """    const auto buoyancyResult = Marine::BuoyancySystem::Calculate(\n        *water_,\n        buoyancy_,\n        Marine::BuoyancyPose{\n            .worldPositionMeters = state->position,\n            .worldOrientation = state->orientation},\n        *gravityMagnitude);\n    if (!buoyancyResult)\n    {\n        return std::unexpected(\"physical playground buoyancy calculation failed: \" + buoyancyResult.error().message);\n    }\n\n    // M3-F is the sole opt-in physical consumer. This uses the Engine-owned beginning-of-step SimulationTime\n    // supplied by Game composition; the submarine's Calculate() above remains flat/reference-plane only.\n""",
    """    // W1-C production surface-vessel response: every longitudinal point samples the same authoritative\n    // spectrum that Render consumes, but hydrostatic force remains vertical. Local crest/trough differences\n    // therefore create heave and pitch without turning a steep wave normal into horizontal propulsion.\n    const auto vesselBuoyancyCalculated = Marine::BuoyancySystem::CalculateWaveHydrostatic(\n        *water_,\n        buoyancy_,\n        Marine::BuoyancyPose{\n            .worldPositionMeters = state->position,\n            .worldOrientation = state->orientation},\n        *gravityMagnitude,\n        simulationTimeSeconds,\n        surfaceVesselBuoyancyResult_);\n    if (!vesselBuoyancyCalculated)\n    {\n        return std::unexpected(\"physical playground W1-C vessel wave hydrostatics failed: \" +\n                               vesselBuoyancyCalculated.error().message);\n    }\n    const Marine::BuoyancyResult* buoyancyResult = &surfaceVesselBuoyancyResult_;\n\n    // M3-F retains its accepted small-float surface-normal response independently from W1-C vessel hydrostatics.\n""",
)

m4 = root / "Tests" / "M4EnvironmentChecks.h"
replace_once(
    m4,
    '#include "Tests/W1SpectralOceanChecks.h"\n#include "Tests/W1WeatherSeaStateChecks.h"\n',
    '#include "Tests/W1SpectralOceanChecks.h"\n#include "Tests/W1SurfaceVesselDynamicsChecks.h"\n#include "Tests/W1WeatherSeaStateChecks.h"\n',
)
replace_once(
    m4,
    """    return !invalidResult && invalidResult.error().code == AcousticErrorCode::InvalidPropagationModifiers &&\n           RunW1WeatherSeaStateChecks() && RunW1SpectralOceanChecks();\n""",
    """    return !invalidResult && invalidResult.error().code == AcousticErrorCode::InvalidPropagationModifiers &&\n           RunW1WeatherSeaStateChecks() && RunW1SpectralOceanChecks() && RunW1SurfaceVesselDynamicsChecks();\n""",
)

test = root / "Tests" / "W1SurfaceVesselDynamicsChecks.h"
test.write_text(r'''#pragma once

#include "Simulation/Environment/WeatherSeaState.h"
#include "Simulation/Marine/BuoyancySystem.h"
#include "Simulation/Marine/ProductionOceanSpectrum.h"
#include "Simulation/Marine/WaterBody.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace DeepRun::Tests
{
namespace W1SurfaceVesselDynamicsDetail
{
[[nodiscard]] inline std::expected<Marine::WaterBody, std::string> BuildWater()
{
    const auto weather = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 7U,
        .windSpeedMetersPerSecond = 15.0F,
        .windGustSpeedMetersPerSecond = 20.0F,
        .windDirectionDegrees = 22.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 3.0F,
            .probableMaximumWaveHeightMeters = 4.0F,
            .peakPeriodSeconds = 6.2F,
            .meanDirectionDegrees = 22.0F,
            .directionalSpreadDegrees = 38.0F},
        .swell = Environment::SwellState{
            .significantWaveHeightMeters = 2.0F,
            .peakPeriodSeconds = 10.5F,
            .meanDirectionDegrees = 338.0F,
            .directionalSpreadDegrees = 12.0F},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 100000.0F,
        .cloudCoverFraction = 0.4F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 0x5731435F53555246ULL});
    if (!weather)
        return std::unexpected(weather.error().message);
    const auto waves = Marine::BuildProductionOceanWaveField(*weather);
    if (!waves || !waves->has_value())
        return std::unexpected(waves ? "W1-C spectrum unexpectedly disabled" : waves.error());
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F, .waves = waves->value()});
    if (!water)
        return std::unexpected(water.error().message);
    return *water;
}

[[nodiscard]] inline Marine::BuoyancyComponent BuildLongHullBuoyancy()
{
    Marine::BuoyancyComponent component;
    constexpr std::array<float, 4> longitudinalX{50.0F, 16.666667F, -16.666667F, -50.0F};
    for (const float x : longitudinalX)
    {
        component.points.push_back(Marine::BuoyancyPoint{
            .bodyLocalPositionMeters = {x, 0.0F, 0.0F},
            .displacedVolumeCubicMeters = 4'696.0F,
            .submersionHalfHeightMeters = 4.35F});
    }
    return component;
}

[[nodiscard]] inline bool SameHydrostaticOutputs(
    const Marine::BuoyancyResult& first,
    const Marine::BuoyancyResult& second) noexcept
{
    if (first.points.size() != second.points.size() ||
        first.totalForceNewtons != second.totalForceNewtons ||
        first.totalSubmergedVolumeCubicMeters != second.totalSubmergedVolumeCubicMeters)
        return false;
    for (std::size_t i = 0U; i < first.points.size(); ++i)
    {
        if (first.points[i].worldPositionMeters != second.points[i].worldPositionMeters ||
            first.points[i].submergedFraction != second.points[i].submergedFraction ||
            first.points[i].submergedVolumeCubicMeters != second.points[i].submergedVolumeCubicMeters ||
            first.points[i].forceNewtons != second.points[i].forceNewtons)
            return false;
    }
    return true;
}
}

[[nodiscard]] inline bool RunW1SurfaceVesselDynamicsChecks()
{
    using namespace W1SurfaceVesselDynamicsDetail;
    const auto water = BuildWater();
    if (!water)
        return false;
    const Marine::BuoyancyComponent component = BuildLongHullBuoyancy();
    constexpr float Gravity = 9.80665F;

    // Deep-water compatibility: all points are fully submerged, therefore wave elevation cannot change
    // displaced volume or Archimedean force. Only diagnostic signed depth is allowed to differ.
    const Marine::BuoyancyPose deepPose{.worldPositionMeters = {0.0F, -100.0F, 0.0F}};
    const auto flatDeep = Marine::BuoyancySystem::Calculate(*water, component, deepPose, Gravity);
    Marine::BuoyancyResult waveDeep;
    waveDeep.points.reserve(component.points.size());
    const auto waveDeepCalculated = Marine::BuoyancySystem::CalculateWaveHydrostatic(
        *water, component, deepPose, Gravity, 2.75, waveDeep);
    if (!flatDeep || !waveDeepCalculated || !SameHydrostaticOutputs(*flatDeep, waveDeep))
        return false;

    // Surface condition: the 154 m-class hull spans several phases. Local height changes submersion and
    // therefore produces a real pitch moment from spatially separated vertical forces, never horizontal thrust.
    const Marine::BuoyancyPose surfacePose{.worldPositionMeters = {0.0F, -2.35F, 0.0F}};
    Marine::BuoyancyResult first;
    Marine::BuoyancyResult repeated;
    Marine::BuoyancyResult later;
    first.points.reserve(component.points.size());
    repeated.points.reserve(component.points.size());
    later.points.reserve(component.points.size());
    if (!Marine::BuoyancySystem::CalculateWaveHydrostatic(*water, component, surfacePose, Gravity, 1.25, first) ||
        !Marine::BuoyancySystem::CalculateWaveHydrostatic(*water, component, surfacePose, Gravity, 1.25, repeated) ||
        !Marine::BuoyancySystem::CalculateWaveHydrostatic(*water, component, surfacePose, Gravity, 3.0, later) ||
        !SameHydrostaticOutputs(first, repeated))
        return false;

    float minimumFraction = 1.0F;
    float maximumFraction = 0.0F;
    double pitchMomentNewtonMeters = 0.0;
    bool timeChanged = false;
    for (std::size_t i = 0U; i < first.points.size(); ++i)
    {
        const auto& point = first.points[i];
        if (point.forceNewtons.x != 0.0F || point.forceNewtons.z != 0.0F || point.forceNewtons.y < 0.0F)
            return false;
        minimumFraction = (std::min)(minimumFraction, point.submergedFraction);
        maximumFraction = (std::max)(maximumFraction, point.submergedFraction);
        pitchMomentNewtonMeters += static_cast<double>(point.worldPositionMeters.x - surfacePose.worldPositionMeters.x) *
                                   static_cast<double>(point.forceNewtons.y);
        timeChanged = timeChanged || std::abs(point.submergedFraction - later.points[i].submergedFraction) > 1.0e-5F;
    }
    if (!(maximumFraction - minimumFraction > 1.0e-3F) ||
        std::abs(pitchMomentNewtonMeters) < 1.0e4 || !timeChanged ||
        first.totalForceNewtons.x != 0.0F || first.totalForceNewtons.z != 0.0F ||
        !(first.totalForceNewtons.y > 0.0F))
        return false;

    Marine::BuoyancyResult invalid;
    const auto invalidTime = Marine::BuoyancySystem::CalculateWaveHydrostatic(
        *water, component, surfacePose, Gravity, -0.01, invalid);
    return !invalidTime && invalidTime.error().code == Marine::BuoyancyErrorCode::InvalidSimulationTime;
}
}
''', encoding="utf-8")

print("W1-C surface-vessel integration patch prepared: PASS")
