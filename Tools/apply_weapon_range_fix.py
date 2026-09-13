from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    text = read(path)
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{path}: expected {expected} matches, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, found {count}: {pattern[:120]!r}")
    write(path, updated)


# -----------------------------------------------------------------------------
# Player weapon selector and canonical torpedo profiles.
# -----------------------------------------------------------------------------
write(
    "Game/Weapons/PlayerWeaponSelection.h",
    r'''#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace DeepRun::Game::Armament
{
enum class PlayerWeaponType
{
    HeavyweightTorpedo,
    Type6576AFast,
    Type6576AEconomy,
    P700Granit,
};

inline constexpr std::array<PlayerWeaponType, 4> PlayerWeaponCycle{
    PlayerWeaponType::HeavyweightTorpedo,
    PlayerWeaponType::Type6576AFast,
    PlayerWeaponType::Type6576AEconomy,
    PlayerWeaponType::P700Granit};

[[nodiscard]] constexpr bool IsPlayerTorpedo(const PlayerWeaponType weapon) noexcept
{
    return weapon == PlayerWeaponType::HeavyweightTorpedo ||
           weapon == PlayerWeaponType::Type6576AFast ||
           weapon == PlayerWeaponType::Type6576AEconomy;
}

[[nodiscard]] constexpr std::string_view PlayerWeaponName(const PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return "USET-80";
    case PlayerWeaponType::Type6576AFast: return "65-76A FAST";
    case PlayerWeaponType::Type6576AEconomy: return "65-76A ECONOMY";
    case PlayerWeaponType::P700Granit: return "P-700 GRANIT";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr PlayerWeaponType CyclePlayerWeapon(
    const PlayerWeaponType current,
    const int direction) noexcept
{
    std::size_t currentIndex = 0U;
    for (std::size_t index = 0; index < PlayerWeaponCycle.size(); ++index)
    {
        if (PlayerWeaponCycle[index] == current)
        {
            currentIndex = index;
            break;
        }
    }
    if (direction < 0)
    {
        return PlayerWeaponCycle[(currentIndex + PlayerWeaponCycle.size() - 1U) % PlayerWeaponCycle.size()];
    }
    return PlayerWeaponCycle[(currentIndex + 1U) % PlayerWeaponCycle.size()];
}
} // namespace DeepRun::Game::Armament
''')

write(
    "Game/Weapons/PlayerTorpedoProfiles.h",
    r'''#pragma once

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
''')

