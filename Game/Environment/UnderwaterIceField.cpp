#include "Game/Environment/UnderwaterIceField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace
{
using DeepRun::Assets::ModelVector3;
using DeepRun::Game::UnderwaterIceFormationType;

struct ContourPoint final
{
    float x = 0.0F;
    float y = 0.0F;
};

struct IceAuthoring final
{
    std::string_view id;
    UnderwaterIceFormationType type = UnderwaterIceFormationType::SurfaceShelf;
    ModelVector3 position{};
    ModelVector3 renderHalfExtents{};
    float rotationRadians = 0.0F;
    bool hasCoarseCollision = false;
    DeepRun::Physics::PhysicsVector3 collisionHalfExtents{};
};

// Three intentionally separated upper-water formations. The explicit positions are Game composition data,
// not wave samples: west shelf, central hanging ice, and an isolated east keel clear of the M3-F float at X=140.
constexpr std::array<IceAuthoring, DeepRun::Game::M3UnderwaterIceFormationCount> IceAuthoringData{{
    {"surface-shelf-west", UnderwaterIceFormationType::SurfaceShelf, {-225.0F, -14.0F, 0.0F},
     {44.0F, 15.0F, 4.0F}, 0.0F, true, {43.0F, 14.0F, 4.0F}},
    {"hanging-formation-central", UnderwaterIceFormationType::HangingFormation, {-85.0F, -30.0F, 0.0F},
     {18.0F, 30.0F, 3.5F}, 0.0F, false, {}},
    {"iceberg-keel-east", UnderwaterIceFormationType::IcebergKeel, {230.0F, -42.0F, 0.0F},
     {25.0F, 34.0F, 4.5F}, 0.0F, true, {23.0F, 30.0F, 4.0F}}}};

// Counter-clockwise contours as viewed from +Z. Distinct silhouettes make the shelf, hanging ice, and keel
// read as authored low-poly ice instead of three debug boxes.
constexpr std::array<ContourPoint, 7> SurfaceShelfContour{{
    {-1.00F, -0.35F}, {-0.65F, -0.82F}, {0.30F, -0.65F}, {0.88F, -0.05F},
    {1.00F, 0.75F}, {-0.35F, 1.00F}, {-1.00F, 0.60F}}};
constexpr std::array<ContourPoint, 6> HangingFormationContour{{
    {-0.80F, -0.35F}, {-0.10F, -0.72F}, {0.45F, -0.95F},
    {0.95F, 0.20F}, {0.75F, 0.85F}, {-0.85F, 1.00F}}};
constexpr std::array<ContourPoint, 7> IcebergKeelContour{{
    {-1.00F, -0.20F}, {-0.45F, -0.80F}, {0.38F, -1.00F}, {1.00F, -0.10F},
    {0.80F, 0.65F}, {0.00F, 1.00F}, {-0.85F, 0.80F}}};

[[nodiscard]] std::span<const ContourPoint> ContourFor(const UnderwaterIceFormationType type) noexcept
{
    switch (type)
    {
    case UnderwaterIceFormationType::SurfaceShelf:
        return SurfaceShelfContour;
    case UnderwaterIceFormationType::HangingFormation:
        return HangingFormationContour;
    case UnderwaterIceFormationType::IcebergKeel:
        return IcebergKeelContour;
    }
    return {};
}

[[nodiscard]] bool IsFinite(const ModelVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void ExtendBounds(ModelVector3& minimum, ModelVector3& maximum, const ModelVector3 position) noexcept
{
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
}

[[nodiscard]] double TriangleAreaSquared(
    const ModelVector3 a,
    const ModelVector3 b,
    const ModelVector3 c) noexcept
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
std::expected<UnderwaterIceField, std::string> BuildUnderwaterIceField(
    const EnvironmentSectionId& sectionId,
    const float referenceSurfaceLevelYMeters)
{
    if (!sectionId.IsValid())
    {
        return std::unexpected("underwater ice requires a valid environment section id");
    }
    if (!std::isfinite(referenceSurfaceLevelYMeters))
    {
        return std::unexpected("underwater ice reference surface level must be finite");
    }

    const auto assetId = Assets::AssetId::FromPath("environment/ice/" + sectionId.value + ".field");
    if (!assetId)
    {
        return std::unexpected("underwater ice could not form a stable asset id: " + assetId.error());
    }
    Assets::ModelAsset model{.id = std::move(*assetId)};
    model.materials.push_back({
        .name = "UnderwaterIce",
        .baseColorFactor = {0.24F, 0.46F, 0.54F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 0.82F});

    Assets::MeshPrimitiveData primitive;
    primitive.vertices.reserve(132U);
    primitive.indices.reserve(252U);
    std::vector<UnderwaterIceInstance> formations;
    formations.reserve(IceAuthoringData.size());
    std::vector<Physics::StaticBoxBodyCreateInfo> collisionBoxes;
    collisionBoxes.reserve(M3UnderwaterIceCollisionCount);
    ModelVector3 boundsMinimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    ModelVector3 boundsMaximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    const auto pushVertex = [&](const ModelVector3 position, const ModelVector3 normal) {
        if (!IsFinite(position) || !IsFinite(normal))
        {
            return false;
        }
        primitive.vertices.push_back({.position = position, .normal = normal});
        ExtendBounds(boundsMinimum, boundsMaximum, position);
        return true;
    };

    for (const IceAuthoring& authored : IceAuthoringData)
    {
        const std::span<const ContourPoint> contour = ContourFor(authored.type);
        if (authored.id.empty() || contour.size() < 3U || !IsFinite(authored.position) ||
            !IsFinite(authored.renderHalfExtents) || !std::isfinite(authored.rotationRadians) ||
            authored.renderHalfExtents.x <= 0.0F || authored.renderHalfExtents.y <= 0.0F ||
            authored.renderHalfExtents.z <= 0.0F)
        {
            return std::unexpected("underwater ice authoring data is invalid");
        }
        if (authored.hasCoarseCollision &&
            (!authored.collisionHalfExtents.IsFinite() || authored.collisionHalfExtents.x <= 0.0F ||
             authored.collisionHalfExtents.y <= 0.0F || authored.collisionHalfExtents.z <= 0.0F))
        {
            return std::unexpected("underwater ice coarse collision authoring is invalid");
        }

        UnderwaterIceInstance formation{
            .id = std::string(authored.id),
            .type = authored.type,
            .position = authored.position,
            .renderHalfExtents = authored.renderHalfExtents,
            .rotationRadians = authored.rotationRadians,
            .hasCoarseCollision = authored.hasCoarseCollision,
            .coarseCollision = {}};
        if (formation.hasCoarseCollision)
        {
            formation.coarseCollision = {
                .halfExtents = authored.collisionHalfExtents,
                .position = {authored.position.x, authored.position.y, authored.position.z}};
            collisionBoxes.push_back(formation.coarseCollision);
        }

        const float cosine = std::cos(formation.rotationRadians);
        const float sine = std::sin(formation.rotationRadians);
        std::vector<ModelVector3> ring;
        ring.reserve(contour.size());
        ModelVector3 center{};
        for (const ContourPoint point : contour)
        {
            const float localX = point.x * formation.renderHalfExtents.x;
            const float localY = point.y * formation.renderHalfExtents.y;
            const ModelVector3 worldPoint{
                formation.position.x + localX * cosine - localY * sine,
                formation.position.y + localX * sine + localY * cosine,
                formation.position.z};
            if (!IsFinite(worldPoint))
            {
                return std::unexpected("underwater ice generated a non-finite contour point");
            }
            center.x += worldPoint.x / static_cast<float>(contour.size());
            center.y += worldPoint.y / static_cast<float>(contour.size());
            ring.push_back(worldPoint);
        }
        if (center.y - formation.renderHalfExtents.y >= referenceSurfaceLevelYMeters)
        {
            return std::unexpected("underwater ice formation does not reach the underwater presentation region");
        }

        const float zFront = formation.position.z + formation.renderHalfExtents.z;
        const float zBack = formation.position.z - formation.renderHalfExtents.z;
        const std::uint32_t frontBase = static_cast<std::uint32_t>(primitive.vertices.size());
        if (!pushVertex({center.x, center.y, zFront}, {0.0F, 0.0F, 1.0F}))
        {
            return std::unexpected("underwater ice generated a non-finite front vertex");
        }
        for (const ModelVector3 point : ring)
        {
            if (!pushVertex({point.x, point.y, zFront}, {0.0F, 0.0F, 1.0F}))
            {
                return std::unexpected("underwater ice generated a non-finite front vertex");
            }
        }
        const std::uint32_t backBase = static_cast<std::uint32_t>(primitive.vertices.size());
        if (!pushVertex({center.x, center.y, zBack}, {0.0F, 0.0F, -1.0F}))
        {
            return std::unexpected("underwater ice generated a non-finite back vertex");
        }
        for (const ModelVector3 point : ring)
        {
            if (!pushVertex({point.x, point.y, zBack}, {0.0F, 0.0F, -1.0F}))
            {
                return std::unexpected("underwater ice generated a non-finite back vertex");
            }
        }

        for (std::uint32_t index = 0U; index < ring.size(); ++index)
        {
            const std::uint32_t next = (index + 1U) % static_cast<std::uint32_t>(ring.size());
            primitive.indices.insert(
                primitive.indices.end(),
                {frontBase, frontBase + 1U + index, frontBase + 1U + next,
                 backBase, backBase + 1U + next, backBase + 1U + index});

            const float edgeX = ring[next].x - ring[index].x;
            const float edgeY = ring[next].y - ring[index].y;
            const float edgeLength = std::hypot(edgeX, edgeY);
            if (!std::isfinite(edgeLength) || edgeLength <= 0.0F)
            {
                return std::unexpected("underwater ice generated a degenerate contour edge");
            }
            const ModelVector3 sideNormal{edgeY / edgeLength, -edgeX / edgeLength, 0.0F};
            const std::uint32_t sideBase = static_cast<std::uint32_t>(primitive.vertices.size());
            if (!pushVertex({ring[index].x, ring[index].y, zFront}, sideNormal) ||
                !pushVertex({ring[next].x, ring[next].y, zFront}, sideNormal) ||
                !pushVertex({ring[next].x, ring[next].y, zBack}, sideNormal) ||
                !pushVertex({ring[index].x, ring[index].y, zBack}, sideNormal))
            {
                return std::unexpected("underwater ice generated a non-finite side vertex");
            }
            primitive.indices.insert(
                primitive.indices.end(),
                {sideBase, sideBase + 2U, sideBase + 1U, sideBase, sideBase + 3U, sideBase + 2U});
        }
        formations.push_back(std::move(formation));
    }

    if (formations.size() != M3UnderwaterIceFormationCount || collisionBoxes.size() != M3UnderwaterIceCollisionCount ||
        primitive.indices.empty() || primitive.indices.size() % 3U != 0U ||
        primitive.indices.size() / 3U > M3UnderwaterIceTriangleBudget || !IsFinite(boundsMinimum) ||
        !IsFinite(boundsMaximum) || boundsMinimum.x > boundsMaximum.x || boundsMinimum.y > boundsMaximum.y ||
        boundsMinimum.z > boundsMaximum.z)
    {
        return std::unexpected("underwater ice geometry did not meet its fixed bounded contract");
    }
    for (const std::uint32_t index : primitive.indices)
    {
        if (index >= primitive.vertices.size())
        {
            return std::unexpected("underwater ice generated an out-of-range index");
        }
    }
    for (std::size_t index = 0U; index < primitive.indices.size(); index += 3U)
    {
        const auto& a = primitive.vertices[primitive.indices[index]].position;
        const auto& b = primitive.vertices[primitive.indices[index + 1U]].position;
        const auto& c = primitive.vertices[primitive.indices[index + 2U]].position;
        const double areaSquared = TriangleAreaSquared(a, b, c);
        if (!std::isfinite(areaSquared) || !(areaSquared > 0.0))
        {
            return std::unexpected("underwater ice generated a degenerate triangle");
        }
    }

    primitive.materialIndex = 0U;
    primitive.localBounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    primitive.hasNormals = true;
    model.primitives.push_back(std::move(primitive));
    model.nodes.push_back({.name = "UnderwaterIce", .localToModel = {}, .primitiveIndices = {0U}});
    model.bounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    return UnderwaterIceField{
        .renderGeometry = std::move(model),
        .formations = std::move(formations),
        .collisionBoxes = std::move(collisionBoxes)};
}
} // namespace DeepRun::Game
