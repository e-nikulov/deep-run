#pragma once

#include "Game/Combat/CombatPlaygroundCamera.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5CombatPlaygroundRuntimeChecks(Physics::PhysicsWorld& physicsWorld)
{
    const auto fail = [](const int line) {
        std::cerr << "[M5-F.2 diagnostic] failure line " << line << '\n';
        return fail(__LINE__);
    };
    using Game::Combat::CombatPlaygroundCameraMode;
    using Game::Combat::CombatPlaygroundPresentationElement;

    if (!physicsWorld.IsInitialized())
    {
        return fail(__LINE__);
    }

    const auto presentationModel = Game::Combat::BuildCombatPlaygroundPresentationModel();
    if (!presentationModel || presentationModel->materials.size() != 1U ||
        presentationModel->primitives.size() != 1U || presentationModel->nodes.size() != 1U ||
        presentationModel->primitives[0].vertices.size() != 24U ||
        presentationModel->primitives[0].indices.size() != 36U ||
        !presentationModel->primitives[0].hasNormals ||
        presentationModel->nodes[0].primitiveIndices != std::vector<std::size_t>{0U})
    {
        return fail(__LINE__);
    }

    const auto runtimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return fail(__LINE__);
    }
    auto runtime = *runtimeResult;

    const auto initialDestroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    if (!initialDestroyerState ||
        std::abs(initialDestroyerState->position.x - Game::Combat::M5CombatDestroyerInitialXMeters) > 0.01F)
    {
        return fail(__LINE__);
    }

    const auto playerSnapshotResult = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = {.x = 0.0F, .y = -100.0F, .z = 0.0F},
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 35.0F,
            .signedDepthMeters = 100.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    if (!playerSnapshotResult)
    {
        return fail(__LINE__);
    }
    const auto playerSnapshot = *playerSnapshotResult;

    // M5-F.2 uses a real PhysicsWorld body and the same I.2 physical-proxy bridge as the windowed production
    // composition. The hostile torpedo never receives this handle; it can discover it only through swept collision.
    const Physics::PhysicsVector3 playerHalfExtentsMeters{.x = 75.0F, .y = 8.0F, .z = 8.0F};
    const auto playerBody = physicsWorld.CreateDynamicBoxBody(Physics::DynamicBoxBodyCreateInfo{
        .halfExtents = playerHalfExtentsMeters,
        .mass = 12'000'000.0F,
        .position = playerSnapshot.emitter.positionMeters,
        .orientation = {},
        .gravityEnabled = false,
        .linearDamping = 0.0F,
        .angularDamping = 0.0F,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {}});
    if (!playerBody.IsValid())
    {
        return fail(__LINE__);
    }
    const auto boundPlayer = runtime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = playerBody,
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .orientation = {},
            .halfExtentsMeters = playerHalfExtentsMeters},
        playerSnapshot,
        0.0);
    if (!boundPlayer || !runtime.Mine().has_value())
    {
        return fail(__LINE__);
    }

    Game::Combat::CombatPlaygroundCameraDirector cameraDirector;
    bool sawPlayerSpatialTrack = false;
    bool sawDestroyerAwareness = false;
    bool sawDestroyerBearingOnlyAwareness = false;
    bool sawDestroyerSpatialFireControlTrack = false;
    bool sawDestroyerPreparation = false;
    bool sawDestroyerLaunch = false;
    bool sawDestroyerTorpedoMaterialized = false;
    bool sawDestroyerTorpedoImpact = false;
    bool sawDestroyerTorpedoUnderwaterWithoutBodyIdentity = false;
    bool sawDestroyerTorpedoHiddenAfterImpact = false;
    bool sawTorpedo = false;
    bool sawDecoy = false;
    bool sawLiveSeekerSelection = false;
    bool sawDecoyDiversion = false;
    bool sawPostDecoyRecovery = false;
    bool sawImpact = false;
    bool sawPresentationTorpedo = false;
    bool sawPresentationDestroyerTorpedo = false;
    bool sawPresentationMine = false;
    bool sawPresentationDecoy = false;
    bool sawPresentationExplosion = false;
    bool sawPostImpactTorpedoHidden = false;
    bool sawHorizontalLaunch = false;
    bool sawStraightRunout = false;
    bool sawGradualAscent = false;
    bool sawCameraTransition = false;
    bool sawTacticalCamera = false;
    bool cameraEverLeftLocal = false;
    std::uint32_t stableTacticalTicks = 0U;
    float previousCameraSpan = Game::Combat::M5CombatLocalCameraHorizontalSpanMeters;

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    constexpr int finalTick = 2700; // 45 s: enough for active echo, 1.8 km run, impact, and post-impact stability.
    for (int tick = 0; tick <= finalTick; ++tick)
    {
        const double simulationTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.Advance(playerSnapshot, simulationTimeSeconds);
        if (!frame)
        {
            return fail(__LINE__);
        }

        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);
        if (!cameraFraming ||
            std::abs(cameraFraming->targetOffsetXMeters - Game::Combat::M5CombatCameraTargetOffsetXMeters) > 0.001F ||
            cameraFraming->horizontalSpanMeters + 0.001F < previousCameraSpan ||
            cameraFraming->horizontalSpanMeters < Game::Combat::M5CombatLocalCameraHorizontalSpanMeters - 0.001F ||
            cameraFraming->horizontalSpanMeters > Game::Combat::M5CombatTacticalCameraHorizontalSpanMeters + 0.001F)
        {
            return fail(__LINE__);
        }

        if (cameraFraming->mode == CombatPlaygroundCameraMode::LocalLaunch)
        {
            if (cameraEverLeftLocal ||
                std::abs(cameraFraming->horizontalSpanMeters - Game::Combat::M5CombatLocalCameraHorizontalSpanMeters) >
                    0.001F ||
                cameraFraming->transitionProgress != 0.0F)
            {
                return fail(__LINE__);
            }
        }
        else if (cameraFraming->mode == CombatPlaygroundCameraMode::TransitionToTactical)
        {
            cameraEverLeftLocal = true;
            sawCameraTransition = true;
            if (cameraFraming->transitionProgress < 0.0F || cameraFraming->transitionProgress >= 1.0F)
            {
                return fail(__LINE__);
            }
        }
        else if (cameraFraming->mode == CombatPlaygroundCameraMode::TacticalOverview)
        {
            cameraEverLeftLocal = true;
            sawTacticalCamera = true;
            ++stableTacticalTicks;
            if (std::abs(cameraFraming->horizontalSpanMeters - Game::Combat::M5CombatTacticalCameraHorizontalSpanMeters) >
                    0.001F ||
                std::abs(cameraFraming->transitionProgress - 1.0F) > 0.001F)
            {
                return fail(__LINE__);
            }
        }
        previousCameraSpan = cameraFraming->horizontalSpanMeters;

        for (const auto& track : frame->playerTracks)
        {
            sawPlayerSpatialTrack = sawPlayerSpatialTrack ||
                (track.lifecycle == Perception::TrackLifecycleState::Confirmed &&
                 track.estimatedPositionMeters.has_value() && track.positionUncertaintyMeters.has_value());
        }
        for (const auto& track : frame->destroyerTracks)
        {
            if (track.lifecycle == Perception::TrackLifecycleState::Confirmed)
            {
                sawDestroyerAwareness = true;
                if (track.estimatedPositionMeters.has_value())
                {
                    if (!track.positionUncertaintyMeters.has_value() ||
                        !std::isfinite(*track.positionUncertaintyMeters) || *track.positionUncertaintyMeters <= 0.0F)
                    {
                        return fail(__LINE__);
                    }
                    sawDestroyerSpatialFireControlTrack = true;
                }
                else
                {
                    sawDestroyerBearingOnlyAwareness = true;
                }
            }
        }
        sawDestroyerPreparation = sawDestroyerPreparation ||
            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;
        if (frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::LaunchWeapon)
        {
            const bool launchHadQualifiedPerceivedTrack = frame->destroyerDecision.perceivedTrackId.has_value() &&
                std::ranges::any_of(frame->destroyerTracks, [&runtime, &frame](const auto& track) {
                    return track.trackId == *frame->destroyerDecision.perceivedTrackId &&
                        Weapons::ValidateTrackForWeapon(runtime.DestroyerDefinition().weapon, track).has_value();
                });
            if (!launchHadQualifiedPerceivedTrack ||
                runtime.Destroyer().weapon.targetTrackId != frame->destroyerDecision.perceivedTrackId)
            {
                return fail(__LINE__);
            }
            if (!runtime.DestroyerTorpedo() || !runtime.DestroyerTorpedoLaunchPosition() ||
                runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Underwater ||
                runtime.DestroyerTorpedo()->impactedBody.has_value() ||
                runtime.DestroyerTorpedo()->guidanceTrackId != frame->destroyerDecision.perceivedTrackId ||
                runtime.DestroyerTorpedo()->weapon.targetTrackId != frame->destroyerDecision.perceivedTrackId)
            {
                return fail(__LINE__);
            }
            sawDestroyerLaunch = true;
            sawDestroyerTorpedoMaterialized = true;
        }
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
        sawDecoy = sawDecoy || (runtime.Decoy().has_value() && runtime.Decoy()->active);
        sawLiveSeekerSelection = sawLiveSeekerSelection || runtime.PlayerTorpedoSeekerState().selectedTrackId.has_value();

        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto& torpedo = *runtime.PlayerTorpedo();
            const auto& launchPosition = runtime.PlayerTorpedoLaunchPosition();
            if (!launchPosition || torpedo.positionMeters.y >= -Game::Combat::M5CombatTorpedoSurfaceSafetyMarginMeters ||
                std::abs(torpedo.headingRadians) >
                    Game::Combat::M5CombatTorpedoMaximumVerticalCourseAngleRadians + 0.001F)
            {
                return fail(__LINE__);
            }

            if (!sawHorizontalLaunch)
            {
                if (std::abs(launchPosition->x - playerSnapshot.emitter.positionMeters.x -
                             Game::Combat::M5CombatTorpedoLaunchClearanceMeters) > 0.01F ||
                    std::abs(launchPosition->y - playerSnapshot.emitter.positionMeters.y) > 0.01F ||
                    std::abs(launchPosition->z - playerSnapshot.emitter.positionMeters.z) > 0.01F ||
                    std::abs(torpedo.headingRadians) > 0.01F)
                {
                    return fail(__LINE__);
                }
                sawHorizontalLaunch = true;
            }

            const float forwardProgress = torpedo.positionMeters.x - launchPosition->x;
            if (forwardProgress > Game::Combat::M5CombatTorpedoStraightRunMeters + 20.0F &&
                runtime.Decoy() && runtime.Decoy()->active &&
                runtime.PlayerTorpedoSeekerState().selectedTrackId.has_value() && torpedo.headingRadians < -0.002F)
            {
                sawDecoyDiversion = true;
            }
            if (sawDecoyDiversion && runtime.Decoy() && !runtime.Decoy()->active && torpedo.headingRadians > 0.01F)
            {
                sawPostDecoyRecovery = true;
            }

            if (forwardProgress >= 120.0F &&
                forwardProgress <= Game::Combat::M5CombatTorpedoStraightRunMeters - 2.0F)
            {
                if (std::abs(torpedo.positionMeters.y - launchPosition->y) > 0.25F ||
                    std::abs(torpedo.headingRadians) > 0.01F)
                {
                    return fail(__LINE__);
                }
                sawStraightRunout = true;
            }
            if (forwardProgress > Game::Combat::M5CombatTorpedoStraightRunMeters + 40.0F &&
                torpedo.positionMeters.y > launchPosition->y + 1.0F)
            {
                sawGradualAscent = true;
            }
        }

        if (runtime.DestroyerTorpedo() &&
            runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater)
        {
            if (runtime.DestroyerTorpedo()->impactedBody.has_value())
            {
                return fail(__LINE__);
            }
            sawDestroyerTorpedoUnderwaterWithoutBodyIdentity = true;
        }

        if (frame->playerTorpedoImpact)
        {
            if (frame->playerTorpedoImpact->physicsHit.body != runtime.Destroyer().body ||
                !runtime.LastExplosion().has_value())
            {
                return fail(__LINE__);
            }
            sawImpact = true;
        }
        if (frame->destroyerTorpedoImpact)
        {
            if (frame->destroyerTorpedoImpact->physicsHit.body != playerBody ||
                frame->destroyerTorpedoImpact->damage.targetBody != playerBody ||
                !runtime.PlayerIntegrity().has_value() || runtime.PlayerIntegrity()->destroyed)
            {
                return fail(__LINE__);
            }
            sawDestroyerTorpedoImpact = true;
        }

        // M5-H.1-A presentation is a pure read-only projection of already-authoritative combat/physics state.
        // Building snapshots/draws on every fixed tick must not create a second collision, target, or time path.
        const auto presentationSnapshot = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(
            runtime, physicsWorld, simulationTimeSeconds);
        if (!presentationSnapshot)
        {
            return fail(__LINE__);
        }
        const auto currentDestroyerBody = physicsWorld.GetBodyState(runtime.Destroyer().body);
        if (!currentDestroyerBody || presentationSnapshot->destroyerBody.position != currentDestroyerBody->position ||
            !Physics::PhysicsQuaternion::SameRotation(
                presentationSnapshot->destroyerBody.orientation, currentDestroyerBody->orientation) ||
            presentationSnapshot->destroyerDestroyed != runtime.Destroyer().integrity.destroyed)
        {
            return fail(__LINE__);
        }

        const auto presentationDraws = Game::Combat::BuildCombatPlaygroundPresentationDraws(*presentationSnapshot);
        if (!presentationDraws || presentationDraws->size() < 2U || presentationDraws->size() > 7U ||
            (*presentationDraws)[0].element != CombatPlaygroundPresentationElement::DestroyerHull ||
            (*presentationDraws)[1].element != CombatPlaygroundPresentationElement::DestroyerSuperstructure)
        {
            return fail(__LINE__);
        }
        if (tick == 0 && (presentationDraws->size() != 3U ||
                          (*presentationDraws)[2].element != CombatPlaygroundPresentationElement::NavalMine))
        {
            return fail(__LINE__);
        }

        const auto hasElement = [&presentationDraws](const CombatPlaygroundPresentationElement element)
        {
            return std::ranges::any_of(*presentationDraws, [element](const auto& draw) {
                return draw.element == element;
            });
        };
        sawPresentationTorpedo = sawPresentationTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        sawPresentationDestroyerTorpedo = sawPresentationDestroyerTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::DestroyerTorpedo);
        sawPresentationMine = sawPresentationMine ||
            hasElement(CombatPlaygroundPresentationElement::NavalMine);
        sawPresentationDecoy = sawPresentationDecoy ||
            hasElement(CombatPlaygroundPresentationElement::AcousticDecoy);
        sawPresentationExplosion = sawPresentationExplosion ||
            hasElement(CombatPlaygroundPresentationElement::Explosion);

        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawPostImpactTorpedoHidden = sawPostImpactTorpedoHidden ||
                !hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        }
        if (runtime.DestroyerTorpedo() && runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawDestroyerTorpedoHiddenAfterImpact = sawDestroyerTorpedoHiddenAfterImpact ||
                !hasElement(CombatPlaygroundPresentationElement::DestroyerTorpedo);
        }

        physicsWorld.Step(fixedDeltaSeconds);
    }

    const auto destroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerBearingOnlyAwareness ||
        !sawDestroyerSpatialFireControlTrack || !sawDestroyerPreparation || !sawDestroyerLaunch ||
        !sawDestroyerTorpedoMaterialized || !sawDestroyerTorpedoImpact ||
        !sawDestroyerTorpedoUnderwaterWithoutBodyIdentity || !sawDestroyerTorpedoHiddenAfterImpact || !sawTorpedo ||
        !sawDecoy || !sawLiveSeekerSelection || !sawDecoyDiversion || !sawPostDecoyRecovery ||
        !sawImpact || !sawPresentationTorpedo || !sawPresentationDestroyerTorpedo || !sawPresentationMine ||
        !sawPresentationDecoy || !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
        !sawStraightRunout || !sawGradualAscent || !sawCameraTransition || !sawTacticalCamera ||
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase != Weapons::WeaponPhase::Launched ||
        !runtime.Destroyer().weapon.targetTrackId.has_value() ||
        runtime.Destroyer().integrity.destroyed ||
        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||
        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||
        !runtime.DestroyerTorpedo() || runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||
        runtime.DestroyerTorpedo()->impactedBody != std::optional<Physics::PhysicsBodyHandle>{playerBody} ||
        !runtime.PlayerIntegrity() || runtime.PlayerIntegrity()->destroyed ||
        std::abs(runtime.PlayerIntegrity()->remainingIntegrity - 45.0F) > 0.001F ||
        !runtime.Mine() || runtime.Mine()->detonated)
    {
        return fail(__LINE__);
    }

    const auto finalPresentation = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, 45.0);
    if (!finalPresentation || std::abs(finalPresentation->destroyerIntegrityFraction - 0.40F) > 0.001F)
    {
        return fail(__LINE__);
    }

    // The camera transition is one-way and SimulationTime authoritative. A time-reversing request must be
    // rejected instead of rewinding the cinematic framing back toward the submarine.
    if (cameraDirector.Evaluate(runtime, 44.0))
    {
        return fail(__LINE__);
    }

    // M5-F.2 now proves the reciprocal physical consequence as well: target identity enters the hostile torpedo
    // only at PhysicsWorld impact, then the existing combat-integrity authority applies damage to the bound body.
    const auto mineBody = runtime.Mine()->body;
    const bool destroyedMine = physicsWorld.DestroyBody(mineBody);
    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);
    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);
    return destroyedMine && destroyedPlayer && destroyedDestroyer;
}
} // namespace DeepRun::Tests
