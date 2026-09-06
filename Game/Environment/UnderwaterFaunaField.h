#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Game/Environment/EnvironmentSection.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game
{
// M3-H.1 is a single bounded presentation school. The fish records are immutable local mesh authoring data;
// they are not entities, agents, bodies, or a population system. One school transform is evaluated per frame.
struct UnderwaterFishSchoolPresentationParameters final
{
    float travelMinimumX = -160.0F;
    float travelMaximumX = 160.0F;
    float centerY = -75.0F;
    float centerZ = 0.0F;
    float horizontalSpeedMetersPerSecond = 5.0F;
    float verticalAmplitudeMeters = 1.5F;
    float verticalAngularFrequencyRadiansPerSecond = 0.16F;
};

struct UnderwaterFish final
{
    Assets::ModelVector3 localPosition{};
    float bodyLengthMeters = 0.0F;
    float bodyHeightMeters = 0.0F;
    float headingRadians = 0.0F;
};

struct UnderwaterFaunaField final
{
    Assets::ModelAsset renderGeometry;
    std::vector<UnderwaterFish> fish;
    UnderwaterFishSchoolPresentationParameters presentation{};
};

inline constexpr std::size_t M3UnderwaterFishCount = 24U;
inline constexpr std::size_t M3UnderwaterFishTrianglesPerFish = 3U;
inline constexpr std::size_t M3UnderwaterFishTriangleBudget = 256U;
inline constexpr float M3UnderwaterFishMinimumBodyLengthMeters = 2.4F;
inline constexpr float M3UnderwaterFishMaximumBodyLengthMeters = 3.2F;
inline constexpr float M3UnderwaterFishMinimumBodyHeightMeters = 0.65F;
inline constexpr float M3UnderwaterFishMaximumBodyHeightMeters = 1.0F;
inline constexpr float M3UnderwaterFishMinimumLocalZ = 2.0F;
inline constexpr float M3UnderwaterFishMaximumLocalZ = 5.0F;

// Builds one deterministic low-poly model and immutable local layout. The reference surface is a plain
// composition value used to reject an invalid authored placement; this function has no clock or world query.
[[nodiscard]] std::expected<UnderwaterFaunaField, std::string> BuildUnderwaterFaunaField(
    const EnvironmentSectionId& sectionId,
    float referenceSurfaceLevelYMeters);

// PresentationTime-only school motion. This returns one finite affine translation for the whole field;
// it is visual presentation only, not authoritative world state, and must never be used by Simulation,
// buoyancy, physics, sonar, or gameplay-depth logic.
[[nodiscard]] std::expected<Assets::ModelTransform, std::string> EvaluateUnderwaterFishSchoolPresentation(
    const UnderwaterFishSchoolPresentationParameters& parameters,
    double presentationTimeSeconds);
} // namespace DeepRun::Game
