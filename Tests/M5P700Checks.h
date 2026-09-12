#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Weapons/P700Granit.h"

#include <cmath>
#include <iostream>
#include <optional>

namespace DeepRun::Tests
{
namespace M5P700Detail
{
[[nodiscard]] inline Weapons::P700GranitDefinition MakeDefinition()
{
    return Weapons::P700GranitDefinition{
        .weapon = Weapons::WeaponDefinition{
            .id = "m5.p700-granit",
            .preparationSeconds = 0.0,
            .targeting = Weapons::WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.65F,
                .maximumBearingUncertaintyRadians = 0.12F,
                .maximumPositionUncertaintyMeters = 500.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}},
        .underwaterExitSpeedMetersPerSecond = 50.0F,
        .waterExitSpeedMetersPerSecond = 100.0F,
        .deploymentFlightSpeedMetersPerSecond = 180.0F,
        .cruiseSpeedMetersPerSecond = 500.0F,
        .terminalSpeedMetersPerSecond = 500.0F,
        .maximumAirborneTurnRateRadiansPerSecond = 0.35F,
        .waterExitTransitionSeconds = 0.50,
        .deploymentSeconds = 1.50,
        .terminalRangeMeters = 5'000.0F,
        .collisionHalfExtentsMeters = {.x = 5.0F, .y = 0.70F, .z = 0.70F},
        .directImpactDamage = 100.0F,
        .explosionRadiusMeters = 30.0F};
}

[[nodiscard]] inline Perception::Track MakeTrack(
    const std::uint64_t trackId,
    const float targetXMeters)
{
    return Perception::Track{
        .trackId = trackId,
        .contactId = trackId + 1U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = Physics::PhysicsVector3{.x = targetXMeters, .y = -2.0F, .z = 0.0F},
        .positionUncertaintyMeters = 50.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.03F,
        .confidence = 0.95F,
        .observationCount = 5U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 0.0};
}

[[nodiscard]] inline Weapons::P700CarrierLaunchContext SubmergedCarrier()
{
    constexpr float fortyDegreesRadians = 0.6981317007977318F;
    return Weapons::P700CarrierLaunchContext{
        .launchPositionMeters = {.x = 0.0F, .y = -30.0F, .z = 0.0F},
        .launchForwardUnitVector = {
            .x = static_cast<float>(std::cos(static_cast<double>(fortyDegreesRadians))),
            .y = static_cast<float>(std::sin(static_cast<double>(fortyDegreesRadians))),
            .z = 0.0F},
        .surfaceLevelYMeters = 0.0F,
        .launchDepthMeters = 30.0F,
        .carrierSpeedMetersPerSecond = 0.0F,
        .carrierHeadingRadians = 0.0F};
}
}

