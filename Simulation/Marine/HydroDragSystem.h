#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Marine/HydroDragComponent.h"
#include "Simulation/Marine/WaterBody.h"

#include <expected>
#include <string>

namespace DeepRun::Marine
{
enum class HydroDragErrorCode
{
    InvalidConfiguration, // coefficient is negative or non-finite
    InvalidState,         // orientation or velocity is invalid
    NonFiniteResult,      // a derived local/world value cannot be represented as a finite float
};

struct HydroDragError final
{
    HydroDragErrorCode code = HydroDragErrorCode::InvalidConfiguration;
    std::string message;
};

// Marine-owned instantaneous motion state. There is intentionally no position, mass, body handle, water
// current, or integration time: F1 evaluates a fully immersed body in stationary water.
struct HydroDragState final
{
    Physics::PhysicsQuaternion worldOrientation{}; // finite non-zero; normalized internally
    Physics::PhysicsVector3 worldLinearVelocityMetersPerSecond{};
    Physics::PhysicsVector3 worldAngularVelocityRadiansPerSecond{};
};

struct HydroDragResult final
{
    Physics::PhysicsVector3 forceNewtons{};
    Physics::PhysicsVector3 torqueNewtonMeters{};
};

// Pure fully-immersed directional hydrodynamic drag calculation (M2 Slice F1). Returns instantaneous
// Newton/Newton-meter values and applies them nowhere. A future integration slice will re-evaluate and apply
// these values each fixed tick; this API has no time-step parameter and performs no velocity mutation.
class HydroDragSystem final
{
public:
    [[nodiscard]] static std::expected<HydroDragResult, HydroDragError> Calculate(
        const WaterBody& water,
        const HydroDragComponent& component,
        const HydroDragState& state);
};
}
