#pragma once

#include "Game/Weapons/PlayerWeaponSelection.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"
#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"

#include <optional>

namespace DeepRun::Game::Armament
{
// Player torpedo kinematics remain explicit GAME POLICY so local combat stays readable. The authoritative
// maximum travel distance is not presentation tuning: it is bound to the same public maximum range used by
// the employment envelope. The runtime time limit is only a generous failsafe and must never cut that range short.
struct PlayerTorpedoProfile final
{
    PlayerWeaponType type = PlayerWeaponType::HeavyweightTorpedo;
    Weapons::ConventionalTorpedoDefinition definition{};
    const Weapons::WeaponEmploymentEnvelope* employmentEnvelope = nullptr;
};

inline constexpr float PlayerUset80GameplaySpeedMetersPerSecond = 55.0F;
inline constexpr float Player6576AFastGameplaySpeedMetersPerSecond = 60.0F;
inline constexpr float Player6576AEconomyGameplaySpeedMetersPerSecond = 35.0F;
inline constexpr double PlayerUset80MaximumRunTimeSeconds = 600.0;
inline constexpr double Player6576AFastMaximumRunTimeSeconds = 1'200.0;
inline constexpr double Player6576AEconomyMaximumRunTimeSeconds = 3'600.0;
inline constexpr float PlayerTorpedoMaximumTurnRateRadiansPerSecond = 0.45F;
inline constexpr float PlayerTorpedoMaximumVerticalCourseAngleRadians = 0.55F;

static_assert(PlayerUset80GameplaySpeedMetersPerSecond * PlayerUset80MaximumRunTimeSeconds >=
              Weapons::Uset80EmploymentEnvelope.maximumTargetRangeMeters);
static_assert(Player6576AFastGameplaySpeedMetersPerSecond * Player6576AFastMaximumRunTimeSeconds >=
              Weapons::Type6576AFastEmploymentEnvelope.maximumTargetRangeMeters);
static_assert(Player6576AEconomyGameplaySpeedMetersPerSecond * Player6576AEconomyMaximumRunTimeSeconds >=
              Weapons::Type6576AEconomyEmploymentEnvelope.maximumTargetRangeMeters);

[[nodiscard]] inline const Weapons::WeaponEmploymentEnvelope* PlayerTorpedoEmploymentEnvelope(
    const PlayerWeaponType type) noexcept
{
    switch (type)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return &Weapons::Uset80EmploymentEnvelope;
    case PlayerWeaponType::Type6576AFast: return &Weapons::Type6576AFastEmploymentEnvelope;
    case PlayerWeaponType::Type6576AEconomy: return &Weapons::Type6576AEconomyEmploymentEnvelope;
    case PlayerWeaponType::P700Granit: return nullptr;
    }
    return nullptr;
}

[[nodiscard]] inline std::optional<PlayerTorpedoProfile> MakePlayerTorpedoProfile(
    const PlayerWeaponType type)
{
    const Weapons::WeaponEmploymentEnvelope* envelope = PlayerTorpedoEmploymentEnvelope(type);
    if (envelope == nullptr)
    {
        return std::nullopt;
    }

    float gameplaySpeedMetersPerSecond = 0.0F;
    double maximumRunTimeSeconds = 0.0;
    const char* definitionId = nullptr;
    switch (type)
    {
    case PlayerWeaponType::HeavyweightTorpedo:
        gameplaySpeedMetersPerSecond = PlayerUset80GameplaySpeedMetersPerSecond;
        maximumRunTimeSeconds = PlayerUset80MaximumRunTimeSeconds;
        definitionId = "m5.live-player-uset80";
        break;
    case PlayerWeaponType::Type6576AFast:
        gameplaySpeedMetersPerSecond = Player6576AFastGameplaySpeedMetersPerSecond;
        maximumRunTimeSeconds = Player6576AFastMaximumRunTimeSeconds;
        definitionId = "m5.live-player-65-76a-fast";
        break;
    case PlayerWeaponType::Type6576AEconomy:
        gameplaySpeedMetersPerSecond = Player6576AEconomyGameplaySpeedMetersPerSecond;
        maximumRunTimeSeconds = Player6576AEconomyMaximumRunTimeSeconds;
        definitionId = "m5.live-player-65-76a-economy";
        break;
    case PlayerWeaponType::P700Granit:
        return std::nullopt;
    }

    return PlayerTorpedoProfile{
        .type = type,
        .definition = Weapons::ConventionalTorpedoDefinition{
            .weapon = Weapons::WeaponDefinition{
                .id = definitionId,
                .preparationSeconds = 1.0,
                .targeting = Weapons::WeaponTargetingRequirements{
                    .minimumTrackConfidence = 0.65F,
                    .maximumBearingUncertaintyRadians = 0.10F,
                    .maximumPositionUncertaintyMeters = 150.0F,
                    .requiresEstimatedPosition = true,
                    .allowCoastingTrack = false}},
            .underwaterSpeedMetersPerSecond = gameplaySpeedMetersPerSecond,
            .maximumTurnRateRadiansPerSecond = PlayerTorpedoMaximumTurnRateRadiansPerSecond,
            .maximumRunTimeSeconds = maximumRunTimeSeconds,
            .maximumTravelDistanceMeters = envelope->maximumTargetRangeMeters,
            .maximumVerticalCourseAngleRadians = PlayerTorpedoMaximumVerticalCourseAngleRadians,
            .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
            .directImpactDamage = 60.0F,
            .explosionRadiusMeters = 8.0F},
        .employmentEnvelope = envelope};
}
} // namespace DeepRun::Game::Armament
