#include "Game/Environment/UnderwaterFloraField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr std::uint32_t M3UnderwaterFloraSeed = 0x4D334747U;
constexpr float MinimumPlantHeightMeters = 2.5F;
constexpr float MaximumPlantHeightMeters = 7.0F;
constexpr float MinimumPlantHalfWidthMeters = 0.18F;
constexpr float MaximumPlantHalfWidthMeters = 0.45F;
constexpr float MinimumLayerOffsetMeters = 0.025F;
constexpr float MaximumLayerOffsetMeters = 0.40F;

struct FloraPatch final
{
    float minimumX = 0.0F;
    float maximumX = 0.0F;
    std::uint32_t plantCount = 0U;
};

// Three intentionally separated, gentle-profile patches. They avoid the authored escarpment and stable rock
// placements rather than spreading vegetation uniformly over the whole seabed.
constexpr std::array<FloraPatch, DeepRun::Game::M3UnderwaterFloraPatchCount> FloraPatches{{
    {-286.0F, -242.0F, 18U},
    {-122.0F, -62.0F, 22U},
    {20.0F, 75.0F, 20U}}};

class FixedFloraRandom final
{
public:
    explicit FixedFloraRandom(const std::uint32_t seed) noexcept : state_(seed) {}

    [[nodiscard]] float NextUnit() noexcept
    {
        state_ = state_ * 1'664'525U + 1'013'904'223U;
        return static_cast<float>(state_ >> 8U) * (1.0F / 16'777'216.0F);
    }

private:
    std::uint32_t state_ = 0U;
};

bool IsFinite(const DeepRun::Assets::ModelVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void ExtendBounds(
    DeepRun::Assets::ModelVector3& minimum,
    DeepRun::Assets::ModelVector3& maximum,
    const DeepRun::Assets::ModelVector3& position) noexcept
{
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
}

double TriangleAreaSquared(
    const DeepRun::Assets::ModelVector3& a,
    const DeepRun::Assets::ModelVector3& b,
    const DeepRun::Assets::ModelVector3& c) noexcept
{
    const double ux = static_cast<double>(b.x) - a.x;
    const double uy = static_cast<double>(b.y) - a.y;
    const double uz = static_cast<double>(b.z) - a.z;
    const double vx = static_cast<double>(c.x) - a.x;
    const double vy = static_cast<double>(c.y) - a.y;
    const double vz = static_cast<double>(c.z) - a.z;
    const double crossX = uy * vz - uz * vy;
    const double crossY = uz * vx - ux * vz;
    const double crossZ = ux * vy - uy * vx;
    return crossX * crossX + crossY * crossY + crossZ * crossZ;
}
} // namespace

