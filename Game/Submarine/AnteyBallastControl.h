#pragma once

#include "Game/Submarine/AnteyHandlingModel.h"
#include "Game/Weapons/AnteyOrdnanceMass.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// GAME POLICY around the calibrated runtime hydrostatic masses. Exact tank/pump/blow timing is not asserted.
struct AnteyBallastControlConfig final
{
    // Main ballast stays flooded during ordinary submerged depth changes. A sustained surface command begins
    // normal blowing in the final near-surface band. Deadlock fallback: if low-speed trim is already at
    // maximum buoyancy and the controller still requests that same maximum, continuing Surface also starts
    // the blow. Once partly blown, the operation may continue while Surface remains commanded.
    float mainBallastBlowArmDepthMeters = 3.0F;
    float mainBallastFillRateFractionPerSecond = 0.025F; // empty -> full in 40 s at full Dive command
    float mainBallastBlowRateFractionPerSecond = 0.025F; // full -> empty in 40 s at full Surface command
    float depthCommandDeadzone = 0.05F;

    // Low-speed trim is real equivalent water mass with finite actuator response, not an applied vertical force.
    float maximumTrimMassKg = AnteyGameplayFullSubmergedMassKg * 0.015F;
    float trimFullRangeResponseSeconds = 10.0F; // neutral -> one extreme; explicit GAME POLICY

    // Weapon launch removes real ordnance mass. Dedicated compensation water restores that lost mass with a
    // finite GAME-policy rate. 2.8 t/s can compensate a two-P-700 (14 t) salvo in ~5 s; this is not a claim
    // about undocumented Project 949A pump/valve performance.
    float maximumWeaponCompensationWaterMassKg = Armament::AnteyConfiguredCombatOrdnanceMassKg;
    float weaponCompensationFillRateKgPerSecond = 2'800.0F;
};

struct AnteyBallastState final
{
    // 0 = calibrated gameplay surfaced mass (main ballast empty), 1 = calibrated full-submerged mass.
    float mainBallastFillFraction = 1.0F;
    // Signed equivalent trim-water delta about the main-ballast mass. Positive = heavier, negative = lighter.
    float trimMassDeltaKg = 0.0F;
    // Water retained to replace mass of weapons that have physically left the submarine.
    float weaponCompensationWaterMassKg = 0.0F;
};