# -----------------------------------------------------------------------------
# Conventional torpedo: authoritative travelled-distance budget on both guidance paths.
# -----------------------------------------------------------------------------
replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """enum class ConventionalTorpedoTerminalReason\n{\n    None,\n    Impact,\n    EnduranceExpired,\n};""",
    """enum class ConventionalTorpedoTerminalReason\n{\n    None,\n    Impact,\n    EnduranceExpired,\n    RangeExpired,\n};""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """    // GAME POLICY only: finite propulsion/energy endurance prevents a missed weapon from pursuing forever.\n    // This is not an exact endurance/range claim for any real torpedo.\n    double maximumRunTimeSeconds = 300.0;\n\n    // Gameplay-authored 2.5D depth-course limit.""",
    """    // GAME POLICY only: finite propulsion/energy endurance prevents a missed weapon from pursuing forever.\n    // This is not an exact endurance/range claim for any real torpedo. Production weapon profiles must also\n    // author maximumTravelDistanceMeters from their canonical employment envelope so time tuning cannot shorten\n    // or silently extend real gameplay range.\n    double maximumRunTimeSeconds = 300.0;\n    float maximumTravelDistanceMeters = 100'000.0F;\n\n    // Gameplay-authored 2.5D depth-course limit.""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """    float headingRadians = 0.0F;\n    float speedMetersPerSecond = 0.0F;\n    double launchTimeSeconds = 0.0;""",
    """    float headingRadians = 0.0F;\n    float speedMetersPerSecond = 0.0F;\n    float travelledDistanceMeters = 0.0F;\n    double launchTimeSeconds = 0.0;""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """        definition.maximumTurnRateRadiansPerSecond > 3.1415927F ||\n        !std::isfinite(definition.maximumRunTimeSeconds) || definition.maximumRunTimeSeconds <= 0.0 ||\n        !std::isfinite(definition.maximumVerticalCourseAngleRadians) ||""",
    """        definition.maximumTurnRateRadiansPerSecond > 3.1415927F ||\n        !std::isfinite(definition.maximumRunTimeSeconds) || definition.maximumRunTimeSeconds <= 0.0 ||\n        !std::isfinite(definition.maximumTravelDistanceMeters) || definition.maximumTravelDistanceMeters <= 0.0F ||\n        !std::isfinite(definition.maximumVerticalCourseAngleRadians) ||""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """        .headingRadians = ClampConventionalTorpedoVerticalCourse(\n            launchHeadingRadians, definition.maximumVerticalCourseAngleRadians),\n        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,\n        .launchTimeSeconds = simulationTimeSeconds,""",
    """        .headingRadians = ClampConventionalTorpedoVerticalCourse(\n            launchHeadingRadians, definition.maximumVerticalCourseAngleRadians),\n        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,\n        .travelledDistanceMeters = 0.0F,\n        .launchTimeSeconds = simulationTimeSeconds,""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """[[nodiscard]] inline std::expected<void, std::string> UpdateConventionalTorpedoGuidance(\n""",
    """[[nodiscard]] inline float ConsumeConventionalTorpedoTravelBudget(\n    const ConventionalTorpedoDefinition& definition,\n    ConventionalTorpedoRuntimeState& state,\n    const float requestedDistanceMeters) noexcept\n{\n    if (!std::isfinite(requestedDistanceMeters) || requestedDistanceMeters <= 0.0F)\n    {\n        return 0.0F;\n    }\n    const float remainingDistanceMeters =\n        std::max(0.0F, definition.maximumTravelDistanceMeters - state.travelledDistanceMeters);\n    const float actualDistanceMeters = std::min(requestedDistanceMeters, remainingDistanceMeters);\n    state.travelledDistanceMeters += actualDistanceMeters;\n    if (state.travelledDistanceMeters + 1.0e-3F >= definition.maximumTravelDistanceMeters)\n    {\n        state.travelledDistanceMeters = definition.maximumTravelDistanceMeters;\n        state.speedMetersPerSecond = 0.0F;\n        state.movementDomain = MovementDomain::Spent;\n        state.terminalReason = ConventionalTorpedoTerminalReason::RangeExpired;\n    }\n    return actualDistanceMeters;\n}\n\n[[nodiscard]] inline std::expected<void, std::string> UpdateConventionalTorpedoGuidance(\n""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """        !state.positionMeters.IsFinite() || !std::isfinite(state.headingRadians) ||\n        !std::isfinite(state.speedMetersPerSecond) || state.speedMetersPerSecond <= 0.0F ||\n        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds ||""",
    """        !state.positionMeters.IsFinite() || !std::isfinite(state.headingRadians) ||\n        !std::isfinite(state.speedMetersPerSecond) || state.speedMetersPerSecond <= 0.0F ||\n        !std::isfinite(state.travelledDistanceMeters) || state.travelledDistanceMeters < 0.0F ||\n        state.travelledDistanceMeters > definition.maximumTravelDistanceMeters + 1.0e-3F ||\n        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds ||""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """    const float distanceMeters = state.speedMetersPerSecond * static_cast<float>(deltaSeconds);\n    state.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(state.headingRadians))) * distanceMeters;\n    state.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(state.headingRadians))) * distanceMeters;\n    state.lastUpdateTimeSeconds = simulationTimeSeconds;""",
    """    const float requestedDistanceMeters = state.speedMetersPerSecond * static_cast<float>(deltaSeconds);\n    if (!std::isfinite(requestedDistanceMeters))\n    {\n        return std::unexpected(\"conventional torpedo requested travel distance is non-finite\");\n    }\n    const float distanceMeters = ConsumeConventionalTorpedoTravelBudget(\n        definition, state, requestedDistanceMeters);\n    state.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(state.headingRadians))) * distanceMeters;\n    state.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(state.headingRadians))) * distanceMeters;\n    state.lastUpdateTimeSeconds = simulationTimeSeconds;""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """    const Physics::PhysicsVector3 startPosition = state.positionMeters;\n    ConventionalTorpedoRuntimeState candidate = state;""",
    """    const Physics::PhysicsVector3 startPosition = state.positionMeters;\n    const float startingTravelledDistanceMeters = state.travelledDistanceMeters;\n    ConventionalTorpedoRuntimeState candidate = state;""")

replace_once(
    "Simulation/Weapons/ConventionalTorpedo.h",
    """    const Physics::PhysicsSweepHit hit = **sweep;\n    candidate.positionMeters = hit.positionMeters;""",
    """    const Physics::PhysicsSweepHit hit = **sweep;\n    candidate.travelledDistanceMeters = startingTravelledDistanceMeters +\n        (candidate.travelledDistanceMeters - startingTravelledDistanceMeters) * hit.fraction;\n    candidate.positionMeters = hit.positionMeters;""")

# -----------------------------------------------------------------------------
# Seeker guidance must consume the exact same torpedo range budget.
# -----------------------------------------------------------------------------
replace_once(
    "Simulation/Weapons/TorpedoSeeker.h",
    """        !torpedo.positionMeters.IsFinite() || !std::isfinite(torpedo.headingRadians) ||\n        !std::isfinite(torpedo.speedMetersPerSecond) || torpedo.speedMetersPerSecond <= 0.0F ||\n        !IsValidTorpedoSeekerCue(seekerConfig, cue) ||""",
    """        !torpedo.positionMeters.IsFinite() || !std::isfinite(torpedo.headingRadians) ||\n        !std::isfinite(torpedo.speedMetersPerSecond) || torpedo.speedMetersPerSecond <= 0.0F ||\n        !std::isfinite(torpedo.travelledDistanceMeters) || torpedo.travelledDistanceMeters < 0.0F ||\n        torpedo.travelledDistanceMeters > definition.maximumTravelDistanceMeters + 1.0e-3F ||\n        !IsValidTorpedoSeekerCue(seekerConfig, cue) ||""")

replace_once(
    "Simulation/Weapons/TorpedoSeeker.h",
    """    const float distanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);\n    torpedo.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(torpedo.headingRadians))) * distanceMeters;\n    torpedo.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(torpedo.headingRadians))) * distanceMeters;\n    torpedo.lastUpdateTimeSeconds = simulationTimeSeconds;""",
    """    const float requestedDistanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);\n    if (!std::isfinite(requestedDistanceMeters))\n    {\n        return std::unexpected(\"torpedo seeker requested travel distance is non-finite\");\n    }\n    const float distanceMeters = ConsumeConventionalTorpedoTravelBudget(\n        definition, torpedo, requestedDistanceMeters);\n    torpedo.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(torpedo.headingRadians))) * distanceMeters;\n    torpedo.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(torpedo.headingRadians))) * distanceMeters;\n    torpedo.lastUpdateTimeSeconds = simulationTimeSeconds;""")

replace_once(
    "Simulation/Weapons/TorpedoSeeker.h",
    """    const Physics::PhysicsVector3 startPosition = torpedo.positionMeters;\n    ConventionalTorpedoRuntimeState candidate = torpedo;""",
    """    const Physics::PhysicsVector3 startPosition = torpedo.positionMeters;\n    const float startingTravelledDistanceMeters = torpedo.travelledDistanceMeters;\n    ConventionalTorpedoRuntimeState candidate = torpedo;""")

replace_once(
    "Simulation/Weapons/TorpedoSeeker.h",
    """    const Physics::PhysicsSweepHit hit = **sweep;\n    candidate.positionMeters = hit.positionMeters;""",
    """    const Physics::PhysicsSweepHit hit = **sweep;\n    candidate.travelledDistanceMeters = startingTravelledDistanceMeters +\n        (candidate.travelledDistanceMeters - startingTravelledDistanceMeters) * hit.fraction;\n    candidate.positionMeters = hit.positionMeters;""")

# -----------------------------------------------------------------------------
# P-700: canonical 550 km propulsion/travel budget across the whole lifecycle.
# -----------------------------------------------------------------------------
replace_once(
    "Simulation/Weapons/P700Granit.h",
    """enum class P700TerminalEngagementOutcome\n{\n    Unresolved,\n    HitPath,\n    SeekerLost,\n    SoftKill,\n    HardKill,\n    ManeuverMiss,\n};\n""",
    """enum class P700TerminalEngagementOutcome\n{\n    Unresolved,\n    HitPath,\n    SeekerLost,\n    SoftKill,\n    HardKill,\n    ManeuverMiss,\n};\n\nenum class P700LifecycleTerminalReason\n{\n    None,\n    Impact,\n    RangeExpired,\n};\n""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """    double deploymentSeconds = 1.50;\n    float terminalRangeMeters = 5'000.0F;\n    Physics::PhysicsVector3 collisionHalfExtentsMeters{5.0F, 0.70F, 0.70F};""",
    """    double deploymentSeconds = 1.50;\n    float terminalRangeMeters = 5'000.0F;\n    // Production default intentionally matches the canonical P-700 employment maximum. Tests may author a\n    // smaller budget to exercise expiry, but normal gameplay must not silently fly beyond 550 km after launch.\n    float maximumTravelDistanceMeters = P700GranitEmploymentEnvelope.maximumTargetRangeMeters;\n    Physics::PhysicsVector3 collisionHalfExtentsMeters{5.0F, 0.70F, 0.70F};""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """    float headingRadians = 0.0F;\n    float speedMetersPerSecond = 0.0F;\n    float hatchOpenProgress = 0.0F;""",
    """    float headingRadians = 0.0F;\n    float speedMetersPerSecond = 0.0F;\n    float travelledDistanceMeters = 0.0F;\n    float hatchOpenProgress = 0.0F;""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """    bool mainEngineActive = false;\n    P700TerminalEngagementOutcome terminalOutcome = P700TerminalEngagementOutcome::Unresolved;\n    std::uint64_t terminalRandomSeed = 0U;""",
    """    bool mainEngineActive = false;\n    P700TerminalEngagementOutcome terminalOutcome = P700TerminalEngagementOutcome::Unresolved;\n    P700LifecycleTerminalReason terminalReason = P700LifecycleTerminalReason::None;\n    std::uint64_t terminalRandomSeed = 0U;""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """        !std::isfinite(definition.deploymentSeconds) || definition.deploymentSeconds <= 0.0 ||\n        !std::isfinite(definition.terminalRangeMeters) || definition.terminalRangeMeters <= 0.0F ||\n        definition.terminalRangeMeters >= P700GranitEmploymentEnvelope.maximumTargetRangeMeters ||\n        !definition.collisionHalfExtentsMeters.IsFinite()""",
    """        !std::isfinite(definition.deploymentSeconds) || definition.deploymentSeconds <= 0.0 ||\n        !std::isfinite(definition.terminalRangeMeters) || definition.terminalRangeMeters <= 0.0F ||\n        definition.terminalRangeMeters >= P700GranitEmploymentEnvelope.maximumTargetRangeMeters ||\n        !std::isfinite(definition.maximumTravelDistanceMeters) || definition.maximumTravelDistanceMeters <= 0.0F ||\n        !definition.collisionHalfExtentsMeters.IsFinite()""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """    state.speedMetersPerSecond = 0.0F;\n    state.hatchOpenProgress = 0.0F;""",
    """    state.speedMetersPerSecond = 0.0F;\n    state.travelledDistanceMeters = 0.0F;\n    state.hatchOpenProgress = 0.0F;""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """    state.mainEngineActive = false;\n    state.terminalOutcome = P700TerminalEngagementOutcome::Unresolved;\n    state.terminalRandomSeed = P700SplitMix64(targetTrack.trackId ^ 0x503730304752414EULL);""",
    """    state.mainEngineActive = false;\n    state.terminalOutcome = P700TerminalEngagementOutcome::Unresolved;\n    state.terminalReason = P700LifecycleTerminalReason::None;\n    state.terminalRandomSeed = P700SplitMix64(targetTrack.trackId ^ 0x503730304752414EULL);""")

replace_once(
    "Simulation/Weapons/P700Granit.h",
    """        state.phase == P700GranitPhase::Spent || !state.positionMeters.IsFinite() ||\n        !state.launchForwardUnitVector.IsFinite() || !std::isfinite(state.surfaceLevelYMeters) ||\n        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||\n        !std::isfinite(state.hatchOpenProgress)""",
    """        state.phase == P700GranitPhase::Spent || !state.positionMeters.IsFinite() ||\n        !state.launchForwardUnitVector.IsFinite() || !std::isfinite(state.surfaceLevelYMeters) ||\n        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||\n        !std::isfinite(state.travelledDistanceMeters) || state.travelledDistanceMeters < 0.0F ||\n        state.travelledDistanceMeters > definition.maximumTravelDistanceMeters + 1.0e-3F ||\n        !std::isfinite(state.hatchOpenProgress)""")

regex_once(
    "Simulation/Weapons/P700Granit.h",
    r"    const auto moveSegment = \[&\]\(const Physics::PhysicsVector3& directionUnit,.*?\n    \};\n\n    while \(remainingSeconds > 1\.0e-9\)",
    r'''    const auto markRangeExpired = [&](const double terminalTimeSeconds)
    {
        state.travelledDistanceMeters = definition.maximumTravelDistanceMeters;
        state.speedMetersPerSecond = 0.0F;
        state.launchBoosterActive = false;
        state.mainEngineActive = false;
        state.phase = P700GranitPhase::Spent;
        state.terminalReason = P700LifecycleTerminalReason::RangeExpired;
        state.phaseStartTimeSeconds = terminalTimeSeconds;
        state.lastUpdateTimeSeconds = terminalTimeSeconds;
    };

    const auto moveSegment = [&](const Physics::PhysicsVector3& directionUnit,
                                 const float speedMetersPerSecond,
                                 const double deltaSeconds)
        -> std::expected<std::optional<P700GranitImpact>, std::string>
    {
        if (deltaSeconds <= 0.0)
        {
            return std::optional<P700GranitImpact>{};
        }
        const Physics::PhysicsVector3 start = state.positionMeters;
        const float requestedDistanceMeters = speedMetersPerSecond * static_cast<float>(deltaSeconds);
        if (!std::isfinite(requestedDistanceMeters) || requestedDistanceMeters < 0.0F)
        {
            return std::unexpected("P-700 requested travel distance is invalid");
        }
        const float remainingTravelMeters =
            std::max(0.0F, definition.maximumTravelDistanceMeters - state.travelledDistanceMeters);
        if (remainingTravelMeters <= 1.0e-3F)
        {
            markRangeExpired(cursorTime);
            return std::optional<P700GranitImpact>{};
        }
        const float distance = std::min(requestedDistanceMeters, remainingTravelMeters);
        const double actualDeltaSeconds = speedMetersPerSecond > 0.0F
            ? static_cast<double>(distance) / speedMetersPerSecond
            : 0.0;
        const Physics::PhysicsVector3 displacement{
            .x = directionUnit.x * distance,
            .y = directionUnit.y * distance,
            .z = directionUnit.z * distance};
        if (std::abs(displacement.x) <= 1.0e-9F && std::abs(displacement.y) <= 1.0e-9F &&
            std::abs(displacement.z) <= 1.0e-9F)
        {
            if (remainingTravelMeters <= requestedDistanceMeters + 1.0e-3F)
            {
                markRangeExpired(cursorTime + actualDeltaSeconds);
            }
            return std::optional<P700GranitImpact>{};
        }
        const float heading = static_cast<float>(std::atan2(
            static_cast<double>(directionUnit.y), static_cast<double>(directionUnit.x)));
        const auto sweep = physicsWorld.SweepBoxClosest(Physics::PhysicsBoxSweepQuery{
            .halfExtentsMeters = definition.collisionHalfExtentsMeters,
            .startPositionMeters = start,
            .orientation = P700HeadingQuaternion(heading),
            .displacementMeters = displacement,
            .ignoredBody = ignoredCarrierBody});
        if (!sweep)
        {
            return std::unexpected("P-700 physics sweep failed: " + sweep.error().message);
        }
        if (!*sweep)
        {
            state.positionMeters.x += displacement.x;
            state.positionMeters.y += displacement.y;
            state.positionMeters.z += displacement.z;
            state.travelledDistanceMeters += distance;
            state.headingRadians = heading;
            state.speedMetersPerSecond = speedMetersPerSecond;
            if (state.travelledDistanceMeters + 1.0e-3F >= definition.maximumTravelDistanceMeters)
            {
                markRangeExpired(cursorTime + actualDeltaSeconds);
            }
            return std::optional<P700GranitImpact>{};
        }

        const Physics::PhysicsSweepHit hit = **sweep;
        const double impactTimeSeconds = cursorTime + actualDeltaSeconds * static_cast<double>(hit.fraction);
        state.positionMeters = hit.positionMeters;
        state.travelledDistanceMeters += distance * hit.fraction;
        state.headingRadians = heading;
        state.speedMetersPerSecond = 0.0F;
        state.phase = P700GranitPhase::Impact;
        state.terminalReason = P700LifecycleTerminalReason::Impact;
        state.phaseStartTimeSeconds = impactTimeSeconds;
        state.lastUpdateTimeSeconds = impactTimeSeconds;
        state.impactedBody = hit.body;
        return std::optional<P700GranitImpact>{P700GranitImpact{
            .physicsHit = hit,
            .damage = Combat::CombatDamageEvent{
                .targetBody = hit.body,
                .positionMeters = hit.positionMeters,
                .damage = definition.directImpactDamage,
                .simulationTimeSeconds = impactTimeSeconds},
            .explosion = Combat::CombatExplosionEvent{
                .positionMeters = hit.positionMeters,
                .nominalDamage = definition.directImpactDamage,
                .radiusMeters = definition.explosionRadiusMeters,
                .simulationTimeSeconds = impactTimeSeconds}}};
    };

    while (remainingSeconds > 1.0e-9)''')

# Every moveSegment caller must stop immediately if the distance budget consumed the missile.
p700_text = read("Simulation/Weapons/P700Granit.h")
needle = "                if (*impact) return *impact;\n"
count = p700_text.count(needle)
if count < 4:
    raise RuntimeError(f"Simulation/Weapons/P700Granit.h: expected at least four moveSegment impact checks, found {count}")
p700_text = p700_text.replace(
    needle,
    "                if (*impact) return *impact;\n                if (state.phase == P700GranitPhase::Spent) return std::optional<P700GranitImpact>{};\n")
write("Simulation/Weapons/P700Granit.h", p700_text)

# -----------------------------------------------------------------------------
# Combat runtime: restore all torpedo choices and bind each to its own envelope/runtime profile.
# -----------------------------------------------------------------------------
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '#include "Game/Weapons/PlayerWeaponSelection.h"\n',
    '#include "Game/Weapons/PlayerWeaponSelection.h"\n#include "Game/Weapons/PlayerTorpedoProfiles.h"\n')

regex_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    r"        const Weapons::WeaponDefinition playerWeapon\{.*?        const Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition\{.*?            \.explosionRadiusMeters = 8\.0F\};",
    r'''        const auto playerTorpedoProfile = Armament::MakePlayerTorpedoProfile(
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
        }''')

replace_count(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "AssessPlayerUset80Employment",
    "AssessPlayerTorpedoEmployment",
    expected=3)

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """        const Weapons::WeaponDefinition definition = next == Armament::PlayerWeaponType::P700Granit\n            ? playerP700Definition_.weapon\n            : playerTorpedoDefinition_.weapon;\n        const auto reconfigured = playerCombat_.ReconfigureStoredWeapon(definition, simulationTimeSeconds);\n        if (!reconfigured)\n        {\n            return std::unexpected(\"M5 Weapon Selector profile switch failed: \" + reconfigured.error());\n        }\n        selectedPlayerWeapon_ = next;""",
    """        std::optional<Weapons::ConventionalTorpedoDefinition> nextTorpedoDefinition{};\n        Weapons::WeaponDefinition definition{};\n        if (next == Armament::PlayerWeaponType::P700Granit)\n        {\n            definition = playerP700Definition_.weapon;\n        }\n        else\n        {\n            const auto profile = Armament::MakePlayerTorpedoProfile(next);\n            if (!profile || profile->employmentEnvelope == nullptr)\n            {\n                return std::unexpected(\"M5 Weapon Selector could not resolve the selected torpedo profile\");\n            }\n            nextTorpedoDefinition = profile->definition;\n            definition = profile->definition.weapon;\n        }\n        const auto reconfigured = playerCombat_.ReconfigureStoredWeapon(definition, simulationTimeSeconds);\n        if (!reconfigured)\n        {\n            return std::unexpected(\"M5 Weapon Selector profile switch failed: \" + reconfigured.error());\n        }\n        if (nextTorpedoDefinition)\n        {\n            playerTorpedoDefinition_ = *nextTorpedoDefinition;\n        }\n        selectedPlayerWeapon_ = next;""")

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """    [[nodiscard]] std::expected<Weapons::WeaponEmploymentAssessment, std::string> AssessPlayerTorpedoEmployment(\n        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const\n    {\n        if (!currentPlayerPhysicalProxy_.has_value()""",
    """    [[nodiscard]] std::expected<Weapons::WeaponEmploymentAssessment, std::string> AssessPlayerTorpedoEmployment(\n        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const\n    {\n        const Weapons::WeaponEmploymentEnvelope* employmentEnvelope =\n            Armament::PlayerTorpedoEmploymentEnvelope(selectedPlayerWeapon_);\n        if (employmentEnvelope == nullptr)\n        {\n            return std::unexpected(\"selected player weapon is not a conventional torpedo profile\");\n        }\n        if (!currentPlayerPhysicalProxy_.has_value()""")

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """        return Weapons::EvaluateWeaponEmployment(\n            Weapons::Uset80EmploymentEnvelope,\n            Weapons::WeaponEmploymentContext{""",
    """        return Weapons::EvaluateWeaponEmployment(\n            *employmentEnvelope,\n            Weapons::WeaponEmploymentContext{""")

# -----------------------------------------------------------------------------
# Regression tests.
# -----------------------------------------------------------------------------
replace_once(
    "Tests/M5ConventionalTorpedoChecks.h",
    """    invalidDefinition = definition;\n    invalidDefinition.maximumVerticalCourseAngleRadians = 0.0F;""",
    """    invalidDefinition = definition;\n    invalidDefinition.maximumTravelDistanceMeters = 0.0F;\n    if (ValidateConventionalTorpedoDefinition(invalidDefinition))\n    {\n        return false;\n    }\n    invalidDefinition = definition;\n    invalidDefinition.maximumVerticalCourseAngleRadians = 0.0F;""")

replace_once(
    "Tests/M5ConventionalTorpedoChecks.h",
    """    if (!shortTorpedo || !UpdateConventionalTorpedoGuidance(\n            shortEnduranceDefinition, *shortTorpedo, std::nullopt, 1.1) ||\n        shortTorpedo->movementDomain != MovementDomain::Spent || shortTorpedo->speedMetersPerSecond != 0.0F ||\n        shortTorpedo->terminalReason != ConventionalTorpedoTerminalReason::EnduranceExpired ||\n        shortTorpedo->impactedBody.has_value())\n    {\n        return false;\n    }\n\n    return true;""",
    """    if (!shortTorpedo || !UpdateConventionalTorpedoGuidance(\n            shortEnduranceDefinition, *shortTorpedo, std::nullopt, 1.1) ||\n        shortTorpedo->movementDomain != MovementDomain::Spent || shortTorpedo->speedMetersPerSecond != 0.0F ||\n        shortTorpedo->terminalReason != ConventionalTorpedoTerminalReason::EnduranceExpired ||\n        shortTorpedo->impactedBody.has_value())\n    {\n        return false;\n    }\n\n    // Distance is authoritative independently of gameplay speed/time tuning. A step that would overshoot the\n    // budget is clamped exactly to the authored distance and terminates as RangeExpired without a fake impact.\n    auto shortRangeDefinition = definition;\n    shortRangeDefinition.maximumRunTimeSeconds = 100.0;\n    shortRangeDefinition.maximumTravelDistanceMeters = 30.0F;\n    auto shortRangeWeaponResult = CreateWeaponRuntime(shortRangeDefinition.weapon, 0.0);\n    if (!shortRangeWeaponResult)\n    {\n        return false;\n    }\n    auto shortRangeWeapon = *shortRangeWeaponResult;\n    if (!PrepareWeapon(shortRangeDefinition.weapon, shortRangeWeapon, 0.0) ||\n        !AssignWeaponTarget(shortRangeDefinition.weapon, shortRangeWeapon, initialTrack, 0.0) ||\n        !LaunchWeapon(shortRangeDefinition.weapon, shortRangeWeapon, 0.0))\n    {\n        return false;\n    }\n    auto shortRangeTorpedo = CreateLaunchedConventionalTorpedo(\n        shortRangeDefinition, shortRangeWeapon, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.0F, initialTrack, 0.0);\n    if (!shortRangeTorpedo || !UpdateConventionalTorpedoGuidance(\n            shortRangeDefinition, *shortRangeTorpedo, std::nullopt, 2.0) ||\n        shortRangeTorpedo->movementDomain != MovementDomain::Spent ||\n        shortRangeTorpedo->terminalReason != ConventionalTorpedoTerminalReason::RangeExpired ||\n        std::abs(shortRangeTorpedo->travelledDistanceMeters - 30.0F) > 0.001F ||\n        std::abs(shortRangeTorpedo->positionMeters.x - 30.0F) > 0.01F ||\n        shortRangeTorpedo->impactedBody.has_value())\n    {\n        return false;\n    }\n\n    return true;""")

replace_once(
    "Tests/M5AcousticDecoyChecks.h",
    """    const ConventionalTorpedoDefinition torpedoDefinition{\n        .weapon = weaponDefinition,\n        .underwaterSpeedMetersPerSecond = 20.0F,\n        .maximumTurnRateRadiansPerSecond = 0.25F};""",
    """    const ConventionalTorpedoDefinition torpedoDefinition{\n        .weapon = weaponDefinition,\n        .underwaterSpeedMetersPerSecond = 20.0F,\n        .maximumTurnRateRadiansPerSecond = 0.25F,\n        .maximumTravelDistanceMeters = 25.0F};""")

replace_once(
    "Tests/M5AcousticDecoyChecks.h",
    """    if (!AdvanceConventionalTorpedoWithSeekerCue(torpedoDefinition, seekerConfig, torpedo, **cueResult, 1.0) ||\n        !originalLaunchTrackId || torpedo.guidanceTrackId != originalLaunchTrackId ||\n        seekerState.selectedTrackId == originalLaunchTrackId ||\n        std::abs(torpedo.headingRadians - 0.25F) > 0.001F || torpedo.positionMeters.y <= 0.0F)\n    {\n        return false;\n    }\n\n    if (!AdvanceAcousticDecoy""",
    """    if (!AdvanceConventionalTorpedoWithSeekerCue(torpedoDefinition, seekerConfig, torpedo, **cueResult, 1.0) ||\n        !originalLaunchTrackId || torpedo.guidanceTrackId != originalLaunchTrackId ||\n        seekerState.selectedTrackId == originalLaunchTrackId ||\n        std::abs(torpedo.headingRadians - 0.25F) > 0.001F || torpedo.positionMeters.y <= 0.0F ||\n        std::abs(torpedo.travelledDistanceMeters - 20.0F) > 0.001F)\n    {\n        return false;\n    }\n    if (!AdvanceConventionalTorpedoWithSeekerCue(torpedoDefinition, seekerConfig, torpedo, **cueResult, 2.0) ||\n        torpedo.movementDomain != MovementDomain::Spent ||\n        torpedo.terminalReason != ConventionalTorpedoTerminalReason::RangeExpired ||\n        std::abs(torpedo.travelledDistanceMeters - 25.0F) > 0.001F)\n    {\n        return false;\n    }\n\n    if (!AdvanceAcousticDecoy""")

replace_once(
    "Tests/P700LauncherInventoryTest.cpp",
    '#include "Game/Weapons/PlayerWeaponSelection.h"\n',
    '#include "Game/Weapons/PlayerWeaponSelection.h"\n#include "Game/Weapons/PlayerTorpedoProfiles.h"\n')

regex_once(
    "Tests/P700LauncherInventoryTest.cpp",
    r"\[\[nodiscard\]\] bool RunWeaponSelectorChecks\(\)\n\{.*?\n\}\n\n\[\[nodiscard\]\] bool RunWorldGeometryChecks",
    r'''[[nodiscard]] bool RunWeaponSelectorChecks()
{
    using namespace DeepRun::Game::Armament;
    using namespace DeepRun::Weapons;
    const auto uset = MakePlayerTorpedoProfile(PlayerWeaponType::HeavyweightTorpedo);
    const auto fast = MakePlayerTorpedoProfile(PlayerWeaponType::Type6576AFast);
    const auto economy = MakePlayerTorpedoProfile(PlayerWeaponType::Type6576AEconomy);
    if (PlayerWeaponName(PlayerWeaponType::HeavyweightTorpedo) != "USET-80" ||
        PlayerWeaponName(PlayerWeaponType::Type6576AFast) != "65-76A FAST" ||
        PlayerWeaponName(PlayerWeaponType::Type6576AEconomy) != "65-76A ECONOMY" ||
        PlayerWeaponName(PlayerWeaponType::P700Granit) != "P-700 GRANIT" ||
        CyclePlayerWeapon(PlayerWeaponType::HeavyweightTorpedo, 1) != PlayerWeaponType::Type6576AFast ||
        CyclePlayerWeapon(PlayerWeaponType::Type6576AFast, 1) != PlayerWeaponType::Type6576AEconomy ||
        CyclePlayerWeapon(PlayerWeaponType::Type6576AEconomy, 1) != PlayerWeaponType::P700Granit ||
        CyclePlayerWeapon(PlayerWeaponType::P700Granit, 1) != PlayerWeaponType::HeavyweightTorpedo ||
        CyclePlayerWeapon(PlayerWeaponType::HeavyweightTorpedo, -1) != PlayerWeaponType::P700Granit ||
        !uset || !fast || !economy || MakePlayerTorpedoProfile(PlayerWeaponType::P700Granit) ||
        uset->employmentEnvelope != &Uset80EmploymentEnvelope ||
        fast->employmentEnvelope != &Type6576AFastEmploymentEnvelope ||
        economy->employmentEnvelope != &Type6576AEconomyEmploymentEnvelope ||
        uset->definition.maximumTravelDistanceMeters != 18'000.0F ||
        fast->definition.maximumTravelDistanceMeters != 50'000.0F ||
        economy->definition.maximumTravelDistanceMeters != 100'000.0F)
    {
        std::cerr << "M5 four-item Weapon Selector/profile range contract is invalid\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunWorldGeometryChecks''')

replace_once(
    "Tests/M5P700Checks.h",
    """    const P700GranitDefinition definition = MakeDefinition();\n    if (!ValidateP700GranitDefinition(definition))\n    {\n        return fail(\"definition validation\");\n    }\n""",
    """    const P700GranitDefinition definition = MakeDefinition();\n    if (!ValidateP700GranitDefinition(definition) ||\n        definition.maximumTravelDistanceMeters != P700GranitEmploymentEnvelope.maximumTargetRangeMeters)\n    {\n        return fail(\"definition validation / canonical 550 km runtime budget\");\n    }\n    auto invalidRangeDefinition = definition;\n    invalidRangeDefinition.maximumTravelDistanceMeters = 0.0F;\n    if (ValidateP700GranitDefinition(invalidRangeDefinition))\n    {\n        return fail(\"zero P-700 travel budget validation\");\n    }\n""")

replace_once(
    "Tests/M5P700Checks.h",
    """    const Perception::Track targetTrack = MakeTrack(7001U, 25'000.0F);\n    const Perception::Track tooCloseTrack = MakeTrack(7002U, 19'000.0F);\n""",
    """    const Perception::Track targetTrack = MakeTrack(7001U, 25'000.0F);\n    const Perception::Track tooCloseTrack = MakeTrack(7002U, 19'000.0F);\n    const Perception::Track tooFarTrack = MakeTrack(7003U, 551'000.0F);\n\n    auto tooFarRuntime = CreateP700GranitRuntime(definition, 0.0);\n    if (!tooFarRuntime)\n    {\n        return fail(\"too-far fixture runtime creation\");\n    }\n    const auto tooFarLaunch = LaunchP700Granit(\n        definition, *tooFarRuntime, tooFarTrack, SubmergedCarrier(), 0.0);\n    if (!tooFarLaunch || tooFarLaunch->allowed ||\n        tooFarLaunch->reason.find(\"maximum range\") == std::string::npos ||\n        tooFarRuntime->phase != P700GranitPhase::Stored)\n    {\n        return fail(\"550 km maximum-range employment gate\");\n    }\n""")

# Insert a short-budget P-700 regression immediately after fixture bodies are created.
replace_once(
    "Tests/M5P700Checks.h",
    """    if (!carrierBody.IsValid() || !targetBody.IsValid())\n    {\n        return fail(\"P-700 carrier/target fixture creation\");\n    }\n\n    auto runtimeResult = CreateP700GranitRuntime(definition, 0.0);""",
    """    if (!carrierBody.IsValid() || !targetBody.IsValid())\n    {\n        return fail(\"P-700 carrier/target fixture creation\");\n    }\n\n    auto shortRangeDefinition = definition;\n    shortRangeDefinition.maximumTravelDistanceMeters = 500.0F;\n    auto shortRangeRuntime = CreateP700GranitRuntime(shortRangeDefinition, 0.0);\n    if (!shortRangeRuntime)\n    {\n        return fail(\"short-range P-700 fixture creation\");\n    }\n    const auto shortRangeLaunch = LaunchP700Granit(\n        shortRangeDefinition, *shortRangeRuntime, targetTrack, SubmergedCarrier(), 0.0);\n    if (!shortRangeLaunch || !shortRangeLaunch->allowed)\n    {\n        return fail(\"short-range P-700 legal launch\");\n    }\n    const auto shortRangeAdvance = AdvanceP700GranitWithCollision(\n        shortRangeDefinition, *shortRangeRuntime, targetTrack, physicsWorld, 10.0, carrierBody);\n    if (!shortRangeAdvance || shortRangeAdvance->has_value() ||\n        shortRangeRuntime->phase != P700GranitPhase::Spent ||\n        shortRangeRuntime->terminalReason != P700LifecycleTerminalReason::RangeExpired ||\n        std::abs(shortRangeRuntime->travelledDistanceMeters - 500.0F) > 0.01F ||\n        shortRangeRuntime->impactedBody.has_value())\n    {\n        return fail(\"P-700 runtime travel budget expiry\");\n    }\n\n    auto runtimeResult = CreateP700GranitRuntime(definition, 0.0);""")

# -----------------------------------------------------------------------------
# Documentation: distinguish launch envelope from in-flight range authority.
# -----------------------------------------------------------------------------
replace_once(
    "docs/design/weapon-employment-envelopes.md",
    """Implementation status: the live M5 player USET-80 fire path is gated **before** the `WeaponRuntime` launch transition, and `canFireWeapon` in the HUD is masked by the same assessment. IG2 P-700 and any future selectable 65-76A profile must reuse this common evaluator before committing their own launch/hatch lifecycle. Direct-runtime regression fixtures bind an authoritative `PhysicsWorld` ownship proxy so tests exercise the same employment gate as windowed play.""",
    """Implementation status: USET-80, 65-76A fast/economy and P-700 all use the common employment evaluator before launch, and `canFireWeapon` in the HUD is masked by the same assessment. The three torpedo choices are selectable runtime profiles, each with an authoritative travelled-distance budget equal to its canonical maximum range (18/50/100 km); their time limits are only larger failsafes. P-700 likewise carries a 550 km in-flight travel budget, so a legal launch cannot chase a moving Track indefinitely beyond the canonical range. Direct-runtime regression fixtures bind an authoritative `PhysicsWorld` ownship proxy so tests exercise the same employment gate as windowed play.""")

print("weapon runtime range/profile patch applied")
