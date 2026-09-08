#pragma once

#include "Engine/Physics/PhysicsTypes.h"

namespace DeepRun::Physics
{
// Backend-independent closest-hit sweep input. The box keeps a fixed orientation during the cast; callers
// should issue one query per fixed simulation step when modelling curved/turning motion.
struct PhysicsBoxSweepQuery final
{
    PhysicsVector3 halfExtentsMeters{};
    PhysicsVector3 startPositionMeters{};
    PhysicsQuaternion orientation{};
    PhysicsVector3 displacementMeters{};
    PhysicsBodyHandle ignoredBody{};
};

// Result of a successful closest-hit sweep. `positionMeters` is the swept box center at first contact,
// reconstructed from the backend hit fraction. No Jolt/backend identifier crosses the public API.
struct PhysicsSweepHit final
{
    PhysicsBodyHandle body{};
    float fraction = 0.0F;
    PhysicsVector3 positionMeters{};

    [[nodiscard]] bool operator==(const PhysicsSweepHit&) const noexcept = default;
};
}
