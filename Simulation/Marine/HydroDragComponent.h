#pragma once

#include "Engine/Physics/PhysicsTypes.h"

namespace DeepRun::Marine
{
// Pure directional-drag tuning for a fully immersed body (M2 Slice F1). Coefficients are expressed on
// BODY-LOCAL X/Y/Z axes and deliberately remain independent from collision geometry, displaced-water
// volume, mass, and any concrete vessel definition.
struct HydroDragComponent final
{
    // Effective Cd * reference area per body-local axis, in m^2. Zero disables linear drag on that axis.
    Physics::PhysicsVector3 linearEffectiveAreaSquareMeters{};

    // Effective quadratic angular-drag moment per body-local axis, in m^5. Zero disables angular drag on
    // that axis. This is a bounded game approximation rather than a full hull-flow model.
    Physics::PhysicsVector3 angularEffectiveMomentMeters5{};
};
}
