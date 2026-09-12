#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Combat/CombatIntegrity.h"
#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"
#include "Simulation/Weapons/WeaponRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Weapons
{
enum class P700GranitPhase
{
    Stored,
    HatchOpening,
    UnderwaterLaunch,
    WaterExit,
    PostExitTransition,
    AirborneDeploying,
    Cruise,
    Terminal,
    Defeated,
    Impact,
    Spent,
};

enum class P700TerminalEngagementOutcome
{
    Unresolved,
    HitPath,
    SeekerLost,
    SoftKill,
    HardKill,
    ManeuverMiss,
};

// Explicit GAME POLICY for terminal effectiveness. These probabilities model independent terminal failure
// opportunities after a legal Track-based launch; they are not claims about classified P-700 seeker/EW/PВО data.
struct P700TerminalDefenseProfile final
{
    float seekerFailureProbability = 0.04F;
    float softKillProbability = 0.10F;
    float hardKillProbability = 0.16F;
    float maneuverDefeatProbability = 0.04F;
};

// Bounded M5 P-700 flight tuning. Employment limits remain the separately documented canonical envelope.
// Speeds, transition durations, turn rate, terminal threshold and coarse payload/collision values are GAME POLICY,
// not claims about classified Project 949A/P-700 flight-control data.
struct P700GranitDefinition final
{
    WeaponDefinition weapon{};
    float underwaterExitSpeedMetersPerSecond = 50.0F;
    float waterExitSpeedMetersPerSecond = 100.0F;
    float deploymentFlightSpeedMetersPerSecond = 180.0F;
    // M5 gameplay flight policy: ~Mach 2 class cruise/terminal pacing without claiming a historical exact profile.
    float cruiseSpeedMetersPerSecond = 680.0F;
    float terminalSpeedMetersPerSecond = 750.0F;
    float maximumAirborneTurnRateRadiansPerSecond = 0.35F;
    double launcherHatchOpeningSeconds = 0.75;
    double waterExitTransitionSeconds = 0.50;
    double postExitTransitionSeconds = 0.60;
    double deploymentSeconds = 1.50;
    float terminalRangeMeters = 5'000.0F;
    Physics::PhysicsVector3 collisionHalfExtentsMeters{5.0F, 0.70F, 0.70F};
    float directImpactDamage = 100.0F;
    float explosionRadiusMeters = 30.0F;
};

struct P700CarrierLaunchContext final
{
    Physics::PhysicsVector3 launchPositionMeters{};
    Physics::PhysicsVector3 launchForwardUnitVector{1.0F, 0.0F, 0.0F};
    float surfaceLevelYMeters = 0.0F;
    float launchDepthMeters = 0.0F;
    float carrierSpeedMetersPerSecond = 0.0F;
    float carrierHeadingRadians = 0.0F;
};

struct P700GranitRuntimeState final
{
    std::string definitionId{};
    P700GranitPhase phase = P700GranitPhase::Stored;
    Physics::PhysicsVector3 positionMeters{};
    Physics::PhysicsVector3 launchForwardUnitVector{1.0F, 0.0F, 0.0F};
    float surfaceLevelYMeters = 0.0F;
    float headingRadians = 0.0F;
    float speedMetersPerSecond = 0.0F;
    float hatchOpenProgress = 0.0F;
    float postExitTransitionProgress = 0.0F;
    float deploymentProgress = 0.0F;
    bool launchBoosterActive = false;
    bool launchBoosterAttached = true;
    bool noseProtectionCapAttached = true;
    bool mainEngineActive = false;
    P700TerminalEngagementOutcome terminalOutcome = P700TerminalEngagementOutcome::Unresolved;
    std::uint64_t terminalRandomSeed = 0U;
    double phaseStartTimeSeconds = 0.0;
    double lastUpdateTimeSeconds = 0.0;
    std::optional<std::uint64_t> guidanceTrackId{};
    std::optional<Physics::PhysicsVector3> perceivedAimPointMeters{};
    std::optional<float> perceivedPositionUncertaintyMeters{};
    std::optional<Physics::PhysicsBodyHandle> impactedBody{};
};

struct P700GranitImpact final
{
    Physics::PhysicsSweepHit physicsHit{};
    Combat::CombatDamageEvent damage{};
    Combat::CombatExplosionEvent explosion{};
};

[[nodiscard]] inline float WrapP700Heading(const float radians) noexcept
{
    return std::remainder(radians, 6.28318530717958647692F);
}

[[nodiscard]] inline std::optional<Physics::PhysicsVector3> NormalizeP700Vector(
    const Physics::PhysicsVector3& value) noexcept
{
    if (!value.IsFinite())
    {
        return std::nullopt;
    }
    const double length = std::sqrt(
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
    if (!std::isfinite(length) || length <= 1.0e-8)
    {
        return std::nullopt;
    }
    const float inverse = static_cast<float>(1.0 / length);
    return Physics::PhysicsVector3{.x = value.x * inverse, .y = value.y * inverse, .z = value.z * inverse};
}

[[nodiscard]] inline float P700DistanceMeters(
    const Physics::PhysicsVector3& first,
    const Physics::PhysicsVector3& second) noexcept
{
    const double dx = static_cast<double>(second.x) - first.x;
    const double dy = static_cast<double>(second.y) - first.y;
    const double dz = static_cast<double>(second.z) - first.z;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

[[nodiscard]] inline Physics::PhysicsQuaternion P700HeadingQuaternion(const float headingRadians) noexcept
{
    const float half = 0.5F * headingRadians;
    return Physics::PhysicsQuaternion{
        .x = 0.0F,
        .y = 0.0F,
        .z = static_cast<float>(std::sin(static_cast<double>(half))),
        .w = static_cast<float>(std::cos(static_cast<double>(half)))};
}

[[nodiscard]] inline bool ValidateP700TerminalDefenseProfile(const P700TerminalDefenseProfile& profile) noexcept
{
    const auto validProbability = [](const float value) noexcept {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    return validProbability(profile.seekerFailureProbability) && validProbability(profile.softKillProbability) &&
           validProbability(profile.hardKillProbability) && validProbability(profile.maneuverDefeatProbability);
}

[[nodiscard]] inline std::uint64_t P700SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] inline float P700UnitRandom(const std::uint64_t seed, const std::uint64_t stream) noexcept
{
    const std::uint64_t bits = P700SplitMix64(seed ^ (stream * 0xD1B54A32D192ED03ULL));
    return static_cast<float>((bits >> 40U) * (1.0 / 16777216.0));
}

[[nodiscard]] inline P700TerminalEngagementOutcome ResolveP700TerminalEngagement(
    const P700TerminalDefenseProfile& defense,
    const std::uint64_t seed,
    const float perceivedPositionUncertaintyMeters) noexcept
{
    if (!ValidateP700TerminalDefenseProfile(defense) || !std::isfinite(perceivedPositionUncertaintyMeters) ||
        perceivedPositionUncertaintyMeters < 0.0F)
    {
        return P700TerminalEngagementOutcome::SeekerLost;
    }
    // Uncertainty is already bounded by WeaponTargetingRequirements at launch/update. It contributes up to
    // another 20 percentage points of terminal seeker failure rather than becoming a hidden distance shortcut.
    const float uncertaintyPenalty = std::clamp(perceivedPositionUncertaintyMeters / 2'500.0F, 0.0F, 0.20F);
    if (P700UnitRandom(seed, 1U) < std::clamp(defense.seekerFailureProbability + uncertaintyPenalty, 0.0F, 1.0F))
        return P700TerminalEngagementOutcome::SeekerLost;
    if (P700UnitRandom(seed, 2U) < defense.softKillProbability)
        return P700TerminalEngagementOutcome::SoftKill;
    if (P700UnitRandom(seed, 3U) < defense.hardKillProbability)
        return P700TerminalEngagementOutcome::HardKill;
    if (P700UnitRandom(seed, 4U) < defense.maneuverDefeatProbability)
        return P700TerminalEngagementOutcome::ManeuverMiss;
    return P700TerminalEngagementOutcome::HitPath;
}

[[nodiscard]] inline std::expected<void, std::string> ValidateP700GranitDefinition(
    const P700GranitDefinition& definition)
{
    const auto weaponValid = ValidateWeaponDefinition(definition.weapon);
    if (!weaponValid)
    {
        return std::unexpected(weaponValid.error());
    }
    if (!std::isfinite(definition.underwaterExitSpeedMetersPerSecond) || definition.underwaterExitSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.waterExitSpeedMetersPerSecond) || definition.waterExitSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.deploymentFlightSpeedMetersPerSecond) || definition.deploymentFlightSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.cruiseSpeedMetersPerSecond) || definition.cruiseSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.terminalSpeedMetersPerSecond) || definition.terminalSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.maximumAirborneTurnRateRadiansPerSecond) ||
        definition.maximumAirborneTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumAirborneTurnRateRadiansPerSecond > 3.14159265358979323846F ||
        !std::isfinite(definition.launcherHatchOpeningSeconds) || definition.launcherHatchOpeningSeconds <= 0.0 ||
        !std::isfinite(definition.waterExitTransitionSeconds) || definition.waterExitTransitionSeconds <= 0.0 ||
        !std::isfinite(definition.postExitTransitionSeconds) || definition.postExitTransitionSeconds <= 0.0 ||
        !std::isfinite(definition.deploymentSeconds) || definition.deploymentSeconds <= 0.0 ||
        !std::isfinite(definition.terminalRangeMeters) || definition.terminalRangeMeters <= 0.0F ||
        definition.terminalRangeMeters >= P700GranitEmploymentEnvelope.maximumTargetRangeMeters ||
        !definition.collisionHalfExtentsMeters.IsFinite() || definition.collisionHalfExtentsMeters.x <= 0.0F ||
        definition.collisionHalfExtentsMeters.y <= 0.0F || definition.collisionHalfExtentsMeters.z <= 0.0F ||
        !std::isfinite(definition.directImpactDamage) || definition.directImpactDamage <= 0.0F ||
        !std::isfinite(definition.explosionRadiusMeters) || definition.explosionRadiusMeters <= 0.0F)
    {
        return std::unexpected("P-700 runtime definition is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<P700GranitRuntimeState, std::string> CreateP700GranitRuntime(
    const P700GranitDefinition& definition,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateP700GranitDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("P-700 runtime creation time is invalid");
    }
    return P700GranitRuntimeState{
        .definitionId = definition.weapon.id,
        .phase = P700GranitPhase::Stored,
        .phaseStartTimeSeconds = simulationTimeSeconds,
        .lastUpdateTimeSeconds = simulationTimeSeconds};
}

