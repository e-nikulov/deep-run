#include "Game/PhysicsRenderSync.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DeepRun::Game
{
bool IsFinite(const Assets::ModelVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The C2 box proxy is derived from ModelAsset::bounds: size = max - min, halfExtents = size * 0.5.
bool ValidateCollisionBounds(const Assets::ModelBounds& bounds, std::string& message)
{
    if (!IsFinite(bounds.minimum) || !IsFinite(bounds.maximum))
    {
        message = "model bounds are not finite";
        return false;
    }
    for (const char axis : {'x', 'y', 'z'})
    {
        const float minimum = axis == 'x' ? bounds.minimum.x : axis == 'y' ? bounds.minimum.y : bounds.minimum.z;
        const float maximum = axis == 'x' ? bounds.maximum.x : axis == 'y' ? bounds.maximum.y : bounds.maximum.z;
        if (maximum <= minimum)
        {
            message = std::string("model bounds are empty or inverted on the ") + axis + " axis";
            return false;
        }
    }
    message.clear();
    return true;
}

Assets::ModelVector3 BoundsCenter(const Assets::ModelBounds& bounds) noexcept
{
    return {
        (bounds.minimum.x + bounds.maximum.x) * 0.5F,
        (bounds.minimum.y + bounds.maximum.y) * 0.5F,
        (bounds.minimum.z + bounds.maximum.z) * 0.5F};
}

Assets::ModelTransform TranslationTransform(const Assets::ModelVector3& translation) noexcept
{
    Assets::ModelTransform transform{};
    transform.values[12] = translation.x;
    transform.values[13] = translation.y;
    transform.values[14] = translation.z;
    return transform;
}

std::expected<Assets::ModelTransform, std::string> BuildBodyToWorld(
    const Physics::PhysicsBodyState& state)
{
    if (!state.position.IsFinite())
    {
        return std::unexpected("body position is not finite");
    }

    // GetBodyState is expected to deliver a normalized orientation, but the conversion must still reject
    // malformed input instead of silently producing NaN.
    const float lengthSquared = state.orientation.LengthSquared();
    if (!state.orientation.IsFinite() || !std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
    {
        return std::unexpected("body orientation is not a finite non-zero quaternion");
    }

    const float invLength = 1.0F / std::sqrt(lengthSquared);
    const float qx = state.orientation.x * invLength;
    const float qy = state.orientation.y * invLength;
    const float qz = state.orientation.z * invLength;
    const float qw = state.orientation.w * invLength;

    Assets::ModelTransform result{};
    // Rotation part from a unit quaternion (x, y, z, w), stored column-major like ModelTransform. The body
    // origin is the bounds center and the model-to-body correction T(-boundsCenter) is applied by the caller
    // through Multiply(bodyToWorld, modelToBody), so this matrix only maps body space to world space:
    // world = R * bodyLocal + position.
    // Column-major storage: values[column * 4 + row]. The matrix implements v' = q v q^-1, so a positive
    // angle around +Z maps +X to +Y (right-handed world per ADR-0006).
    result.values[0] = 1.0F - 2.0F * (qy * qy + qz * qz);
    result.values[1] = 2.0F * (qx * qy + qw * qz);
    result.values[2] = 2.0F * (qx * qz - qw * qy);
    result.values[4] = 2.0F * (qx * qy - qw * qz);
    result.values[5] = 1.0F - 2.0F * (qx * qx + qz * qz);
    result.values[6] = 2.0F * (qy * qz + qw * qx);
    result.values[8] = 2.0F * (qx * qz + qw * qy);
    result.values[9] = 2.0F * (qy * qz - qw * qx);
    result.values[10] = 1.0F - 2.0F * (qx * qx + qy * qy);
    result.values[12] = state.position.x;
    result.values[13] = state.position.y;
    result.values[14] = state.position.z;
    result.values[15] = 1.0F;

    for (const float value : result.values)
    {
        if (!std::isfinite(value))
        {
            return std::unexpected("body-to-world transform produced non-finite values");
        }
    }
    return result;
}

std::expected<Assets::ModelBounds, std::string> TransformBounds(
    const Assets::ModelBounds& bounds,
    const Assets::ModelTransform& transform)
{
    if (!IsFinite(bounds.minimum) || !IsFinite(bounds.maximum))
    {
        return std::unexpected("bounds are not finite");
    }

    auto element = [](const Assets::ModelTransform& matrix, const std::size_t row, const std::size_t column) noexcept {
        return matrix.values[column * 4 + row];
    };
    for (const float value : transform.values)
    {
        if (!std::isfinite(value))
        {
            return std::unexpected("transform is not finite");
        }
    }

    Assets::ModelVector3 minimum{std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity()};
    Assets::ModelVector3 maximum{-minimum.x, -minimum.y, -minimum.z};
    for (const float x : {bounds.minimum.x, bounds.maximum.x})
    {
        for (const float y : {bounds.minimum.y, bounds.maximum.y})
        {
            for (const float z : {bounds.minimum.z, bounds.maximum.z})
            {
                const Assets::ModelVector3 transformed{
                    element(transform, 0, 0) * x + element(transform, 0, 1) * y + element(transform, 0, 2) * z +
                        element(transform, 0, 3),
                    element(transform, 1, 0) * x + element(transform, 1, 1) * y + element(transform, 1, 2) * z +
                        element(transform, 1, 3),
                    element(transform, 2, 0) * x + element(transform, 2, 1) * y + element(transform, 2, 2) * z +
                        element(transform, 2, 3)};
                if (!IsFinite(transformed))
                {
                    return std::unexpected("transformed bounds corner is not finite");
                }
                minimum.x = std::min(minimum.x, transformed.x);
                minimum.y = std::min(minimum.y, transformed.y);
                minimum.z = std::min(minimum.z, transformed.z);
                maximum.x = std::max(maximum.x, transformed.x);
                maximum.y = std::max(maximum.y, transformed.y);
                maximum.z = std::max(maximum.z, transformed.z);
            }
        }
    }
    return Assets::ModelBounds{.minimum = minimum, .maximum = maximum};
}
} // namespace DeepRun::Game
