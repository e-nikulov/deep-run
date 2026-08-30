#pragma once

#include "Simulation/Marine/PropulsionComponent.h"

#include <expected>
#include <string>

namespace DeepRun::Marine
{
// Authoritative simulation state for exactly one shaft/propulsor. Signed RPM denotes ahead (>0), stopped
// (=0), or astern (<0) rotation. Visual propeller angle is presentation state and deliberately absent.
struct PropulsionState final
{
    float shaftRpm = 0.0F;
};

// Generic simulation input, not a player Throttle mapping. Requested drive is [-1, +1]; externally supplied
// available power is [0, 1]. G1 does not calculate a reactor/electrical power budget.
struct PropulsionCommand final
{
    float requestedDriveFraction = 0.0F;
    float availablePowerFraction = 0.0F;
};

struct PropulsionResult final
{
    PropulsionState nextState{};
    float effectiveDriveFraction = 0.0F;
    float targetRpm = 0.0F;
    float thrustNewtons = 0.0F;
};

enum class PropulsionErrorCode
{
    InvalidConfiguration,
    InvalidCommand,
    InvalidState,
    InvalidDeltaTime,
    NonFiniteResult,
};

struct PropulsionError final
{
    PropulsionErrorCode code = PropulsionErrorCode::InvalidConfiguration;
    std::string message;
};

// Pure deterministic one-shaft state evolution (M2 Slice G1). Delta time controls only the finite RPM
// response; thrust remains a Newton output and is neither integrated nor applied here. The caller owns and
// stores nextState. No hidden/static state exists.
class PropulsionSystem final
{
public:
    [[nodiscard]] static std::expected<PropulsionResult, PropulsionError> Advance(
        const PropulsionComponent& component,
        const PropulsionState& currentState,
        const PropulsionCommand& command,
        float fixedDeltaSeconds);
};
}
