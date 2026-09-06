#pragma once

#include "Game/Environment/EnvironmentSection.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game
{
// M3-H is one fixed Game-owned authored ice field. It is neither an environment-object hierarchy nor an
// ice simulation: these stable records are the shared source for independent render and coarse-collision
// payloads. The generated model is presentation data only and never supplies collision/environment truth.
enum class UnderwaterIceFormationType : std::uint8_t
{
    SurfaceShelf,
    HangingFormation,
    IcebergKeel
};

struct UnderwaterIceInstance final
{
    std::string id;
    UnderwaterIceFormationType type = UnderwaterIceFormationType::SurfaceShelf;
    Assets::ModelVector3 position{};
    Assets::ModelVector3 renderHalfExtents{};
    float rotationRadians = 0.0F;
    bool hasCoarseCollision = false;
    // An authored axis-aligned proxy, deliberately separate from faceted render topology. It is meaningful
    // only when hasCoarseCollision is true.
    Physics::StaticBoxBodyCreateInfo coarseCollision{};
};

struct UnderwaterIceField final
{
    Assets::ModelAsset renderGeometry;
    std::vector<UnderwaterIceInstance> formations;
    // Copies of the enabled authored coarseCollision records. The physics backend owns any later bodies;
    // the field retains no backend handles.
    std::vector<Physics::StaticBoxBodyCreateInfo> collisionBoxes;
};

inline constexpr std::size_t M3UnderwaterIceFormationCount = 3U;
inline constexpr std::size_t M3UnderwaterIceCollisionCount = 2U;
inline constexpr std::size_t M3UnderwaterIceTriangleBudget = 1'000U;

// Builds M3-H's immutable authored ice field relative only to a finite Game-provided mean/reference surface
// value. The field is static: it has no time input and does not query water, waves, simulation, or render APIs.
[[nodiscard]] std::expected<UnderwaterIceField, std::string> BuildUnderwaterIceField(
    const EnvironmentSectionId& sectionId,
    float referenceSurfaceLevelYMeters);
} // namespace DeepRun::Game
