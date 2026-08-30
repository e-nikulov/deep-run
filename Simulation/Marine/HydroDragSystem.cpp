#include "Simulation/Marine/HydroDragSystem.h"

#include <cmath>
#include <limits>
#include <string_view>

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

HydroDragError MakeError(const HydroDragErrorCode code, const std::string& message)
{
    return HydroDragError{code, message};
}

bool FitsInFloat(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
}

bool FitsInFloat(const Vector3Double& value) noexcept
{
    return FitsInFloat(value.x) && FitsInFloat(value.y) && FitsInFloat(value.z);
}

bool ValidateCoefficients(
    const Physics::PhysicsVector3& coefficients,
    const std::string_view fieldName,
    HydroDragError* error)
{
    if (!coefficients.IsFinite() || coefficients.x < 0.0F || coefficients.y < 0.0F || coefficients.z < 0.0F)
    {
        *error = MakeError(
            HydroDragErrorCode::InvalidConfiguration,
            std::string(fieldName) + " must contain finite, non-negative coefficients");
        return false;
    }
    return true;
}

Vector3Double ToDouble(const Physics::PhysicsVector3& value) noexcept
{
    return {static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z)};
}

// q * v * q^-1 for a normalized quaternion in DeepRun x,y,z,w order.
Vector3Double RotateVector(const UnitQuaternion& q, const Vector3Double& value) noexcept
{
    const double cx = q.y * value.z - q.z * value.y;
    const double cy = q.z * value.x - q.x * value.z;
    const double cz = q.x * value.y - q.y * value.x;
    const double tx = 2.0 * cx;
    const double ty = 2.0 * cy;
    const double tz = 2.0 * cz;

    return {
        value.x + q.w * tx + (q.y * tz - q.z * ty),
        value.y + q.w * ty + (q.z * tx - q.x * tz),
        value.z + q.w * tz + (q.x * ty - q.y * tx)};
}

Vector3Double RotateInverse(const UnitQuaternion& orientation, const Physics::PhysicsVector3& worldValue) noexcept
{
    return RotateVector(
        UnitQuaternion{-orientation.x, -orientation.y, -orientation.z, orientation.w},
        ToDouble(worldValue));
}

double AxisDrag(const double density, const double coefficient, const double velocity) noexcept
{
    // Keep naturally exact zero output and avoid manufacturing a negative signed zero.
    if (coefficient == 0.0 || velocity == 0.0)
    {
        return 0.0;
    }
    return -0.5 * density * coefficient * velocity * std::abs(velocity);
}

Vector3Double CalculateLocalDrag(
    const double density,
    const Physics::PhysicsVector3& coefficients,
    const Vector3Double& velocity) noexcept
{
    return {
        AxisDrag(density, static_cast<double>(coefficients.x), velocity.x),
        AxisDrag(density, static_cast<double>(coefficients.y), velocity.y),
        AxisDrag(density, static_cast<double>(coefficients.z), velocity.z)};
}
} // namespace

std::expected<HydroDragResult, HydroDragError> HydroDragSystem::Calculate(
    const WaterBody& water,
    const HydroDragComponent& component,
    const HydroDragState& state)
{
    HydroDragError validationError{};
    if (!ValidateCoefficients(
            component.linearEffectiveAreaSquareMeters,
            "linearEffectiveAreaSquareMeters",
            &validationError) ||
        !ValidateCoefficients(
            component.angularEffectiveMomentMeters5,
            "angularEffectiveMomentMeters5",
            &validationError))
    {
        return std::unexpected(validationError);
    }

    if (!state.worldLinearVelocityMetersPerSecond.IsFinite())
    {
        return std::unexpected(MakeError(HydroDragErrorCode::InvalidState, "world linear velocity must be finite"));
    }
    if (!state.worldAngularVelocityRadiansPerSecond.IsFinite())
    {
        return std::unexpected(MakeError(HydroDragErrorCode::InvalidState, "world angular velocity must be finite"));
    }
    if (!state.worldOrientation.IsFinite())
    {
        return std::unexpected(MakeError(HydroDragErrorCode::InvalidState, "world orientation must be finite"));
    }

    const Physics::PhysicsQuaternion& inputOrientation = state.worldOrientation;
    const double lengthSquared = static_cast<double>(inputOrientation.x) * inputOrientation.x +
                                 static_cast<double>(inputOrientation.y) * inputOrientation.y +
                                 static_cast<double>(inputOrientation.z) * inputOrientation.z +
                                 static_cast<double>(inputOrientation.w) * inputOrientation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared == 0.0)
    {
        return std::unexpected(MakeError(HydroDragErrorCode::InvalidState, "world orientation must be non-zero"));
    }

    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    const UnitQuaternion orientation{
        static_cast<double>(inputOrientation.x) * inverseLength,
        static_cast<double>(inputOrientation.y) * inverseLength,
        static_cast<double>(inputOrientation.z) * inverseLength,
        static_cast<double>(inputOrientation.w) * inverseLength};
    if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) || !std::isfinite(orientation.z) ||
        !std::isfinite(orientation.w))
    {
        return std::unexpected(
            MakeError(HydroDragErrorCode::NonFiniteResult, "normalized world orientation is not finite"));
    }

    const Vector3Double localLinearVelocity =
        RotateInverse(orientation, state.worldLinearVelocityMetersPerSecond);
    const Vector3Double localAngularVelocity =
        RotateInverse(orientation, state.worldAngularVelocityRadiansPerSecond);
    if (!FitsInFloat(localLinearVelocity) || !FitsInFloat(localAngularVelocity))
    {
        return std::unexpected(
            MakeError(HydroDragErrorCode::NonFiniteResult, "body-local velocity is outside finite float range"));
    }

    // WaterBody construction validates density; F1 consumes that retained environmental value directly and
    // does not sample surface depth because its explicit contract is a fully immersed body in stationary water.
    const double density = static_cast<double>(water.Config().densityKgPerCubicMeter);
    const Vector3Double localForce =
        CalculateLocalDrag(density, component.linearEffectiveAreaSquareMeters, localLinearVelocity);
    const Vector3Double localTorque =
        CalculateLocalDrag(density, component.angularEffectiveMomentMeters5, localAngularVelocity);
    if (!FitsInFloat(localForce) || !FitsInFloat(localTorque))
    {
        return std::unexpected(
            MakeError(HydroDragErrorCode::NonFiniteResult, "body-local drag output is outside finite float range"));
    }

    const Vector3Double worldForce = RotateVector(orientation, localForce);
    const Vector3Double worldTorque = RotateVector(orientation, localTorque);
    if (!FitsInFloat(worldForce) || !FitsInFloat(worldTorque))
    {
        return std::unexpected(
            MakeError(HydroDragErrorCode::NonFiniteResult, "world drag output is outside finite float range"));
    }

    return HydroDragResult{
        .forceNewtons = {
            static_cast<float>(worldForce.x),
            static_cast<float>(worldForce.y),
            static_cast<float>(worldForce.z)},
        .torqueNewtonMeters = {
            static_cast<float>(worldTorque.x),
            static_cast<float>(worldTorque.y),
            static_cast<float>(worldTorque.z)}};
}
}
