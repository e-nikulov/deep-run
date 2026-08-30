#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Physics/PhysicsTypes.h"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace DeepRun::Game
{
inline constexpr std::string_view M2PrototypePropellerNodeName = "SM_Submarine_Prototype_Propeller";

// Resolves the one canonical presentation node and validates its authored hub against independent Game
// physics tuning. The asset is a validation input only; it never supplies the authoritative propulsor point.
[[nodiscard]] std::expected<std::size_t, std::string> ResolveM2PrototypePropellerNode(
    const Assets::ModelAsset& model,
    const Assets::ModelVector3& assetBoundsCenter,
    const Physics::PhysicsVector3& expectedBodyLocalHubMeters,
    float alignmentToleranceMeters);

// Presentation-only trapezoidal integration from authoritative old/new shaft RPM, wrapped to [0, 2*pi).
[[nodiscard]] std::expected<float, std::string> AdvancePropellerPresentationAngle(
    float currentAngleRadians,
    float previousShaftRpm,
    float nextShaftRpm,
    float fixedDeltaSeconds);

// Finite affine right-handed rotation about local +X. Positive angles map local +Y toward local +Z.
[[nodiscard]] std::expected<Assets::ModelTransform, std::string> RotationXTransform(float angleRadians);
}
