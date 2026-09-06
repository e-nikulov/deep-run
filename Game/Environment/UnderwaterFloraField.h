#pragma once

#include "Game/Environment/EnvironmentSection.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game
{
// M3-G is one fixed Game-owned presentation field, not an ecosystem, entity, or instancing framework.
// Each record retains the authored-profile contact used to build its immutable render geometry so headless
// tests can validate placement without inspecting generated terrain vertices.
struct UnderwaterFloraPlant final
{
    Assets::ModelVector3 rootPosition{};
    float heightMeters = 0.0F;
    float halfWidthMeters = 0.0F;
    float bendMeters = 0.0F;
    std::uint32_t patchIndex = 0U;
    std::uint32_t segmentCount = 0U;
};

struct UnderwaterFloraField final
{
    Assets::ModelAsset renderGeometry;
    std::vector<UnderwaterFloraPlant> plants;
    std::uint32_t patchCount = 0U;
};

inline constexpr std::size_t M3UnderwaterFloraPlantCount = 60U;
inline constexpr std::size_t M3UnderwaterFloraPatchCount = 3U;
inline constexpr std::uint32_t M3UnderwaterFloraSegmentsPerPlant = 4U;
inline constexpr float M3UnderwaterFloraRootLiftMeters = 0.02F;
inline constexpr float M3UnderwaterFloraRootContactToleranceMeters = 0.025F;
inline constexpr std::size_t M3UnderwaterFloraTriangleBudget = 1'000U;

// Builds the one deterministic M3-G flora field from the same authored profile passed to BuildSeabedSection.
// referenceSurfaceLevelYMeters is a Game-owned scene value used only to reject geometry that would breach the
// canonical presentation surface; this module does not query or include water, physics, simulation, or render APIs.
[[nodiscard]] std::expected<UnderwaterFloraField, std::string> BuildUnderwaterFloraField(
    const EnvironmentSectionId& sectionId,
    const SeabedProfileConfig& profile,
    float referenceSurfaceLevelYMeters);
} // namespace DeepRun::Game
