#pragma once

#include "Game/Combat/SimpleDestroyerRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"
#include "Simulation/Weapons/AcousticDecoy.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Combat
{
// M5 playground framing/weapon-profile tuning. These values exist to make the bounded side-view combat scenario
// readable and to distinguish a conventional heavyweight torpedo from a future P-700 water-exit/airborne path.
// They are gameplay-authored values, not claimed real-world Project 949A or torpedo performance data.
inline constexpr float M5CombatCameraTargetOffsetXMeters = 150.0F;
inline constexpr float M5CombatDestroyerInitialXMeters = 380.0F;
inline constexpr float M5CombatTorpedoLaunchClearanceMeters = 85.0F;
inline constexpr float M5CombatTorpedoStraightRunMeters = 60.0F;
inline constexpr float M5CombatTorpedoMaximumVerticalCourseAngleRadians = 0.55F;
inline constexpr float M5CombatTorpedoAttackPointBelowPerceivedTargetMeters = 1.5F;
inline constexpr float M5CombatTorpedoSurfaceSafetyMarginMeters = 0.25F;

struct CombatPlaygroundFrame final
{
    std::vector<Perception::Track> playerTracks;
    std::vector<Perception::Track> destroyerTracks;
    SimpleDestroyerCombatDecision destroyerDecision{};
    std::optional<Weapons::ConventionalTorpedoImpact> playerTorpedoImpact{};
};

// M5-H live fixed-step combat composition. Authoritative scenario truth is used only where a simulator must
// physically/acoustically model the remote participant. Player targeting and destroyer AI consume Tracks only;
// target body identity reaches weapon state only after PhysicsWorld confirms an impact.
class CombatPlaygroundRuntime final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundRuntime, std::string> Create(
        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds)
    {
        if (!physicsWorld.IsInitialized() || !std::isfinite(surfaceLevelY) ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        {
            return std::unexpected("M5-H combat playground creation input is invalid");
        }

        const auto acousticWorld = Acoustics::AcousticWorld::Create({});
        if (!acousticWorld)
        {
            return std::unexpected("M5-H acoustic world creation failed: " + acousticWorld.error().message);
        }
        const auto playerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 5.0,
            .lostAfterSeconds = 20.0,
            .confidenceDecayPerSecond = 0.02F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.01F,
            .positionUncertaintyGrowthMetersPerSecond = 5.0F,
            .maximumTracks = 8U});
        const auto destroyerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .observationsToConfirm = 2U,
            .coastAfterSeconds = 5.0,
            .lostAfterSeconds = 20.0,
            .confidenceDecayPerSecond = 0.02F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.01F,
            .positionUncertaintyGrowthMetersPerSecond = 5.0F,
            .maximumTracks = 8U});
        if (!playerTracks || !destroyerTracks)
        {
            return std::unexpected("M5-H perception manager creation failed");
        }

        const SimpleDestroyerDefinition destroyerDefinition{
            .id = "m5.live-destroyer-proxy",
            .collisionHalfExtentsMeters = {.x = 25.0F, .y = 3.0F, .z = 3.0F},
            .massKilograms = 2'500'000.0F,
            .cruiseVelocityXMetersPerSecond = -2.0F,
            .bodyCenterBelowSurfaceMeters = 2.0F,
            .maximumIntegrity = 100.0F,
            .continuousSourceLevelDb = {.levelDb = {145.0F, 141.0F, 136.0F, 130.0F}},
            .passiveSensorId = "M5_LIVE_DESTROYER_PASSIVE_ARRAY",
            .ambientNoiseLevelDb = {.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}},
            .selfNoiseLevelDb = {.levelDb = {46.0F, 44.0F, 42.0F, 40.0F}},
            .sensitivityDb = {.levelDb = {1.0F, 1.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F,
            .weapon = Weapons::WeaponDefinition{
                .id = "m5.live-destroyer-heavyweight",
                .preparationSeconds = 1.0,
                .targeting = Weapons::WeaponTargetingRequirements{
                    .minimumTrackConfidence = 0.70F,
                    .maximumBearingUncertaintyRadians = 0.10F,
                    .maximumPositionUncertaintyMeters = 150.0F,
                    .requiresEstimatedPosition = true,
                    .allowCoastingTrack = false}},
            .combat = SimpleDestroyerCombatConfig{
                .minimumAwarenessConfidence = 0.35F,
                .maximumAwarenessBearingUncertaintyRadians = 0.30F,
                .allowCoastingAwareness = false}};
        auto destroyer = CreateSimpleDestroyerRuntime(
            destroyerDefinition,
            physicsWorld,
            surfaceLevelY,
            M5CombatDestroyerInitialXMeters,
            0.0F,
            simulationTimeSeconds);
        if (!destroyer)
        {
            return std::unexpected("M5-H destroyer creation failed: " + destroyer.error());
        }

        const Weapons::WeaponDefinition playerWeapon{
            .id = "m5.live-player-heavyweight",
            .preparationSeconds = 0.0,
            .targeting = Weapons::WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.65F,
                .maximumBearingUncertaintyRadians = 0.10F,
                .maximumPositionUncertaintyMeters = 150.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}};
        auto playerWeaponState = Weapons::CreateWeaponRuntime(playerWeapon, simulationTimeSeconds);
        if (!playerWeaponState)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5-H player weapon creation failed: " + playerWeaponState.error());
        }

        const Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition{
            .weapon = playerWeapon,
            .underwaterSpeedMetersPerSecond = 55.0F,
            .maximumTurnRateRadiansPerSecond = 0.45F,
            .maximumVerticalCourseAngleRadians = M5CombatTorpedoMaximumVerticalCourseAngleRadians,
            .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
            .directImpactDamage = 60.0F,
            .explosionRadiusMeters = 8.0F};
        const Weapons::AcousticDecoyDefinition decoyDefinition{
            .id = "m5.live-acoustic-decoy",
            .continuousSourceLevelDb = {.levelDb = {158.0F, 154.0F, 149.0F, 143.0F}},
            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},
            .activeLifetimeSeconds = 10.0};

        return CombatPlaygroundRuntime(
            physicsWorld,
            *acousticWorld,
            *playerTracks,
            *destroyerTracks,
            destroyerDefinition,
            *destroyer,
            playerTorpedoDefinition,
            *playerWeaponState,
            decoyDefinition,
            simulationTimeSeconds);
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!playerSnapshot.emitter.positionMeters.IsFinite() || !playerSnapshot.emitter.velocityMetersPerSecond.IsFinite() ||
            !playerSnapshot.emitter.continuousSourceLevelDb.IsFinite() ||
            !playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < lastUpdateTimeSeconds_)
        {
            return std::unexpected("M5-H live frame input is invalid or time-reversing");
        }

        const auto destroyerAcoustics = SampleSimpleDestroyerAcoustics(
            destroyerDefinition_, destroyer_, *physicsWorld_);
        if (!destroyerAcoustics)
        {
            return std::unexpected("M5-H destroyer acoustic snapshot failed: " + destroyerAcoustics.error());
        }

        // Destroyer awareness consumes a normal passive observation generated from the player's current
        // acoustic signature. The scenario knows both participants only to run propagation; source identity is
        // stripped before the observation crosses into its TrackManager/AI.
        const double passiveDistance = Distance(
            playerSnapshot.emitter.positionMeters,
            destroyerAcoustics->passiveReceiver.positionMeters);
        const double passiveTravelSeconds = passiveDistance /
            static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
        const double passiveEmissionTime = simulationTimeSeconds > passiveTravelSeconds
            ? std::max(0.0, simulationTimeSeconds - passiveTravelSeconds - 1.0e-6)
            : 0.0;
        const Acoustics::AcousticEmission playerEmission{
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .sourceLevelDb = playerSnapshot.emitter.continuousSourceLevelDb,
            .emissionTimeSeconds = passiveEmissionTime};
        const auto destroyerObserved = acousticWorld_.CollectPassiveDirectObservation(
            playerEmission, destroyerAcoustics->passiveReceiver, simulationTimeSeconds);
        if (!destroyerObserved)
        {
            return std::unexpected("M5-H destroyer passive propagation failed: " + destroyerObserved.error().message);
        }
        if (destroyerObserved->has_value())
        {
            const auto perceived = Perception::FromAcousticObservation(**destroyerObserved);
            if (!perceived || !destroyerTracks_.IntegrateObservation(*perceived))
            {
                return std::unexpected("M5-H destroyer passive evidence failed perception integration");
            }
        }
        else if (!destroyerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H destroyer TrackManager failed to advance");
        }

        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        // Emit one deterministic active pulse when the live scenario first has both participants. The reflector
        // position is simulator truth scoped to the active-echo calculation only; the player later receives only
        // the resulting ranged SensorObservation/Track.
        if (!activePulse_.has_value())
        {
            const Physics::PhysicsVector3 delta = Difference(
                destroyerAcoustics->emitter.positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const auto direction = Normalize(delta);
            if (!direction)
            {
                return std::unexpected("M5-H active pulse direction is invalid");
            }
            activePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = playerSnapshot.passiveReceiver.positionMeters,
                .forwardUnitVector = *direction,
                .sourceLevelDb = {.levelDb = {230.0F, 232.0F, 234.0F, 230.0F}},
                .beamHalfAngleRadians = 0.35F,
                .emissionTimeSeconds = simulationTimeSeconds};
            activeReflector_ = Acoustics::AcousticReflector{
                .positionMeters = destroyerAcoustics->emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
        }

        if (!activeEchoConsumed_ && activePulse_ && activeReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = playerSnapshot.passiveReceiver;
            activeReceiver.positionMeters = activePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *activePulse_, *activeReflector_, activeReceiver, simulationTimeSeconds);
            if (!activeEcho)
            {
                return std::unexpected("M5-H player active echo failed: " + activeEcho.error());
            }
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, activePulse_->originMeters);
                if (!perceived || !playerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-H active echo failed perception integration");
                }
                activeEchoConsumed_ = true;
            }
        }
        else if (!playerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H player TrackManager failed to advance");
        }

        if (!playerTorpedo_.has_value())
        {
            const auto qualifyingTrack = BestPlayerWeaponTrack(playerTracks_.Tracks());
            if (qualifyingTrack)
            {
                if (playerWeapon_.phase == Weapons::WeaponPhase::Stored &&
                    !Weapons::PrepareWeapon(playerTorpedoDefinition_.weapon, playerWeapon_, simulationTimeSeconds))
                {
                    return std::unexpected("M5-H player weapon preparation failed");
                }
                if (!Weapons::AssignWeaponTarget(
                        playerTorpedoDefinition_.weapon, playerWeapon_, *qualifyingTrack, simulationTimeSeconds) ||
                    !Weapons::LaunchWeapon(playerTorpedoDefinition_.weapon, playerWeapon_, simulationTimeSeconds))
                {
                    return std::unexpected("M5-H player weapon target/launch failed");
                }

                const float targetDeltaX = qualifyingTrack->estimatedPositionMeters->x - playerSnapshot.emitter.positionMeters.x;
                if (!std::isfinite(targetDeltaX) || std::abs(targetDeltaX) <= 1.0e-3F)
                {
                    return std::unexpected("M5-H torpedo launch has no horizontal separation from its perceived track");
                }
                playerTorpedoForwardSign_ = targetDeltaX > 0.0F ? 1.0F : -1.0F;
                const Physics::PhysicsVector3 launchPosition{
                    .x = playerSnapshot.emitter.positionMeters.x +
                         playerTorpedoForwardSign_ * M5CombatTorpedoLaunchClearanceMeters,
                    .y = playerSnapshot.emitter.positionMeters.y,
                    .z = playerSnapshot.emitter.positionMeters.z};
                const float launchHeading = playerTorpedoForwardSign_ > 0.0F ? 0.0F : 3.1415927F;
                const auto launched = Weapons::CreateLaunchedConventionalTorpedo(
                    playerTorpedoDefinition_, playerWeapon_, launchPosition, launchHeading,
                    *qualifyingTrack, simulationTimeSeconds);
                if (!launched)
                {
                    return std::unexpected("M5-H torpedo runtime creation failed: " + launched.error());
                }
                playerTorpedo_ = *launched;
                playerTorpedoLaunchPosition_ = launchPosition;

                const Physics::PhysicsVector3 decoyPosition{
                    .x = destroyerAcoustics->emitter.positionMeters.x - 20.0F,
                    .y = destroyerAcoustics->emitter.positionMeters.y - 15.0F,
                    .z = destroyerAcoustics->emitter.positionMeters.z};
                const auto decoy = Weapons::DeployAcousticDecoy(
                    decoyDefinition_, decoyPosition, simulationTimeSeconds);
                if (!decoy)
                {
                    return std::unexpected("M5-H decoy deployment failed: " + decoy.error());
                }
                decoy_ = *decoy;
            }
        }

        std::optional<Weapons::ConventionalTorpedoImpact> impact{};
        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerTorpedo_->guidanceTrackId);
            const auto guidanceTrack = BuildPlayerTorpedoGuidanceTrack(perceivedTrack);
            const auto advanced = Weapons::AdvanceConventionalTorpedoWithCollision(
                playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-H torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                impact = **advanced;
                lastExplosion_ = impact->explosion;
                if (impact->physicsHit.body != destroyer_.body)
                {
                    return std::unexpected("M5-H torpedo struck an unexpected physical body");
                }
                const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                if (!damaged)
                {
                    return std::unexpected("M5-H destroyer damage application failed: " + damaged.error());
                }
            }
        }

        if (decoy_)
        {
            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-H decoy advance failed: " + advanced.error());
            }
        }

        lastUpdateTimeSeconds_ = simulationTimeSeconds;
        return CombatPlaygroundFrame{
            .playerTracks = playerTracks_.Tracks(),
            .destroyerTracks = destroyerTracks_.Tracks(),
            .destroyerDecision = *destroyerDecision,
            .playerTorpedoImpact = impact};
    }

    [[nodiscard]] const SimpleDestroyerRuntimeState& Destroyer() const noexcept { return destroyer_; }
    [[nodiscard]] const SimpleDestroyerDefinition& DestroyerDefinition() const noexcept { return destroyerDefinition_; }
    [[nodiscard]] const std::optional<Weapons::ConventionalTorpedoRuntimeState>& PlayerTorpedo() const noexcept
    {
        return playerTorpedo_;
    }
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept
    {
        return playerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
    [[nodiscard]] const std::optional<DeepRun::Combat::CombatExplosionEvent>& LastExplosion() const noexcept
    {
        return lastExplosion_;
    }

private:
    CombatPlaygroundRuntime(
        Physics::PhysicsWorld& physicsWorld,
        Acoustics::AcousticWorld acousticWorld,
        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        SimpleDestroyerDefinition destroyerDefinition,
        SimpleDestroyerRuntimeState destroyer,
        Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition,
        Weapons::WeaponRuntimeState playerWeapon,
        Weapons::AcousticDecoyDefinition decoyDefinition,
        const double simulationTimeSeconds)
        : physicsWorld_(&physicsWorld),
          acousticWorld_(std::move(acousticWorld)),
          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          destroyerDefinition_(std::move(destroyerDefinition)),
          destroyer_(std::move(destroyer)),
          playerTorpedoDefinition_(std::move(playerTorpedoDefinition)),
          playerWeapon_(std::move(playerWeapon)),
          decoyDefinition_(std::move(decoyDefinition)),
          lastUpdateTimeSeconds_(simulationTimeSeconds)
    {
    }

    [[nodiscard]] static double Distance(
        const Physics::PhysicsVector3& first,
        const Physics::PhysicsVector3& second) noexcept
    {
        const double dx = static_cast<double>(second.x) - first.x;
        const double dy = static_cast<double>(second.y) - first.y;
        const double dz = static_cast<double>(second.z) - first.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    [[nodiscard]] static Physics::PhysicsVector3 Difference(
        const Physics::PhysicsVector3& to,
        const Physics::PhysicsVector3& from) noexcept
    {
        return {.x = to.x - from.x, .y = to.y - from.y, .z = to.z - from.z};
    }

    [[nodiscard]] static std::optional<Physics::PhysicsVector3> Normalize(
        const Physics::PhysicsVector3& value) noexcept
    {
        if (!value.IsFinite())
        {
            return std::nullopt;
        }
        const double length = std::sqrt(
            static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y +
            static_cast<double>(value.z) * value.z);
        if (!std::isfinite(length) || length <= 1.0e-6)
        {
            return std::nullopt;
        }
        const float inverse = static_cast<float>(1.0 / length);
        return Physics::PhysicsVector3{.x = value.x * inverse, .y = value.y * inverse, .z = value.z * inverse};
    }

    [[nodiscard]] std::optional<Perception::Track> BestPlayerWeaponTrack(
        const std::vector<Perception::Track>& tracks) const
    {
        std::optional<Perception::Track> best{};
        for (const auto& track : tracks)
        {
            if (!Weapons::ValidateTrackForWeapon(playerTorpedoDefinition_.weapon, track))
            {
                continue;
            }
            if (!best || track.confidence > best->confidence ||
                (track.confidence == best->confidence && track.trackId < best->trackId))
            {
                best = track;
            }
        }
        return best;
    }

    [[nodiscard]] std::optional<Perception::Track> BuildPlayerTorpedoGuidanceTrack(
        const std::optional<Perception::Track>& perceivedTrack) const
    {
        if (!perceivedTrack || !perceivedTrack->estimatedPositionMeters || !playerTorpedo_ ||
            !playerTorpedoLaunchPosition_)
        {
            return perceivedTrack;
        }

        Perception::Track guidanceTrack = *perceivedTrack;
        const float forwardProgressMeters =
            (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_;
        if (forwardProgressMeters < M5CombatTorpedoStraightRunMeters)
        {
            // Tube exit/run-out: preserve launch depth while still using the perceived track's horizontal
            // coordinate. This is a weapon waypoint derived from perceived evidence, not hostile ground truth.
            guidanceTrack.estimatedPositionMeters->y = playerTorpedoLaunchPosition_->y;
        }
        else
        {
            // Aim slightly below the perceived surface-target reference so the conventional torpedo attacks
            // the underwater physical hull rather than steering toward an above-water visual superstructure.
            guidanceTrack.estimatedPositionMeters->y -= M5CombatTorpedoAttackPointBelowPerceivedTargetMeters;
        }
        return guidanceTrack;
    }

    [[nodiscard]] static std::optional<Perception::Track> FindTrack(
        const std::vector<Perception::Track>& tracks,
        const std::optional<std::uint64_t> trackId)
    {
        if (!trackId)
        {
            return std::nullopt;
        }
        for (const auto& track : tracks)
        {
            if (track.trackId == *trackId)
            {
                return track;
            }
        }
        return std::nullopt;
    }

    Physics::PhysicsWorld* physicsWorld_ = nullptr;
    Acoustics::AcousticWorld acousticWorld_;
    Perception::TrackManager playerTracks_;
    Perception::TrackManager destroyerTracks_;
    SimpleDestroyerDefinition destroyerDefinition_;
    SimpleDestroyerRuntimeState destroyer_;
    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
    Weapons::WeaponRuntimeState playerWeapon_;
    Weapons::AcousticDecoyDefinition decoyDefinition_;
    std::optional<Weapons::AcousticDecoyRuntimeState> decoy_{};
    std::optional<Weapons::ConventionalTorpedoRuntimeState> playerTorpedo_{};
    std::optional<Physics::PhysicsVector3> playerTorpedoLaunchPosition_{};
    float playerTorpedoForwardSign_ = 1.0F;
    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    bool activeEchoConsumed_ = false;
    std::optional<DeepRun::Combat::CombatExplosionEvent> lastExplosion_{};
    double lastUpdateTimeSeconds_ = 0.0;
};
} // namespace DeepRun::Game::Combat