namespace DeepRun::Game
{
std::expected<UnderwaterFloraField, std::string> BuildUnderwaterFloraField(
    const EnvironmentSectionId& sectionId,
    const SeabedProfileConfig& profile,
    const float referenceSurfaceLevelYMeters)
{
    if (!sectionId.IsValid())
    {
        return std::unexpected("underwater flora requires a valid environment section id");
    }
    if (!std::isfinite(referenceSurfaceLevelYMeters))
    {
        return std::unexpected("underwater flora reference surface level must be finite");
    }
    if (const auto profileProbe = SampleSeabedProfileY(profile, profile.minX); !profileProbe)
    {
        return std::unexpected("underwater flora profile is invalid: " + profileProbe.error());
    }

    const auto assetId = Assets::AssetId::FromPath("environment/flora/" + sectionId.value + ".field");
    if (!assetId)
    {
        return std::unexpected("underwater flora could not form a stable asset id: " + assetId.error());
    }
    Assets::ModelAsset model{.id = std::move(*assetId)};
    model.materials.push_back({
        .name = "UnderwaterFlora",
        .baseColorFactor = {0.08F, 0.18F, 0.055F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 0.95F});

    Assets::MeshPrimitiveData primitive;
    primitive.vertices.reserve(M3UnderwaterFloraPlantCount * M3UnderwaterFloraSegmentsPerPlant * 4U);
    primitive.indices.reserve(M3UnderwaterFloraPlantCount * M3UnderwaterFloraSegmentsPerPlant * 6U);
    std::vector<UnderwaterFloraPlant> plants;
    plants.reserve(M3UnderwaterFloraPlantCount);
    Assets::ModelVector3 boundsMinimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Assets::ModelVector3 boundsMaximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    const float frontSeabedZ = 0.5F * profile.zThicknessMeters;
    FixedFloraRandom random(M3UnderwaterFloraSeed);
    for (std::size_t patchIndex = 0; patchIndex < FloraPatches.size(); ++patchIndex)
    {
        const FloraPatch& patch = FloraPatches[patchIndex];
        if (!std::isfinite(patch.minimumX) || !std::isfinite(patch.maximumX) || patch.maximumX <= patch.minimumX ||
            patch.plantCount == 0U || patch.minimumX < profile.minX || patch.maximumX > profile.maxX)
        {
            return std::unexpected("underwater flora patch is outside the authored profile extent");
        }
        const float patchSpan = patch.maximumX - patch.minimumX;
        for (std::uint32_t plantIndex = 0U; plantIndex < patch.plantCount; ++plantIndex)
        {
            const float lane = (static_cast<float>(plantIndex) + 0.5F) / static_cast<float>(patch.plantCount);
            const float spacing = patchSpan / static_cast<float>(patch.plantCount);
            const float rootX = patch.minimumX + patchSpan * lane +
                                (random.NextUnit() - 0.5F) * spacing * 0.45F;
            const float height = MinimumPlantHeightMeters +
                                 (MaximumPlantHeightMeters - MinimumPlantHeightMeters) * random.NextUnit();
            const float halfWidth = MinimumPlantHalfWidthMeters +
                                    (MaximumPlantHalfWidthMeters - MinimumPlantHalfWidthMeters) * random.NextUnit();
            const float bend = (random.NextUnit() * 2.0F - 1.0F) * (0.25F + 0.35F * random.NextUnit());
            const float rootZ = frontSeabedZ + MinimumLayerOffsetMeters +
                                (MaximumLayerOffsetMeters - MinimumLayerOffsetMeters) * random.NextUnit();
            const auto rootSurfaceY = SampleSeabedProfileY(profile, rootX);
            const auto rootLeftSurfaceY = SampleSeabedProfileY(profile, rootX - halfWidth);
            const auto rootRightSurfaceY = SampleSeabedProfileY(profile, rootX + halfWidth);
            if (!rootSurfaceY || !rootLeftSurfaceY || !rootRightSurfaceY)
            {
                return std::unexpected("underwater flora root profile sample failed");
            }

            const UnderwaterFloraPlant plant{
                .rootPosition = {rootX, *rootSurfaceY + M3UnderwaterFloraRootLiftMeters, rootZ},
                .heightMeters = height,
                .halfWidthMeters = halfWidth,
                .bendMeters = bend,
                .patchIndex = static_cast<std::uint32_t>(patchIndex),
                .segmentCount = M3UnderwaterFloraSegmentsPerPlant};
            if (!IsFinite(plant.rootPosition) || !std::isfinite(height) || !std::isfinite(halfWidth) ||
                !std::isfinite(bend) || plant.rootPosition.y >= referenceSurfaceLevelYMeters ||
                plant.rootPosition.y + height >= referenceSurfaceLevelYMeters)
            {
                return std::unexpected("underwater flora plant exceeds the bounded underwater presentation region");
            }

            for (std::uint32_t segment = 0U; segment < plant.segmentCount; ++segment)
            {
                const float t0 = static_cast<float>(segment) / static_cast<float>(plant.segmentCount);
                const float t1 = static_cast<float>(segment + 1U) / static_cast<float>(plant.segmentCount);
                const auto bladePoint = [&](const float t, const bool left) {
                    const float halfWidthAtT = plant.halfWidthMeters * (1.0F - 0.55F * t);
                    const float x = plant.rootPosition.x + plant.bendMeters * t * t +
                                    (left ? -halfWidthAtT : halfWidthAtT);
                    const float y = plant.rootPosition.y + plant.heightMeters * t;
                    return Assets::ModelVector3{x, y, plant.rootPosition.z};
                };
                Assets::ModelVector3 bottomLeft = bladePoint(t0, true);
                Assets::ModelVector3 bottomRight = bladePoint(t0, false);
                if (segment == 0U)
                {
                    bottomLeft.y = *rootLeftSurfaceY + M3UnderwaterFloraRootLiftMeters;
                    bottomRight.y = *rootRightSurfaceY + M3UnderwaterFloraRootLiftMeters;
                }
                const Assets::ModelVector3 topRight = bladePoint(t1, false);
                const Assets::ModelVector3 topLeft = bladePoint(t1, true);
                const std::array<Assets::ModelVector3, 4> positions{{
                    bottomLeft, bottomRight, topRight, topLeft}};
                const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertices.size());
                for (const Assets::ModelVector3& position : positions)
                {
                    if (!IsFinite(position))
                    {
                        return std::unexpected("underwater flora generated a non-finite vertex");
                    }
                    primitive.vertices.push_back({.position = position, .normal = {0.0F, 0.0F, 1.0F}});
                    ExtendBounds(boundsMinimum, boundsMaximum, position);
                }
                // Counter-clockwise from the +Z side camera: the existing BACK-face model cull keeps this ribbon.
                primitive.indices.insert(
                    primitive.indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
            }
            plants.push_back(plant);
        }
    }

    if (plants.size() != M3UnderwaterFloraPlantCount || primitive.indices.size() % 3U != 0U ||
        primitive.indices.size() / 3U > M3UnderwaterFloraTriangleBudget || !IsFinite(boundsMinimum) ||
        !IsFinite(boundsMaximum) || boundsMinimum.x > boundsMaximum.x || boundsMinimum.y > boundsMaximum.y ||
        boundsMinimum.z > boundsMaximum.z)
    {
        return std::unexpected("underwater flora geometry did not meet its fixed bounded contract");
    }
    for (const std::uint32_t index : primitive.indices)
    {
        if (index >= primitive.vertices.size())
        {
            return std::unexpected("underwater flora generated an out-of-range index");
        }
    }
    for (std::size_t index = 0U; index < primitive.indices.size(); index += 3U)
    {
        const auto& a = primitive.vertices[primitive.indices[index]].position;
        const auto& b = primitive.vertices[primitive.indices[index + 1U]].position;
        const auto& c = primitive.vertices[primitive.indices[index + 2U]].position;
        if (!std::isfinite(TriangleAreaSquared(a, b, c)) || !(TriangleAreaSquared(a, b, c) > 0.0))
        {
            return std::unexpected("underwater flora generated a degenerate triangle");
        }
    }

    primitive.materialIndex = 0U;
    primitive.localBounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    primitive.hasNormals = true;
    model.primitives.push_back(std::move(primitive));
    model.nodes.push_back({.name = "UnderwaterFlora", .localToModel = {}, .primitiveIndices = {0U}});
    model.bounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    return UnderwaterFloraField{
        .renderGeometry = std::move(model),
        .plants = std::move(plants),
        .patchCount = static_cast<std::uint32_t>(FloraPatches.size())};
}
} // namespace DeepRun::Game
