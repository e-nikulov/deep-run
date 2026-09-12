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
        .cruiseSpeedMetersPerSecond = 680.0F,
        .terminalSpeedMetersPerSecond = 750.0F,
        .maximumAirborneTurnRateRadiansPerSecond = 0.35F,
        .launcherHatchOpeningSeconds = 0.75,
        .waterExitTransitionSeconds = 0.50,
        .postExitTransitionSeconds = 0.60,
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
    if (!surfaceLaunch || !surfaceLaunch->allowed || surfaceRuntime->phase != P700GranitPhase::HatchOpening ||
        surfaceRuntime->deploymentProgress != 0.0F || surfaceRuntime->hatchOpenProgress != 0.0F)
    {
        return fail("surface launch must still open the selected launcher hatch before booster ignition");
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
    if (!launch || !launch->allowed || runtime.phase != P700GranitPhase::HatchOpening ||
        runtime.deploymentProgress != 0.0F || runtime.guidanceTrackId != targetTrack.trackId ||
        runtime.launchBoosterActive || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("submerged launch must begin with the selected launcher hatch opening");
    }

    double lifecycleTimeSeconds = 0.50;
    const auto hatchOpening = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!hatchOpening || *hatchOpening || runtime.phase != P700GranitPhase::HatchOpening ||
        runtime.hatchOpenProgress <= 0.0F || runtime.hatchOpenProgress >= 1.0F || runtime.positionMeters.y != -30.0F)
    {
        return fail("launcher hatch must animate before missile motion/booster ignition");
    }

    lifecycleTimeSeconds = 1.00;
    const auto underwater = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.hatchOpenProgress != 1.0F || !runtime.launchBoosterActive ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= runtime.surfaceLevelYMeters)
    {
        return fail("underwater exit must use the attached launch booster and stay folded");
    }

    const auto advanceUntilPhase = [&](const P700GranitPhase expectedPhase, const double deadlineSeconds)
    {
        while (runtime.phase != expectedPhase && lifecycleTimeSeconds + 1.0e-9 < deadlineSeconds)
        {
            lifecycleTimeSeconds = (std::min)(deadlineSeconds, lifecycleTimeSeconds + 0.05);
            const auto advanced = AdvanceP700GranitWithCollision(
                definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
            if (!advanced || *advanced)
            {
                return false;
            }
        }
        return runtime.phase == expectedPhase;
    };

    if (!advanceUntilPhase(P700GranitPhase::WaterExit, 3.0) ||
        runtime.deploymentProgress != 0.0F ||
        runtime.positionMeters.y < runtime.surfaceLevelYMeters - 0.001F ||
        !runtime.launchBoosterActive || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("water-exit phase must begin only after physical surface crossing and retain launch hardware");
    }

    const double waterExitObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::PostExitTransition,
            waterExitObservedSeconds + definition.waterExitTransitionSeconds + 0.20) ||
        runtime.deploymentProgress != 0.0F || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("bounded water-exit climb must precede launch-hardware separation");
    }

    lifecycleTimeSeconds += 0.20;
    const auto separating = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!separating || *separating || runtime.phase != P700GranitPhase::PostExitTransition ||
        runtime.postExitTransitionProgress <= 0.0F || runtime.postExitTransitionProgress >= 1.0F ||
        runtime.deploymentProgress != 0.0F || runtime.noseProtectionCapAttached ||
        !runtime.launchBoosterAttached || !runtime.launchBoosterActive || runtime.mainEngineActive)
    {
        return fail("nose cap must clear before booster separation and before P700_Deploy");
    }

    const double separationObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::AirborneDeploying,
            separationObservedSeconds + definition.postExitTransitionSeconds + 0.50) ||
        runtime.launchBoosterAttached || runtime.noseProtectionCapAttached || !runtime.mainEngineActive)
    {
        return fail("booster must separate and main engine ignite before aerodynamic deployment");
    }

    lifecycleTimeSeconds += 0.05;
    const auto deploying = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!deploying || *deploying || runtime.phase != P700GranitPhase::AirborneDeploying ||
        runtime.deploymentProgress <= 0.0F || runtime.deploymentProgress >= 1.0F)
    {
        return fail("P700_Deploy must visibly advance only after main-engine transition");
    }

    const double deploymentObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::Cruise,
            deploymentObservedSeconds + definition.deploymentSeconds + 0.50) ||
        std::abs(runtime.deploymentProgress - 1.0F) > 1.0e-6F)
    {
        return fail("deployment must complete before Cruise");
    }

    Perception::Track wrongTrack = targetTrack;
    wrongTrack.trackId = targetTrack.trackId + 1U;
    lifecycleTimeSeconds += 0.10;
    if (AdvanceP700GranitWithCollision(
            definition, runtime, wrongTrack, physicsWorld, lifecycleTimeSeconds, carrierBody))
    {
        return fail("in-flight P-700 must reject retargeting to a different perceived Track identity");
    }

    // A weak same-ID update cannot erase the last accepted perceived aim point.
    auto weakSameTrack = targetTrack;
    weakSameTrack.confidence = 0.1F;
    lifecycleTimeSeconds += 0.10;
    const auto weakAdvance = AdvanceP700GranitWithCollision(
        definition, runtime, weakSameTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!weakAdvance || *weakAdvance || !runtime.perceivedAimPointMeters)
    {
        return fail("weak same-ID evidence must preserve the last qualified aim point");
    }

    bool sawTerminal = false;
    std::optional<P700GranitImpact> impact{};
    double timeSeconds = lifecycleTimeSeconds;
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

    // Probability is deterministic from the launch Track seed, so CI is reproducible. A 100% hard-kill
    // profile must defeat the weapon at terminal entry without fabricating a physics impact/damage event.
    const Physics::PhysicsBodyHandle defendedTargetBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 50.0F, .y = 10.0F, .z = 10.0F},
            .position = {.x = 30'000.0F, .y = -2.0F, .z = 0.0F}});
    if (!defendedTargetBody.IsValid())
        return fail("defended target fixture creation");
    const Perception::Track defendedTrack = MakeTrack(7010U, 30'000.0F);
    auto defendedRuntimeResult = CreateP700GranitRuntime(definition, 0.0);
    if (!defendedRuntimeResult)
        return fail("defended runtime creation");
    auto defendedRuntime = *defendedRuntimeResult;
    const auto defendedLaunch = LaunchP700Granit(definition, defendedRuntime, defendedTrack, SubmergedCarrier(), 0.0);
    if (!defendedLaunch || !defendedLaunch->allowed)
        return fail("defended launch employment");
    const P700TerminalDefenseProfile guaranteedHardKill{
        .seekerFailureProbability = 0.0F,
        .softKillProbability = 0.0F,
        .hardKillProbability = 1.0F,
        .maneuverDefeatProbability = 0.0F};
    std::optional<P700GranitImpact> defendedImpact{};
    double defendedTime = 0.0;
    for (int step = 0; step < 120 && defendedRuntime.phase != P700GranitPhase::Defeated; ++step)
    {
        defendedTime += 1.0;
        const auto advanced = AdvanceP700GranitWithCollision(
            definition, defendedRuntime, defendedTrack, physicsWorld, defendedTime, carrierBody, guaranteedHardKill);
        if (!advanced)
            return fail("defended terminal advance");
        if (*advanced)
            defendedImpact = **advanced;
    }
    if (defendedImpact || defendedRuntime.phase != P700GranitPhase::Defeated ||
        defendedRuntime.terminalOutcome != P700TerminalEngagementOutcome::HardKill ||
        defendedRuntime.impactedBody.has_value())
    {
        return fail("terminal hard-kill probability must defeat without a fake impact");
    }
    const auto defeatedSpent = AdvanceP700GranitWithCollision(
        definition, defendedRuntime, std::nullopt, physicsWorld, defendedTime + 0.1, carrierBody, guaranteedHardKill);
    if (!defeatedSpent || *defeatedSpent || defendedRuntime.phase != P700GranitPhase::Spent)
        return fail("defeated P-700 must consume cleanly into Spent");

    if (!physicsWorld.DestroyBody(carrierBody) || !physicsWorld.DestroyBody(targetBody) ||
        !physicsWorld.DestroyBody(defendedTargetBody))
    {
        return fail("P-700 fixture cleanup");
    }
    return true;
}
} // namespace DeepRun::Tests
