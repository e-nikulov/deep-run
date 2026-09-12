#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Simulation/Marine/PropulsionComponent.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Project 949A public sources commonly publish about 24,000 t full/submerged displacement. Rubin's public
// Project 949A page identifies the design but does not publish a displacement table, while specialist public
// references disagree on whether 19,400 t or ~24,000 t is the appropriate submerged/full-load figure.
// Deep Run therefore uses 24,000 t as the canonical fully-submerged gameplay mass, not as a classified claim.
inline constexpr float AnteyCanonicalFullSubmergedMassKg = 24'000'000.0F;

// GAME POLICY. No reliable public Project 949A maximum-astern figure was found. Reverse drive is deliberately
// much weaker than ahead drive, while shaft reversal itself remains physical: an ahead-turning shaft must spin
// down through zero before it can build astern RPM.
inline constexpr Marine::PropulsionComponent AnteyGameplayPropulsion{
    .maxForwardRpm = 180.0F,
    .maxReverseRpm = 90.0F,
    .maxForwardThrustNewtons = 12'000'000.0F,
    .maxReverseThrustNewtons = 3'000'000.0F,
    .spinUpRateRpmPerSecond = 30.0F,
    .spinDownRateRpmPerSecond = 45.0F};

// GAME POLICY. A 154 m / ~24,000 t boat must not snap-flip in a side-view game. The production rigid body
// remains constrained to the XY gameplay plane; this state provides the missing longitudinal facing dimension.
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

static_assert(AnteyCanonicalFullSubmergedMassKg == 24'000'000.0F);
static_assert(AnteyGameplayPropulsion.maxReverseRpm < AnteyGameplayPropulsion.maxForwardRpm);
static_assert(AnteyGameplayPropulsion.maxReverseThrustNewtons < AnteyGameplayPropulsion.maxForwardThrustNewtons);
static_assert(AnteyTurnAroundDurationSeconds >= 30.0F);
} // namespace DeepRun::Game::Submarine
