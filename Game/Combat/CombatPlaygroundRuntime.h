#pragma once

#include "Game/Combat/CombatKnowledge.h"
#include "Game/Combat/P700SeekerObservation.h"
#include "Game/Combat/PeriscopeCombatRuntime.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"
#include "Game/Combat/SimpleCivilianVesselRuntime.h"
#include "Game/Combat/SimpleDestroyerRuntime.h"
#include "Game/Combat/SurfaceContactSensorTruth.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"
#include "Game/Weapons/P700CarrierLaunchContract.h"
#include "Game/Weapons/P700LauncherInventory.h"
#include "Game/Weapons/P700SalvoLaunchCoordinator.h"
#include "Game/Weapons/AnteyTorpedoInventory.h"
#include "Game/Weapons/PlayerWeaponSelection.h"
#include "Game/Weapons/PlayerTorpedoProfiles.h"
#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Acoustics/AcousticEnvironment.h"
#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"
#include "Simulation/Weapons/AcousticDecoy.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"
#include "Simulation/Weapons/NavalMine.h"
#include "Simulation/Weapons/P700Granit.h"
#include "Simulation/Weapons/TorpedoSeeker.h"
#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Combat
{
inline constexpr float M5CombatCameraTargetOffsetXMeters = 0.0F;
inline constexpr float M5CombatDestroyerInitialXMeters = 1800.0F;
inline constexpr float M5CombatDestroyerCruiseVelocityXMetersPerSecond = -2.0F;
inline constexpr float M5CombatCivilianInitialXMeters = -1600.0F;
inline constexpr float M5CombatDestroyerVisibleHeightMeters = 20.0F;
inline constexpr float M5CombatCivilianVisibleHeightMeters = 18.0F;
inline constexpr float M5CombatSurfaceTruthAssociationGateRadians = 0.50F;
inline constexpr float M5CombatTorpedoLaunchClearanceMeters = 85.0F;
inline constexpr float M5CombatDestroyerTorpedoLaunchClearanceMeters = 32.0F;
inline constexpr float M5CombatDestroyerTorpedoLaunchDepthOffsetMeters = 6.0F;
inline constexpr float M5CombatTorpedoStraightRunMeters = 250.0F;
inline constexpr float M5CombatTorpedoMaximumVerticalCourseAngleRadians = 0.55F;
inline constexpr float M5CombatTorpedoAttackPointBelowPerceivedTargetMeters = 1.5F;
inline constexpr float M5CombatTorpedoSurfaceSafetyMarginMeters = 0.25F;
inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;
inline constexpr double M5CombatDestroyerActiveRangingIntervalSeconds = 6.0;
inline constexpr float M5CombatMineForwardOffsetMeters = 520.0F;
inline constexpr float M5CombatMineDepthOffsetMeters = 35.0F;
inline constexpr float M5CombatPlayerMaximumIntegrity = 100.0F;
inline constexpr double M5CombatTorpedoSeekerEmissionSampleIntervalSeconds = 0.10;
inline constexpr float M5CombatTorpedoSeekerAssociationGateRadians = 0.03F;
inline constexpr double M5CombatTorpedoActiveListenWindowSeconds = 4.0;
inline constexpr float M5CombatTorpedoActiveBeamHalfAngleRadians = 0.45F;
inline constexpr Acoustics::ActiveSonarConfig M5CombatTorpedoActiveSonarConfig{
    .bearingUncertaintyRadians = 0.06F,
    .minimumRangeUncertaintyMeters = 3.0F,
    .fractionalRangeUncertainty = 0.04F};
inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;
inline constexpr float M5CombatPlayerDecoyVerticalOffsetMeters = 120.0F;
inline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;
inline constexpr Acoustics::ActiveSonarConfig M5CombatPlayerFireControlActiveSonarConfig{
    .bearingUncertaintyRadians = 0.0174532925F,
    .minimumRangeUncertaintyMeters = 5.0F,
    .fractionalRangeUncertainty = 0.01F};

struct CombatPlaygroundFrame final
{
    std::vector<Perception::Track> playerTracks;
    std::vector<Perception::Track> destroyerTracks;
    PlayerCombatPresentationSnapshot playerCombat{};
    SimpleDestroyerCombatDecision destroyerDecision{};
    std::optional<Weapons::ConventionalTorpedoImpact> playerTorpedoImpact{};
    std::optional<Weapons::ConventionalTorpedoImpact> destroyerTorpedoImpact{};
    std::optional<Weapons::NavalMineDetonation> playerMineDetonation{};
    std::optional<Weapons::P700GranitImpact> playerP700Impact{};
    float playerIntegrityFraction = 1.0F;
    bool playerDestroyed = false;
    std::optional<float> civilianIntegrityFraction{};
    bool civilianDestroyed = false;
};

class CombatPlaygroundRuntime final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundRuntime, std::string> Create(
        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false,
        const float destroyerCruiseVelocityXMetersPerSecond = M5CombatDestroyerCruiseVelocityXMetersPerSecond)
    {
        if (!physicsWorld.IsInitialized() || !std::isfinite(surfaceLevelY) ||
            !std::isfinite(destroyerInitialXMeters) || destroyerInitialXMeters <= 0.0F ||
            !std::isfinite(destroyerCruiseVelocityXMetersPerSecond) ||
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
        const auto playerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.35,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.50F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 8U});
        const auto playerTorpedoActiveSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.50,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.35F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 2.0F,
            .maximumTracks = 8U});
        const auto destroyerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.35,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.50F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 8U});
        const auto destroyerTorpedoActiveSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.50,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.35F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 2.0F,
            .maximumTracks = 8U});
        const auto incomingThreatTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = 0.12F,
            .observationsToConfirm = 2U,
            .coastAfterSeconds = 0.75,
            .lostAfterSeconds = 2.0,
            .confidenceDecayPerSecond = 0.40F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.03F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 4U});
        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks || !playerTorpedoActiveSeekerTracks ||
            !destroyerTorpedoSeekerTracks || !destroyerTorpedoActiveSeekerTracks || !incomingThreatTracks)
        {
            return std::unexpected("M5-H/M5-E.1/M5-J3/M5-J4 perception manager creation failed");
        }

        const SimpleDestroyerDefinition destroyerDefinition{
            .id = "m5.live-destroyer-proxy",
            .collisionHalfExtentsMeters = {.x = 25.0F, .y = 3.0F, .z = 3.0F},
            .massKilograms = 2'500'000.0F,
            .cruiseVelocityXMetersPerSecond = destroyerCruiseVelocityXMetersPerSecond,
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
            destroyerInitialXMeters,
            0.0F,
            simulationTimeSeconds);
        if (!destroyer)
        {
            return std::unexpected("M5-H destroyer creation failed: " + destroyer.error());
        }

        const auto playerTorpedoProfile = Armament::MakePlayerTorpedoProfile(
            Armament::PlayerWeaponType::HeavyweightTorpedo);
        if (!playerTorpedoProfile)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5 player USET-80 profile creation failed");
        }
        const Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition =
            playerTorpedoProfile->definition;
        auto playerCombat = PlayerCombatCommandRuntime::Create(
            playerTorpedoDefinition.weapon, simulationTimeSeconds);
        if (!playerCombat)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5-H player commander runtime creation failed: " + playerCombat.error());
        }
        const auto initialPreparation = playerCombat->BeginAutomaticPreparation(simulationTimeSeconds);
        if (!initialPreparation)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("player automatic weapon preparation failed: " + initialPreparation.error());
        }
        const Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition{
            .weapon = destroyerDefinition.weapon,
            .underwaterSpeedMetersPerSecond = 44.0F,
            .maximumTurnRateRadiansPerSecond = 0.35F,
            .maximumVerticalCourseAngleRadians = M5CombatTorpedoMaximumVerticalCourseAngleRadians,
            .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
            .directImpactDamage = 55.0F,
            .explosionRadiusMeters = 8.0F};
        const Weapons::P700GranitDefinition playerP700Definition{
            .weapon = Weapons::WeaponDefinition{
                .id = "m5.live-player-p700",
                .preparationSeconds = 1.0,
                .targeting = Weapons::WeaponTargetingRequirements{
                    .minimumTrackConfidence = 0.65F,
                    .maximumBearingUncertaintyRadians = 0.12F,
                    .maximumPositionUncertaintyMeters = 500.0F,
                    .requiresEstimatedPosition = true,
                    .allowCoastingTrack = false}}};
        if (!Weapons::ValidateP700GranitDefinition(playerP700Definition))
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5 P-700 player runtime definition is invalid");
        }

        const Weapons::AcousticDecoyDefinition decoyDefinition{
            .id = "m5.live-acoustic-decoy",
            .continuousSourceLevelDb = {.levelDb = {158.0F, 154.0F, 149.0F, 143.0F}},
            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},
            .activeLifetimeSeconds = 10.0};
        const Weapons::AcousticDecoyDefinition playerDecoyDefinition{
            .id = "m5.live-player-acoustic-decoy",
            .continuousSourceLevelDb = {.levelDb = {172.0F, 168.0F, 162.0F, 156.0F}},
            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},
            .activeLifetimeSeconds = 12.0};

        return CombatPlaygroundRuntime(
            physicsWorld,
            *acousticWorld,
            *playerTracks,
            *destroyerTracks,
            *playerTorpedoSeekerTracks,
            *playerTorpedoActiveSeekerTracks,
            *destroyerTorpedoSeekerTracks,
            *destroyerTorpedoActiveSeekerTracks,
            *incomingThreatTracks,
            destroyerDefinition,
            *destroyer,
            destroyerTorpedoDefinition,
            playerTorpedoDefinition,
            playerP700Definition,
            std::move(*playerCombat),
            decoyDefinition,
            playerDecoyDefinition,
            simulationTimeSeconds,
            p700AcceptanceMode);
    }

    [[nodiscard]] static std::expected<CombatPlaygroundRuntime, std::string> Create(
        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds,
        Armament::P700CarrierLaunchContract p700CarrierLaunchContract,
        Armament::P700LauncherInventory p700LauncherInventory,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false,
        const float destroyerCruiseVelocityXMetersPerSecond = M5CombatDestroyerCruiseVelocityXMetersPerSecond)
    {
        if (p700CarrierLaunchContract.Anchors().size() != Armament::AnteyP700LauncherSlotCount ||
            p700LauncherInventory.Slots().size() != Armament::AnteyP700LauncherSlotCount ||
            p700LauncherInventory.LoadedCount() != Armament::AnteyP700LauncherSlotCount ||
            p700LauncherInventory.SpentCount() != 0U)
        {
            return std::unexpected("M5 P-700 production runtime requires a fresh 24-slot launcher inventory");
        }
        auto runtime = Create(
            physicsWorld, surfaceLevelY, simulationTimeSeconds, destroyerInitialXMeters,
            p700AcceptanceMode, destroyerCruiseVelocityXMetersPerSecond);
        if (!runtime)
        {
            return runtime;
        }
        runtime->p700CarrierLaunchContract_ = std::move(p700CarrierLaunchContract);
        runtime->p700LauncherInventory_ = std::move(p700LauncherInventory);
        runtime->civilianGameplayEnabled_ = !p700AcceptanceMode;
        return std::move(*runtime);
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        return AdvanceImpl(playerSnapshot, {}, simulationTimeSeconds, true);
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvancePlayerControlled(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {
        return AdvanceImpl(playerSnapshot, commands, simulationTimeSeconds, false);
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvanceP700Acceptance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        std::array<PlayerCombatCommand, 2> commands{};
        std::size_t count = 0U;
        if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit &&
            playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
        {
            commands[count++] = {.type = PlayerCombatCommandType::NextWeapon};
        }
        const auto tracks = playerTracks_.Tracks();
        if (!playerCombat_.SelectedTrackId().has_value() && !tracks.empty())
        {
            commands[count++] = {.type = PlayerCombatCommandType::SelectNextTrack};
        }
        else if (playerCombat_.SelectedTrackId().has_value() && selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit)
        {
            const auto selected = FindTrack(tracks, playerCombat_.SelectedTrackId());
            if (selected && !selected->estimatedPositionMeters.has_value() && !activePulse_.has_value() &&
                simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_)
            {
                commands[count++] = {.type = PlayerCombatCommandType::ActiveSonarPing};
            }
            else if (selected && selected->estimatedPositionMeters.has_value())
            {
                if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
                    commands[count++] = {.type = PlayerCombatCommandType::PrepareWeapon};
                else if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
                    commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};
            }
        }
        return AdvanceImpl(playerSnapshot, std::span<const PlayerCombatCommand>{commands.data(), count}, simulationTimeSeconds, false);
    }

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
            (proxy.gameplayLongitudinalFacingSign != 1.0F && proxy.gameplayLongitudinalFacingSign != -1.0F) ||
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

    [[nodiscard]] std::expected<void, std::string> UpdatePlayerPhysicalProxy(
        const Submarine::AnteyPhysicalCollisionProxySnapshot& proxy,
        const Submarine::AnteyAcousticSnapshot& playerSnapshot)
    {
        if (!playerIntegrity_ || !mine_ || !previousPlayerPositionMeters_ || !playerBody_.IsValid() ||
            proxy.body != playerBody_ || !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||
            !proxy.halfExtentsMeters.IsFinite() ||
            (proxy.gameplayLongitudinalFacingSign != 1.0F && proxy.gameplayLongitudinalFacingSign != -1.0F) ||
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
    [[nodiscard]] const std::optional<SimpleCivilianVesselRuntimeState>& Civilian() const noexcept { return civilian_; }
    [[nodiscard]] const SimpleCivilianVesselDefinition& CivilianDefinition() const noexcept { return civilianDefinition_; }
    [[nodiscard]] const PlayerCombatCommandRuntime& PlayerCombat() const noexcept { return playerCombat_; }
    [[nodiscard]] const std::optional<Weapons::ConventionalTorpedoRuntimeState>& PlayerTorpedo() const noexcept
    {
        return playerTorpedo_;
    }
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept
    {
        return playerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::ConventionalTorpedoRuntimeState>& DestroyerTorpedo() const noexcept
    {
        return destroyerTorpedo_;
    }
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& DestroyerTorpedoLaunchPosition() const noexcept
    {
        return destroyerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& PlayerDecoy() const noexcept
    {
        return playerDecoy_;
    }
    [[nodiscard]] bool PlayerDecoyAvailable() const noexcept { return playerDecoyAvailable_; }
    [[nodiscard]] const std::optional<Acoustics::ActiveAcousticPulse>& PlayerActivePulse() const noexcept
    {
        return activePulse_;
    }
    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& DestroyerTorpedoSeekerState() const noexcept
    {
        return destroyerTorpedoSeekerState_;
    }
    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& PlayerTorpedoSeekerState() const noexcept
    {
        return playerTorpedoSeekerState_;
    }
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
    [[nodiscard]] bool PlayerFogOfWarActive() const noexcept { return playerFogOfWarActive_; }
    [[nodiscard]] bool PlayerHasVisualClassification(
        const Perception::ContactClassification classification) const
    {
        return std::ranges::any_of(playerTracks_.Tracks(), [classification](const Perception::Track& track) {
            return track.lifecycle != Perception::TrackLifecycleState::Lost && track.visuallyIdentified &&
                   track.classification == classification &&
                   static_cast<int>(track.opticalIdentificationLevel) >=
                       static_cast<int>(Perception::OpticalIdentificationLevel::TypeResolved);
        });
    }
    [[nodiscard]] const std::optional<Armament::P700LauncherInventory>& P700Launchers() const noexcept
    {
        return p700LauncherInventory_;
    }
    [[nodiscard]] std::size_t PlayerTorpedoRoundsRemaining(const Armament::PlayerWeaponType weapon) const noexcept
    {
        return playerTorpedoInventory_.LoadedCount(weapon);
    }
    [[nodiscard]] float PlayerExpendedOrdnanceMassKg() const noexcept
    {
        const std::size_t spentP700 = p700LauncherInventory_ ? p700LauncherInventory_->SpentCount() : 0U;
        return playerTorpedoInventory_.ExpendedMassKg() +
            static_cast<float>(spentP700) * Armament::P700GranitRoundMassKg;
    }
    [[nodiscard]] Armament::PlayerWeaponType SelectedPlayerWeapon() const noexcept { return selectedPlayerWeapon_; }
    [[nodiscard]] const std::optional<Weapons::P700GranitRuntimeState>& PlayerP700() const noexcept
    {
        return playerP700_;
    }
    [[nodiscard]] const std::vector<Weapons::P700GranitRuntimeState>& PlayerP700Wingmen() const noexcept
    {
        return playerP700Wingmen_;
    }
    [[nodiscard]] std::optional<std::string> PlayerP700HatchGroupSemanticId() const
    {
        if (!playerP700LaunchSlotIndex_ || !p700LauncherInventory_ ||
            *playerP700LaunchSlotIndex_ >= p700LauncherInventory_->Slots().size())
            return std::nullopt;
        return p700LauncherInventory_->Slots()[*playerP700LaunchSlotIndex_].anchor.hatchGroupSemanticId;
    }
    [[nodiscard]] std::expected<void, std::string> SetPeriscopeOpticalConditions(
        const PeriscopeOpticalConditions& conditions)
    {
        if (!ValidPeriscopeOpticalConditions(conditions))
            return std::unexpected("invalid periscope optical conditions");
        periscopeOpticalConditions_ = conditions;
        return {};
    }

private:
    CombatPlaygroundRuntime(
        Physics::PhysicsWorld& physicsWorld,
        Acoustics::AcousticWorld acousticWorld,
        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        Perception::TrackManager playerTorpedoSeekerTracks,
        Perception::TrackManager playerTorpedoActiveSeekerTracks,
        Perception::TrackManager destroyerTorpedoSeekerTracks,
        Perception::TrackManager destroyerTorpedoActiveSeekerTracks,
        Perception::TrackManager incomingThreatTracks,
        SimpleDestroyerDefinition destroyerDefinition,
        SimpleDestroyerRuntimeState destroyer,
        Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition,
        Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition,
        Weapons::P700GranitDefinition playerP700Definition,
        PlayerCombatCommandRuntime playerCombat,
        Weapons::AcousticDecoyDefinition decoyDefinition,
        Weapons::AcousticDecoyDefinition playerDecoyDefinition,
        const double simulationTimeSeconds,
        const bool p700AcceptanceMode)
        : physicsWorld_(&physicsWorld),
          acousticWorld_(std::move(acousticWorld)),
          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),
          playerTorpedoActiveSeekerTracks_(std::move(playerTorpedoActiveSeekerTracks)),
          destroyerTorpedoSeekerTracks_(std::move(destroyerTorpedoSeekerTracks)),
          destroyerTorpedoActiveSeekerTracks_(std::move(destroyerTorpedoActiveSeekerTracks)),
          incomingThreatTracks_(std::move(incomingThreatTracks)),
          destroyerDefinition_(std::move(destroyerDefinition)),
          destroyer_(std::move(destroyer)),
          destroyerTorpedoDefinition_(std::move(destroyerTorpedoDefinition)),
          playerTorpedoDefinition_(std::move(playerTorpedoDefinition)),
          playerP700Definition_(std::move(playerP700Definition)),
          playerCombat_(std::move(playerCombat)),
          decoyDefinition_(std::move(decoyDefinition)),
          playerDecoyDefinition_(std::move(playerDecoyDefinition)),
          nextActivePulseTimeSeconds_(simulationTimeSeconds),
          nextDestroyerActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds),
          p700AcceptanceMode_(p700AcceptanceMode)
    {
    }

    [[nodiscard]] std::expected<void, std::string> EnsureCivilianGameplay(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (civilian_ || !civilianGameplayEnabled_ || p700AcceptanceMode_)
            return {};
        if (physicsWorld_ == nullptr || !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !std::isfinite(playerSnapshot.signedDepthMeters))
            return std::unexpected("civilian gameplay cannot resolve live surface authority");
        const float surfaceLevelYMeters =
            playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
        auto created = CreateSimpleCivilianVesselRuntime(
            civilianDefinition_, *physicsWorld_, surfaceLevelYMeters, M5CombatCivilianInitialXMeters,
            playerSnapshot.emitter.positionMeters.z, simulationTimeSeconds);
        if (!created)
            return std::unexpected("civilian gameplay creation failed: " + created.error());
        civilian_ = std::move(*created);
        return {};
    }

    [[nodiscard]] std::vector<SurfaceContactSensorTruth> BuildSurfaceContactTruths(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const std::optional<Acoustics::AcousticEmitter>& civilianEmitter) const
    {
        std::vector<SurfaceContactSensorTruth> truths;
        truths.reserve(civilianEmitter ? 2U : 1U);
        truths.push_back(SurfaceContactSensorTruth{
            .kind = SurfaceContactTruthKind::MilitaryCombatant,
            .body = destroyer_.body,
            .emitter = destroyerAcoustics.emitter,
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant,
            .visibleHeightAboveSurfaceMeters = M5CombatDestroyerVisibleHeightMeters});
        if (civilianEmitter && civilian_)
        {
            truths.push_back(SurfaceContactSensorTruth{
                .kind = SurfaceContactTruthKind::CivilianVessel,
                .body = civilian_->body,
                .emitter = *civilianEmitter,
                .visualClassification = Perception::ContactClassification::CivilianSurfaceVessel,
                .visibleHeightAboveSurfaceMeters = M5CombatCivilianVisibleHeightMeters});
        }
        return truths;
    }

    [[nodiscard]] std::expected<bool, std::string> IntegratePlayerPassiveEmitter(
        const Acoustics::AcousticEmitter& emitter,
        const Acoustics::AcousticReceiver& receiver,
        const double simulationTimeSeconds,
        const std::string_view label)
    {
        if (!emitter.positionMeters.IsFinite() || !emitter.continuousSourceLevelDb.IsFinite() ||
            !receiver.positionMeters.IsFinite())
            return std::unexpected(std::string(label) + " passive source/receiver is invalid");
        const double distanceMeters = Distance(emitter.positionMeters, receiver.positionMeters);
        const double travelSeconds = distanceMeters /
            static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
        const double emissionTime = simulationTimeSeconds > travelSeconds
            ? std::max(0.0, simulationTimeSeconds - travelSeconds - 1.0e-6)
            : 0.0;
        const Acoustics::AcousticEmission emission{
            .positionMeters = emitter.positionMeters,
            .sourceLevelDb = emitter.continuousSourceLevelDb,
            .emissionTimeSeconds = emissionTime};
        const auto observed = acousticWorld_.CollectPassiveDirectObservation(
            emission, receiver, simulationTimeSeconds);
        if (!observed)
            return std::unexpected(std::string(label) + " passive propagation failed: " + observed.error().message);
        if (!observed->has_value())
            return false;
        const auto perceived = Perception::FromAcousticObservation(**observed);
        if (!perceived || !playerTracks_.IntegrateObservation(*perceived))
            return std::unexpected(std::string(label) + " passive evidence failed perception integration");
        return true;
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

        playerFogOfWarActive_ = !automatedPlayer && !p700AcceptanceMode_;
        if (playerFogOfWarActive_)
        {
            const auto civilianReady = EnsureCivilianGameplay(playerSnapshot, simulationTimeSeconds);
            if (!civilianReady)
                return std::unexpected(civilianReady.error());
        }

        const auto destroyerAcoustics = SampleSimpleDestroyerAcoustics(
            destroyerDefinition_, destroyer_, *physicsWorld_);
        if (!destroyerAcoustics)
        {
            return std::unexpected("M5-H destroyer acoustic snapshot failed: " + destroyerAcoustics.error());
        }
        std::optional<Acoustics::AcousticEmitter> civilianEmitter{};
        if (civilian_)
        {
            const auto sampledCivilian = SampleSimpleCivilianVesselEmitter(
                civilianDefinition_, *civilian_, *physicsWorld_);
            if (!sampledCivilian)
                return std::unexpected("civilian acoustic snapshot failed: " + sampledCivilian.error());
            civilianEmitter = *sampledCivilian;
        }
        const std::vector<SurfaceContactSensorTruth> surfaceTruths =
            BuildSurfaceContactTruths(*destroyerAcoustics, civilianEmitter);

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

        if (periscopeState_.raised)
        {
            const float surfaceLevelYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto mastObserved = ObserveExposedPeriscopeMast(
                ExposedPeriscopeMastDetectionConfig{},
                periscopeState_,
                playerSnapshot.emitter.positionMeters,
                playerSnapshot.signedDepthMeters,
                surfaceLevelYMeters,
                destroyerAcoustics->passiveReceiver.positionMeters,
                simulationTimeSeconds,
                periscopeOpticalConditions_);
            if (!mastObserved)
                return std::unexpected("destroyer visual-watch periscope detection failed: " + mastObserved.error());
            if (mastObserved->has_value() && !destroyerTracks_.IntegrateObservation(**mastObserved))
                return std::unexpected("destroyer visual-watch evidence failed perception integration");
        }

        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
        {
            const auto awarenessTrack = SelectBestSimpleDestroyerTrack(
                destroyerDefinition_.combat, destroyerTracks_.Tracks());
            if (awarenessTrack)
            {
                const float bearing = awarenessTrack->estimatedBearingRadians;
                destroyerActivePulse_ = Acoustics::ActiveAcousticPulse{
                    .originMeters = destroyerAcoustics->passiveReceiver.positionMeters,
                    .forwardUnitVector = {
                        .x = static_cast<float>(std::cos(static_cast<double>(bearing))),
                        .y = static_cast<float>(std::sin(static_cast<double>(bearing))),
                        .z = 0.0F},
                    .sourceLevelDb = {.levelDb = {228.0F, 232.0F, 234.0F, 230.0F}},
                    .beamHalfAngleRadians = 0.30F,
                    .emissionTimeSeconds = simulationTimeSeconds};
                destroyerActiveReflector_ = Acoustics::AcousticReflector{
                    .positionMeters = playerSnapshot.emitter.positionMeters,
                    .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            }
        }

        if (destroyerActivePulse_ && destroyerActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = destroyerAcoustics->passiveReceiver;
            activeReceiver.positionMeters = destroyerActivePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_,
                *destroyerActivePulse_,
                *destroyerActiveReflector_,
                activeReceiver,
                simulationTimeSeconds);
            if (!activeEcho)
            {
                return std::unexpected("M5-F.1 destroyer active echo failed: " + activeEcho.error());
            }
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, destroyerActivePulse_->originMeters);
                if (!perceived || !destroyerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-F.1 destroyer ranged evidence failed perception integration");
                }
                destroyerActivePulse_.reset();
                destroyerActiveReflector_.reset();
                nextDestroyerActivePulseTimeSeconds_ =
                    simulationTimeSeconds + M5CombatDestroyerActiveRangingIntervalSeconds;
            }
        }

        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        if (destroyerDecision->action == SimpleDestroyerCombatAction::LaunchWeapon && !destroyerTorpedo_)
        {
            const auto targetTrack = FindTrack(destroyerTracks_.Tracks(), destroyer_.weapon.targetTrackId);
            if (!targetTrack)
            {
                return std::unexpected("M5-F.2 destroyer launch lost its perceived fire-control track");
            }
            const auto materialized = MaterializeDestroyerLaunch(
                *destroyerAcoustics, *targetTrack, simulationTimeSeconds);
            if (!materialized)
            {
                return std::unexpected(materialized.error());
            }
        }

        bool integratedPlayerEvidence = false;
        const auto destroyerPassive = IntegratePlayerPassiveEmitter(
            destroyerAcoustics->emitter, playerSnapshot.passiveReceiver, simulationTimeSeconds, "destroyer");
        if (!destroyerPassive)
            return std::unexpected(destroyerPassive.error());
        integratedPlayerEvidence = *destroyerPassive;
        if (civilianEmitter)
        {
            const auto civilianPassive = IntegratePlayerPassiveEmitter(
                *civilianEmitter, playerSnapshot.passiveReceiver, simulationTimeSeconds, "civilian vessel");
            if (!civilianPassive)
                return std::unexpected(civilianPassive.error());
            integratedPlayerEvidence = integratedPlayerEvidence || *civilianPassive;
        }

        if (automatedPlayer && !activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
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

        if (activePulse_ && activeReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = playerSnapshot.passiveReceiver;
            activeReceiver.positionMeters = activePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *activePulse_, *activeReflector_, activeReceiver, simulationTimeSeconds,
                {}, {}, M5CombatPlayerFireControlActiveSonarConfig);
            if (!activeEcho)
            {
                return std::unexpected("M5-H player active echo failed: " + activeEcho.error());
            }
            if (activeEcho->has_value())
            {
                lastPlayerActiveEchoObservation_ = **activeEcho;
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, activePulse_->originMeters);
                if (!perceived || !playerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-H active echo failed perception integration");
                }
                integratedPlayerEvidence = true;
                activePulse_.reset();
                activeReflector_.reset();
                nextActivePulseTimeSeconds_ = simulationTimeSeconds + M5CombatActiveRangingIntervalSeconds;
            }
        }
        if (!integratedPlayerEvidence && !playerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H player TrackManager failed to advance");
        }

        const auto readiness = playerCombat_.Advance(simulationTimeSeconds);
        if (!readiness)
        {
            return std::unexpected("M5-J2 player commander readiness failed: " + readiness.error());
        }

        if (automatedPlayer)
        {
            if (!playerTorpedo_.has_value())
            {
                const auto automated = AdvanceAutomatedPlayerCommander(playerSnapshot, simulationTimeSeconds);
                if (!automated)
                {
                    return std::unexpected(automated.error());
                }
            }
        }
        else
        {
            for (const PlayerCombatCommand command : commands)
            {
                if (command.type == PlayerCombatCommandType::PreviousWeapon ||
                    command.type == PlayerCombatCommandType::NextWeapon)
                {
                    const auto feedback = ExecutePlayerWeaponSelectionCommand(command, simulationTimeSeconds);
                    if (!feedback)
                    {
                        return std::unexpected("M5 Weapon Selector command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (command.type == PlayerCombatCommandType::ToggleP700SalvoMode)
                {
                    if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit)
                    {
                        lastCombatCommand_ = PlayerCombatCommandFeedback{
                            .command = command.type, .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = "P-700 salvo mode is available only while P-700 GRANIT is selected"};
                        continue;
                    }
                    if (playerCombat_.P700SalvoMode() == Weapons::P700SalvoMode::Single &&
                        (!p700LauncherInventory_ || !p700LauncherInventory_->LoadedSlotIndices(2U)))
                    {
                        lastCombatCommand_ = PlayerCombatCommandFeedback{
                            .command = command.type, .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = "PAIR requires a complete loaded two-missile hatch group"};
                        continue;
                    }
                    const auto toggled = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
                    if (!toggled) return std::unexpected("D2 P-700 salvo-mode command failed: " + toggled.error());
                    lastCombatCommand_ = *toggled;
                    continue;
                }
                if (command.type == PlayerCombatCommandType::ActiveSonarPing)
                {
                    const auto feedback = ExecutePlayerActiveSonarCommand(
                        playerSnapshot, *destroyerAcoustics, civilianEmitter, simulationTimeSeconds);
                    if (!feedback)
                    {
                        return std::unexpected("M5-J5 player active-sonar command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (command.type == PlayerCombatCommandType::DeployDecoy)
                {
                    const auto feedback = ExecutePlayerDecoyCommand(playerSnapshot, simulationTimeSeconds);
                    if (!feedback)
                    {
                        return std::unexpected("M5-J4 player decoy command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (command.type == PlayerCombatCommandType::TogglePeriscope)
                {
                    lastCombatCommand_ = TogglePeriscopeForSelectedTrack(
                        periscopeState_, playerTracks_, playerCombat_.SelectedTrackId(), playerSnapshot.signedDepthMeters);
                    continue;
                }
                if (command.type == PlayerCombatCommandType::VisualIdentify)
                {
                    const auto selectedTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
                    const auto truth = selectedTrack
                        ? SelectSurfaceContactTruthByBearing(
                              playerSnapshot.passiveReceiver.positionMeters,
                              selectedTrack->estimatedBearingRadians,
                              surfaceTruths,
                              M5CombatSurfaceTruthAssociationGateRadians)
                        : std::nullopt;
                    if (!selectedTrack || !truth)
                    {
                        lastCombatCommand_ = PlayerCombatCommandFeedback{
                            .command = PlayerCombatCommandType::VisualIdentify,
                            .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = "visual identification has no surface contact aligned with the selected perceived bearing"};
                        continue;
                    }
                    const float surfaceLevelYMeters =
                        playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
                    const auto feedback = VisualIdentifySelectedTrack(
                        periscopeState_,
                        playerTracks_,
                        playerCombat_.SelectedTrackId(),
                        playerSnapshot.emitter.positionMeters,
                        playerSnapshot.signedDepthMeters,
                        surfaceLevelYMeters,
                        PeriscopeTargetTruth{
                            .positionMeters = truth->emitter.positionMeters,
                            .visualClassification = truth->visualClassification,
                            .visibleHeightAboveSurfaceMeters = truth->visibleHeightAboveSurfaceMeters},
                        simulationTimeSeconds,
                        periscopeOpticalConditions_);
                    if (!feedback)
                    {
                        return std::unexpected("periscope visual-identification command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (playerTorpedo_.has_value() || playerP700_.has_value())
                {
                    continue;
                }
                if (command.type == PlayerCombatCommandType::FireWeapon)
                {
                    const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit
                        ? AssessPlayerP700Employment(playerSnapshot)
                        : AssessPlayerTorpedoEmployment(playerSnapshot);
                    if (!employment)
                    {
                        return std::unexpected("M5 weapon employment assessment failed: " + employment.error());
                    }
                    if (!employment->allowed)
                    {
                        lastCombatCommand_ = PlayerCombatCommandFeedback{
                            .command = PlayerCombatCommandType::FireWeapon,
                            .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = std::string(Armament::PlayerWeaponName(selectedPlayerWeapon_)) +
                                       " launch blocked: " + employment->reason};
                        continue;
                    }
                }
                const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
                if (!executed)
                {
                    return std::unexpected("M5-J2 player command failed: " + executed.error());
                }
                lastCombatCommand_ = *executed;
            }
        }

        if (!playerTorpedo_.has_value() && !playerP700_.has_value() &&
            playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)
        {
            const auto targetTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.Weapon().targetTrackId);
            if (!targetTrack)
            {
                return std::unexpected("M5-J2 launched weapon lost its perceived launch track on the launch tick");
            }
            const auto launch = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit
                ? MaterializePlayerP700Launch(playerSnapshot, *destroyerAcoustics, *targetTrack, simulationTimeSeconds)
                : MaterializePlayerLaunch(
                      playerSnapshot, *destroyerAcoustics, civilianEmitter, *targetTrack, simulationTimeSeconds);
            if (!launch)
            {
                return std::unexpected(launch.error());
            }
        }

        if (decoy_)
        {
            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-E.1 decoy advance failed: " + advanced.error());
            }
        }
        if (playerDecoy_)
        {
            const auto advanced = Weapons::AdvanceAcousticDecoy(
                playerDecoyDefinition_, *playerDecoy_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-J4 player decoy advance failed: " + advanced.error());
            }
        }

        std::optional<Weapons::ConventionalTorpedoImpact> impact{};
        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto seekerDecision = AdvancePlayerTorpedoSeeker(
                *destroyerAcoustics, civilianEmitter, surfaceTruths, simulationTimeSeconds);
            if (!seekerDecision)
            {
                return std::unexpected("M5-E.1 live torpedo seeker failed: " + seekerDecision.error());
            }

            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerTorpedo_->guidanceTrackId);
            const float forwardProgressMeters = playerTorpedoLaunchPosition_
                ? (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_
                : 0.0F;
            const bool seekerEnabled = forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
            const auto guidanceTrack = seekerEnabled
                ? std::optional<Perception::Track>{}
                : BuildPlayerTorpedoGuidanceTrack(perceivedTrack);

            const auto advanced = seekerDecision->guidanceCue
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    playerTorpedoDefinition_,
                    playerTorpedoSeekerConfig_,
                    *playerTorpedo_,
                    *seekerDecision->guidanceCue,
                    *physicsWorld_,
                    simulationTimeSeconds)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-E.1 torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                impact = **advanced;
                lastExplosion_ = impact->explosion;
                pendingPlayerTorpedoSeekerEmissions_.clear();
                playerTorpedoActivePulse_.reset();
                playerTorpedoActiveReflector_.reset();
                if (impact->physicsHit.body == destroyer_.body)
                {
                    const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-E.1 destroyer damage application failed: " + damaged.error());
                    }
                }
                else if (civilian_ && impact->physicsHit.body == civilian_->body)
                {
                    const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, impact->damage);
                    if (!damaged)
                        return std::unexpected("civilian torpedo damage application failed: " + damaged.error());
                }
            }
        }

        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent &&
            (playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::RangeExpired ||
             playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::EnduranceExpired))
        {
            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);
            if (!rearmed)
                return std::unexpected("player torpedo range/endurance re-arm failed: " + rearmed.error());
            playerTorpedo_.reset();
            playerTorpedoLaunchPosition_.reset();
            playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{
                .selectedTrackId = std::nullopt,
                .lastUpdateTimeSeconds = simulationTimeSeconds};
            pendingPlayerTorpedoSeekerEmissions_.clear();
            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
            playerTorpedoActivePulse_.reset();
            playerTorpedoActiveReflector_.reset();
            playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;
        }

        std::optional<Weapons::P700GranitImpact> p700Impact{};
        if (playerP700_)
        {
            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerP700_->guidanceTrackId);
            std::vector<Weapons::P700SalvoObservation> cooperativeEvidence;
            const auto collectSeekerEvidence = [&](const Weapons::P700GranitRuntimeState& missile,
                                                   const std::uint64_t missileId) -> std::expected<void, std::string>
            {
                if (missile.phase != Weapons::P700GranitPhase::Cruise &&
                    missile.phase != Weapons::P700GranitPhase::Terminal)
                    return {};
                if (!missile.guidanceTrackId) return {};
                const std::uint64_t sample = static_cast<std::uint64_t>(std::llround(simulationTimeSeconds * 4.0));
                const auto observed = ObserveP700SurfaceContact(
                    P700SeekerObservationConfig{}, missile, missileId, *missile.guidanceTrackId,
                    surfaceTruths, simulationTimeSeconds, missile.terminalRandomSeed ^ sample);
                if (!observed) return std::unexpected(observed.error());
                if (observed->has_value()) cooperativeEvidence.push_back(**observed);
                return {};
            };
            if (auto r = collectSeekerEvidence(*playerP700_, 1U); !r)
                return std::unexpected("P-700 leader seeker failed: " + r.error());
            for (std::size_t index = 0U; index < playerP700Wingmen_.size(); ++index)
                if (auto r = collectSeekerEvidence(playerP700Wingmen_[index], index + 2U); !r)
                    return std::unexpected("P-700 wingman seeker failed: " + r.error());

            if (!cooperativeEvidence.empty())
            {
                const auto fused = Weapons::FuseP700SalvoObservations(cooperativeEvidence);
                if (fused)
                {
                    const Perception::Track salvoTrack = Weapons::P700SalvoTrackAsPerceivedTrack(*fused, simulationTimeSeconds);
                    if (Weapons::ValidateTrackForWeapon(playerP700Definition_.weapon, salvoTrack))
                    {
                        const auto leaderUpdated = Weapons::UpdateP700PerceivedGuidance(
                            playerP700Definition_, *playerP700_, salvoTrack);
                        if (!leaderUpdated) return std::unexpected("P-700 fused leader guidance failed: " + leaderUpdated.error());
                        for (auto& wingman : playerP700Wingmen_)
                        {
                            const auto updated = Weapons::UpdateP700PerceivedGuidance(playerP700Definition_, wingman, salvoTrack);
                            if (!updated) return std::unexpected("P-700 fused wingman guidance failed: " + updated.error());
                        }
                    }
                }
            }

            bool actualTargetHasTerminalDefense = true;
            if (perceivedTrack)
            {
                const auto truth = SelectSurfaceContactTruthByBearing(
                    playerSnapshot.passiveReceiver.positionMeters, perceivedTrack->estimatedBearingRadians,
                    surfaceTruths, M5CombatSurfaceTruthAssociationGateRadians);
                if (truth && truth->kind == SurfaceContactTruthKind::CivilianVessel)
                    actualTargetHasTerminalDefense = false;
            }
            std::optional<Weapons::P700TerminalDefenseProfile> targetDefense{};
            if (!p700AcceptanceMode_ && actualTargetHasTerminalDefense)
            {
                Weapons::P700TerminalDefenseProfile defense{};
                if (playerP700LaunchRangeMeters_ &&
                    *playerP700LaunchRangeMeters_ < Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters)
                {
                    // GAME POLICY: a deliberately too-close Granit shot gets less time/distance to establish its
                    // preferred flight profile, giving a defended combatant a better terminal interception window.
                    const float minimumRange = Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters;
                    const float shortfall = std::clamp(
                        (minimumRange - *playerP700LaunchRangeMeters_) / minimumRange, 0.0F, 1.0F);
                    defense.hardKillProbability = std::clamp(
                        defense.hardKillProbability + 0.30F * shortfall, 0.0F, 1.0F);
                    defense.maneuverDefeatProbability = std::clamp(
                        defense.maneuverDefeatProbability + 0.10F * shortfall, 0.0F, 1.0F);
                    defense.seekerFailureProbability = std::clamp(
                        defense.seekerFailureProbability + 0.05F * shortfall, 0.0F, 1.0F);
                }
                targetDefense = defense;
            }

            const auto applyP700Impact = [&](const Weapons::P700GranitImpact& hit) -> std::expected<void, std::string>
            {
                if (!p700Impact) p700Impact = hit;
                lastExplosion_ = hit.explosion;
                if (hit.physicsHit.body == destroyer_.body && !destroyer_.integrity.destroyed)
                {
                    const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, hit.damage);
                    if (!damaged) return std::unexpected("P-700 destroyer damage failed: " + damaged.error());
                }
                else if (civilian_ && hit.physicsHit.body == civilian_->body && !civilian_->integrity.destroyed)
                {
                    const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, hit.damage);
                    if (!damaged) return std::unexpected("civilian P-700 damage failed: " + damaged.error());
                }
                return {};
            };
            const auto advanceMember = [&](Weapons::P700GranitRuntimeState& missile) -> std::expected<void, std::string>
            {
                if (missile.phase == Weapons::P700GranitPhase::Stored || missile.phase == Weapons::P700GranitPhase::Spent)
                    return {};
                const auto guidance = missile.guidanceTrackId == playerP700_->guidanceTrackId
                    ? FindTrack(playerTracks_.Tracks(), missile.guidanceTrackId)
                    : std::optional<Perception::Track>{};
                const auto advanced = Weapons::AdvanceP700GranitWithCollision(
                    playerP700Definition_, missile, guidance, *physicsWorld_, simulationTimeSeconds, playerBody_, targetDefense);
                if (!advanced) return std::unexpected(advanced.error());
                if (advanced->has_value()) return applyP700Impact(**advanced);
                return {};
            };
            if (auto r = advanceMember(*playerP700_); !r)
                return std::unexpected("P-700 leader fixed-step advance failed: " + r.error());
            for (auto& wingman : playerP700Wingmen_)
                if (auto r = advanceMember(wingman); !r)
                    return std::unexpected("P-700 wingman fixed-step advance failed: " + r.error());

            const bool allSpent = playerP700_->phase == Weapons::P700GranitPhase::Spent &&
                std::ranges::all_of(playerP700Wingmen_, [](const auto& missile) {
                    return missile.phase == Weapons::P700GranitPhase::Spent;
                });
            if (allSpent && !p700AcceptanceMode_)
            {
                const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);
                if (!rearmed) return std::unexpected("P-700 commander re-arm failed: " + rearmed.error());
                playerP700_.reset();
                playerP700Wingmen_.clear();
                playerP700LaunchRangeMeters_.reset();
                playerP700LaunchSlotIndex_.reset();
                playerP700LaunchSlotIndices_.clear();
            }
        }

        const auto threatPerception = AdvanceIncomingThreatPerception(playerSnapshot, simulationTimeSeconds);
        if (!threatPerception)
        {
            return std::unexpected(threatPerception.error());
        }

        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto seekerDecision = AdvanceDestroyerTorpedoSeeker(playerSnapshot, simulationTimeSeconds);
            if (!seekerDecision)
            {
                return std::unexpected("M5-J4 destroyer torpedo seeker failed: " + seekerDecision.error());
            }
            const auto perceivedTrack = FindTrack(destroyerTracks_.Tracks(), destroyerTorpedo_->guidanceTrackId);
            const float forwardProgressMeters = destroyerTorpedoLaunchPosition_
                ? (destroyerTorpedo_->positionMeters.x - destroyerTorpedoLaunchPosition_->x) *
                    destroyerTorpedoForwardSign_
                : 0.0F;
            const bool seekerEnabled = forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
            const auto guidanceTrack = seekerEnabled ? std::optional<Perception::Track>{} : perceivedTrack;
            const auto advanced = seekerDecision->guidanceCue
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    destroyerTorpedoDefinition_,
                    destroyerTorpedoSeekerConfig_,
                    *destroyerTorpedo_,
                    *seekerDecision->guidanceCue,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    destroyerTorpedoDefinition_,
                    *destroyerTorpedo_,
                    guidanceTrack,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body);
            if (!advanced)
            {
                return std::unexpected("M5-F.2/J4 destroyer torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                destroyerImpact = **advanced;
                lastExplosion_ = destroyerImpact->explosion;
                pendingDestroyerTorpedoSeekerEmissions_.clear();
                destroyerTorpedoActivePulse_.reset();
                destroyerTorpedoActiveReflector_.reset();
                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)
                {
                    const auto damaged = DeepRun::Combat::ApplyCombatDamage(*playerIntegrity_, destroyerImpact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-F.2 player torpedo-damage application failed: " + damaged.error());
                    }
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
        std::optional<float> civilianIntegrityFraction{};
        bool civilianDestroyed = false;
        if (civilian_)
        {
            civilianIntegrityFraction = std::clamp(
                civilian_->integrity.remainingIntegrity / civilian_->integrity.maximumIntegrity, 0.0F, 1.0F);
            civilianDestroyed = civilian_->integrity.destroyed;
        }
        PlayerCombatPresentationSnapshot playerCombatPresentation =
            playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot);
        playerCombatPresentation.selectedWeapon = selectedPlayerWeapon_;
        playerCombatPresentation.p700LoadedCount = p700LauncherInventory_ ? p700LauncherInventory_->LoadedCount() : 0U;
        if (selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit &&
            (playerCombatPresentation.p700LoadedCount == 0U ||
             (playerCombat_.P700SalvoMode() == Weapons::P700SalvoMode::Pair &&
              (!p700LauncherInventory_ || !p700LauncherInventory_->LoadedSlotIndices(2U)))))
        {
            playerCombatPresentation.canPrepareWeapon = false;
            playerCombatPresentation.canFireWeapon = false;
        }
        if (playerCombatPresentation.canFireWeapon)
        {
            const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit
                ? AssessPlayerP700Employment(playerSnapshot)
                : AssessPlayerTorpedoEmployment(playerSnapshot);
            playerCombatPresentation.canFireWeapon = employment.has_value() && employment->allowed;
        }
        ApplyIncomingThreatPresentation(playerCombatPresentation);
        const auto selectedPlayerTrack = FindTrack(
            playerTrackSnapshot, playerCombat_.SelectedTrackId());
        playerCombatPresentation.canActiveSonarPing = selectedPlayerTrack.has_value() &&
            selectedPlayerTrack->lifecycle != Perception::TrackLifecycleState::Lost &&
            !activePulse_.has_value() && simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_;
        playerCombatPresentation.activeSonarPulsePending = activePulse_.has_value();
        ApplyPeriscopePresentation(playerCombatPresentation, periscopeState_, playerSnapshot.signedDepthMeters);

        float sonarOwnshipHeadingRadians = 0.0F;
        if (currentPlayerPhysicalProxy_.has_value())
        {
            const auto& orientation = currentPlayerPhysicalProxy_->orientation;
            sonarOwnshipHeadingRadians = static_cast<float>(std::atan2(
                2.0 * (static_cast<double>(orientation.w) * orientation.z +
                       static_cast<double>(orientation.x) * orientation.y),
                1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                             static_cast<double>(orientation.z) * orientation.z)));
            if (currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign < 0.0F)
            {
                sonarOwnshipHeadingRadians += 3.14159265358979323846F;
            }
        }
        const auto sonarPresentation = BuildSonarPresentation(
            playerTrackSnapshot,
            playerCombat_.SelectedTrackId(),
            playerSnapshot.passiveReceiver.positionMeters,
            sonarOwnshipHeadingRadians,
            activePulse_,
            lastPlayerActiveEchoObservation_,
            simulationTimeSeconds);
        if (!sonarPresentation)
        {
            return std::unexpected("M5-V2 sonar presentation projection failed: " + sonarPresentation.error());
        }
        playerCombatPresentation.sonar = *sonarPresentation;
        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;
        playerCombatPresentation.playerDecoyActive = playerDecoy_.has_value() && playerDecoy_->active;
        if (lastCombatCommand_)
        {
            playerCombatPresentation.lastCommand = lastCombatCommand_;
        }
        return CombatPlaygroundFrame{
            .playerTracks = playerTrackSnapshot,
            .destroyerTracks = destroyerTracks_.Tracks(),
            .playerCombat = std::move(playerCombatPresentation),
            .destroyerDecision = *destroyerDecision,
            .playerTorpedoImpact = impact,
            .destroyerTorpedoImpact = destroyerImpact,
            .playerMineDetonation = mineDetonation,
            .playerP700Impact = p700Impact,
            .playerIntegrityFraction = playerIntegrityFraction,
            .playerDestroyed = playerDestroyed,
            .civilianIntegrityFraction = civilianIntegrityFraction,
            .civilianDestroyed = civilianDestroyed};
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerWeaponSelectionCommand(
        const PlayerCombatCommand command,
        const double simulationTimeSeconds)
    {
        if (command.type != PlayerCombatCommandType::PreviousWeapon && command.type != PlayerCombatCommandType::NextWeapon)
        {
            return std::unexpected("M5 Weapon Selector received a non-selector command");
        }
        const int direction = command.type == PlayerCombatCommandType::PreviousWeapon ? -1 : 1;
        const Armament::PlayerWeaponType next = Armament::CyclePlayerWeapon(selectedPlayerWeapon_, direction);
        if (next == Armament::PlayerWeaponType::P700Granit &&
            (!p700LauncherInventory_ || p700LauncherInventory_->LoadedCount() == 0U || !p700CarrierLaunchContract_))
        {
            return PlayerCombatCommandFeedback{
                .command = command.type,
                .accepted = false,
                .trackId = playerCombat_.SelectedTrackId(),
                .message = "P-700 GRANIT is unavailable without a loaded production Antey launcher"};
        }
        std::optional<Weapons::ConventionalTorpedoDefinition> nextTorpedoDefinition{};
        Weapons::WeaponDefinition definition{};
        if (next == Armament::PlayerWeaponType::P700Granit)
        {
            definition = playerP700Definition_.weapon;
        }
        else
        {
            const auto profile = Armament::MakePlayerTorpedoProfile(next);
            if (!profile || profile->employmentEnvelope == nullptr)
            {
                return std::unexpected("M5 Weapon Selector could not resolve the selected torpedo profile");
            }
            nextTorpedoDefinition = profile->definition;
            definition = profile->definition.weapon;
        }
        const auto reconfigured = playerCombat_.ReconfigureStoredWeapon(definition, simulationTimeSeconds);
        if (!reconfigured)
        {
            return std::unexpected("M5 Weapon Selector profile switch failed: " + reconfigured.error());
        }
        if (nextTorpedoDefinition)
        {
            playerTorpedoDefinition_ = *nextTorpedoDefinition;
        }
        selectedPlayerWeapon_ = next;
        return PlayerCombatCommandFeedback{
            .command = command.type,
            .accepted = true,
            .trackId = playerCombat_.SelectedTrackId(),
            .message = "selected " + std::string(Armament::PlayerWeaponName(selectedPlayerWeapon_)) +
                       "; automatic preparation started"};
    }

    struct PlayerP700LaunchCandidate final
    {
        std::size_t slotIndex = 0U;
        Weapons::P700CarrierLaunchContext carrier{};
    };

    [[nodiscard]] std::expected<PlayerP700LaunchCandidate, std::string> BuildPlayerP700LaunchCandidate(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const
    {
        if (!p700CarrierLaunchContract_ || !p700LauncherInventory_ || !currentPlayerPhysicalProxy_ ||
            !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.velocityMetersPerSecond.IsFinite() || !std::isfinite(playerSnapshot.signedDepthMeters))
        {
            return std::unexpected("P-700 production carrier state is unavailable");
        }
        const auto slotIndex = p700LauncherInventory_->FirstLoadedSlotIndex();
        if (!slotIndex)
        {
            return std::unexpected("P-700 production launcher inventory is exhausted");
        }
        const auto worldAnchor = p700CarrierLaunchContract_->BuildWorldAnchor(*slotIndex, *currentPlayerPhysicalProxy_);
        if (!worldAnchor)
        {
            return std::unexpected("P-700 production world anchor failed: " + worldAnchor.error());
        }
        const auto& orientation = currentPlayerPhysicalProxy_->orientation;
        float carrierHeadingRadians = static_cast<float>(std::atan2(
            2.0 * (static_cast<double>(orientation.w) * orientation.z +
                   static_cast<double>(orientation.x) * orientation.y),
            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                         static_cast<double>(orientation.z) * orientation.z)));
        if (currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign < 0.0F)
        {
            carrierHeadingRadians = Weapons::WrapEmploymentAngle(carrierHeadingRadians + 3.14159265358979323846F);
        }
        const auto& velocity = playerSnapshot.emitter.velocityMetersPerSecond;
        const float carrierSpeedMetersPerSecond = static_cast<float>(std::sqrt(
            static_cast<double>(velocity.x) * velocity.x +
            static_cast<double>(velocity.y) * velocity.y +
            static_cast<double>(velocity.z) * velocity.z));
        const float surfaceLevelMeters = playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
        return PlayerP700LaunchCandidate{
            .slotIndex = *slotIndex,
            .carrier = Weapons::P700CarrierLaunchContext{
                .launchPositionMeters = worldAnchor->positionMeters,
                .launchForwardUnitVector = worldAnchor->forwardUnitVector,
                .surfaceLevelYMeters = surfaceLevelMeters,
                .launchDepthMeters = playerSnapshot.signedDepthMeters,
                .carrierSpeedMetersPerSecond = carrierSpeedMetersPerSecond,
                .carrierHeadingRadians = carrierHeadingRadians}};
    }

    [[nodiscard]] std::expected<Weapons::WeaponEmploymentAssessment, std::string> AssessPlayerP700Employment(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const
    {
        const auto selected = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
        if (!selected || !selected->estimatedPositionMeters)
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false,
                .reason = "selected perceived track has no spatial estimate"};
        }
        if (playerCombat_.P700SalvoMode() == Weapons::P700SalvoMode::Pair &&
            (!p700LauncherInventory_ || !p700LauncherInventory_->LoadedSlotIndices(2U)))
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false, .reason = "PAIR requires two loaded missiles under one paired production hatch"};
        }
        const auto launch = BuildPlayerP700LaunchCandidate(playerSnapshot);
        if (!launch)
        {
            return Weapons::WeaponEmploymentAssessment{.allowed = false, .reason = launch.error()};
        }
        const float perceivedTargetDepthMeters = (std::max)(
            0.0F, launch->carrier.surfaceLevelYMeters - selected->estimatedPositionMeters->y);
        return Weapons::EvaluateWeaponEmployment(
            Weapons::P700GranitEmploymentEnvelope,
            Weapons::WeaponEmploymentContext{
                .launchPositionMeters = launch->carrier.launchPositionMeters,
                .perceivedTargetPositionMeters = *selected->estimatedPositionMeters,
                .launchDepthMeters = launch->carrier.launchDepthMeters,
                .perceivedTargetDepthMeters = perceivedTargetDepthMeters,
                .carrierSpeedMetersPerSecond = launch->carrier.carrierSpeedMetersPerSecond,
                .launcherHeadingRadians = launch->carrier.carrierHeadingRadians});
    }

    [[nodiscard]] std::expected<Weapons::WeaponEmploymentAssessment, std::string> AssessPlayerTorpedoEmployment(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const
    {
        const Weapons::WeaponEmploymentEnvelope* employmentEnvelope =
            Armament::PlayerTorpedoEmploymentEnvelope(selectedPlayerWeapon_);
        if (employmentEnvelope == nullptr)
        {
            return std::unexpected("selected player weapon is not a conventional torpedo profile");
        }
        if (playerTorpedoInventory_.LoadedCount(selectedPlayerWeapon_) == 0U)
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false, .reason = "selected torpedo inventory is exhausted"};
        }
        if (!currentPlayerPhysicalProxy_.has_value() || !currentPlayerPhysicalProxy_->orientation.IsFinite() ||
            !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.velocityMetersPerSecond.IsFinite() || !std::isfinite(playerSnapshot.signedDepthMeters))
        {
            return std::unexpected("live ownship state is unavailable for weapon employment");
        }

        if (currentPlayerPhysicalProxy_->turningAround)
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false,
                .reason = "weapon launch is blocked while the 2.5D carrier is turning around"};
        }

        const auto selected = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
        if (!selected.has_value() || !selected->estimatedPositionMeters.has_value())
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false,
                .reason = "selected perceived track has no spatial estimate"};
        }

        const auto& orientation = currentPlayerPhysicalProxy_->orientation;
        float launcherHeadingRadians = static_cast<float>(std::atan2(
            2.0 * (static_cast<double>(orientation.w) * orientation.z +
                   static_cast<double>(orientation.x) * orientation.y),
            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                         static_cast<double>(orientation.z) * orientation.z)));
        if (currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign < 0.0F)
        {
            launcherHeadingRadians = Weapons::WrapEmploymentAngle(launcherHeadingRadians + 3.14159265358979323846F);
        }
        const auto& velocity = playerSnapshot.emitter.velocityMetersPerSecond;
        const float carrierSpeedMetersPerSecond = static_cast<float>(std::sqrt(
            static_cast<double>(velocity.x) * velocity.x +
            static_cast<double>(velocity.y) * velocity.y +
            static_cast<double>(velocity.z) * velocity.z));
        const float surfaceLevelMeters = playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
        const float perceivedTargetDepthMeters = (std::max)(
            0.0F, surfaceLevelMeters - selected->estimatedPositionMeters->y);

        return Weapons::EvaluateWeaponEmployment(
            *employmentEnvelope,
            Weapons::WeaponEmploymentContext{
                .launchPositionMeters = playerSnapshot.emitter.positionMeters,
                .perceivedTargetPositionMeters = *selected->estimatedPositionMeters,
                .launchDepthMeters = playerSnapshot.signedDepthMeters,
                .perceivedTargetDepthMeters = perceivedTargetDepthMeters,
                .carrierSpeedMetersPerSecond = carrierSpeedMetersPerSecond,
                .launcherHeadingRadians = launcherHeadingRadians});
    }

    [[nodiscard]] std::expected<void, std::string> AdvanceAutomatedPlayerCommander(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
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
            const auto employment = AssessPlayerTorpedoEmployment(playerSnapshot);
            if (!employment)
            {
                return std::unexpected("M5-H automated weapon employment assessment failed: " + employment.error());
            }
            if (!employment->allowed)
            {
                return {};
            }
            const auto fired = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::FireWeapon}, tracks, simulationTimeSeconds);
            if (!fired || !fired->accepted)
            {
                return std::unexpected("M5-H automated commander could not fire on the qualifying perceived track");
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerActiveSonarCommand(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const std::optional<Acoustics::AcousticEmitter>& civilianEmitter,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || !playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !destroyerAcoustics.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-J5 active-sonar command input is invalid");
        }
        const auto selectedTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
        if (!selectedTrack || selectedTrack->lifecycle == Perception::TrackLifecycleState::Lost ||
            !std::isfinite(selectedTrack->estimatedBearingRadians))
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = playerCombat_.SelectedTrackId(),
                .message = "active sonar requires a selected perceived contact"};
        }
        if (activePulse_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = selectedTrack->trackId,
                .message = "active sonar pulse is already awaiting its echo"};
        }
        if (simulationTimeSeconds + 1.0e-9 < nextActivePulseTimeSeconds_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = selectedTrack->trackId,
                .message = "active sonar is cooling down"};
        }

        const auto truths = BuildSurfaceContactTruths(destroyerAcoustics, civilianEmitter);
        const auto truth = SelectSurfaceContactTruthByBearing(
            playerSnapshot.passiveReceiver.positionMeters,
            selectedTrack->estimatedBearingRadians,
            truths,
            M5CombatSurfaceTruthAssociationGateRadians);
        if (!truth)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = selectedTrack->trackId,
                .message = "active sonar found no physical reflector on the selected perceived bearing"};
        }

        const float bearing = selectedTrack->estimatedBearingRadians;
        activePulse_ = Acoustics::ActiveAcousticPulse{
            .originMeters = playerSnapshot.passiveReceiver.positionMeters,
            .forwardUnitVector = {
                .x = static_cast<float>(std::cos(static_cast<double>(bearing))),
                .y = static_cast<float>(std::sin(static_cast<double>(bearing))),
                .z = 0.0F},
            .sourceLevelDb = {.levelDb = {230.0F, 232.0F, 234.0F, 230.0F}},
            .beamHalfAngleRadians = 0.35F,
            .emissionTimeSeconds = simulationTimeSeconds};
        activeReflector_ = Acoustics::AcousticReflector{
            .positionMeters = truth->emitter.positionMeters,
            .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
        const auto exposed = ObserveExposureEvent(
            ExposureSource::ActiveSonarTransmission,
            destroyerAcoustics.passiveReceiver.positionMeters,
            playerSnapshot.emitter.positionMeters,
            destroyerAcoustics.passiveReceiver.sensorId,
            simulationTimeSeconds,
            static_cast<std::uint64_t>(std::llround(simulationTimeSeconds * 60.0)) ^ 0xA571C50AULL);
        if (!exposed) return std::unexpected("active-sonar exposure simulation failed: " + exposed.error());
        if (exposed->has_value() && !destroyerTracks_.IntegrateObservation(**exposed))
            return std::unexpected("active-sonar exposure failed hostile Track integration");
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::ActiveSonarPing,
            .accepted = true,
            .trackId = selectedTrack->trackId,
            .message = "active sonar ping emitted on selected contact bearing"};
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerDecoyCommand(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || !playerSnapshot.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-J4 player decoy command input is invalid");
        }
        if (!playerDecoyAvailable_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::DeployDecoy,
                .accepted = false,
                .trackId = std::nullopt,
                .message = "player acoustic decoy already expended"};
        }

        const Physics::PhysicsVector3 decoyPosition{
            .x = playerSnapshot.emitter.positionMeters.x - 20.0F,
            .y = playerSnapshot.emitter.positionMeters.y - M5CombatPlayerDecoyVerticalOffsetMeters,
            .z = playerSnapshot.emitter.positionMeters.z};
        const auto deployed = Weapons::DeployAcousticDecoy(
            playerDecoyDefinition_, decoyPosition, simulationTimeSeconds);
        if (!deployed)
        {
            return std::unexpected("M5-J4 player acoustic decoy deployment failed: " + deployed.error());
        }
        playerDecoy_ = *deployed;
        playerDecoyAvailable_ = false;
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::DeployDecoy,
            .accepted = true,
            .trackId = std::nullopt,
            .message = "player acoustic decoy deployed"};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializeDestroyerLaunch(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const Perception::Track& targetTrack,
        const double simulationTimeSeconds)
    {
        if (destroyer_.weapon.phase != Weapons::WeaponPhase::Launched ||
            destroyer_.weapon.targetTrackId != std::optional<std::uint64_t>{targetTrack.trackId} ||
            !targetTrack.estimatedPositionMeters ||
            !Weapons::ValidateTrackForWeapon(destroyerTorpedoDefinition_.weapon, targetTrack))
        {
            return std::unexpected("M5-F.2 destroyer torpedo materialization requires its accepted ranged Track");
        }
        if (!destroyerAcoustics.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-F.2 destroyer torpedo launch origin is invalid");
        }

        const float deltaX = targetTrack.estimatedPositionMeters->x - destroyerAcoustics.emitter.positionMeters.x;
        if (!std::isfinite(deltaX) || std::abs(deltaX) <= 1.0e-3F)
        {
            return std::unexpected("M5-F.2 destroyer torpedo launch has no horizontal target separation");
        }
        const float forwardSign = deltaX > 0.0F ? 1.0F : -1.0F;
        const Physics::PhysicsVector3 launchPosition{
            .x = destroyerAcoustics.emitter.positionMeters.x +
                 forwardSign * M5CombatDestroyerTorpedoLaunchClearanceMeters,
            .y = destroyerAcoustics.emitter.positionMeters.y - M5CombatDestroyerTorpedoLaunchDepthOffsetMeters,
            .z = destroyerAcoustics.emitter.positionMeters.z};
        const float launchHeading = static_cast<float>(std::atan2(
            static_cast<double>(targetTrack.estimatedPositionMeters->y) - launchPosition.y,
            static_cast<double>(targetTrack.estimatedPositionMeters->x) - launchPosition.x));
        const auto launched = Weapons::CreateLaunchedConventionalTorpedo(
            destroyerTorpedoDefinition_,
            destroyer_.weapon,
            launchPosition,
            launchHeading,
            targetTrack,
            simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("M5-F.2 destroyer torpedo runtime creation failed: " + launched.error());
        }
        destroyerTorpedo_ = *launched;
        destroyerTorpedoLaunchPosition_ = launchPosition;
        destroyerTorpedoForwardSign_ = forwardSign;
        destroyerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{
            .selectedTrackId = std::nullopt,
            .lastUpdateTimeSeconds = simulationTimeSeconds};
        pendingDestroyerTorpedoSeekerEmissions_.clear();
        nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        destroyerTorpedoActivePulse_.reset();
        destroyerTorpedoActiveReflector_.reset();
        destroyerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;
        pendingIncomingThreatEmissions_.clear();
        nextIncomingThreatEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializePlayerLaunch(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const std::optional<Acoustics::AcousticEmitter>& civilianEmitter,
        const Perception::Track& targetTrack,
        const double simulationTimeSeconds)
    {
        if (!targetTrack.estimatedPositionMeters ||
            !Weapons::ValidateTrackForWeapon(playerTorpedoDefinition_.weapon, targetTrack))
        {
            return std::unexpected("M5-J2 launch materialization requires the accepted perceived spatial track");
        }

        if (!currentPlayerPhysicalProxy_.has_value() || currentPlayerPhysicalProxy_->turningAround)
        {
            return std::unexpected("M5-H torpedo launch requires a stable 2.5D ownship facing");
        }
        const auto& orientation = currentPlayerPhysicalProxy_->orientation;
        float launchHeading = static_cast<float>(std::atan2(
            2.0 * (static_cast<double>(orientation.w) * orientation.z +
                   static_cast<double>(orientation.x) * orientation.y),
            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                         static_cast<double>(orientation.z) * orientation.z)));
        playerTorpedoForwardSign_ = currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign;
        if (playerTorpedoForwardSign_ < 0.0F)
        {
            launchHeading = Weapons::WrapEmploymentAngle(launchHeading + 3.14159265358979323846F);
        }
        const Physics::PhysicsVector3 launchPosition{
            .x = playerSnapshot.emitter.positionMeters.x +
                 std::cos(launchHeading) * M5CombatTorpedoLaunchClearanceMeters,
            .y = playerSnapshot.emitter.positionMeters.y +
                 std::sin(launchHeading) * M5CombatTorpedoLaunchClearanceMeters,
            .z = playerSnapshot.emitter.positionMeters.z};
        auto nextTorpedoInventory = playerTorpedoInventory_;
        const auto consumedRoundMass = nextTorpedoInventory.Consume(selectedPlayerWeapon_);
        if (!consumedRoundMass)
        {
            return std::unexpected("M5-H torpedo inventory consumption failed: " + consumedRoundMass.error());
        }
        const auto launched = Weapons::CreateLaunchedConventionalTorpedo(
            playerTorpedoDefinition_, playerCombat_.Weapon(), launchPosition, launchHeading,
            targetTrack, simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("M5-H torpedo runtime creation failed: " + launched.error());
        }
        // Commit the staged inventory only after the weapon runtime is successfully materialized.
        playerTorpedoInventory_ = nextTorpedoInventory;
        playerTorpedo_ = *launched;
        playerTorpedoLaunchPosition_ = launchPosition;
        playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{
            .selectedTrackId = std::nullopt,
            .lastUpdateTimeSeconds = simulationTimeSeconds};
        pendingPlayerTorpedoSeekerEmissions_.clear();
        nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        playerTorpedoActivePulse_.reset();
        playerTorpedoActiveReflector_.reset();
        playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;

        const auto torpedoExposure = ObserveExposureEvent(
            ExposureSource::TorpedoLaunch,
            destroyerAcoustics.passiveReceiver.positionMeters,
            launchPosition,
            destroyerAcoustics.passiveReceiver.sensorId,
            simulationTimeSeconds,
            static_cast<std::uint64_t>(std::llround(simulationTimeSeconds * 60.0)) ^ 0x70A0ED0ULL);
        if (!torpedoExposure) return std::unexpected("torpedo-launch exposure simulation failed: " + torpedoExposure.error());
        if (torpedoExposure->has_value() && !destroyerTracks_.IntegrateObservation(**torpedoExposure))
            return std::unexpected("torpedo-launch exposure failed hostile Track integration");

        const auto truths = BuildSurfaceContactTruths(destroyerAcoustics, civilianEmitter);
        const auto actualTarget = SelectSurfaceContactTruthByBearing(
            playerSnapshot.passiveReceiver.positionMeters,
            targetTrack.estimatedBearingRadians,
            truths,
            M5CombatSurfaceTruthAssociationGateRadians);
        if (actualTarget && actualTarget->kind == SurfaceContactTruthKind::MilitaryCombatant)
        {
            const Physics::PhysicsVector3 decoyPosition{
                .x = destroyerAcoustics.emitter.positionMeters.x - 20.0F,
                .y = destroyerAcoustics.emitter.positionMeters.y - M5CombatDecoyVerticalOffsetMeters,
                .z = destroyerAcoustics.emitter.positionMeters.z};
            const auto decoy = Weapons::DeployAcousticDecoy(
                decoyDefinition_, decoyPosition, simulationTimeSeconds);
            if (!decoy)
            {
                return std::unexpected("M5-H decoy deployment failed: " + decoy.error());
            }
            decoy_ = *decoy;
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializePlayerP700Launch(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const Perception::Track& targetTrack,
        const double simulationTimeSeconds)
    {
        if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit ||
            playerCombat_.Weapon().phase != Weapons::WeaponPhase::Launched ||
            playerCombat_.Weapon().targetTrackId != std::optional<std::uint64_t>{targetTrack.trackId} ||
            !Weapons::ValidateTrackForWeapon(playerP700Definition_.weapon, targetTrack) ||
            !p700CarrierLaunchContract_ || !p700LauncherInventory_ || !currentPlayerPhysicalProxy_)
        {
            return std::unexpected("D2 P-700 salvo materialization requires accepted perceived Track and production carrier");
        }
        const auto& velocity = playerSnapshot.emitter.velocityMetersPerSecond;
        const float carrierSpeed = static_cast<float>(std::sqrt(
            static_cast<double>(velocity.x) * velocity.x +
            static_cast<double>(velocity.y) * velocity.y +
            static_cast<double>(velocity.z) * velocity.z));
        const float surfaceLevel = playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
        const std::uint64_t salvoId = nextP700SalvoId_++;
        auto committed = Armament::LaunchProductionP700Salvo(
            playerP700Definition_, targetTrack, *p700CarrierLaunchContract_, *p700LauncherInventory_,
            *currentPlayerPhysicalProxy_, surfaceLevel, playerSnapshot.signedDepthMeters, carrierSpeed,
            playerCombat_.P700SalvoMode(), simulationTimeSeconds, salvoId);
        if (!committed)
            return std::unexpected("D2 production P-700 salvo launch failed: " + committed.error());
        if (committed->runtime.missiles.empty() || committed->launcherSlotIndices.empty())
            return std::unexpected("D2 production P-700 salvo committed no missile");

        playerP700LaunchSlotIndex_ = committed->launcherSlotIndices.front();
        playerP700LaunchSlotIndices_ = committed->launcherSlotIndices;
        if (!targetTrack.estimatedPositionMeters)
            return std::unexpected("D2 P-700 launch lost its perceived spatial estimate");
        playerP700LaunchRangeMeters_ = Weapons::P700DistanceMeters(
            committed->runtime.missiles.front().positionMeters, *targetTrack.estimatedPositionMeters);
        playerP700_ = std::move(committed->runtime.missiles.front());
        playerP700Wingmen_.clear();
        for (std::size_t index = 1U; index < committed->runtime.missiles.size(); ++index)
            playerP700Wingmen_.push_back(std::move(committed->runtime.missiles[index]));

        const auto exposure = ObserveExposureEvent(
            ExposureSource::P700Launch,
            destroyerAcoustics.passiveReceiver.positionMeters,
            playerSnapshot.emitter.positionMeters,
            destroyerAcoustics.passiveReceiver.sensorId,
            simulationTimeSeconds,
            salvoId ^ 0x503730304558504FULL);
        if (!exposure) return std::unexpected("P-700 launch exposure simulation failed: " + exposure.error());
        if (exposure->has_value() && !destroyerTracks_.IntegrateObservation(**exposure))
            return std::unexpected("P-700 launch exposure failed hostile Track integration");
        return {};
    }

    [[nodiscard]] std::expected<Weapons::TorpedoSeekerModeDecision, std::string> AdvancePlayerTorpedoSeeker(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const std::optional<Acoustics::AcousticEmitter>& civilianEmitter,
        const std::span<const SurfaceContactSensorTruth> surfaceTruths,
        const double simulationTimeSeconds)
    {
        if (!playerTorpedo_ || playerTorpedo_->movementDomain != Weapons::MovementDomain::Underwater ||
            !playerTorpedo_->positionMeters.IsFinite() || !destroyerAcoustics.emitter.positionMeters.IsFinite() ||
            !destroyerAcoustics.emitter.continuousSourceLevelDb.IsFinite() || !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-E.1 seeker source/runtime input is invalid");
        }

        const float forwardProgressMeters = playerTorpedoLaunchPosition_
            ? (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_
            : 0.0F;
        const bool seekerEnabled = playerTorpedoLaunchPosition_.has_value() &&
            forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
        if (!seekerEnabled)
        {
            return Weapons::UpdateTorpedoSeekerMode(
                playerTorpedoSeekerConfig_, playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_,
                false, std::nullopt, std::nullopt, simulationTimeSeconds);
        }

        if (simulationTimeSeconds + 1.0e-9 >= nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_)
        {
            pendingPlayerTorpedoSeekerEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = destroyerAcoustics.emitter.positionMeters,
                .sourceLevelDb = destroyerAcoustics.emitter.continuousSourceLevelDb,
                .emissionTimeSeconds = simulationTimeSeconds});
            if (civilianEmitter)
            {
                pendingPlayerTorpedoSeekerEmissions_.push_back(Acoustics::AcousticEmission{
                    .positionMeters = civilianEmitter->positionMeters,
                    .sourceLevelDb = civilianEmitter->continuousSourceLevelDb,
                    .emissionTimeSeconds = simulationTimeSeconds});
            }
            if (decoy_)
            {
                const auto decoyEmission = Weapons::SampleAcousticDecoyEmission(
                    decoyDefinition_, *decoy_, simulationTimeSeconds);
                if (!decoyEmission)
                    return std::unexpected("M5-E.1 decoy emission snapshot failed: " + decoyEmission.error());
                if (decoyEmission->has_value())
                    pendingPlayerTorpedoSeekerEmissions_.push_back(**decoyEmission);
            }
            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver passiveReceiver{
            .sensorId = "M5_PLAYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = playerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedPassiveObservation = false;
        auto emission = pendingPlayerTorpedoSeekerEmissions_.begin();
        while (emission != pendingPlayerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
                return std::unexpected("M5-E.1 seeker emission arrival time is non-finite");
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }
            const float referenceSurfaceYMeters = destroyerAcoustics.emitter.positionMeters.y +
                destroyerDefinition_.bodyCenterBelowSurfaceMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters, passiveReceiver.positionMeters, referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 torpedo passive environment path failed: " + environment.error());
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
                return std::unexpected("M5-E.1 seeker acoustic propagation failed: " + observed.error().message);
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !playerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5-E.1 seeker perception integration failed");
                integratedPassiveObservation = true;
            }
            emission = pendingPlayerTorpedoSeekerEmissions_.erase(emission);
        }
        if (!integratedPassiveObservation && !playerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5-E.1 passive seeker TrackManager failed to advance");

        bool integratedActiveObservation = false;
        if (playerTorpedoActivePulse_ && playerTorpedoActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = passiveReceiver;
            activeReceiver.sensorId = "M5_PLAYER_TORPEDO_ACTIVE_SEEKER";
            activeReceiver.positionMeters = playerTorpedoActivePulse_->originMeters;
            const float referenceSurfaceYMeters = destroyerAcoustics.emitter.positionMeters.y +
                destroyerDefinition_.bodyCenterBelowSurfaceMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                playerTorpedoActivePulse_->originMeters, playerTorpedoActiveReflector_->positionMeters,
                referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 torpedo active environment path failed: " + environment.error());
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *playerTorpedoActivePulse_, *playerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, *environment, *environment, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)
                return std::unexpected("M5 torpedo active echo failed: " + activeEcho.error());
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, playerTorpedoActivePulse_->originMeters);
                if (!perceived || !playerTorpedoActiveSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5 torpedo active echo failed perception integration");
                integratedActiveObservation = true;
                playerTorpedoActivePulse_.reset();
                playerTorpedoActiveReflector_.reset();
            }
            else if (simulationTimeSeconds + 1.0e-9 >= playerTorpedoActivePulseDeadlineSeconds_)
            {
                playerTorpedoActivePulse_.reset();
                playerTorpedoActiveReflector_.reset();
            }
        }
        if (!integratedActiveObservation && !playerTorpedoActiveSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5 torpedo active seeker TrackManager failed to advance");

        const auto passiveCue = Weapons::SelectBestTorpedoSeekerCue(
            playerTorpedoSeekerConfig_, playerTorpedoSeekerTracks_.Tracks());
        const auto activeCue = Weapons::SelectBestTorpedoSeekerCue(
            playerTorpedoSeekerConfig_, playerTorpedoActiveSeekerTracks_.Tracks());
        if (!passiveCue || !activeCue)
            return std::unexpected("M5 torpedo local seeker cue selection failed");
        auto decision = Weapons::UpdateTorpedoSeekerMode(
            playerTorpedoSeekerConfig_, playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_,
            true, *passiveCue, *activeCue, simulationTimeSeconds);
        if (!decision)
            return decision;

        if (decision->requestActivePing && !playerTorpedoActivePulse_)
        {
            playerTorpedoActivePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = playerTorpedo_->positionMeters,
                .forwardUnitVector = {
                    .x = static_cast<float>(std::cos(static_cast<double>(playerTorpedo_->headingRadians))),
                    .y = static_cast<float>(std::sin(static_cast<double>(playerTorpedo_->headingRadians))),
                    .z = 0.0F},
                .sourceLevelDb = {.levelDb = {188.0F, 191.0F, 193.0F, 189.0F}},
                .beamHalfAngleRadians = M5CombatTorpedoActiveBeamHalfAngleRadians,
                .emissionTimeSeconds = simulationTimeSeconds};
            const auto truth = SelectSurfaceContactTruthByBearing(
                playerTorpedo_->positionMeters,
                playerTorpedo_->headingRadians,
                surfaceTruths,
                M5CombatTorpedoActiveBeamHalfAngleRadians);
            if (truth)
            {
                playerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                    .positionMeters = truth->emitter.positionMeters,
                    .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            }
            playerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
            const auto notified = Weapons::NotifyTorpedoSeekerActivePingEmitted(
                playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_, simulationTimeSeconds);
            if (!notified)
                return std::unexpected(notified.error());
        }
        return decision;
    }

    [[nodiscard]] std::expected<Weapons::TorpedoSeekerModeDecision, std::string>
    AdvanceDestroyerTorpedoSeeker(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!destroyerTorpedo_ || destroyerTorpedo_->movementDomain != Weapons::MovementDomain::Underwater ||
            !destroyerTorpedo_->positionMeters.IsFinite() || !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.continuousSourceLevelDb.IsFinite() || !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-J4 hostile seeker source/runtime input is invalid");
        }

        const float forwardProgressMeters = destroyerTorpedoLaunchPosition_
            ? (destroyerTorpedo_->positionMeters.x - destroyerTorpedoLaunchPosition_->x) * destroyerTorpedoForwardSign_
            : 0.0F;
        const bool seekerEnabled = destroyerTorpedoLaunchPosition_.has_value() &&
            forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
        if (!seekerEnabled)
        {
            return Weapons::UpdateTorpedoSeekerMode(
                destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_,
                false, std::nullopt, std::nullopt, simulationTimeSeconds);
        }

        if (simulationTimeSeconds + 1.0e-9 >= nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_)
        {
            pendingDestroyerTorpedoSeekerEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .sourceLevelDb = playerSnapshot.emitter.continuousSourceLevelDb,
                .emissionTimeSeconds = simulationTimeSeconds});
            if (playerDecoy_)
            {
                const auto decoyEmission = Weapons::SampleAcousticDecoyEmission(
                    playerDecoyDefinition_, *playerDecoy_, simulationTimeSeconds);
                if (!decoyEmission)
                    return std::unexpected("M5-J4 player decoy emission snapshot failed: " + decoyEmission.error());
                if (decoyEmission->has_value())
                    pendingDestroyerTorpedoSeekerEmissions_.push_back(**decoyEmission);
            }
            nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver passiveReceiver{
            .sensorId = "M5_DESTROYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = destroyerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedPassiveObservation = false;
        auto emission = pendingDestroyerTorpedoSeekerEmissions_.begin();
        while (emission != pendingDestroyerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
                return std::unexpected("M5-J4 hostile seeker emission arrival time is non-finite");
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }
            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters, passiveReceiver.positionMeters, referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 hostile torpedo passive environment path failed: " + environment.error());
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
                return std::unexpected("M5-J4 hostile seeker acoustic propagation failed: " + observed.error().message);
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !destroyerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5-J4 hostile seeker perception integration failed");
                integratedPassiveObservation = true;
            }
            emission = pendingDestroyerTorpedoSeekerEmissions_.erase(emission);
        }
        if (!integratedPassiveObservation && !destroyerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5-J4 hostile passive seeker TrackManager failed to advance");

        bool integratedActiveObservation = false;
        if (destroyerTorpedoActivePulse_ && destroyerTorpedoActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = passiveReceiver;
            activeReceiver.sensorId = "M5_DESTROYER_TORPEDO_ACTIVE_SEEKER";
            activeReceiver.positionMeters = destroyerTorpedoActivePulse_->originMeters;
            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                destroyerTorpedoActivePulse_->originMeters, destroyerTorpedoActiveReflector_->positionMeters,
                referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 hostile torpedo active environment path failed: " + environment.error());
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *destroyerTorpedoActivePulse_, *destroyerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, *environment, *environment, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)
                return std::unexpected("M5 hostile torpedo active echo failed: " + activeEcho.error());
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, destroyerTorpedoActivePulse_->originMeters);
                if (!perceived || !destroyerTorpedoActiveSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5 hostile torpedo active echo failed perception integration");
                integratedActiveObservation = true;
                destroyerTorpedoActivePulse_.reset();
                destroyerTorpedoActiveReflector_.reset();
            }
            else if (simulationTimeSeconds + 1.0e-9 >= destroyerTorpedoActivePulseDeadlineSeconds_)
            {
                destroyerTorpedoActivePulse_.reset();
                destroyerTorpedoActiveReflector_.reset();
            }
        }
        if (!integratedActiveObservation && !destroyerTorpedoActiveSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5 hostile torpedo active seeker TrackManager failed to advance");

        const auto passiveCue = Weapons::SelectBestTorpedoSeekerCue(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerTracks_.Tracks());
        const auto activeCue = Weapons::SelectBestTorpedoSeekerCue(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoActiveSeekerTracks_.Tracks());
        if (!passiveCue || !activeCue)
            return std::unexpected("M5 hostile torpedo local seeker cue selection failed");
        auto decision = Weapons::UpdateTorpedoSeekerMode(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_,
            true, *passiveCue, *activeCue, simulationTimeSeconds);
        if (!decision)
            return decision;

        if (decision->requestActivePing && !destroyerTorpedoActivePulse_)
        {
            destroyerTorpedoActivePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = destroyerTorpedo_->positionMeters,
                .forwardUnitVector = {
                    .x = static_cast<float>(std::cos(static_cast<double>(destroyerTorpedo_->headingRadians))),
                    .y = static_cast<float>(std::sin(static_cast<double>(destroyerTorpedo_->headingRadians))),
                    .z = 0.0F},
                .sourceLevelDb = {.levelDb = {188.0F, 191.0F, 193.0F, 189.0F}},
                .beamHalfAngleRadians = M5CombatTorpedoActiveBeamHalfAngleRadians,
                .emissionTimeSeconds = simulationTimeSeconds};
            const auto outgoingPing = Acoustics::MakeActiveTransmissionEmission(*destroyerTorpedoActivePulse_);
            if (!outgoingPing)
                return std::unexpected("M5 hostile torpedo active transmission emission failed: " + outgoingPing.error());
            pendingIncomingThreatEmissions_.push_back(*outgoingPing);
            destroyerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            destroyerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
            const auto notified = Weapons::NotifyTorpedoSeekerActivePingEmitted(
                destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_, simulationTimeSeconds);
            if (!notified)
                return std::unexpected(notified.error());
        }
        return decision;
    }

    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat receiver/time input is invalid");
        }

        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater &&
            destroyerTorpedo_->positionMeters.IsFinite() &&
            simulationTimeSeconds + 1.0e-9 >= nextIncomingThreatEmissionSampleTimeSeconds_)
        {
            pendingIncomingThreatEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = destroyerTorpedo_->positionMeters,
                .sourceLevelDb = {.levelDb = {176.0F, 172.0F, 164.0F, 156.0F}},
                .emissionTimeSeconds = simulationTimeSeconds});
            nextIncomingThreatEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatIncomingThreatEmissionSampleIntervalSeconds;
        }

        bool integratedObservation = false;
        auto emission = pendingIncomingThreatEmissions_.begin();
        while (emission != pendingIncomingThreatEmissions_.end())
        {
            const double distanceMeters = Distance(
                emission->positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds + distanceMeters /
                static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
            {
                return std::unexpected("M5-J3 incoming-threat acoustic arrival time is invalid");
            }
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }

            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters,
                playerSnapshot.passiveReceiver.positionMeters,
                referenceSurfaceYMeters,
                0.0F);
            if (!environment)
            {
                return std::unexpected("M5-J3 incoming-threat environment path failed: " + environment.error());
            }
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, playerSnapshot.passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
            {
                return std::unexpected("M5-J3 incoming-threat acoustic propagation failed: " +
                                       observed.error().message);
            }
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !incomingThreatTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-J3 incoming-threat evidence failed perception integration");
                }
                integratedObservation = true;
            }
            emission = pendingIncomingThreatEmissions_.erase(emission);
        }

        if (!integratedObservation && !incomingThreatTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat TrackManager failed to advance");
        }
        return {};
    }

    void ApplyIncomingThreatPresentation(PlayerCombatPresentationSnapshot& snapshot) const noexcept
    {
        std::optional<Perception::Track> best{};
        for (const auto& track : incomingThreatTracks_.Tracks())
        {
            const bool present = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
                                 track.lifecycle == Perception::TrackLifecycleState::Coasting;
            if (!present)
            {
                continue;
            }
            if (!best || track.confidence > best->confidence ||
                (track.confidence == best->confidence && track.trackId < best->trackId))
            {
                best = track;
            }
        }
        if (!best)
        {
            return;
        }

        snapshot.incomingThreatDetected = true;
        snapshot.incomingThreatLifecycle = best->lifecycle;
        snapshot.incomingThreatBearingRadians = best->estimatedBearingRadians;
        snapshot.incomingThreatBearingUncertaintyRadians = best->bearingUncertaintyRadians;
        snapshot.incomingThreatConfidence = best->confidence;
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
    Perception::TrackManager playerTorpedoSeekerTracks_;
    Perception::TrackManager playerTorpedoActiveSeekerTracks_;
    Perception::TrackManager destroyerTorpedoSeekerTracks_;
    Perception::TrackManager destroyerTorpedoActiveSeekerTracks_;
    Perception::TrackManager incomingThreatTracks_;
    PeriscopeState periscopeState_{};
    PeriscopeOpticalConditions periscopeOpticalConditions_{};
    std::vector<Acoustics::AcousticEmission> pendingIncomingThreatEmissions_{};
    double nextIncomingThreatEmissionSampleTimeSeconds_ = 0.0;
    Weapons::TorpedoSeekerConfig playerTorpedoSeekerConfig_{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    Weapons::TorpedoSeekerRuntimeState playerTorpedoSeekerState_{};
    std::vector<Acoustics::AcousticEmission> pendingPlayerTorpedoSeekerEmissions_{};
    double nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;
    Weapons::TorpedoSeekerModeConfig playerTorpedoSeekerModeConfig_{
        .passiveSearchBeforeActiveSeconds = 1.5,
        .activePingIntervalSeconds = 1.0,
        .lostContactBeforeActiveSeconds = 0.35,
        .maximumSearchWithoutContactSeconds = 30.0,
        .preferPassiveCue = true};
    std::optional<Acoustics::ActiveAcousticPulse> playerTorpedoActivePulse_{};
    std::optional<Acoustics::AcousticReflector> playerTorpedoActiveReflector_{};
    double playerTorpedoActivePulseDeadlineSeconds_ = 0.0;
    Weapons::TorpedoSeekerConfig destroyerTorpedoSeekerConfig_{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    Weapons::TorpedoSeekerRuntimeState destroyerTorpedoSeekerState_{};
    std::vector<Acoustics::AcousticEmission> pendingDestroyerTorpedoSeekerEmissions_{};
    double nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;
    Weapons::TorpedoSeekerModeConfig destroyerTorpedoSeekerModeConfig_{
        .passiveSearchBeforeActiveSeconds = 1.5,
        .activePingIntervalSeconds = 1.0,
        .lostContactBeforeActiveSeconds = 0.35,
        .maximumSearchWithoutContactSeconds = 30.0,
        .preferPassiveCue = true};
    std::optional<Acoustics::ActiveAcousticPulse> destroyerTorpedoActivePulse_{};
    std::optional<Acoustics::AcousticReflector> destroyerTorpedoActiveReflector_{};
    double destroyerTorpedoActivePulseDeadlineSeconds_ = 0.0;
    SimpleDestroyerDefinition destroyerDefinition_;
    SimpleDestroyerRuntimeState destroyer_;
    SimpleCivilianVesselDefinition civilianDefinition_{};
    std::optional<SimpleCivilianVesselRuntimeState> civilian_{};
    Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition_;
    std::optional<Weapons::ConventionalTorpedoRuntimeState> destroyerTorpedo_{};
    std::optional<Physics::PhysicsVector3> destroyerTorpedoLaunchPosition_{};
    float destroyerTorpedoForwardSign_ = -1.0F;
    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
    Weapons::P700GranitDefinition playerP700Definition_;
    Armament::PlayerWeaponType selectedPlayerWeapon_ = Armament::PlayerWeaponType::HeavyweightTorpedo;
    Armament::AnteyTorpedoInventory playerTorpedoInventory_{};
    std::optional<Armament::P700CarrierLaunchContract> p700CarrierLaunchContract_{};
    std::optional<Armament::P700LauncherInventory> p700LauncherInventory_{};
    std::optional<Weapons::P700GranitRuntimeState> playerP700_{};
    std::vector<Weapons::P700GranitRuntimeState> playerP700Wingmen_{};
    std::optional<float> playerP700LaunchRangeMeters_{};
    std::optional<std::size_t> playerP700LaunchSlotIndex_{};
    std::vector<std::size_t> playerP700LaunchSlotIndices_{};
    std::uint64_t nextP700SalvoId_ = 1U;
    PlayerCombatCommandRuntime playerCombat_;
    Weapons::AcousticDecoyDefinition decoyDefinition_;
    std::optional<Weapons::AcousticDecoyRuntimeState> decoy_{};
    Weapons::AcousticDecoyDefinition playerDecoyDefinition_;
    std::optional<Weapons::AcousticDecoyRuntimeState> playerDecoy_{};
    bool playerDecoyAvailable_ = true;
    std::optional<PlayerCombatCommandFeedback> lastCombatCommand_{};
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
    std::optional<Acoustics::AcousticObservation> lastPlayerActiveEchoObservation_{};
    double nextActivePulseTimeSeconds_ = 0.0;
    std::optional<Acoustics::ActiveAcousticPulse> destroyerActivePulse_{};
    std::optional<Acoustics::AcousticReflector> destroyerActiveReflector_{};
    double nextDestroyerActivePulseTimeSeconds_ = 0.0;
    std::optional<DeepRun::Combat::CombatExplosionEvent> lastExplosion_{};
    double lastUpdateTimeSeconds_ = 0.0;
    bool p700AcceptanceMode_ = false;
    bool playerFogOfWarActive_ = false;
    bool civilianGameplayEnabled_ = false;
};
} // namespace DeepRun::Game::Combat