[[nodiscard]] inline bool RunM5P700Checks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace M5P700Detail;
    using namespace Weapons;

    const auto fail = [](const char* message) {
        std::cerr << "M5 P-700 check failed: " << message << '\n';
        return false;
    };

    const P700GranitDefinition definition = MakeDefinition();
    if (!ValidateP700GranitDefinition(definition))
    {
        return fail("definition validation");
    }

    const Perception::Track targetTrack = MakeTrack(7001U, 25'000.0F);
    const Perception::Track tooCloseTrack = MakeTrack(7002U, 19'000.0F);

    auto tooCloseRuntime = CreateP700GranitRuntime(definition, 0.0);
    if (!tooCloseRuntime)
    {
        return fail("too-close fixture runtime creation");
    }
    const auto tooCloseLaunch = LaunchP700Granit(
        definition, *tooCloseRuntime, tooCloseTrack, SubmergedCarrier(), 0.0);
    if (!tooCloseLaunch || tooCloseLaunch->allowed ||
        tooCloseLaunch->reason.find("minimum range") == std::string::npos ||
        tooCloseRuntime->phase != P700GranitPhase::Stored)
    {
        return fail("20 km minimum-range employment gate");
    }

    auto excessiveDepthRuntime = CreateP700GranitRuntime(definition, 0.0);
    auto excessiveDepthCarrier = SubmergedCarrier();
    excessiveDepthCarrier.launchDepthMeters = 50.1F;
    excessiveDepthCarrier.launchPositionMeters.y = -50.1F;
    const auto excessiveDepthLaunch = LaunchP700Granit(
        definition, *excessiveDepthRuntime, targetTrack, excessiveDepthCarrier, 0.0);
    if (!excessiveDepthLaunch || excessiveDepthLaunch->allowed ||
        excessiveDepthLaunch->reason.find("depth") == std::string::npos)
    {
        return fail("50 m maximum submerged launch-depth gate");
    }

    auto excessiveSpeedRuntime = CreateP700GranitRuntime(definition, 0.0);
    auto excessiveSpeedCarrier = SubmergedCarrier();
    excessiveSpeedCarrier.carrierSpeedMetersPerSecond = KnotsToMetersPerSecond(5.1F);
    const auto excessiveSpeedLaunch = LaunchP700Granit(
        definition, *excessiveSpeedRuntime, targetTrack, excessiveSpeedCarrier, 0.0);
    if (!excessiveSpeedLaunch || excessiveSpeedLaunch->allowed ||
        excessiveSpeedLaunch->reason.find("carrier speed") == std::string::npos)
    {
        return fail("5 kt carrier-speed gate");
    }

    auto surfaceRuntime = CreateP700GranitRuntime(definition, 0.0);
    auto surfaceCarrier = SubmergedCarrier();
    surfaceCarrier.launchPositionMeters.y = 0.0F;
    surfaceCarrier.launchDepthMeters = 0.0F;
    const auto surfaceLaunch = LaunchP700Granit(
        definition, *surfaceRuntime, targetTrack, surfaceCarrier, 0.0);
    if (!surfaceLaunch || !surfaceLaunch->allowed || surfaceRuntime->phase != P700GranitPhase::WaterExit ||
        surfaceRuntime->deploymentProgress != 0.0F)
    {
        return fail("surface launch must be accepted and bypass UnderwaterLaunch while remaining stowed");
    }

    const Physics::PhysicsBodyHandle carrierBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 30.0F, .y = 8.0F, .z = 8.0F},
            .position = {.x = 0.0F, .y = -30.0F, .z = 0.0F}});
    const Physics::PhysicsBodyHandle targetBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 50.0F, .y = 10.0F, .z = 10.0F},
            .position = {.x = 25'000.0F, .y = -2.0F, .z = 0.0F}});
    if (!carrierBody.IsValid() || !targetBody.IsValid())
    {
        return fail("P-700 carrier/target fixture creation");
    }

    auto runtimeResult = CreateP700GranitRuntime(definition, 0.0);
    if (!runtimeResult)
    {
        return fail("submerged runtime creation");
    }
    auto runtime = *runtimeResult;
    const auto launch = LaunchP700Granit(
        definition, runtime, targetTrack, SubmergedCarrier(), 0.0);
    if (!launch || !launch->allowed || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.deploymentProgress != 0.0F || runtime.guidanceTrackId != targetTrack.trackId)
    {
        return fail("submerged launch must enter UnderwaterLaunch from a perceived target");
    }

    const auto underwater = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 0.50, carrierBody);
    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= 0.0F)
    {
        return fail("underwater exit movement must stay stowed before reaching the surface");
    }

    const auto waterExit = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 1.20, carrierBody);
    if (!waterExit || *waterExit || runtime.phase != P700GranitPhase::WaterExit ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y <= 0.0F)
    {
        return fail("water-exit phase must begin only after the physical surface crossing and remain stowed");
    }

    const auto deploying = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 2.00, carrierBody);
    if (!deploying || *deploying || runtime.phase != P700GranitPhase::AirborneDeploying ||
        runtime.deploymentProgress <= 0.0F || runtime.deploymentProgress >= 1.0F)
    {
        return fail("P700_Deploy presentation progress must start only after WaterExit");
    }

    const auto cruise = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 3.10, carrierBody);
    if (!cruise || *cruise || runtime.phase != P700GranitPhase::Cruise ||
        std::abs(runtime.deploymentProgress - 1.0F) > 1.0e-6F)
    {
        return fail("deployment must complete before Cruise");
    }

    Perception::Track wrongTrack = targetTrack;
    wrongTrack.trackId = targetTrack.trackId + 1U;
    if (AdvanceP700GranitWithCollision(
            definition, runtime, wrongTrack, physicsWorld, 3.20, carrierBody))
    {
        return fail("in-flight P-700 must reject retargeting to a different perceived Track identity");
    }

    // A weak same-ID update cannot erase the last accepted perceived aim point.
    auto weakSameTrack = targetTrack;
    weakSameTrack.confidence = 0.1F;
    const auto weakAdvance = AdvanceP700GranitWithCollision(
        definition, runtime, weakSameTrack, physicsWorld, 4.0, carrierBody);
    if (!weakAdvance || *weakAdvance || !runtime.perceivedAimPointMeters)
    {
        return fail("weak same-ID evidence must preserve the last qualified aim point");
    }

    bool sawTerminal = false;
    std::optional<P700GranitImpact> impact{};
    double timeSeconds = 4.0;
    for (int step = 0; step < 80 && !impact; ++step)
    {
        timeSeconds += 1.0;
        const auto advanced = AdvanceP700GranitWithCollision(
            definition, runtime, targetTrack, physicsWorld, timeSeconds, carrierBody);
        if (!advanced)
        {
            return fail("cruise/terminal advance");
        }
        sawTerminal = sawTerminal || runtime.phase == P700GranitPhase::Terminal;
        if (*advanced)
        {
            impact = **advanced;
        }
    }
    if (!impact || !sawTerminal || runtime.phase != P700GranitPhase::Impact ||
        runtime.impactedBody != targetBody || impact->physicsHit.body != targetBody ||
        impact->damage.targetBody != targetBody || impact->damage.damage != definition.directImpactDamage ||
        impact->explosion.radiusMeters != definition.explosionRadiusMeters ||
        P700DistanceMeters(impact->physicsHit.positionMeters, impact->explosion.positionMeters) > 0.001F)
    {
        return fail("Terminal -> physical Impact/damage/explosion contract");
    }

    const double impactTime = runtime.lastUpdateTimeSeconds;
    const auto spent = AdvanceP700GranitWithCollision(
        definition, runtime, std::nullopt, physicsWorld, impactTime + 0.10, carrierBody);
    if (!spent || *spent || runtime.phase != P700GranitPhase::Spent || runtime.speedMetersPerSecond != 0.0F)
    {
        return fail("Impact must consume into Spent without a duplicate event");
    }
    if (AdvanceP700GranitWithCollision(
            definition, runtime, std::nullopt, physicsWorld, impactTime + 1.0, carrierBody))
    {
        return fail("Spent P-700 must not advance or emit another impact");
    }

    if (!physicsWorld.DestroyBody(carrierBody) || !physicsWorld.DestroyBody(targetBody))
    {
        return fail("P-700 fixture cleanup");
    }
    return true;
}
} // namespace DeepRun::Tests