// Launch authority consumes a perceived Track plus own-carrier/production-launcher state only. The employment
// context is constructed here from that Track; no hostile body handle/Transform can authorize or seed guidance.
[[nodiscard]] inline std::expected<WeaponEmploymentAssessment, std::string> LaunchP700Granit(
    const P700GranitDefinition& definition,
    P700GranitRuntimeState& state,
    const Perception::Track& targetTrack,
    const P700CarrierLaunchContext& carrier,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateP700GranitDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (state.definitionId != definition.weapon.id || state.phase != P700GranitPhase::Stored ||
        state.guidanceTrackId.has_value() || state.impactedBody.has_value() ||
        !carrier.launchPositionMeters.IsFinite() || !carrier.launchForwardUnitVector.IsFinite() ||
        !std::isfinite(carrier.surfaceLevelYMeters) || !std::isfinite(carrier.launchDepthMeters) ||
        !std::isfinite(carrier.carrierSpeedMetersPerSecond) || !std::isfinite(carrier.carrierHeadingRadians) ||
        carrier.launchDepthMeters < 0.0F || carrier.carrierSpeedMetersPerSecond < 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("P-700 launch state/carrier input is invalid, non-stored, or time-reversing");
    }
    const auto targetValid = ValidateTrackForWeapon(definition.weapon, targetTrack);
    if (!targetValid)
    {
        return std::unexpected("P-700 perceived target is not qualified: " + targetValid.error());
    }
    if (!targetTrack.estimatedPositionMeters || !targetTrack.positionUncertaintyMeters)
    {
        return std::unexpected("P-700 launch requires a perceived spatial target solution with uncertainty");
    }
    const auto launchForward = NormalizeP700Vector(carrier.launchForwardUnitVector);
    if (!launchForward || launchForward->y <= 0.0F)
    {
        return std::unexpected("P-700 production launch direction must be finite and point toward the surface");
    }

    const float perceivedTargetDepthMeters = std::max(
        0.0F, carrier.surfaceLevelYMeters - targetTrack.estimatedPositionMeters->y);
    const WeaponEmploymentAssessment employment = EvaluateWeaponEmployment(
        P700GranitEmploymentEnvelope,
        WeaponEmploymentContext{
            .launchPositionMeters = carrier.launchPositionMeters,
            .perceivedTargetPositionMeters = *targetTrack.estimatedPositionMeters,
            .launchDepthMeters = carrier.launchDepthMeters,
            .perceivedTargetDepthMeters = perceivedTargetDepthMeters,
            .carrierSpeedMetersPerSecond = carrier.carrierSpeedMetersPerSecond,
            .launcherHeadingRadians = carrier.carrierHeadingRadians});
    if (!employment.allowed)
    {
        return employment;
    }

    // Fire commits the weapon to the launch sequence, but the missile remains physically in its canister until
    // the selected paired hatch is fully open. Presentation consumes hatchOpenProgress from this same state.
    state.phase = P700GranitPhase::HatchOpening;
    state.positionMeters = carrier.launchPositionMeters;
    state.launchForwardUnitVector = *launchForward;
    state.surfaceLevelYMeters = carrier.surfaceLevelYMeters;
    state.headingRadians = static_cast<float>(std::atan2(
        static_cast<double>(launchForward->y), static_cast<double>(launchForward->x)));
    state.speedMetersPerSecond = 0.0F;
    state.hatchOpenProgress = 0.0F;
    state.postExitTransitionProgress = 0.0F;
    state.deploymentProgress = 0.0F;
    state.launchBoosterActive = false;
    state.launchBoosterAttached = true;
    state.noseProtectionCapAttached = true;
    state.mainEngineActive = false;
    state.terminalOutcome = P700TerminalEngagementOutcome::Unresolved;
    state.terminalRandomSeed = P700SplitMix64(targetTrack.trackId ^ 0x503730304752414EULL);
    state.phaseStartTimeSeconds = simulationTimeSeconds;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.guidanceTrackId = targetTrack.trackId;
    state.perceivedAimPointMeters = targetTrack.estimatedPositionMeters;
    state.perceivedPositionUncertaintyMeters = targetTrack.positionUncertaintyMeters;
    return employment;
}

