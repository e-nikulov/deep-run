#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Marine/BuoyancyComponent.h"
#include "Simulation/Marine/WaterBody.h"

#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Marine
{
// Recoverable error for the pure buoyancy calculation (M2 Slice E2). Programming invariants still use
// assertions; invalid configuration/pose/environment data and non-finite derived results are reported,
// never silently corrected or clamped to a finite sentinel.
enum class BuoyancyErrorCode
{
    InvalidConfiguration, // empty point set or an invalid per-point value
    InvalidPose,          // non-finite world position or zero/non-finite orientation quaternion
    InvalidGravity,       // gravity magnitude not finite and strictly positive
    NonFiniteResult,      // a derived value (world point, depth, fraction, volume, force, total) overflowed
};

struct BuoyancyError final
{
    BuoyancyErrorCode code = BuoyancyErrorCode::InvalidConfiguration;
    std::string message;
};

// Marine-owned body pose value: the world position and orientation of the rigid-body origin. This is a
// plain value, deliberately independent from any rigid-body lifecycle or handle API. q and -q denote the
// same rotation, and a finite non-unit quaternion is normalized internally — callers never have to
// pre-normalize.
struct BuoyancyPose final
{
    Physics::PhysicsVector3 worldPositionMeters{};
    Physics::PhysicsQuaternion worldOrientation{}; // x, y, z, w order
};

// Per-point output. One input point always yields exactly one result in the same order — dry points are
// kept with fraction/volume/force zero so consumers can iterate all outputs uniformly (consistent with the
// E1 true zero-force no-op).
struct BuoyancyPointResult final
{
    Physics::PhysicsVector3 worldPositionMeters{};
    float signedDepthMeters = 0.0F; // from WaterBody truth, never recomputed here
    float submergedFraction = 0.0F; // [0, 1]
    float submergedVolumeCubicMeters = 0.0F;
    Physics::PhysicsVector3 forceNewtons{};
};

// Aggregate result: per-point results in input order plus the sums of those exact per-point values. No
// torque and no center-of-buoyancy are derived here — moment response belongs to rigid-body integration.
struct BuoyancyResult final
{
    std::vector<BuoyancyPointResult> points;
    Physics::PhysicsVector3 totalForceNewtons{};
    float totalSubmergedVolumeCubicMeters = 0.0F;
};

// Pure multi-point buoyancy model and force calculation (M2 Slice E2). Stateless: the same inputs always
// produce the same outputs, with no time step, velocity, mass or damping involved — this is an
// instantaneous hydrostatic evaluation, not a simulation step, and it applies nothing to anything.
class BuoyancySystem final
{
public:
    // Computes per-point submersion and Archimedes force for one body pose against one water body.
    //
    // Contract (per point, in input order):
    //   worldPoint = pose.worldPositionMeters + Rotate(pose.worldOrientation, bodyLocalPositionMeters)
    //   sample     = WaterBody::Sample(worldPoint)  (signed depth and normal come only from here)
    //   fraction   = clamp((sample.signedDepthMeters + h) / (2h), 0, 1), h = submersionHalfHeightMeters
    //   volume     = displacedVolumeCubicMeters * fraction
    //   force      = sample.surfaceNormal * densityKgPerCubicMeter * gravityMagnitude * volume
    //
    // Invalid configuration/pose/gravity and any non-finite derived value are recoverable errors; nothing
    // is clamped to a finite sentinel.
    [[nodiscard]] static std::expected<BuoyancyResult, BuoyancyError> Calculate(
        const WaterBody& water,
        const BuoyancyComponent& component,
        const BuoyancyPose& pose,
        float gravityMagnitudeMetersPerSecondSquared);
};
}
