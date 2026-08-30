#include "Simulation/Marine/ControlSurfaceSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DeepRun::Marine
{
namespace
{
struct Vector3Double final
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct UnitQuaternion final
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

ControlSurfaceError MakeError(const ControlSurfaceErrorCode code, const std::string& message)
{
    return ControlSurfaceError{code, message};
}

bool FitsInFloat(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
}

bool FitsInFloat(const Vector3Double& value) noexcept
{
    return FitsInFloat(value.x) && FitsInFloat(value.y) && FitsInFloat(value.z);
}

Vector3Double ToDouble(const Physics::PhysicsVector3& value) noexcept
{
    return {static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z)};
}

Vector3Double RotateVector(const UnitQuaternion& q, const Vector3Double& value) noexcept
{
    const double tx = 2.0 * (q.y * value.z - q.z * value.y);
    const double ty = 2.0 * (q.z * value.x - q.x * value.z);
    const double tz = 2.0 * (q.x * value.y - q.y * value.x);
    return {
        value.x + q.w * tx + (q.y * tz - q.z * ty),
        value.y + q.w * ty + (q.z * tx - q.x * tz),
        value.z + q.w * tz + (q.x * ty - q.y * tx)};
}

Vector3Double RotateInverse(const UnitQuaternion& orientation, const Physics::PhysicsVector3& worldValue) noexcept
{
    return RotateVector(
        {-orientation.x, -orientation.y, -orientation.z, orientation.w},
        ToDouble(worldValue));
}

std::expected<UnitQuaternion, ControlSurfaceError> Normalize(
    const Physics::PhysicsQuaternion& input)
{
    if (!input.IsFinite())
    {
        return std::unexpected(
            MakeError(ControlSurfaceErrorCode::InvalidKinematics, "world orientation must be finite"));
    }

    // Scale first so every finite non-zero float quaternion can be normalized without squaring overflow.
    const double maximumComponent = (std::max)(
        {(std::abs)(static_cast<double>(input.x)),
         (std::abs)(static_cast<double>(input.y)),
         (std::abs)(static_cast<double>(input.z)),
         (std::abs)(static_cast<double>(input.w))});
    if (maximumComponent == 0.0)
    {
        return std::unexpected(
            MakeError(ControlSurfaceErrorCode::InvalidKinematics, "world orientation must be non-zero"));
    }
    const double scaledX = static_cast<double>(input.x) / maximumComponent;
    const double scaledY = static_cast<double>(input.y) / maximumComponent;
    const double scaledZ = static_cast<double>(input.z) / maximumComponent;
    const double scaledW = static_cast<double>(input.w) / maximumComponent;
    const double scaledLength =
        std::sqrt(scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ + scaledW * scaledW);
    const UnitQuaternion normalized{
        scaledX / scaledLength,
        scaledY / scaledLength,
        scaledZ / scaledLength,
        scaledW / scaledLength};
    if (!std::isfinite(normalized.x) || !std::isfinite(normalized.y) ||
        !std::isfinite(normalized.z) || !std::isfinite(normalized.w))
    {
        return std::unexpected(
            MakeError(ControlSurfaceErrorCode::NonFiniteResult, "normalized world orientation is not finite"));
    }
    return normalized;
}
} // namespace

std::expected<ControlSurfaceResult, ControlSurfaceError> ControlSurfaceSystem::Calculate(
    const WaterBody& water,
    const ControlSurfaceComponent& component,
    const ControlSurfaceKinematics& kinematics,
    const float deflectionFraction)
{
    if (!component.bodyLocalPositionMeters.IsFinite() ||
        !std::isfinite(component.maxEffectiveLiftAreaSquareMeters) ||
        component.maxEffectiveLiftAreaSquareMeters < 0.0F)
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::InvalidConfiguration,
            "control surface position and effective lift area must be finite, and area must be non-negative"));
    }
    if (!std::isfinite(deflectionFraction) || deflectionFraction < -1.0F || deflectionFraction > 1.0F)
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::InvalidDeflection,
            "deflectionFraction must be finite and within [-1, 1]"));
    }
    if (!kinematics.bodyWorldPositionMeters.IsFinite() ||
        !kinematics.worldLinearVelocityMetersPerSecond.IsFinite())
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::InvalidKinematics,
            "body world position and world linear velocity must be finite"));
    }
    const auto orientation = Normalize(kinematics.worldOrientation);
    if (!orientation)
    {
        return std::unexpected(orientation.error());
    }

    const Vector3Double bodyVelocity =
        RotateInverse(*orientation, kinematics.worldLinearVelocityMetersPerSecond);
    if (!FitsInFloat(bodyVelocity))
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::NonFiniteResult,
            "body-local velocity is outside finite float range"));
    }
    double forwardSpeed = bodyVelocity.x;
    if (forwardSpeed == 0.0)
    {
        forwardSpeed = 0.0;
    }

    const double density = static_cast<double>(water.Config().densityKgPerCubicMeter);
    const double effectiveArea = static_cast<double>(component.maxEffectiveLiftAreaSquareMeters);
    const double deflection = static_cast<double>(deflectionFraction);
    const double signedSpeedTerm = forwardSpeed * std::abs(forwardSpeed);
    if (!std::isfinite(signedSpeedTerm))
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::NonFiniteResult,
            "signed body-forward speed term is not finite"));
    }

    double localLiftY = 0.0;
    if (effectiveArea != 0.0 && deflection != 0.0 && forwardSpeed != 0.0)
    {
        localLiftY = 0.5 * density * effectiveArea * deflection * signedSpeedTerm;
    }
    const Vector3Double localForce{0.0, localLiftY, 0.0};
    if (!FitsInFloat(localForce))
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::NonFiniteResult,
            "body-local control force is outside finite float range"));
    }

    const Vector3Double worldForce = RotateVector(*orientation, localForce);
    const Vector3Double rotatedPosition =
        RotateVector(*orientation, ToDouble(component.bodyLocalPositionMeters));
    const Vector3Double worldPosition{
        static_cast<double>(kinematics.bodyWorldPositionMeters.x) + rotatedPosition.x,
        static_cast<double>(kinematics.bodyWorldPositionMeters.y) + rotatedPosition.y,
        static_cast<double>(kinematics.bodyWorldPositionMeters.z) + rotatedPosition.z};
    if (!FitsInFloat(worldForce) || !FitsInFloat(worldPosition))
    {
        return std::unexpected(MakeError(
            ControlSurfaceErrorCode::NonFiniteResult,
            "world control force or application point is outside finite float range"));
    }

    return ControlSurfaceResult{
        .worldPositionMeters = {
            static_cast<float>(worldPosition.x),
            static_cast<float>(worldPosition.y),
            static_cast<float>(worldPosition.z)},
        .bodyForwardSpeedMetersPerSecond = static_cast<float>(forwardSpeed),
        .deflectionFraction = deflectionFraction == 0.0F ? 0.0F : deflectionFraction,
        .forceNewtons = {
            static_cast<float>(worldForce.x),
            static_cast<float>(worldForce.y),
            static_cast<float>(worldForce.z)}};
}
}
