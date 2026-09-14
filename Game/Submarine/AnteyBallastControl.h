#pragma once

#include "Game/Submarine/AnteyHandlingModel.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// GAME POLICY around the Project 949A public surfaced reference and calibrated gameplay hydrostatics.
// Exact tank/pump/blow timing is not asserted.
struct AnteyBallastControlConfig final
{
    // Main ballast stays flooded during ordinary submerged depth changes. A sustained surface command may
    // begin normal blowing only in the final near-surface band; once partly blown, the operation may continue.
    float mainBallastBlowArmDepthMeters = 3.0F;
    float mainBallastFillRateFractionPerSecond = 0.025F; // empty -> full in 40 s at full Dive command
    float mainBallastBlowRateFractionPerSecond = 0.025F; // full -> empty in 40 s at full Surface command
    float depthCommandDeadzone = 0.05F;

    // Low-speed trim is real equivalent water mass with finite actuator response, not an applied vertical force.
    float maximumTrimMassKg = AnteyGameplayFullSubmergedMassKg * 0.015F;
    float trimFullRangeResponseSeconds = 10.0F; // neutral -> one extreme; explicit GAME POLICY
};

struct AnteyBallastState final
{
    // 0 = public surfaced mass (main ballast empty), 1 = calibrated gameplay full-submerged mass (main ballast full).
    float mainBallastFillFraction = 1.0F;
    // Signed equivalent trim-water delta about the main-ballast mass. Positive = heavier, negative = lighter.
    float trimMassDeltaKg = 0.0F;
};

[[nodiscard]] inline std::expected<AnteyBallastState, std::string> AdvanceAnteyBallastState(
    const AnteyBallastControlConfig& config,
    const AnteyBallastState& current,
    const float depthCommandFraction,
    const float signedDepthMeters,
    const float requestedTrimMassDeltaKg,
    const float fixedDeltaSeconds)
{
    const bool validConfig =
        std::isfinite(config.mainBallastBlowArmDepthMeters) && config.mainBallastBlowArmDepthMeters >= 0.0F &&
        std::isfinite(config.mainBallastFillRateFractionPerSecond) && config.mainBallastFillRateFractionPerSecond > 0.0F &&
        std::isfinite(config.mainBallastBlowRateFractionPerSecond) && config.mainBallastBlowRateFractionPerSecond > 0.0F &&
        std::isfinite(config.depthCommandDeadzone) && config.depthCommandDeadzone >= 0.0F && config.depthCommandDeadzone < 1.0F &&
        std::isfinite(config.maximumTrimMassKg) && config.maximumTrimMassKg > 0.0F &&
        std::isfinite(config.trimFullRangeResponseSeconds) && config.trimFullRangeResponseSeconds > 0.0F;
    if (!validConfig)
        return std::unexpected("Antey ballast control configuration is invalid");
    if (!std::isfinite(current.mainBallastFillFraction) || current.mainBallastFillFraction < 0.0F ||
        current.mainBallastFillFraction > 1.0F || !std::isfinite(current.trimMassDeltaKg) ||
        std::abs(current.trimMassDeltaKg) > config.maximumTrimMassKg)
        return std::unexpected("Antey ballast state is invalid");
    if (!std::isfinite(depthCommandFraction) || depthCommandFraction < -1.0F || depthCommandFraction > 1.0F ||
        !std::isfinite(signedDepthMeters) || !std::isfinite(requestedTrimMassDeltaKg) ||
        !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
        return std::unexpected("Antey ballast control input is invalid");

    AnteyBallastState next = current;
    if (depthCommandFraction > config.depthCommandDeadzone && current.mainBallastFillFraction < 1.0F)
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastFillRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
    else if (depthCommandFraction < -config.depthCommandDeadzone &&
             (current.mainBallastFillFraction < 1.0F ||
              signedDepthMeters <= config.mainBallastBlowArmDepthMeters))
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastBlowRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }

    const float trimTargetKg = std::clamp(
        requestedTrimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);
    const float maximumTrimStepKg =
        (config.maximumTrimMassKg / config.trimFullRangeResponseSeconds) * fixedDeltaSeconds;
    const float trimDifferenceKg = trimTargetKg - current.trimMassDeltaKg;
    next.trimMassDeltaKg = current.trimMassDeltaKg +
        std::clamp(trimDifferenceKg, -maximumTrimStepKg, maximumTrimStepKg);
    next.trimMassDeltaKg = std::clamp(
        next.trimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);
    return next;
}

[[nodiscard]] inline float AnteyPhysicalMassKg(
    const AnteyBallastControlConfig& config,
    const AnteyBallastState& state) noexcept
{
    const float baseMassKg = AnteyPublicSurfaceDisplacementMassKg +
        AnteyMainBallastWaterCapacityKg * std::clamp(state.mainBallastFillFraction, 0.0F, 1.0F);
    return std::clamp(
        baseMassKg + state.trimMassDeltaKg,
        AnteyPublicSurfaceDisplacementMassKg,
        AnteyGameplayFullSubmergedMassKg + config.maximumTrimMassKg);
}
} // namespace DeepRun::Game::Submarine