[[nodiscard]] inline std::expected<AnteyBallastState, std::string> AdvanceAnteyBallastState(
    const AnteyBallastControlConfig& config,
    const AnteyBallastState& current,
    const float depthCommandFraction,
    const float signedDepthMeters,
    const float requestedTrimMassDeltaKg,
    const float requestedWeaponCompensationWaterMassKg,
    const float fixedDeltaSeconds)
{
    const bool validConfig =
        std::isfinite(config.mainBallastBlowArmDepthMeters) && config.mainBallastBlowArmDepthMeters >= 0.0F &&
        std::isfinite(config.mainBallastFillRateFractionPerSecond) && config.mainBallastFillRateFractionPerSecond > 0.0F &&
        std::isfinite(config.mainBallastBlowRateFractionPerSecond) && config.mainBallastBlowRateFractionPerSecond > 0.0F &&
        std::isfinite(config.depthCommandDeadzone) && config.depthCommandDeadzone >= 0.0F &&
        config.depthCommandDeadzone < 1.0F &&
        std::isfinite(config.maximumTrimMassKg) && config.maximumTrimMassKg > 0.0F &&
        std::isfinite(config.trimFullRangeResponseSeconds) && config.trimFullRangeResponseSeconds > 0.0F &&
        std::isfinite(config.maximumWeaponCompensationWaterMassKg) &&
        config.maximumWeaponCompensationWaterMassKg >= 0.0F &&
        std::isfinite(config.weaponCompensationFillRateKgPerSecond) &&
        config.weaponCompensationFillRateKgPerSecond > 0.0F;
    if (!validConfig)
    {
        return std::unexpected("Antey ballast control configuration is invalid");
    }

    if (!std::isfinite(current.mainBallastFillFraction) || current.mainBallastFillFraction < 0.0F ||
        current.mainBallastFillFraction > 1.0F || !std::isfinite(current.trimMassDeltaKg) ||
        std::abs(current.trimMassDeltaKg) > config.maximumTrimMassKg ||
        !std::isfinite(current.weaponCompensationWaterMassKg) || current.weaponCompensationWaterMassKg < 0.0F ||
        current.weaponCompensationWaterMassKg > config.maximumWeaponCompensationWaterMassKg)
    {
        return std::unexpected("Antey ballast state is invalid");
    }

    if (!std::isfinite(depthCommandFraction) || depthCommandFraction < -1.0F || depthCommandFraction > 1.0F ||
        !std::isfinite(signedDepthMeters) || !std::isfinite(requestedTrimMassDeltaKg) ||
        !std::isfinite(requestedWeaponCompensationWaterMassKg) || requestedWeaponCompensationWaterMassKg < 0.0F ||
        requestedWeaponCompensationWaterMassKg > config.maximumWeaponCompensationWaterMassKg ||
        !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected("Antey ballast control input is invalid");
    }

    AnteyBallastState next = current;
    const float trimTargetKg = std::clamp(
        requestedTrimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);
    constexpr float TrimSaturationToleranceKg = 1.0F;
    const bool surfaceCommandHeld = depthCommandFraction < -config.depthCommandDeadzone;
    const bool surfaceTrimSaturated =
        current.trimMassDeltaKg <= -config.maximumTrimMassKg + TrimSaturationToleranceKg &&
        trimTargetKg <= -config.maximumTrimMassKg + TrimSaturationToleranceKg;

    if (depthCommandFraction > config.depthCommandDeadzone && current.mainBallastFillFraction < 1.0F)
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastFillRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
    else if (surfaceCommandHeld &&
             (current.mainBallastFillFraction < 1.0F ||
              signedDepthMeters <= config.mainBallastBlowArmDepthMeters ||
              surfaceTrimSaturated))
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastBlowRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
    const float maximumTrimStepKg =
        (config.maximumTrimMassKg / config.trimFullRangeResponseSeconds) * fixedDeltaSeconds;
    const float trimDifferenceKg = trimTargetKg - current.trimMassDeltaKg;
    next.trimMassDeltaKg = current.trimMassDeltaKg +
        std::clamp(trimDifferenceKg, -maximumTrimStepKg, maximumTrimStepKg);
    next.trimMassDeltaKg = std::clamp(
        next.trimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);

    const float compensationTargetKg = std::clamp(
        requestedWeaponCompensationWaterMassKg,
        0.0F,
        config.maximumWeaponCompensationWaterMassKg);
    const float maximumCompensationStepKg =
        config.weaponCompensationFillRateKgPerSecond * fixedDeltaSeconds;
    const float compensationDifferenceKg = compensationTargetKg - current.weaponCompensationWaterMassKg;
    next.weaponCompensationWaterMassKg = current.weaponCompensationWaterMassKg +
        std::clamp(compensationDifferenceKg, -maximumCompensationStepKg, maximumCompensationStepKg);
    next.weaponCompensationWaterMassKg = std::clamp(
        next.weaponCompensationWaterMassKg,
        0.0F,
        config.maximumWeaponCompensationWaterMassKg);
    return next;
}

[[nodiscard]] inline float AnteyPhysicalMassKg(
    const AnteyBallastControlConfig& config,
    const AnteyBallastState& state,
    const float expendedOrdnanceMassKg = 0.0F) noexcept
{
    const float expendedMassKg = std::clamp(
        std::isfinite(expendedOrdnanceMassKg) ? expendedOrdnanceMassKg : 0.0F,
        0.0F,
        config.maximumWeaponCompensationWaterMassKg);
    const float compensationWaterMassKg = std::clamp(
        state.weaponCompensationWaterMassKg,
        0.0F,
        config.maximumWeaponCompensationWaterMassKg);
    const float baseMassKg = AnteyGameplaySurfaceMassKg +
        AnteyGameplayMainBallastWaterCapacityKg * std::clamp(state.mainBallastFillFraction, 0.0F, 1.0F) -
        expendedMassKg + compensationWaterMassKg;
    return std::clamp(
        baseMassKg + state.trimMassDeltaKg,
        AnteyGameplaySurfaceMassKg - expendedMassKg,
        AnteyGameplayFullSubmergedMassKg + config.maximumTrimMassKg);
}
} // namespace DeepRun::Game::Submarine
