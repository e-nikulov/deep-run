#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Simulation/Marine/PropulsionComponent.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Public Project 949A displacement figures are not perfectly uniform. Keep one documented source set as
// reference data, separate from the production-geometry hydrostatic tuning used by normal gameplay.
// Apalkov/Deepstorm-style values are 14,700 t surfaced and 19,400 t submerged; they remain public-reference
// facts and are not silently rewritten to make the rendered waterline fit.
inline constexpr float AnteyPublicSurfaceDisplacementMassKg = 14'700'000.0F;
inline constexpr float AnteyPublicSubmergedDisplacementMassKg = 19'400'000.0F;
inline constexpr float AnteyMainBallastWaterCapacityKg =
    AnteyPublicSubmergedDisplacementMassKg - AnteyPublicSurfaceDisplacementMassKg;
inline constexpr float AnteyPublicReserveBuoyancyFraction =
    AnteyMainBallastWaterCapacityKg / AnteyPublicSurfaceDisplacementMassKg;

// GAME POLICY: runtime hydrostatic mass/displacement is calibrated against the accepted production LOD0
// surfaced geometry, with no buoyancy-point or render offset. Empty main ballast keeps the public 14,700 t
// surfaced mass. Fully flooded mass is 20,622.267 t; at 1025 kg/m^3 this is 20,119.285 m^3 of full
// displacement. The resulting level flat-water body-centre depth is ~1.85155 m: the deployed bow-plane
// lower edge is ~0.500 m above water while the highest propeller geometry remains ~0.877 m submerged.
inline constexpr float AnteyGameplaySurfaceMassKg = AnteyPublicSurfaceDisplacementMassKg;
inline constexpr float AnteyGameplayFullSubmergedMassKg = 20'622'267.0F;
inline constexpr float AnteyGameplayMainBallastWaterCapacityKg =
    AnteyGameplayFullSubmergedMassKg - AnteyGameplaySurfaceMassKg;
inline constexpr float AnteyGameplayReserveBuoyancyFraction =
    AnteyGameplayMainBallastWaterCapacityKg / AnteyGameplaySurfaceMassKg;
inline constexpr float AnteyPublicMaximumSubmergedSpeedKnots = 32.0F;
inline constexpr float AnteyPublicMaximumSurfacedSpeedKnots = 15.0F;
inline constexpr float KnotsToMetersPerSecond = 0.514444F;
inline constexpr float AnteyPublicMaximumSubmergedSpeedMetersPerSecond =
    AnteyPublicMaximumSubmergedSpeedKnots * KnotsToMetersPerSecond;
inline constexpr float AnteyPublicMaximumSurfacedSpeedMetersPerSecond =
    AnteyPublicMaximumSurfacedSpeedKnots * KnotsToMetersPerSecond;
// Compatibility name retained for existing runtime code; it now follows the calibrated physical gameplay mass.
inline constexpr float AnteyCanonicalFullSubmergedMassKg = AnteyGameplayFullSubmergedMassKg;

// GAME POLICY calibrated jointly with hydrodynamic drag so full ahead asymptotically matches the public 32 kn
// submerged / 15 kn surfaced envelope. Public sources give roughly 2 x 50,000 hp shaft power; the thrust value
// itself is not published and is therefore not presented as a historical hardware specification.
inline constexpr Marine::PropulsionComponent AnteyGameplayPropulsion{
    .maxForwardRpm = 180.0F,
    .maxReverseRpm = 90.0F,
    .maxForwardThrustNewtons = 3'350'000.0F,
    .maxReverseThrustNewtons = 837'500.0F,
    .spinUpRateRpmPerSecond = 30.0F,
    .spinDownRateRpmPerSecond = 45.0F};

// GAME POLICY. A 154 m / ~19,400 t fully submerged boat must not snap-flip in a side-view game. The production
// rigid body remains constrained to the XY gameplay plane; this state provides the missing longitudinal facing dimension.
inline constexpr float AnteyTurnAroundDurationSeconds = 60.0F;

struct AnteyFacingState final
{
    // +1: bow points toward world +X (screen right). -1: bow points toward world -X (screen left).
    int longitudinalSign = 1;
    bool turningAround = false;
    float turnProgress = 0.0F; // [0, 1] only while turningAround
};

