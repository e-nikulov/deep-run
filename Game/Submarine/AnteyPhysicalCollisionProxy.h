#pragma once

#include "Engine/Physics/PhysicsTypes.h"

namespace DeepRun::Game::Submarine
{
// M5-I.2 read-only bridge from the production Antey physical composition into bounded combat hazards.
// It exposes only the opaque body identity and production collision-box snapshot needed for a generic sweep.
// No render mesh, authoring hierarchy, Jolt/backend type, force command, or mutable physics object crosses it.
struct AnteyPhysicalCollisionProxySnapshot final
{
    Physics::PhysicsBodyHandle body{};
    Physics::PhysicsVector3 positionMeters{};
    Physics::PhysicsQuaternion orientation{};
    Physics::PhysicsVector3 halfExtentsMeters{};
    // 2.5D longitudinal facing is Game authority, not an extra Jolt degree of freedom.
    float gameplayLongitudinalFacingSign = 1.0F;
    bool turningAround = false;
};
} // namespace DeepRun::Game::Submarine
