#pragma once

#include "Game/Combat/SimpleDestroyerCombatController.h"

#include <optional>
#include <vector>

namespace DeepRun::Tests
{
namespace M5SimpleDestroyerCombatDetail
{
[[nodiscard]] inline Perception::Track MakeBearingTrack(
    const std::uint64_t trackId,
    const float confidence = 0.80F,
    const float bearingUncertaintyRadians = 0.10F)
{
    return Perception::Track{
        .trackId = trackId,
        .contactId = trackId + 1000U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = std::nullopt,
        .positionUncertaintyMeters = std::nullopt,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.25F,
        .bearingUncertaintyRadians = bearingUncertaintyRadians,
        .confidence = confidence,
        .observationCount = 3U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 0.0};
}

[[nodiscard]] inline Perception::Track MakeSpatialTrack(
    const std::uint64_t trackId,
    const float confidence = 0.75F)
{
    auto track = MakeBearingTrack(trackId, confidence, 0.05F);
    track.estimatedPositionMeters = Physics::PhysicsVector3{.x = 1500.0F, .y = -50.0F, .z = 0.0F};
    track.positionUncertaintyMeters = 40.0F;
    return track;
}
}

[[nodiscard]] inline bool RunM5SimpleDestroyerCombatChecks()
{
    using namespace M5SimpleDestroyerCombatDetail;
    using namespace Game::Combat;
    using namespace Weapons;

    const SimpleDestroyerCombatConfig controllerConfig{
        .minimumAwarenessConfidence = 0.35F,
        .maximumAwarenessBearingUncertaintyRadians = 0.30F,
        .allowCoastingAwareness = false};
    const WeaponDefinition weaponDefinition{
        .id = "m5.destroyer-heavyweight",
        .preparationSeconds = 5.0,
        .targeting = WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};

    auto weaponResult = CreateWeaponRuntime(weaponDefinition, 0.0);
    if (!weaponResult)
    {
        return false;
    }
    auto weapon = *weaponResult;
    SimpleDestroyerCombatState controller{};

    // No perceived threat: the AI must not prepare a weapon just because world truth exists elsewhere.
    const auto noContact = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {}, 0.5);
    if (!noContact || noContact->action != SimpleDestroyerCombatAction::Hold ||
        weapon.phase != WeaponPhase::Stored || weapon.targetTrackId.has_value())
    {
        return false;
    }

    auto weakTrack = MakeBearingTrack(10U, 0.20F);
    const auto weakOnly = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {weakTrack}, 1.0);
    if (!weakOnly || weakOnly->action != SimpleDestroyerCombatAction::Hold || weapon.phase != WeaponPhase::Stored)
    {
        return false;
    }

    // Bearing-only evidence is enough for threat awareness/preparation, but not enough for a position-requiring
    // weapon target solution.
    const auto bearingOnly = MakeBearingTrack(20U, 0.85F);
    const auto prepare = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {bearingOnly}, 2.0);
    if (!prepare || prepare->action != SimpleDestroyerCombatAction::PrepareWeapon ||
        prepare->perceivedTrackId != bearingOnly.trackId || weapon.phase != WeaponPhase::Preparing ||
        weapon.targetTrackId.has_value())
    {
        return false;
    }

    // A weaker but spatially qualified current Track can become the actual weapon solution while preparation
    // continues. The controller must use perceived quality, not an entity/Transform identity.
    const auto spatial = MakeSpatialTrack(30U, 0.78F);
    const auto trackTarget = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {bearingOnly, spatial}, 3.0);
    if (!trackTarget || trackTarget->action != SimpleDestroyerCombatAction::TrackTarget ||
        trackTarget->perceivedTrackId != spatial.trackId || weapon.phase != WeaponPhase::Preparing ||
        weapon.targetTrackId != spatial.trackId)
    {
        return false;
    }

    // Preparation completes at t=7 (five seconds after t=2), but the current track has disappeared. The old
    // targetTrackId remains historical weapon state and must not authorize an AI launch by itself.
    const auto staleAtReady = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {}, 7.0);
    if (!staleAtReady || staleAtReady->action != SimpleDestroyerCombatAction::Hold ||
        weapon.phase != WeaponPhase::Ready || weapon.targetTrackId != spatial.trackId)
    {
        return false;
    }

    // Current qualified perception returns: only now may the ready weapon launch.
    const auto launch = AdvanceSimpleDestroyerCombat(
        controllerConfig, weaponDefinition, controller, weapon, {spatial}, 8.0);
    if (!launch || launch->action != SimpleDestroyerCombatAction::LaunchWeapon ||
        launch->perceivedTrackId != spatial.trackId || weapon.phase != WeaponPhase::Launched ||
        weapon.targetTrackId != spatial.trackId)
    {
        return false;
    }

    const double controllerTimeBeforeReverse = controller.lastUpdateTimeSeconds;
    const double weaponTimeBeforeReverse = weapon.lastUpdateTimeSeconds;
    if (AdvanceSimpleDestroyerCombat(controllerConfig, weaponDefinition, controller, weapon, {spatial}, 7.5) ||
        controller.lastUpdateTimeSeconds != controllerTimeBeforeReverse ||
        weapon.lastUpdateTimeSeconds != weaponTimeBeforeReverse)
    {
        return false;
    }

    // Deterministic selection: confidence wins, then lower bearing uncertainty, then lower stable track id.
    // Temporary candidate vectors are intentional here: selectors return value-safe optional Tracks rather
    // than pointers into caller-owned storage.
    const auto first = MakeSpatialTrack(40U, 0.82F);
    auto second = MakeSpatialTrack(41U, 0.90F);
    const auto best = SelectBestWeaponQualifiedTrack(weaponDefinition, controllerConfig, {first, second});
    if (!best || best->trackId != second.trackId)
    {
        return false;
    }
    second.confidence = first.confidence;
    second.bearingUncertaintyRadians = first.bearingUncertaintyRadians;
    const auto stableTie = SelectBestWeaponQualifiedTrack(weaponDefinition, controllerConfig, {second, first});
    if (!stableTie || stableTie->trackId != first.trackId)
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