[[nodiscard]] inline std::expected<void, std::string> UpdateP700PerceivedGuidance(
    const P700GranitDefinition& definition,
    P700GranitRuntimeState& state,
    const std::optional<Perception::Track>& perceivedTrack)
{
    if (!perceivedTrack)
    {
        return {};
    }
    if (!state.guidanceTrackId || perceivedTrack->trackId != *state.guidanceTrackId)
    {
        return std::unexpected("P-700 guidance update does not match its perceived launch track");
    }
    const auto targetValid = ValidateTrackForWeapon(definition.weapon, *perceivedTrack);
    if (targetValid)
    {
        state.perceivedAimPointMeters = perceivedTrack->estimatedPositionMeters;
        state.perceivedPositionUncertaintyMeters = perceivedTrack->positionUncertaintyMeters;
    }
    // A degraded same-ID Track cannot erase the last accepted aim point; it simply contributes no new guidance.
    return {};
}

// Advances the explicit launch/flight lifecycle. Deployment remains exactly zero through underwater launch and
// WaterExit, starts only in AirborneDeploying, and reaches one before Cruise. Flight guidance uses only the last
// qualified perceived aim point. A PhysicsWorld sweep is the sole authority that can create Impact/body identity.
[[nodiscard]] inline std::expected<std::optional<P700GranitImpact>, std::string> AdvanceP700GranitWithCollision(
    const P700GranitDefinition& definition,
    P700GranitRuntimeState& state,
    const std::optional<Perception::Track>& perceivedTrack,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredCarrierBody = {},
    const std::optional<P700TerminalDefenseProfile>& targetDefense = std::nullopt)
{
    const auto definitionValid = ValidateP700GranitDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (state.definitionId != definition.weapon.id || state.phase == P700GranitPhase::Stored ||
        state.phase == P700GranitPhase::Spent || !state.positionMeters.IsFinite() ||
        !state.launchForwardUnitVector.IsFinite() || !std::isfinite(state.surfaceLevelYMeters) ||
        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||
        !std::isfinite(state.hatchOpenProgress) || state.hatchOpenProgress < 0.0F || state.hatchOpenProgress > 1.0F ||
        !std::isfinite(state.postExitTransitionProgress) || state.postExitTransitionProgress < 0.0F ||
        state.postExitTransitionProgress > 1.0F || !std::isfinite(state.deploymentProgress) ||
        state.deploymentProgress < 0.0F || state.deploymentProgress > 1.0F ||
        (targetDefense.has_value() && !ValidateP700TerminalDefenseProfile(*targetDefense)) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("P-700 runtime is invalid, stored/spent, or time-reversing");
    }
    if (state.phase == P700GranitPhase::Impact || state.phase == P700GranitPhase::Defeated)
    {
        state.phase = P700GranitPhase::Spent;
        state.phaseStartTimeSeconds = simulationTimeSeconds;
        state.lastUpdateTimeSeconds = simulationTimeSeconds;
        return std::optional<P700GranitImpact>{};
    }
    const auto guidance = UpdateP700PerceivedGuidance(definition, state, perceivedTrack);
    if (!guidance)
    {
        return std::unexpected(guidance.error());
    }

    double cursorTime = state.lastUpdateTimeSeconds;
    double remainingSeconds = simulationTimeSeconds - cursorTime;
    std::size_t guard = 0U;

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
        const float distance = speedMetersPerSecond * static_cast<float>(deltaSeconds);
        const Physics::PhysicsVector3 displacement{
            .x = directionUnit.x * distance,
            .y = directionUnit.y * distance,
            .z = directionUnit.z * distance};
        if (std::abs(displacement.x) <= 1.0e-9F && std::abs(displacement.y) <= 1.0e-9F &&
            std::abs(displacement.z) <= 1.0e-9F)
        {
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
            state.headingRadians = heading;
            state.speedMetersPerSecond = speedMetersPerSecond;
            return std::optional<P700GranitImpact>{};
        }

        const Physics::PhysicsSweepHit hit = **sweep;
        const double impactTimeSeconds = cursorTime + deltaSeconds * static_cast<double>(hit.fraction);
        state.positionMeters = hit.positionMeters;
        state.headingRadians = heading;
        state.speedMetersPerSecond = 0.0F;
        state.phase = P700GranitPhase::Impact;
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

    while (remainingSeconds > 1.0e-9)
    {
        if (++guard > 20'000U)
        {
            return std::unexpected("P-700 lifecycle advance exceeded its bounded integration guard");
        }

        if (state.phase == P700GranitPhase::HatchOpening)
        {
            if (state.speedMetersPerSecond != 0.0F || state.deploymentProgress != 0.0F ||
                state.postExitTransitionProgress != 0.0F || state.launchBoosterActive || state.mainEngineActive ||
                !state.launchBoosterAttached || !state.noseProtectionCapAttached)
            {
                return std::unexpected("P-700 hatch-opening component contract is invalid");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.launcherHatchOpeningSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            cursorTime += stepSeconds;
            remainingSeconds -= stepSeconds;
            state.lastUpdateTimeSeconds = cursorTime;
            state.hatchOpenProgress = std::clamp(
                static_cast<float>((cursorTime - state.phaseStartTimeSeconds) / definition.launcherHatchOpeningSeconds),
                0.0F, 1.0F);
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.hatchOpenProgress = 1.0F;
                state.launchBoosterActive = true;
                state.phase = state.positionMeters.y < state.surfaceLevelYMeters - 0.05F
                    ? P700GranitPhase::UnderwaterLaunch
                    : P700GranitPhase::WaterExit;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = state.phase == P700GranitPhase::UnderwaterLaunch
                    ? definition.underwaterExitSpeedMetersPerSecond
                    : definition.waterExitSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase == P700GranitPhase::UnderwaterLaunch)
        {
            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||
                state.postExitTransitionProgress != 0.0F || state.launchForwardUnitVector.y <= 0.0F ||
                !state.launchBoosterActive || !state.launchBoosterAttached || !state.noseProtectionCapAttached ||
                state.mainEngineActive)
            {
                return std::unexpected("P-700 underwater booster launch/deployment contract is invalid");
            }
            const double verticalSpeed = static_cast<double>(state.launchForwardUnitVector.y) *
                                         definition.underwaterExitSpeedMetersPerSecond;
            const double verticalDistance = static_cast<double>(state.surfaceLevelYMeters) - state.positionMeters.y;
            const double timeToSurface = verticalDistance <= 0.0 ? 0.0 : verticalDistance / verticalSpeed;
            const double stepSeconds = std::min(remainingSeconds, timeToSurface);
            if (stepSeconds > 0.0)
            {
                const auto impact = moveSegment(
                    state.launchForwardUnitVector, definition.underwaterExitSpeedMetersPerSecond, stepSeconds);
                if (!impact) return std::unexpected(impact.error());
                if (*impact) return *impact;
                cursorTime += stepSeconds;
                remainingSeconds -= stepSeconds;
                state.lastUpdateTimeSeconds = cursorTime;
            }
            if (timeToSurface <= stepSeconds + 1.0e-9)
            {
                state.positionMeters.y = state.surfaceLevelYMeters;
                state.phase = P700GranitPhase::WaterExit;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = definition.waterExitSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase == P700GranitPhase::WaterExit)
        {
            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||
                state.postExitTransitionProgress != 0.0F || !state.launchBoosterActive ||
                !state.launchBoosterAttached || !state.noseProtectionCapAttached || state.mainEngineActive)
            {
                return std::unexpected("P-700 must remain booster-driven and stowed through WaterExit");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.waterExitTransitionSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            if (stepSeconds > 0.0)
            {
                const auto impact = moveSegment(
                    state.launchForwardUnitVector, definition.waterExitSpeedMetersPerSecond, stepSeconds);
                if (!impact) return std::unexpected(impact.error());
                if (*impact) return *impact;
                cursorTime += stepSeconds;
                remainingSeconds -= stepSeconds;
                state.lastUpdateTimeSeconds = cursorTime;
            }
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.phase = P700GranitPhase::PostExitTransition;
                state.phaseStartTimeSeconds = cursorTime;
                state.postExitTransitionProgress = 0.0F;
                state.speedMetersPerSecond = definition.deploymentFlightSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase == P700GranitPhase::PostExitTransition)
        {
            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F)
            {
                return std::unexpected("P-700 post-exit transition must precede aerodynamic deployment");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.postExitTransitionSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            if (stepSeconds > 0.0)
            {
                const auto impact = moveSegment(
                    state.launchForwardUnitVector, definition.deploymentFlightSpeedMetersPerSecond, stepSeconds);
                if (!impact) return std::unexpected(impact.error());
                if (*impact) return *impact;
                cursorTime += stepSeconds;
                remainingSeconds -= stepSeconds;
                state.lastUpdateTimeSeconds = cursorTime;
            }
            state.postExitTransitionProgress = std::clamp(
                static_cast<float>((cursorTime - state.phaseStartTimeSeconds) / definition.postExitTransitionSeconds),
                0.0F, 1.0F);
            // The protective nose cap clears first; the launch booster separates next; only then does the
            // main engine own thrust. Fractions are explicit visual/gameplay timing policy.
            if (state.postExitTransitionProgress >= 0.25F)
                state.noseProtectionCapAttached = false;
            if (state.postExitTransitionProgress >= 0.50F)
            {
                state.launchBoosterAttached = false;
                state.launchBoosterActive = false;
                state.mainEngineActive = true;
            }
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.postExitTransitionProgress = 1.0F;
                state.noseProtectionCapAttached = false;
                state.launchBoosterAttached = false;
                state.launchBoosterActive = false;
                state.mainEngineActive = true;
                state.phase = P700GranitPhase::AirborneDeploying;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = definition.deploymentFlightSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase == P700GranitPhase::AirborneDeploying)
        {
            if (!state.mainEngineActive || state.launchBoosterActive || state.launchBoosterAttached ||
                state.noseProtectionCapAttached || state.postExitTransitionProgress != 1.0F)
            {
                return std::unexpected("P-700 aerodynamic deployment requires completed launch-hardware separation and main-engine ignition");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.deploymentSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            if (stepSeconds > 0.0)
            {
                const auto impact = moveSegment(
                    state.launchForwardUnitVector, definition.deploymentFlightSpeedMetersPerSecond, stepSeconds);
                if (!impact) return std::unexpected(impact.error());
                if (*impact) return *impact;
                cursorTime += stepSeconds;
                remainingSeconds -= stepSeconds;
                state.lastUpdateTimeSeconds = cursorTime;
            }
            state.deploymentProgress = std::clamp(
                static_cast<float>((cursorTime - state.phaseStartTimeSeconds) / definition.deploymentSeconds),
                0.0F,
                1.0F);
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.deploymentProgress = 1.0F;
                state.phase = P700GranitPhase::Cruise;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = definition.cruiseSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase != P700GranitPhase::Cruise && state.phase != P700GranitPhase::Terminal)
        {
            return std::unexpected("P-700 lifecycle reached an unsupported active phase");
        }
        if (!state.mainEngineActive || state.launchBoosterActive || state.launchBoosterAttached ||
            state.noseProtectionCapAttached || !state.perceivedAimPointMeters ||
            !state.perceivedAimPointMeters->IsFinite() || state.deploymentProgress != 1.0F)
        {
            return std::unexpected("P-700 airborne guidance requires a deployed weapon and perceived aim point");
        }

        const float targetDistance = P700DistanceMeters(state.positionMeters, *state.perceivedAimPointMeters);
        if (!std::isfinite(targetDistance))
        {
            return std::unexpected("P-700 perceived target distance is non-finite");
        }
        if (state.phase == P700GranitPhase::Cruise && targetDistance <= definition.terminalRangeMeters)
        {
            state.phase = P700GranitPhase::Terminal;
            state.phaseStartTimeSeconds = cursorTime;
            if (state.terminalOutcome == P700TerminalEngagementOutcome::Unresolved)
            {
                state.terminalOutcome = targetDefense.has_value()
                    ? ResolveP700TerminalEngagement(
                          *targetDefense,
                          state.terminalRandomSeed,
                          state.perceivedPositionUncertaintyMeters.value_or(500.0F))
                    : P700TerminalEngagementOutcome::HitPath;
            }
            if (state.terminalOutcome != P700TerminalEngagementOutcome::HitPath)
            {
                state.phase = P700GranitPhase::Defeated;
                state.speedMetersPerSecond = 0.0F;
                state.mainEngineActive = false;
                state.lastUpdateTimeSeconds = cursorTime;
                return std::optional<P700GranitImpact>{};
            }
        }

        constexpr double MaximumAirborneIntegrationStepSeconds = 0.25;
        const double stepSeconds = std::min(remainingSeconds, MaximumAirborneIntegrationStepSeconds);
        const double dx = static_cast<double>(state.perceivedAimPointMeters->x) - state.positionMeters.x;
        const double dy = static_cast<double>(state.perceivedAimPointMeters->y) - state.positionMeters.y;
        const double dz = static_cast<double>(state.perceivedAimPointMeters->z) - state.positionMeters.z;
        const float desiredHeading = static_cast<float>(std::atan2(dy, dx));
        const float headingDelta = WrapP700Heading(desiredHeading - state.headingRadians);
        const float maximumTurn = definition.maximumAirborneTurnRateRadiansPerSecond * static_cast<float>(stepSeconds);
        state.headingRadians = WrapP700Heading(
            state.headingRadians + std::clamp(headingDelta, -maximumTurn, maximumTurn));

        // Production Antey launchers are physically offset to port/starboard from the 2.5D gameplay plane.
        // Preserve that real launch anchor, then converge the airborne weapon toward the perceived Track Z
        // instead of silently freezing the launcher-side offset for the entire flight. X/Y heading still obeys
        // the accepted bounded turn-rate policy; this lateral slope only closes the Track-derived cross-plane error.
        const double planarTargetDistance = std::hypot(dx, dy);
        const float crossPlaneSlope = planarTargetDistance > 1.0e-6
            ? static_cast<float>(dz / planarTargetDistance)
            : (std::abs(dz) > 1.0e-6 ? static_cast<float>(std::copysign(1.0, dz)) : 0.0F);
        const auto direction = NormalizeP700Vector(Physics::PhysicsVector3{
            .x = static_cast<float>(std::cos(static_cast<double>(state.headingRadians))),
            .y = static_cast<float>(std::sin(static_cast<double>(state.headingRadians))),
            .z = crossPlaneSlope});
        if (!direction)
        {
            return std::unexpected("P-700 airborne guidance produced an invalid 3D direction");
        }
        const float speed = state.phase == P700GranitPhase::Terminal
            ? definition.terminalSpeedMetersPerSecond
            : definition.cruiseSpeedMetersPerSecond;
        const auto impact = moveSegment(*direction, speed, stepSeconds);
        if (!impact) return std::unexpected(impact.error());
        if (*impact) return *impact;
        cursorTime += stepSeconds;
        remainingSeconds -= stepSeconds;
        state.lastUpdateTimeSeconds = cursorTime;
    }

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    return std::optional<P700GranitImpact>{};
}
} // namespace DeepRun::Weapons
