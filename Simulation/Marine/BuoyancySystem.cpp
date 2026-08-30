#include "Simulation/Marine/BuoyancySystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DeepRun::Marine
{
namespace
{
BuoyancyError MakeError(const BuoyancyErrorCode code, const std::string& message)
{
    return BuoyancyError{code, message};
}

// Unit quaternion kept in double precision: the input float components are normalized once, and every
// derived value is computed from these doubles so intermediate rounding never feeds back into itself.
struct UnitQuaternion final
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

// Rotates a body-local vector by a unit quaternion in DeepRun x,y,z,w order: v' = q v q^-1, evaluated as
// t = 2 * (q.xyz x v); v' = v + w * t + (q.xyz x t). A positive angle around +Z maps +X to +Y — the same
// right-handed convention used by every other DeepRun transform.
void RotateVector(const UnitQuaternion& q, const Physics::PhysicsVector3& local, double* out) noexcept
{
    const double vx = static_cast<double>(local.x);
    const double vy = static_cast<double>(local.y);
    const double vz = static_cast<double>(local.z);

    const double cx = q.y * vz - q.z * vy;
    const double cy = q.z * vx - q.x * vz;
    const double cz = q.x * vy - q.y * vx;
    const double tx = 2.0 * cx;
    const double ty = 2.0 * cy;
    const double tz = 2.0 * cz;

    const double dx = q.y * tz - q.z * ty;
    const double dy = q.z * tx - q.x * tz;
    const double dz = q.x * ty - q.y * tx;

    out[0] = vx + q.w * tx + dx;
    out[1] = vy + q.w * ty + dy;
    out[2] = vz + q.w * tz + dz;
}

// A derived value is usable only if it is finite AND representable as a finite float: the public result
// types are float, so an in-range-double but out-of-float-range value must be rejected, never clamped to
// FLT_MAX.
bool FitsInFloat(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
}

bool ValidatePoint(const BuoyancyPoint& point, const std::size_t index, BuoyancyError* error)
{
    if (!point.bodyLocalPositionMeters.IsFinite())
    {
        *error = MakeError(BuoyancyErrorCode::InvalidConfiguration,
                           "buoyancy point " + std::to_string(index) + ": bodyLocalPositionMeters must be finite");
        return false;
    }
    if (!std::isfinite(point.displacedVolumeCubicMeters) || point.displacedVolumeCubicMeters <= 0.0F)
    {
        *error = MakeError(BuoyancyErrorCode::InvalidConfiguration, "buoyancy point " + std::to_string(index) +
                                                                         ": displacedVolumeCubicMeters must be finite and strictly positive");
        return false;
    }
    if (!std::isfinite(point.submersionHalfHeightMeters) || point.submersionHalfHeightMeters <= 0.0F)
    {
        *error = MakeError(BuoyancyErrorCode::InvalidConfiguration, "buoyancy point " + std::to_string(index) +
                                                                         ": submersionHalfHeightMeters must be finite and strictly positive");
        return false;
    }
    return true;
}
} // namespace

std::expected<BuoyancyResult, BuoyancyError> BuoyancySystem::Calculate(
    const WaterBody& water,
    const BuoyancyComponent& component,
    const BuoyancyPose& pose,
    const float gravityMagnitudeMetersPerSecondSquared)
{
    // --- Configuration: every point must be individually valid; no silent correction. -------------------
    if (component.points.empty())
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::InvalidConfiguration, "buoyancy point set is empty"));
    }

    for (std::size_t index = 0; index < component.points.size(); ++index)
    {
        BuoyancyError error{};
        if (!ValidatePoint(component.points[index], index, &error))
        {
            return std::unexpected(error);
        }
    }

    // --- Pose: finite world position; finite non-zero quaternion (normalized internally). ----------------
    if (!pose.worldPositionMeters.IsFinite())
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::InvalidPose, "body world position must be finite"));
    }

    const Physics::PhysicsQuaternion& orientation = pose.worldOrientation;
    if (!orientation.IsFinite())
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::InvalidPose, "orientation quaternion must be finite"));
    }
    const double lengthSquared = static_cast<double>(orientation.x) * orientation.x +
                                 static_cast<double>(orientation.y) * orientation.y +
                                 static_cast<double>(orientation.z) * orientation.z +
                                 static_cast<double>(orientation.w) * orientation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared == 0.0)
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::InvalidPose, "orientation quaternion must be non-zero"));
    }

    const double invLength = 1.0 / std::sqrt(lengthSquared);
    const UnitQuaternion unit{static_cast<double>(orientation.x) * invLength,
                              static_cast<double>(orientation.y) * invLength,
                              static_cast<double>(orientation.z) * invLength,
                              static_cast<double>(orientation.w) * invLength};
    if (!std::isfinite(unit.x) || !std::isfinite(unit.y) || !std::isfinite(unit.z) || !std::isfinite(unit.w))
    {
        return std::unexpected(
            MakeError(BuoyancyErrorCode::NonFiniteResult, "normalized orientation quaternion is not finite"));
    }

    // --- Gravity: explicit positive finite scalar; never hard-coded. --------------------------------------
    if (!std::isfinite(gravityMagnitudeMetersPerSecondSquared) || gravityMagnitudeMetersPerSecondSquared <= 0.0F)
    {
        return std::unexpected(
            MakeError(BuoyancyErrorCode::InvalidGravity, "gravity magnitude must be finite and strictly positive"));
    }

    // Water density comes exclusively from the water body's validated configuration — never duplicated here.
    const double density = static_cast<double>(water.Config().densityKgPerCubicMeter);
    const double gravity = static_cast<double>(gravityMagnitudeMetersPerSecondSquared);

    BuoyancyResult result;
    result.points.resize(component.points.size());

    double totalForceX = 0.0;
    double totalForceY = 0.0;
    double totalForceZ = 0.0;
    double totalSubmergedVolume = 0.0;

    for (std::size_t index = 0; index < component.points.size(); ++index)
    {
        const BuoyancyPoint& point = component.points[index];

        // Body-local -> world: position + Rotate(orientation, local). Computed in double so a finite-but-
        // extreme input overflows here (and is rejected) instead of producing an inf float silently.
        double rotated[3];
        RotateVector(unit, point.bodyLocalPositionMeters, rotated);
        const double worldX = static_cast<double>(pose.worldPositionMeters.x) + rotated[0];
        const double worldY = static_cast<double>(pose.worldPositionMeters.y) + rotated[1];
        const double worldZ = static_cast<double>(pose.worldPositionMeters.z) + rotated[2];
        if (!FitsInFloat(worldX) || !FitsInFloat(worldY) || !FitsInFloat(worldZ))
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": world position overflowed"));
        }
        const Physics::PhysicsVector3 worldPosition{static_cast<float>(worldX), static_cast<float>(worldY),
                                                    static_cast<float>(worldZ)};

        // Signed depth and surface normal come only from the water body's canonical sample.
        const auto sample = water.Sample(worldPosition);
        if (!sample)
        {
            // Unreachable after the finiteness check above (flat water rejects only non-finite positions),
            // but never propagate a failed sample into derived values.
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": water sample failed"));
        }
        if (!std::isfinite(sample->signedDepthMeters) || !sample->surfaceNormal.IsFinite())
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult,
                "buoyancy point " + std::to_string(index) + ": water sample produced a non-finite result"));
        }

        // Linear partial-submersion approximation: 0 at h above the surface, 0.5 on it, 1 at h below.
        const double depth = static_cast<double>(sample->signedDepthMeters);
        const double halfHeight = static_cast<double>(point.submersionHalfHeightMeters);
        const double submergedFraction = std::clamp((depth + halfHeight) / (2.0 * halfHeight), 0.0, 1.0);
        if (!std::isfinite(submergedFraction))
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": submerged fraction is not finite"));
        }

        const double submergedVolume = static_cast<double>(point.displacedVolumeCubicMeters) * submergedFraction;
        if (!FitsInFloat(submergedVolume))
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": submerged volume overflowed"));
        }

        // Archimedes: instantaneous hydrostatic force along the surface normal. No dt, velocity, mass or
        // damping — this is a pure force evaluation, not an integration step.
        const double forceMagnitude = density * gravity * submergedVolume;
        if (!FitsInFloat(forceMagnitude))
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": force magnitude overflowed"));
        }

        const Physics::PhysicsVector3& normal = sample->surfaceNormal;
        const double forceX = static_cast<double>(normal.x) * forceMagnitude;
        const double forceY = static_cast<double>(normal.y) * forceMagnitude;
        const double forceZ = static_cast<double>(normal.z) * forceMagnitude;
        if (!FitsInFloat(forceX) || !FitsInFloat(forceY) || !FitsInFloat(forceZ))
        {
            return std::unexpected(MakeError(
                BuoyancyErrorCode::NonFiniteResult, "buoyancy point " + std::to_string(index) + ": force vector overflowed"));
        }

        result.points[index] = BuoyancyPointResult{
            .worldPositionMeters = worldPosition,
            .signedDepthMeters = sample->signedDepthMeters,
            .submergedFraction = static_cast<float>(submergedFraction),
            .submergedVolumeCubicMeters = static_cast<float>(submergedVolume),
            .forceNewtons = {static_cast<float>(forceX), static_cast<float>(forceY), static_cast<float>(forceZ)}};

        // Published per-point floats are authoritative: E3 will apply these exact force values. Aggregate
        // totals are therefore accumulated from the stored outputs, not from their higher-precision
        // intermediates, so diagnostics describe the same forces and volumes consumers observe.
        const BuoyancyPointResult& published = result.points[index];
        totalForceX += static_cast<double>(published.forceNewtons.x);
        totalForceY += static_cast<double>(published.forceNewtons.y);
        totalForceZ += static_cast<double>(published.forceNewtons.z);
        totalSubmergedVolume += static_cast<double>(published.submergedVolumeCubicMeters);
    }

    // Totals are sums of the published per-point result values. Accumulation stays in double, and a total
    // is rejected (not clamped) if the final value leaves the finite float range.
    if (!FitsInFloat(totalForceX) || !FitsInFloat(totalForceY) || !FitsInFloat(totalForceZ))
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::NonFiniteResult, "total buoyant force overflowed"));
    }
    if (!std::isfinite(totalSubmergedVolume) || !FitsInFloat(totalSubmergedVolume))
    {
        return std::unexpected(MakeError(BuoyancyErrorCode::NonFiniteResult, "total submerged volume overflowed"));
    }

    result.totalForceNewtons = {static_cast<float>(totalForceX), static_cast<float>(totalForceY),
                                static_cast<float>(totalForceZ)};
    result.totalSubmergedVolumeCubicMeters = static_cast<float>(totalSubmergedVolume);
    return result;
}
}
