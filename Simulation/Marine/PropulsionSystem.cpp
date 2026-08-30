#include "Simulation/Marine/PropulsionSystem.h"

#include <cmath>
#include <limits>
#include <string_view>

namespace DeepRun::Marine
{
namespace
{
PropulsionError MakeError(const PropulsionErrorCode code, const std::string& message)
{
    return PropulsionError{code, message};
}

bool FitsInFloat(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
}

bool ValidatePositiveFinite(
    const float value,
    const std::string_view fieldName,
    PropulsionError* error)
{
    if (!std::isfinite(value) || value <= 0.0F)
    {
        *error = MakeError(
            PropulsionErrorCode::InvalidConfiguration,
            std::string(fieldName) + " must be finite and greater than zero");
        return false;
    }
    return true;
}

double MoveTowards(const double current, const double target, const double maximumDelta) noexcept
{
    const double difference = target - current;
    if (std::abs(difference) <= maximumDelta)
    {
        return target;
    }
    return current + std::copysign(maximumDelta, difference);
}
} // namespace

std::expected<PropulsionResult, PropulsionError> PropulsionSystem::Advance(
    const PropulsionComponent& component,
    const PropulsionState& currentState,
    const PropulsionCommand& command,
    const float fixedDeltaSeconds)
{
    PropulsionError validationError{};
    if (!ValidatePositiveFinite(component.maxForwardRpm, "maxForwardRpm", &validationError) ||
        !ValidatePositiveFinite(component.maxReverseRpm, "maxReverseRpm", &validationError) ||
        !ValidatePositiveFinite(
            component.maxForwardThrustNewtons, "maxForwardThrustNewtons", &validationError) ||
        !ValidatePositiveFinite(
            component.maxReverseThrustNewtons, "maxReverseThrustNewtons", &validationError) ||
        !ValidatePositiveFinite(
            component.spinUpRateRpmPerSecond, "spinUpRateRpmPerSecond", &validationError) ||
        !ValidatePositiveFinite(
            component.spinDownRateRpmPerSecond, "spinDownRateRpmPerSecond", &validationError))
    {
        return std::unexpected(validationError);
    }

    if (!std::isfinite(command.requestedDriveFraction) ||
        command.requestedDriveFraction < -1.0F || command.requestedDriveFraction > 1.0F)
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::InvalidCommand,
            "requestedDriveFraction must be finite and within [-1, 1]"));
    }
    if (!std::isfinite(command.availablePowerFraction) ||
        command.availablePowerFraction < 0.0F || command.availablePowerFraction > 1.0F)
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::InvalidCommand,
            "availablePowerFraction must be finite and within [0, 1]"));
    }

    if (!std::isfinite(currentState.shaftRpm) ||
        currentState.shaftRpm < -component.maxReverseRpm ||
        currentState.shaftRpm > component.maxForwardRpm)
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::InvalidState,
            "shaftRpm must be finite and within configured reverse/forward capability"));
    }
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::InvalidDeltaTime,
            "fixedDeltaSeconds must be finite and greater than zero"));
    }

    double effectiveDrive = static_cast<double>(command.requestedDriveFraction) *
                            static_cast<double>(command.availablePowerFraction);
    if (!FitsInFloat(effectiveDrive))
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::NonFiniteResult,
            "effective drive is outside finite float range"));
    }
    // Canonicalize signed zero so zero-power/rest results are exact, direction-neutral zero outputs.
    if (effectiveDrive == 0.0)
    {
        effectiveDrive = 0.0;
    }

    const double targetRpm = effectiveDrive >= 0.0
                                 ? effectiveDrive * static_cast<double>(component.maxForwardRpm)
                                 : effectiveDrive * static_cast<double>(component.maxReverseRpm);
    if (!FitsInFloat(targetRpm))
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::NonFiniteResult,
            "target RPM is outside finite float range"));
    }

    const double currentRpm = static_cast<double>(currentState.shaftRpm);
    const bool reversing = (currentRpm > 0.0 && targetRpm < 0.0) ||
                           (currentRpm < 0.0 && targetRpm > 0.0);
    const double responseTarget = reversing ? 0.0 : targetRpm;
    const bool increasingMagnitude = !reversing && std::abs(targetRpm) > std::abs(currentRpm);
    const double responseRate = increasingMagnitude
                                    ? static_cast<double>(component.spinUpRateRpmPerSecond)
                                    : static_cast<double>(component.spinDownRateRpmPerSecond);
    const double maximumDelta = responseRate * static_cast<double>(fixedDeltaSeconds);
    if (!FitsInFloat(maximumDelta))
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::NonFiniteResult,
            "RPM response delta is outside finite float range"));
    }

    // A direction change consumes this update only moving toward zero at spin-down rate. Even if zero is
    // reached early, no leftover time is used to begin opposite rotation; a subsequent update spins up.
    double nextRpm = MoveTowards(currentRpm, responseTarget, maximumDelta);
    if (nextRpm == 0.0)
    {
        nextRpm = 0.0;
    }
    if (!FitsInFloat(nextRpm))
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::NonFiniteResult,
            "next shaft RPM is outside finite float range"));
    }

    const bool forward = nextRpm >= 0.0;
    const double rpmLimit = forward ? static_cast<double>(component.maxForwardRpm)
                                    : static_cast<double>(component.maxReverseRpm);
    const double thrustLimit = forward ? static_cast<double>(component.maxForwardThrustNewtons)
                                       : static_cast<double>(component.maxReverseThrustNewtons);
    const double normalizedRpm = nextRpm / rpmLimit;
    double thrustNewtons = thrustLimit * normalizedRpm * std::abs(normalizedRpm);
    if (nextRpm == 0.0)
    {
        thrustNewtons = 0.0;
    }
    if (!FitsInFloat(normalizedRpm) || !FitsInFloat(thrustNewtons))
    {
        return std::unexpected(MakeError(
            PropulsionErrorCode::NonFiniteResult,
            "thrust output is outside finite float range"));
    }

    return PropulsionResult{
        .nextState = {.shaftRpm = static_cast<float>(nextRpm)},
        .effectiveDriveFraction = static_cast<float>(effectiveDrive),
        .targetRpm = static_cast<float>(targetRpm),
        .thrustNewtons = static_cast<float>(thrustNewtons)};
}
}
