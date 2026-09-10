#pragma once

#include "Game/Combat/CombatPlaygroundCamera.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5CombatPlaygroundRuntimeChecks(Physics::PhysicsWorld& physicsWorld)
{
    using Game::Combat::CombatPlaygroundCameraMode;
    using Game::Combat::CombatPlaygroundPresentationElement;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }

    const auto presentationModel = Game::Combat::BuildCombatPlaygroundPresentationModel();
    if (!presentationModel || presentationModel->materials.size() != 1U ||
        presentationModel->primitives.size() != 1U || presentationModel->nodes.size() != 1U ||
        presentationModel->primitives[0].vertices.size() != 24U ||
        presentationModel->primitives[0].indices.size() != 36U ||
        !presentationModel->primitives[0].hasNormals ||
        presentationModel->nodes[0].primitiveIndices != std::vector<std::size_t>{0U})
    {
        return false;
    }

    const auto runtimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = *runtimeResult;

    const auto initialDestroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    if (!initialDestroyerState ||
        std::abs(initialDestroyerState->position.x - Game::Combat::M5CombatDestroyerInitialXMeters) > 0.01F)
    {
        return false;
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
        return false;
    }
    const auto playerSnapshot = *playerSnapshotResult;

    Game::Combat::CombatPlaygroundCameraDirector cameraDirector;
    bool sawPlayerSpatialTrack = false;
    bool sawDestroyerAwareness = false;
    bool sawDestroyerPreparation = false;
    bool sawTorpedo = false;
    bool sawDecoy = false;
    bool sawLiveSeekerSelection = false;
    bool sawDecoyDiversion = false;
    bool sawPostDecoyRecovery = false;
    bool sawImpact = false;
    bool sawPresentationTorpedo = false;
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
            return false;
        }

        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);
        if (!cameraFraming ||
            std::abs(cameraFraming->targetOffsetXMeters - Game::Combat::M5CombatCameraTargetOffsetXMeters) > 0.001F ||
            cameraFraming->horizontalSpanMeters + 0.001F < previousCameraSpan ||
            cameraFraming->horizontalSpanMeters < Game::Combat::M5CombatLocalCameraHorizontalSpanMeters - 0.001F ||
            cameraFraming->horizontalSpanMeters > Game::Combat::M5CombatTacticalCameraHorizontalSpanMeters + 0.001F)
        {
            return false;
        }

        if (cameraFraming->mode == CombatPlaygroundCameraMode::LocalLaunch)
        {
            if (cameraEverLeftLocal ||
                std::abs(cameraFraming->horizontalSpanMeters - Game::Combat::M5CombatLocalCameraHorizontalSpanMeters) >
                    0.001F ||
                cameraFraming->transitionProgress != 0.0F)
            {
                return false;
            }
        }
        else if (cameraFraming->mode == CombatPlaygroundCameraMode::TransitionToTactical)
        {
            cameraEverLeftLocal = true;
            sawCameraTransition = true;
            if (cameraFraming->transitionProgress < 0.0F || cameraFraming->transitionProgress >= 1.0F)
            {
                return false;
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
                return false;
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
            sawDestroyerAwareness = sawDestroyerAwareness ||
                track.lifecycle == Perception::TrackLifecycleState::Confirmed;
            // Passive destroyer awareness must remain bearing-only in this live composition.
            if (track.estimatedPositionMeters.has_value())
            {
                return false;
            }
        }
        sawDestroyerPreparation = sawDestroyerPreparation ||
            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;
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
                return false;
            }

            if (!sawHorizontalLaunch)
            {
                if (std::abs(launchPosition->x - playerSnapshot.emitter.positionMeters.x -
                             Game::Combat::M5CombatTorpedoLaunchClearanceMeters) > 0.01F ||
                    std::abs(launchPosition->y - playerSnapshot.emitter.positionMeters.y) > 0.01F ||
                    std::abs(launchPosition->z - playerSnapshot.emitter.positionMeters.z) > 0.01F ||
                    std::abs(torpedo.headingRadians) > 0.01F)
                {
                    return false;
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
                    return false;
                }
                sawStraightRunout = true;
            }
            if (forwardProgress > Game::Combat::M5CombatTorpedoStraightRunMeters + 40.0F &&
                torpedo.positionMeters.y > launchPosition->y + 1.0F)
            {
                sawGradualAscent = true;
            }
        }

        if (frame->playerTorpedoImpact)
        {
            if (frame->playerTorpedoImpact->physicsHit.body != runtime.Destroyer().body ||
                !runtime.LastExplosion().has_value())
            {
                return false;
            }
            sawImpact = true;
        }

        // M5-H.1-A presentation is a pure read-only projection of already-authoritative combat/physics state.
        // Building snapshots/draws on every fixed tick must not create a second collision, target, or time path.
        const auto presentationSnapshot = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(
            runtime, physicsWorld, simulationTimeSeconds);
        if (!presentationSnapshot)
        {
            return false;
        }
        const auto currentDestroyerBody = physicsWorld.GetBodyState(runtime.Destroyer().body);
        if (!currentDestroyerBody || presentationSnapshot->destroyerBody.position != currentDestroyerBody->position ||
            !Physics::PhysicsQuaternion::SameRotation(
                presentationSnapshot->destroyerBody.orientation, currentDestroyerBody->orientation) ||
            presentationSnapshot->destroyerDestroyed != runtime.Destroyer().integrity.destroyed)
        {
            return false;
        }

        const auto presentationDraws = Game::Combat::BuildCombatPlaygroundPresentationDraws(*presentationSnapshot);
        if (!presentationDraws || presentationDraws->size() < 2U || presentationDraws->size() > 5U ||
            (*presentationDraws)[0].element != CombatPlaygroundPresentationElement::DestroyerHull ||
            (*presentationDraws)[1].element != CombatPlaygroundPresentationElement::DestroyerSuperstructure)
        {
            return false;
        }
        if (tick == 0 && presentationDraws->size() != 2U)
        {
            return false;
        }

        const auto hasElement = [&presentationDraws](const CombatPlaygroundPresentationElement element)
        {
            return std::ranges::any_of(*presentationDraws, [element](const auto& draw) {
                return draw.element == element;
            });
        };
        sawPresentationTorpedo = sawPresentationTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        sawPresentationDecoy = sawPresentationDecoy ||
            hasElement(CombatPlaygroundPresentationElement::AcousticDecoy);
        sawPresentationExplosion = sawPresentationExplosion ||
            hasElement(CombatPlaygroundPresentationElement::Explosion);

        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawPostImpactTorpedoHidden = sawPostImpactTorpedoHidden ||
                !hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        }

        physicsWorld.Step(fixedDeltaSeconds);
    }

    const auto destroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerPreparation || !sawTorpedo ||
        !sawDecoy || !sawLiveSeekerSelection || !sawDecoyDiversion || !sawPostDecoyRecovery ||
        !sawImpact || !sawPresentationTorpedo || !sawPresentationDecoy ||
        !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
        !sawStraightRunout || !sawGradualAscent || !sawCameraTransition || !sawTacticalCamera ||
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase == Weapons::WeaponPhase::Launched ||
        runtime.Destroyer().integrity.destroyed ||
        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||
        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent)
    {
        return false;
    }

    const auto finalPresentation = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, 45.0);
    if (!finalPresentation || std::abs(finalPresentation->destroyerIntegrityFraction - 0.40F) > 0.001F)
    {
        return false;
    }

    // The camera transition is one-way and SimulationTime authoritative. A time-reversing request must be
    // rejected instead of rewinding the cinematic framing back toward the submarine.
    if (cameraDirector.Evaluate(runtime, 44.0))
    {
        return false;
    }

    // The destroyer AI heard and reacted to the player but never received a ranged target solution, so it must
    // not launch merely because the scenario simulator knows where the player is.
    if (runtime.Destroyer().weapon.targetTrackId.has_value())
    {
        return false;
    }

    return physicsWorld.DestroyBody(runtime.Destroyer().body);
}
} // namespace DeepRun::Tests
