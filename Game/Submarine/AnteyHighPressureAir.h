#pragma once

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Normalized high-pressure-air gameplay model used by main-ballast blowing and the RKP compressor intake.
// Exact Project 949A bottle volume, compressor output and blow consumption are not asserted by public data;
// every rate below is explicit GAME POLICY. Physics remains authoritative for ballast water/mass.
struct AnteyHighPressureAirConfig final
{
    float minimumBlowPressureFraction = 0.08F;
    float reservePressureFraction = 0.02F;
    float fullMainBallastBlowAirCostFraction = 0.55F;
    float rkpCompressorRechargeFractionPerSecond = 0.004F; // empty -> full in 250 s, GAME POLICY
    float rkpMaximumOperatingDepthMeters = 20.0F;
};

struct AnteyHighPressureAirState final
{
    float pressureFraction = 1.0F;
    bool rkpCompressorRunning = false;
};

[[nodiscard]] inline float AnteyMainBallastBlowAuthorityFraction(
    const AnteyHighPressureAirConfig& config,
    const AnteyHighPressureAirState& state) noexcept
{
    if (!std::isfinite(state.pressureFraction) ||
        !std::isfinite(config.minimumBlowPressureFraction) ||
        !std::isfinite(config.reservePressureFraction) ||
        config.minimumBlowPressureFraction <= config.reservePressureFraction)
    {
        return 0.0F;
    }
    return std::clamp(
        (state.pressureFraction - config.reservePressureFraction) /
            (config.minimumBlowPressureFraction - config.reservePressureFraction),
        0.0F,
        1.0F);
}

[[nodiscard]] inline std::expected<AnteyHighPressureAirState, std::string> AdvanceAnteyHighPressureAir(
    const AnteyHighPressureAirConfig& config,
    const AnteyHighPressureAirState& current,
    const bool rkpCompressorRequested,
    const float signedDepthMeters,
    const float mainBallastFlowFractionPerSecond,
    const float fixedDeltaSeconds)
{
    const bool validConfig =
        std::isfinite(config.minimumBlowPressureFraction) && config.minimumBlowPressureFraction > 0.0F &&
        config.minimumBlowPressureFraction <= 1.0F &&
        std::isfinite(config.reservePressureFraction) && config.reservePressureFraction >= 0.0F &&
        config.reservePressureFraction < config.minimumBlowPressureFraction &&
        std::isfinite(config.fullMainBallastBlowAirCostFraction) && config.fullMainBallastBlowAirCostFraction > 0.0F &&
        config.fullMainBallastBlowAirCostFraction <= 1.0F &&
        std::isfinite(config.rkpCompressorRechargeFractionPerSecond) &&
        config.rkpCompressorRechargeFractionPerSecond > 0.0F &&
        std::isfinite(config.rkpMaximumOperatingDepthMeters) && config.rkpMaximumOperatingDepthMeters >= 0.0F;
    if (!validConfig)
        return std::unexpected("Antey high-pressure-air configuration is invalid");

    if (!std::isfinite(current.pressureFraction) || current.pressureFraction < 0.0F ||
        current.pressureFraction > 1.0F || !std::isfinite(signedDepthMeters) || signedDepthMeters < 0.0F ||
        !std::isfinite(mainBallastFlowFractionPerSecond) || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected("Antey high-pressure-air state/input is invalid");
    }

    AnteyHighPressureAirState next = current;
    next.rkpCompressorRunning = rkpCompressorRequested &&
        signedDepthMeters <= config.rkpMaximumOperatingDepthMeters;

    // Negative main-ballast flow means water is leaving the tank: that is the only operation which consumes
    // this normalized HP-air resource. Flooding and low-speed trim-water motion do not consume it here.
    // Parenthesized std::max avoids collision with the Win32 max macro in translation units that include this
    // gameplay header after Windows SDK headers.
    const float blownMainBallastFraction =
        (std::max)(0.0F, -mainBallastFlowFractionPerSecond) * fixedDeltaSeconds;
    next.pressureFraction -= blownMainBallastFraction * config.fullMainBallastBlowAirCostFraction;

    if (next.rkpCompressorRunning)
        next.pressureFraction += config.rkpCompressorRechargeFractionPerSecond * fixedDeltaSeconds;

    next.pressureFraction = std::clamp(next.pressureFraction, 0.0F, 1.0F);
    return next;
}
} // namespace DeepRun::Game::Submarine
