#pragma once

#include "Engine/Physics/PhysicsTypes.h"

namespace DeepRun::Marine
{
// Pure H1 tuning for one independently evaluated, fully immersed diving surface or surface group.
// The coefficient and body-local application point are vessel/content data; they are not mesh-derived.
struct ControlSurfaceComponent final
{
    Physics::PhysicsVector3 bodyLocalPositionMeters{};

    // Effective Cl_max * reference area in m^2. Zero represents a valid disabled/no-authority surface.
    float maxEffectiveLiftAreaSquareMeters = 0.0F;
};
}
