#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <vector>

namespace DeepRun::Marine
{
// One buoyancy point of a marine object (M2 Slice E2).
//
// bodyLocalPositionMeters is in BODY-LOCAL space relative to the rigid-body origin used by the body pose —
// not model/mesh/node/render-pivot space. The displaced-water model is deliberately separate from any
// collision proxy: point volumes are configured explicitly and never derived from geometry bounds.
struct BuoyancyPoint final
{
    // Body-local offset of this point (meters).
    Physics::PhysicsVector3 bodyLocalPositionMeters{};

    // Share of the object's total potential displaced volume owned by this point (m^3, strictly positive).
    // The sum over all points IS the total potential displaced volume — there is no separate total field
    // that could drift apart from it.
    float displacedVolumeCubicMeters = 0.0F;

    // Half-height of the linear partial-submersion transition around this point's center (meters, strictly
    // positive): fraction 0 at h above the surface, 0.5 on it, 1 at h below. A zero value would create a
    // step discontinuity / division-by-zero semantics, so it is rejected, never corrected.
    float submersionHalfHeightMeters = 0.0F;
};

// Simple simulation data carrier: the buoyancy points owned by one object (M2 Slice E2). No scene/ECS
// registration, no body handle, no enabled state — an object without buoyancy simply has none of these.
struct BuoyancyComponent final
{
    std::vector<BuoyancyPoint> points;
};
}
