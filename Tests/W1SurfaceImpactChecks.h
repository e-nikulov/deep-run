#pragma once

#include "Simulation/Marine/SurfaceImpactSystem.h"

#include <cmath>
#include <limits>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1SurfaceImpactChecks()
{
    using namespace Marine;

    constexpr SurfaceImpactConfig Config{};
    constexpr float Density = 1025.0F;
    constexpr float Dt = 0.1F;
    constexpr std::size_t PointIndex = 2U;

    // Starting fully submerged must not fabricate an impact and must remain disarmed until this hull region
    // actually emerges through the hysteresis rearm threshold.
    const auto submergedStart = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = 10.0F, .worldYMeters = -3.0F, .signedDepthMeters = 3.0F, .submergedFraction = 1.0F},
        {},
        Dt);
    if (!submergedStart || submergedStart->event || submergedStart->nextState.armed)
        return false;

    const auto emerged = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = 10.0F, .worldYMeters = 0.2F, .signedDepthMeters = -0.2F, .submergedFraction = 0.40F},
        {},
        Dt);
    if (!emerged || emerged->event || !emerged->nextState.armed)
        return false;

    // Re-entry: signed depth rises by 0.4 m in 0.1 s => 4 m/s relative wetting speed.
    const SurfaceImpactPointSample impactSample{
        .worldXMeters = 10.0F,
        .worldYMeters = -0.2F,
        .worldZMeters = 1.5F,
        .signedDepthMeters = 0.2F,
        .submergedFraction = 0.70F};
    const auto impact = SurfaceImpactSystem::AdvancePoint(
        Config, Density, PointIndex, impactSample, emerged->nextState, Dt);
    const auto repeated = SurfaceImpactSystem::AdvancePoint(
        Config, Density, PointIndex, impactSample, emerged->nextState, Dt);
    if (!impact || !repeated || !impact->event || !repeated->event ||
        impact->event->pointIndex != PointIndex ||
        impact->event->worldXMeters != impactSample.worldXMeters ||
        impact->event->worldYMeters != impactSample.worldYMeters ||
        impact->event->worldZMeters != impactSample.worldZMeters ||
        std::abs(impact->event->relativeWettingSpeedMetersPerSecond - 4.0F) > 1.0e-5F ||
        std::abs(impact->event->dynamicPressurePascals - 8200.0F) > 1.0e-3F ||
        std::abs(impact->event->severity - 0.6F) > 1.0e-5F ||
        impact->nextState.armed ||
        repeated->event->dynamicPressurePascals != impact->event->dynamicPressurePascals ||
        repeated->event->severity != impact->event->severity)
        return false;

    // Remaining wet after one crossing cannot spam repeated impact events.
    const auto stillWet = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = 10.0F, .worldYMeters = -0.3F, .signedDepthMeters = 0.3F, .submergedFraction = 0.80F},
        impact->nextState,
        Dt);
    if (!stillWet || stillWet->event || stillWet->nextState.armed)
        return false;

    // A fresh emergence rearms. A gentle threshold crossing consumes the arm but deliberately emits no event.
    const auto rearmed = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = 10.0F, .worldYMeters = 0.1F, .signedDepthMeters = -0.10F, .submergedFraction = 0.50F},
        stillWet->nextState,
        Dt);
    if (!rearmed || rearmed->event || !rearmed->nextState.armed)
        return false;

    const auto gentle = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = 10.0F, .worldYMeters = 0.05F, .signedDepthMeters = -0.05F, .submergedFraction = 0.70F},
        rearmed->nextState,
        Dt);
    if (!gentle || gentle->event || gentle->nextState.armed)
        return false;

    // At/above the configured severe speed, normalized severity saturates at one while dimensional pressure
    // remains available to future M6 consumers.
    const auto severeArmed = SurfaceImpactSystem::AdvancePoint(
        Config,
        Density,
        PointIndex,
        {.worldXMeters = -20.0F, .worldYMeters = 0.3F, .signedDepthMeters = -0.30F, .submergedFraction = 0.50F},
        gentle->nextState,
        Dt);
    const auto severe = severeArmed
        ? SurfaceImpactSystem::AdvancePoint(
              Config,
              Density,
              PointIndex,
              {.worldXMeters = -20.0F, .worldYMeters = -0.5F, .signedDepthMeters = 0.50F, .submergedFraction = 0.70F},
              severeArmed->nextState,
              Dt)
        : std::expected<SurfaceImpactPointAdvance, SurfaceImpactError>{
              std::unexpected(SurfaceImpactError{})};
    if (!severeArmed || !severe || !severe->event ||
        std::abs(severe->event->relativeWettingSpeedMetersPerSecond - 8.0F) > 1.0e-5F ||
        std::abs(severe->event->dynamicPressurePascals - 32800.0F) > 1.0e-2F ||
        severe->event->severity != 1.0F)
        return false;

    SurfaceImpactConfig badConfig = Config;
    badConfig.rearmSubmergedFraction = badConfig.triggerSubmergedFraction;
    const auto invalidConfig = SurfaceImpactSystem::AdvancePoint(
        badConfig, Density, 0U, {}, {}, Dt);

    SurfaceImpactPointSample badSample{};
    badSample.worldXMeters = (std::numeric_limits<float>::quiet_NaN)();
    const auto invalidSample = SurfaceImpactSystem::AdvancePoint(
        Config, Density, 0U, badSample, {}, Dt);

    SurfaceImpactPointState badState{
        .initialized = true,
        .armed = true,
        .previousSignedDepthMeters = (std::numeric_limits<float>::quiet_NaN)(),
        .previousSubmergedFraction = 0.4F};
    const auto invalidState = SurfaceImpactSystem::AdvancePoint(
        Config, Density, 0U, {}, badState, Dt);
    const auto invalidDensity = SurfaceImpactSystem::AdvancePoint(
        Config, 0.0F, 0U, {}, {}, Dt);
    const auto invalidDelta = SurfaceImpactSystem::AdvancePoint(
        Config, Density, 0U, {}, {}, 0.0F);

    return !invalidConfig && invalidConfig.error().code == SurfaceImpactErrorCode::InvalidConfiguration &&
           !invalidSample && invalidSample.error().code == SurfaceImpactErrorCode::InvalidInput &&
           !invalidState && invalidState.error().code == SurfaceImpactErrorCode::InvalidState &&
           !invalidDensity && invalidDensity.error().code == SurfaceImpactErrorCode::InvalidInput &&
           !invalidDelta && invalidDelta.error().code == SurfaceImpactErrorCode::InvalidInput;
}
}
