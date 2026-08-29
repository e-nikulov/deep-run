#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Physics/PhysicsTypes.h"

#include <expected>
#include <string>

namespace DeepRun::Game
{
// Minimal cross-system math helpers for M2 Slice C2: converting an authoritative physics body snapshot into
// render transforms. These are pure functions with no Jolt, D3D12, or gameplay dependencies so they can be
// tested directly (see Tests/TestMain.cpp). They intentionally do not form a generic math engine.

[[nodiscard]] bool IsFinite(const Assets::ModelVector3& value) noexcept;

// Validates the source bounds for the C2 box collision proxy: finite and strictly positive size on every axis.
[[nodiscard]] bool ValidateCollisionBounds(const Assets::ModelBounds& bounds, std::string& message);

// (minimum + maximum) * 0.5 per axis. The asset origin is NOT assumed to coincide with this center.
[[nodiscard]] Assets::ModelVector3 BoundsCenter(const Assets::ModelBounds& bounds) noexcept;

[[nodiscard]] Assets::ModelTransform TranslationTransform(const Assets::ModelVector3& translation) noexcept;

// Converts a copied PhysicsBodyState into an Assets::ModelTransform-compatible body-to-world matrix.
// Quaternion component order is x, y, z, w (ADR-0007); the world convention is right-handed with +X right,
// +Y up, and +Z toward camera (ADR-0006). The result is column-major like ModelTransform. Malformed input
// (non-finite position or a zero/non-finite quaternion) is rejected instead of producing NaN.
[[nodiscard]] std::expected<Assets::ModelTransform, std::string> BuildBodyToWorld(
    const Physics::PhysicsBodyState& state,
    const Assets::ModelVector3& boundsCenter);

// Transforms the 8 corners of a model-space AABB through an affine transform and rebuilds a world-space AABB.
[[nodiscard]] std::expected<Assets::ModelBounds, std::string> TransformBounds(
    const Assets::ModelBounds& bounds,
    const Assets::ModelTransform& transform);
}
