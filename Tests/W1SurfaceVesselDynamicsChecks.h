#pragma once

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
