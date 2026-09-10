#pragma once

#include "Game/Combat/PlayerCombatCommandRuntime.h"
#include "Game/Combat/SimpleDestroyerRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"
#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"
#include "Simulation/Weapons/AcousticDecoy.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"
#include "Simulation/Weapons/NavalMine.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Combat
{
// M5 playground framing/weapon-profile tuning. The physical engagement is authored in kilometres; camera span
// is presentation policy and must not dictate target placement. The local launch frame places Antey at roughly
// one quarter of the view, while the later tactical overview reveals the remote target. These are gameplay
// values, not claimed real-world Project 949A or torpedo performance data.
inline constexpr float M5CombatCameraTargetOffsetXMeters = 400.0F;
inline constexpr float M5CombatDestroyerInitialXMeters = 1800.0F;
inline constexpr float M5CombatTorpedoLaunchClearanceMeters = 85.0F;
inline constexpr float M5CombatTorpedoStraightRunMeters = 250.0F;
inline constexpr float M5CombatTorpedoMaximumVerticalCourseAngleRadians = 0.55F;
inline constexpr float M5CombatTorpedoAttackPointBelowPerceivedTargetMeters = 1.5F;
inline constexpr float M5CombatTorpedoSurfaceSafetyMarginMeters = 0.25F;
inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;
inline constexpr float M5CombatMineForwardOffsetMeters = 520.0F;
inline constexpr float M5CombatMineDepthOffsetMeters = 35.0F;
inline constexpr float M5CombatPlayerMaximumIntegrity = 100.0F;

struct CombatPlaygroundFrame final
{
    std::vector<Perception::Track> playerTracks;
    std::vector<Perception::Track> destroyerTracks;
    PlayerCombatPresentationSnapshot playerCombat{};
    SimpleDestroyerCombatDecision destroyerDecision{};
    std::optional<Weapons::ConventionalTorpedoImpact> playerTorpedoImpact{};
    std::optional<Weapons::NavalMineDetonation> playerMineDetonation{};
    float playerIntegrityFraction = 1.0F;
    bool playerDestroyed = false;
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
            .preparationSeconds = 1.0,
            .targeting = Weapons::WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.65F,
                .maximumBearingUncertaintyRadians = 0.10F,
                .maximumPositionUncertaintyMeters = 150.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}};
        auto playerCombat = PlayerCombatCommandRuntime::Create(playerWeapon, simulationTimeSeconds);
        if (!playerCombat)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5-H player commander runtime creation failed: " + playerCombat.error());
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
            std::move(*playerCombat),
            decoyDefinition,
            simulationTimeSeconds);
    }

    // Accepted H/H.1 smoke and headless regression path. It deliberately retains deterministic automatic
    // commander decisions so existing combat/capture gates remain reproducible after J2 introduces live input.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        return AdvanceImpl(playerSnapshot, {}, simulationTimeSeconds, true);
    }

    // Normal-play J2 path. Commands are already semantic edge events; this runtime does not inspect keyboard,
    // mouse or controller state and does not maintain a generic command queue.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvancePlayerControlled(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {
        return AdvanceImpl(playerSnapshot, commands, simulationTimeSeconds, false);
    }

    // M5-I.2 binds the production Antey physical proxy once after the windowed composition has both the
    // PhysicalPlayground and CombatPlayground alive. The mine is a physical hazard, not perceived target truth.
    [[nodiscard]] std::expected<void, std::string> BindPlayerPhysicalProxy(
        const Submarine::AnteyPhysicalCollisionProxySnapshot& proxy,
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (physicsWorld_ == nullptr || playerIntegrity_.has_value() || mine_.has_value() ||
            previousPlayerPositionMeters_.has_value() || !proxy.body.IsValid() ||
            !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||
            !proxy.halfExtentsMeters.IsFinite() || proxy.halfExtentsMeters.x <= 0.0F ||
            proxy.halfExtentsMeters.y <= 0.0F || proxy.halfExtentsMeters.z <= 0.0F ||
            !playerSnapshot.emitter.positionMeters.IsFinite() || !std::isfinite(simulationTimeSeconds) ||
            simulationTimeSeconds < lastUpdateTimeSeconds_)
        {
            return std::unexpected("M5-I.2 player physical proxy binding input is invalid or already bound");
        }

        const auto authoritativeBody = physicsWorld_->GetBodyState(proxy.body);
        if (!authoritativeBody || !authoritativeBody->position.IsFinite() ||
            !authoritativeBody->orientation.IsFinite() ||
            Distance(authoritativeBody->position, proxy.positionMeters) > 0.05 ||
            !Physics::PhysicsQuaternion::SameRotation(authoritativeBody->orientation, proxy.orientation) ||
            Distance(proxy.positionMeters, playerSnapshot.emitter.positionMeters) > 0.05)
        {
            return std::unexpected("M5-I.2 player physical proxy does not match live physics/acoustic authority");
        }

        const auto integrity = DeepRun::Combat::CreateCombatIntegrity(
            proxy.body, M5CombatPlayerMaximumIntegrity, simulationTimeSeconds);
        if (!integrity)
        {
            return std::unexpected("M5-I.2 player integrity creation failed: " + integrity.error());
        }

        Weapons::NavalMineDefinition definition{
            .id = "m5.live-contact-mine",
            .collisionHalfExtentsMeters = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .contactDamage = 80.0F,
            .explosionRadiusMeters = 10.0F};
        const Physics::PhysicsVector3 minePosition{
            .x = playerSnapshot.emitter.positionMeters.x + M5CombatMineForwardOffsetMeters,
            .y = playerSnapshot.emitter.positionMeters.y - M5CombatMineDepthOffsetMeters,
            .z = playerSnapshot.emitter.positionMeters.z};
        const auto mine = Weapons::CreateNavalMineRuntime(
            definition, *physicsWorld_, minePosition, simulationTimeSeconds);
        if (!mine)
        {
            return std::unexpected("M5-I.2 live naval mine creation failed: " + mine.error());
        }

        playerBody_ = proxy.body;
        playerCollisionHalfExtentsMeters_ = proxy.halfExtentsMeters;
        previousPlayerPositionMeters_ = proxy.positionMeters;
        currentPlayerPhysicalProxy_ = proxy;
        playerIntegrity_ = *integrity;
        mineDefinition_ = std::move(definition);
        mine_ = *mine;
        return {};
    }

    // Refreshes the read-only production physical snapshot for the upcoming fixed combat tick. The acoustic
    // body-reference position must agree with the physical bridge, but the physical snapshot owns sweep geometry.
    [[nodiscard]] std::expected<void, std::string> UpdatePlayerPhysicalProxy(
        const Submarine::AnteyPhysicalCollisionProxySnapshot& proxy,
        const Submarine::AnteyAcousticSnapshot& playerSnapshot)
    {
        if (!playerIntegrity_ || !mine_ || !previousPlayerPositionMeters_ || !playerBody_.IsValid() ||
            proxy.body != playerBody_ || !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||
            !proxy.halfExtentsMeters.IsFinite() ||
            Distance(proxy.positionMeters, playerSnapshot.emitter.positionMeters) > 0.05 ||
            Distance(proxy.halfExtentsMeters, playerCollisionHalfExtentsMeters_) > 1.0e-4 ||
            physicsWorld_ == nullptr || !physicsWorld_->GetBodyState(proxy.body).has_value())
        {
            return std::unexpected("M5-I.2 player physical proxy update is invalid or disagrees with acoustic authority");
        }
        currentPlayerPhysicalProxy_ = proxy;
        return {};
    }

    [[nodiscard]] const SimpleDestroyerRuntimeState& Destroyer() const noexcept { return destroyer_; }
    [[nodiscard]] const SimpleDestroyerDefinition& DestroyerDefinition() const noexcept { return destroyerDefinition_; }
    [[nodiscard]] const PlayerCombatCommandRuntime& PlayerCombat() const noexcept { return playerCombat_; }
    [[nodiscard]] const std::optional<Weapons::ConventionalTorpedoRuntimeState>& PlayerTorpedo() const noexcept
    {
        return playerTorpedo_;
    }
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept
    {
        return playerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
    [[nodiscard]] const std::optional<Weapons::NavalMineDefinition>& MineDefinition() const noexcept { return mineDefinition_; }
    [[nodiscard]] const std::optional<Weapons::NavalMineRuntimeState>& Mine() const noexcept { return mine_; }
    [[nodiscard]] const std::optional<DeepRun::Combat::CombatIntegrityState>& PlayerIntegrity() const noexcept
    {
        return playerIntegrity_;
    }
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
        PlayerCombatCommandRuntime playerCombat,
        Weapons::AcousticDecoyDefinition decoyDefinition,
        const double simulationTimeSeconds)
        : physicsWorld_(&physicsWorld),
          acousticWorld_(std::move(acousticWorld)),
          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          destroyerDefinition_(std::move(destroyerDefinition)),
          destroyer_(std::move(destroyer)),
          playerTorpedoDefinition_(std::move(playerTorpedoDefinition)),
          playerCombat_(std::move(playerCombat)),
          decoyDefinition_(std::move(decoyDefinition)),
          nextActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds)
    {
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvanceImpl(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds,
        const bool automatedPlayer)
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

        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
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

        bool integratedActiveEcho = false;
        if (activePulse_ && activeReflector_)
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
                integratedActiveEcho = true;
                activePulse_.reset();
                activeReflector_.reset();
                nextActivePulseTimeSeconds_ = simulationTimeSeconds + M5CombatActiveRangingIntervalSeconds;
            }
        }
        if (!integratedActiveEcho && !playerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H player TrackManager failed to advance");
        }

        const auto readiness = playerCombat_.Advance(simulationTimeSeconds);
        if (!readiness)
        {
            return std::unexpected("M5-J2 player commander readiness failed: " + readiness.error());
        }

        if (!playerTorpedo_.has_value())
        {
            if (automatedPlayer)
            {
                const auto automated = AdvanceAutomatedPlayerCommander(simulationTimeSeconds);
                if (!automated)
                {
                    return std::unexpected(automated.error());
                }
            }
            else
            {
                for (const PlayerCombatCommand command : commands)
                {
                    const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
                    if (!executed)
                    {
                        return std::unexpected("M5-J2 player command failed: " + executed.error());
                    }
                }
            }

            if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)
            {
                const auto targetTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.Weapon().targetTrackId);
                if (!targetTrack)
                {
                    return std::unexpected("M5-J2 launched weapon lost its perceived launch track on the launch tick");
                }
                const auto launch = MaterializePlayerLaunch(
                    playerSnapshot, *destroyerAcoustics, *targetTrack, simulationTimeSeconds);
                if (!launch)
                {
                    return std::unexpected(launch.error());
                }
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

        std::optional<Weapons::NavalMineDetonation> mineDetonation{};
        if (mineDefinition_ && mine_ && playerIntegrity_ && previousPlayerPositionMeters_)
        {
            if (playerIntegrity_->body != playerBody_ || !playerCollisionHalfExtentsMeters_.IsFinite() ||
                !currentPlayerPhysicalProxy_ || currentPlayerPhysicalProxy_->body != playerBody_)
            {
                return std::unexpected("M5-I.2 bound player combat/physical identity is inconsistent");
            }
            const Physics::PhysicsVector3 displacement = Difference(
                currentPlayerPhysicalProxy_->positionMeters, *previousPlayerPositionMeters_);
            const double displacementSquared =
                static_cast<double>(displacement.x) * displacement.x +
                static_cast<double>(displacement.y) * displacement.y +
                static_cast<double>(displacement.z) * displacement.z;
            if (displacementSquared > 1.0e-10 && !mine_->detonated)
            {
                const auto detonated = Weapons::AdvanceNavalMineAgainstSweep(
                    *mineDefinition_,
                    *mine_,
                    playerBody_,
                    Physics::PhysicsBoxSweepQuery{
                        .halfExtentsMeters = playerCollisionHalfExtentsMeters_,
                        .startPositionMeters = *previousPlayerPositionMeters_,
                        .orientation = currentPlayerPhysicalProxy_->orientation,
                        .displacementMeters = displacement},
                    *physicsWorld_,
                    simulationTimeSeconds);
                if (!detonated)
                {
                    return std::unexpected("M5-I.2 live naval mine sweep failed: " + detonated.error());
                }
                if (detonated->has_value())
                {
                    mineDetonation = **detonated;
                    const auto damaged = DeepRun::Combat::ApplyCombatDamage(*playerIntegrity_, mineDetonation->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-I.2 player mine damage application failed: " + damaged.error());
                    }
                    lastExplosion_ = mineDetonation->explosion;
                }
            }
            previousPlayerPositionMeters_ = currentPlayerPhysicalProxy_->positionMeters;
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
        const std::vector<Perception::Track> playerTrackSnapshot = playerTracks_.Tracks();
        float playerIntegrityFraction = 1.0F;
        bool playerDestroyed = false;
        if (playerIntegrity_)
        {
            playerIntegrityFraction = std::clamp(
                playerIntegrity_->remainingIntegrity / playerIntegrity_->maximumIntegrity, 0.0F, 1.0F);
            playerDestroyed = playerIntegrity_->destroyed;
        }
        return CombatPlaygroundFrame{
            .playerTracks = playerTrackSnapshot,
            .destroyerTracks = destroyerTracks_.Tracks(),
            .playerCombat = playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot),
            .destroyerDecision = *destroyerDecision,
            .playerTorpedoImpact = impact,
            .playerMineDetonation = mineDetonation,
            .playerIntegrityFraction = playerIntegrityFraction,
            .playerDestroyed = playerDestroyed};
    }

    [[nodiscard]] std::expected<void, std::string> AdvanceAutomatedPlayerCommander(
        const double simulationTimeSeconds)
    {
        const std::vector<Perception::Track> tracks = playerTracks_.Tracks();
        const auto qualifyingTrack = BestPlayerWeaponTrack(tracks);
        if (!qualifyingTrack)
        {
            return {};
        }

        for (std::size_t attempt = 0; attempt < tracks.size() && playerCombat_.SelectedTrackId() != qualifyingTrack->trackId;
             ++attempt)
        {
            const auto selected = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::SelectNextTrack}, tracks, simulationTimeSeconds);
            if (!selected || !selected->accepted)
            {
                return std::unexpected("M5-H automated commander could not select a perceived track");
            }
        }
        if (playerCombat_.SelectedTrackId() != qualifyingTrack->trackId)
        {
            return std::unexpected("M5-H automated commander could not reach the qualifying perceived track");
        }

        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
        {
            const auto prepared = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::PrepareWeapon}, tracks, simulationTimeSeconds);
            if (!prepared || !prepared->accepted)
            {
                return std::unexpected("M5-H automated commander could not prepare the player weapon");
            }
        }
        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
        {
            const auto fired = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::FireWeapon}, tracks, simulationTimeSeconds);
            if (!fired || !fired->accepted)
            {
                return std::unexpected("M5-H automated commander could not fire on the qualifying perceived track");
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializePlayerLaunch(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const Perception::Track& targetTrack,
        const double simulationTimeSeconds)
    {
        if (!targetTrack.estimatedPositionMeters ||
            !Weapons::ValidateTrackForWeapon(playerTorpedoDefinition_.weapon, targetTrack))
        {
            return std::unexpected("M5-J2 launch materialization requires the accepted perceived spatial track");
        }

        const float targetDeltaX = targetTrack.estimatedPositionMeters->x - playerSnapshot.emitter.positionMeters.x;
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
            playerTorpedoDefinition_, playerCombat_.Weapon(), launchPosition, launchHeading,
            targetTrack, simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("M5-H torpedo runtime creation failed: " + launched.error());
        }
        playerTorpedo_ = *launched;
        playerTorpedoLaunchPosition_ = launchPosition;

        const Physics::PhysicsVector3 decoyPosition{
            .x = destroyerAcoustics.emitter.positionMeters.x - 20.0F,
            .y = destroyerAcoustics.emitter.positionMeters.y - 15.0F,
            .z = destroyerAcoustics.emitter.positionMeters.z};
        const auto decoy = Weapons::DeployAcousticDecoy(
            decoyDefinition_, decoyPosition, simulationTimeSeconds);
        if (!decoy)
        {
            return std::unexpected("M5-H decoy deployment failed: " + decoy.error());
        }
        decoy_ = *decoy;
        return {};
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
            guidanceTrack.estimatedPositionMeters->y = playerTorpedoLaunchPosition_->y;
        }
        else
        {
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
    PlayerCombatCommandRuntime playerCombat_;
    Weapons::AcousticDecoyDefinition decoyDefinition_;
    std::optional<Weapons::AcousticDecoyRuntimeState> decoy_{};
    // M5-I.2 live mine state is bound only when a real production player physical proxy is supplied.
    std::optional<Weapons::NavalMineDefinition> mineDefinition_{};
    std::optional<Weapons::NavalMineRuntimeState> mine_{};
    Physics::PhysicsBodyHandle playerBody_{};
    Physics::PhysicsVector3 playerCollisionHalfExtentsMeters_{};
    std::optional<Physics::PhysicsVector3> previousPlayerPositionMeters_{};
    std::optional<Submarine::AnteyPhysicalCollisionProxySnapshot> currentPlayerPhysicalProxy_{};
    std::optional<DeepRun::Combat::CombatIntegrityState> playerIntegrity_{};
    std::optional<Weapons::ConventionalTorpedoRuntimeState> playerTorpedo_{};
    std::optional<Physics::PhysicsVector3> playerTorpedoLaunchPosition_{};
    float playerTorpedoForwardSign_ = 1.0F;
    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    double nextActivePulseTimeSeconds_ = 0.0;
    std::optional<DeepRun::Combat::CombatExplosionEvent> lastExplosion_{};
    double lastUpdateTimeSeconds_ = 0.0;
};
} // namespace DeepRun::Game::Combat