struct AnteyFacingAdvance final
{
    AnteyFacingState nextState{};
    bool turnRequestAccepted = false;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateAnteyFacingState(const AnteyFacingState& state)
{
    if ((state.longitudinalSign != 1 && state.longitudinalSign != -1) ||
        !std::isfinite(state.turnProgress) || state.turnProgress < 0.0F || state.turnProgress > 1.0F ||
        (!state.turningAround && state.turnProgress != 0.0F))
    {
        return std::unexpected("Antey 2.5D facing state is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<AnteyFacingAdvance, std::string> AdvanceAnteyFacing(
    const AnteyFacingState& current,
    const bool requestTurnAround,
    const float fixedDeltaSeconds)
{
    const auto valid = ValidateAnteyFacingState(current);
    if (!valid || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected(valid ? "Antey facing delta must be finite and positive" : valid.error());
    }

    AnteyFacingAdvance result{.nextState = current};
    if (requestTurnAround && !result.nextState.turningAround)
    {
        result.nextState.turningAround = true;
        result.nextState.turnProgress = 0.0F;
        result.turnRequestAccepted = true;
    }

    if (result.nextState.turningAround)
    {
        result.nextState.turnProgress = (std::min)(
            1.0F,
            result.nextState.turnProgress + fixedDeltaSeconds / AnteyTurnAroundDurationSeconds);
        if (result.nextState.turnProgress >= 1.0F)
        {
            result.nextState.longitudinalSign = -result.nextState.longitudinalSign;
            result.nextState.turningAround = false;
            result.nextState.turnProgress = 0.0F;
        }
    }
    return result;
}

// 2.5D maneuver projection: visual yaw reaches 90 degrees halfway through the turn, where longitudinal thrust
// projects to zero. It then changes sign continuously. Existing inertia/drag keep acting, so a turn is never an
// instantaneous velocity reversal and astern shaft thrust remains available independently of facing.
[[nodiscard]] inline float AnteyLongitudinalForwardProjection(const AnteyFacingState& state) noexcept
{
    constexpr float Pi = 3.14159265358979323846F;
    if (!state.turningAround)
    {
        return static_cast<float>(state.longitudinalSign);
    }
    return static_cast<float>(state.longitudinalSign) *
        std::cos(Pi * std::clamp(state.turnProgress, 0.0F, 1.0F));
}

[[nodiscard]] inline float AnteyFacingPresentationYawRadians(const AnteyFacingState& state) noexcept
{
    constexpr float Pi = 3.14159265358979323846F;
    const float base = state.longitudinalSign > 0 ? 0.0F : Pi;
    return base + (state.turningAround ? Pi * std::clamp(state.turnProgress, 0.0F, 1.0F) : 0.0F);
}

[[nodiscard]] inline Assets::ModelTransform BuildAnteyFacingPresentationTransform(
    const AnteyFacingState& state) noexcept
{
    const float radians = AnteyFacingPresentationYawRadians(state);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Assets::ModelTransform transform{};
    // Column-major Y-axis rotation. Runtime Y is vertical, so this is a presentation-only turn through the
    // screen-depth dimension while the Jolt body stays in the 2.5D XY plane.
    transform.values[0] = cosine;
    transform.values[2] = -sine;
    transform.values[8] = sine;
    transform.values[10] = cosine;
    return transform;
}

static_assert(AnteyPublicSurfaceDisplacementMassKg == 14'700'000.0F);
static_assert(AnteyPublicSubmergedDisplacementMassKg == 19'400'000.0F);
static_assert(AnteyMainBallastWaterCapacityKg == 4'700'000.0F);
static_assert(AnteyPublicReserveBuoyancyFraction > 0.319F && AnteyPublicReserveBuoyancyFraction < 0.321F);
static_assert(AnteyGameplaySurfaceMassKg == 14'700'000.0F);
static_assert(AnteyGameplayFullSubmergedMassKg == 20'622'267.0F);
static_assert(AnteyGameplayMainBallastWaterCapacityKg == 5'922'267.0F);
static_assert(AnteyGameplayReserveBuoyancyFraction > 0.402F && AnteyGameplayReserveBuoyancyFraction < 0.404F);
static_assert(AnteyCanonicalFullSubmergedMassKg == AnteyGameplayFullSubmergedMassKg);
static_assert(AnteyGameplayPropulsion.maxReverseRpm < AnteyGameplayPropulsion.maxForwardRpm);
static_assert(AnteyGameplayPropulsion.maxReverseThrustNewtons < AnteyGameplayPropulsion.maxForwardThrustNewtons);
static_assert(AnteyTurnAroundDurationSeconds >= 30.0F);
} // namespace DeepRun::Game::Submarine