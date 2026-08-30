#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Marine/ControlSurfaceComponent.h"
#include "Simulation/Marine/WaterBody.h"

#include <expected>
#include <string>

namespace DeepRun::Marine
{
enum class ControlSurfaceErrorCode
{
    InvalidConfiguration,
    InvalidDeflection,
    InvalidKinematics,
    NonFiniteResult,
};

struct ControlSurfaceError final
{
    ControlSurfaceErrorCode code = ControlSurfaceErrorCode::InvalidConfiguration;
    std::string message;
};

// Marine-owned pure kinematic input. H1 intentionally uses center-of-body linear velocity only: there is
// no rigid-body handle, angular velocity, local-flow correction, actuator state, or integration time.
struct ControlSurfaceKinematics final
{
    Physics::PhysicsVector3 bodyWorldPositionMeters{};
    Physics::PhysicsQuaternion worldOrientation{}; // finite non-zero; normalized internally
    Physics::PhysicsVector3 worldLinearVelocityMetersPerSecond{};
};

struct ControlSurfaceResult final
{
    Physics::PhysicsVector3 worldPositionMeters{};
    float bodyForwardSpeedMetersPerSecond = 0.0F;
    float deflectionFraction = 0.0F;
    Physics::PhysicsVector3 forceNewtons{};
};

// Pure fully-immersed M2 diving-plane calculation. Local +X is the flow axis and local +Y is the lift axis.
// The result is an instantaneous force and world application point; H1 applies no force and derives no torque.
class ControlSurfaceSystem final
{
public:
    [[nodiscard]] static std::expected<ControlSurfaceResult, ControlSurfaceError> Calculate(
        const WaterBody& water,
        const ControlSurfaceComponent& component,
        const ControlSurfaceKinematics& kinematics,
        float deflectionFraction);
};
}
