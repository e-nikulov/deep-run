#pragma once

#include "Game/PhysicsRenderSync.h"
#include "Game/Submarine/ProductionAnteyAsset.h"

#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Armament
{
struct P700WorldLaunchAnchor final
{
    std::string semanticId;
    Physics::PhysicsVector3 positionMeters{};
    Physics::PhysicsVector3 forwardUnitVector{};
};

// Pure production composition boundary:
// ProductionLaunchAnchor (vessel/model space) -> modelToBody correction -> accepted 2.5D facing ->
// authoritative Jolt body pose. It has no renderer, input, target or missile-state dependency.
[[nodiscard]] inline std::expected<P700WorldLaunchAnchor, std::string> ComposeP700WorldLaunchAnchor(
    const Submarine::ProductionLaunchAnchor& anchor,
    const Physics::PhysicsBodyState& bodyState,
    const Assets::ModelTransform& modelToBody,
    const int longitudinalFacingSign,
    const bool turningAround)
{
    if (turningAround)
    {
        return std::unexpected("P-700 launch anchor is unavailable during the 2.5D turnaround transition");
    }
    if (longitudinalFacingSign != -1 && longitudinalFacingSign != 1)
    {
        return std::unexpected("P-700 launch anchor facing sign must be +1 or -1");
    }
    if (anchor.semanticId.empty() || !anchor.semanticId.starts_with("p700."))
    {
        return std::unexpected("P-700 launch anchor semantic ID is invalid");
    }

    const auto finiteAffine = [](const Assets::ModelTransform& transform) noexcept
    {
        for (const float value : transform.values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        constexpr float tolerance = 1.0e-5F;
        return std::abs(transform.values[3]) <= tolerance &&
               std::abs(transform.values[7]) <= tolerance &&
               std::abs(transform.values[11]) <= tolerance &&
               std::abs(transform.values[15] - 1.0F) <= tolerance;
    };
    if (!finiteAffine(anchor.localTransform) || !finiteAffine(modelToBody))
    {
        return std::unexpected("P-700 launch-anchor transforms must be finite affine matrices");
    }
    if (!std::isfinite(anchor.launchForward.x) || !std::isfinite(anchor.launchForward.y) ||
        !std::isfinite(anchor.launchForward.z))
    {
        return std::unexpected("P-700 launch forward vector must be finite");
    }

    const Assets::ModelVector3 modelPosition{
        .x = anchor.localTransform.values[12],
        .y = anchor.localTransform.values[13],
        .z = anchor.localTransform.values[14]};
    const auto transformPoint = [](const Assets::ModelTransform& transform, const Assets::ModelVector3 point) noexcept
    {
        return Physics::PhysicsVector3{
            .x = transform.values[0] * point.x + transform.values[4] * point.y +
                 transform.values[8] * point.z + transform.values[12],
            .y = transform.values[1] * point.x + transform.values[5] * point.y +
                 transform.values[9] * point.z + transform.values[13],
            .z = transform.values[2] * point.x + transform.values[6] * point.y +
                 transform.values[10] * point.z + transform.values[14]};
    };
    const auto transformVector = [](const Assets::ModelTransform& transform, const Assets::ModelVector3 vector) noexcept
    {
        return Physics::PhysicsVector3{
            .x = transform.values[0] * vector.x + transform.values[4] * vector.y + transform.values[8] * vector.z,
            .y = transform.values[1] * vector.x + transform.values[5] * vector.y + transform.values[9] * vector.z,
            .z = transform.values[2] * vector.x + transform.values[6] * vector.y + transform.values[10] * vector.z};
    };

    Physics::PhysicsVector3 bodyLocalPosition = transformPoint(modelToBody, modelPosition);
    Physics::PhysicsVector3 bodyLocalForward = transformVector(modelToBody, anchor.launchForward);
    if (!bodyLocalPosition.IsFinite() || !bodyLocalForward.IsFinite())
    {
        return std::unexpected("P-700 model-to-body launch composition produced non-finite values");
    }

    const float facing = static_cast<float>(longitudinalFacingSign);
    // Accepted side-view turnaround is a 180-degree rotation around runtime +Y: X/Z change sign, Y is preserved.
    bodyLocalPosition.x *= facing;
    bodyLocalPosition.z *= facing;
    bodyLocalForward.x *= facing;
    bodyLocalForward.z *= facing;

    const float forwardLength = std::sqrt(
        bodyLocalForward.x * bodyLocalForward.x + bodyLocalForward.y * bodyLocalForward.y +
        bodyLocalForward.z * bodyLocalForward.z);
    if (!std::isfinite(forwardLength) || forwardLength <= 1.0e-6F)
    {
        return std::unexpected("P-700 composed launch forward vector has zero/non-finite length");
    }
    bodyLocalForward.x /= forwardLength;
    bodyLocalForward.y /= forwardLength;
    bodyLocalForward.z /= forwardLength;

    const auto worldPosition = TransformBodyLocalPointToWorld(
        bodyState.position, bodyState.orientation, bodyLocalPosition);
    const auto worldForward = RotateBodyLocalVectorToWorld(bodyState.orientation, bodyLocalForward);
    if (!worldPosition || !worldForward || !worldForward->IsFinite())
    {
        return std::unexpected("P-700 launch anchor could not be transformed through the authoritative body pose");
    }
    const float worldForwardLength = std::sqrt(
        worldForward->x * worldForward->x + worldForward->y * worldForward->y + worldForward->z * worldForward->z);
    if (!std::isfinite(worldForwardLength) || std::abs(worldForwardLength - 1.0F) > 1.0e-3F)
    {
        return std::unexpected("P-700 world launch forward vector is not normalized");
    }

    return P700WorldLaunchAnchor{
        .semanticId = anchor.semanticId,
        .positionMeters = *worldPosition,
        .forwardUnitVector = *worldForward};
}
} // namespace DeepRun::Game::Armament
