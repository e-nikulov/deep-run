#include "Game/Environment/EnvironmentSection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr int MaxSeabedSamples = 4'096;

bool IsFiniteVec(const DeepRun::Assets::ModelVector3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void ExtendBounds(
    DeepRun::Assets::ModelVector3& min_,
    DeepRun::Assets::ModelVector3& max_,
    const DeepRun::Assets::ModelVector3& point) noexcept
{
    min_.x = std::min(min_.x, point.x);
    min_.y = std::min(min_.y, point.y);
    min_.z = std::min(min_.z, point.z);
    max_.x = std::max(max_.x, point.x);
    max_.y = std::max(max_.y, point.y);
    max_.z = std::max(max_.z, point.z);
}
} // namespace

namespace DeepRun::Game
{
bool EnvironmentSectionId::IsValid() const noexcept
{
    if (value.empty())
    {
        return false;
    }
    for (const unsigned char c : value)
    {
        const bool asciiAlphaNumeric =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');

        if (!asciiAlphaNumeric && c != '-' && c != '_')
        {
            return false;
        }
    }
    return true;
}

bool EnvironmentBounds::IsFiniteAndValid() const noexcept
{
    if (!IsFiniteVec(minimum) || !IsFiniteVec(maximum))
    {
        return false;
    }
    return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
}

bool IsSceneLinearBaseColor(const Assets::ModelMaterialData& material) noexcept
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        const float c = material.baseColorFactor[i];
        if (!std::isfinite(c) || c < 0.0F)
        {
            return false;
        }
    }
    return std::isfinite(material.baseColorFactor[3]) && material.baseColorFactor[3] > 0.0F &&
           material.baseColorFactor[3] <= 1.0F;
}

std::expected<EnvironmentSection, std::string> BuildSeabedSection(
    const EnvironmentSectionId& id,
    const SeabedProfileConfig& profile)
{
    std::ostringstream err;
    if (!id.IsValid())
    {
        return std::unexpected("environment section id must be non-empty and contain only letters, digits, '-' or '_'");
    }
    const bool finite = std::isfinite(profile.minX) && std::isfinite(profile.maxX) &&
                        std::isfinite(profile.fillBottomYMeters) && std::isfinite(profile.zThicknessMeters);
    if (!finite || profile.maxX <= profile.minX || profile.sampleCount < 2 ||
        profile.sampleCount > MaxSeabedSamples || profile.zThicknessMeters <= 0.0F ||
        profile.controlPoints.size() < 2U || profile.controlPoints.size() > 128U)
    {
        err << "seabed profile config is invalid (minX=" << profile.minX << ", maxX=" << profile.maxX
            << ", samples=" << profile.sampleCount << ", knots=" << profile.controlPoints.size()
            << ", fillBottomY=" << profile.fillBottomYMeters << ")";
        return std::unexpected(err.str());
    }
    if (profile.controlPoints.front().xMeters != profile.minX ||
        profile.controlPoints.back().xMeters != profile.maxX)
    {
        return std::unexpected("seabed profile knots must begin at minX and end at maxX");
    }
    for (std::size_t index = 0; index < profile.controlPoints.size(); ++index)
    {
        const auto& knot = profile.controlPoints[index];
        if (!std::isfinite(knot.xMeters) || !std::isfinite(knot.yMeters) ||
            knot.yMeters <= profile.fillBottomYMeters ||
            (index > 0U && !(knot.xMeters > profile.controlPoints[index - 1U].xMeters)))
        {
            return std::unexpected("seabed profile knots must be finite, ordered, and above fillBottomY");
        }
    }

    const int sampleCount = profile.sampleCount;
    const float spanX = profile.maxX - profile.minX;
    const float zMax = 0.5F * profile.zThicknessMeters;
    const float zMin = -zMax;
    const float sampleStep = spanX / static_cast<float>(sampleCount - 1);
    if (!std::isfinite(spanX) || !std::isfinite(zMax) || zMax <= 0.0F || !std::isfinite(sampleStep) ||
        sampleStep <= 0.0F || !(profile.minX + sampleStep > profile.minX))
    {
        return std::unexpected("seabed profile derived values are invalid or not representable");
    }

    // Deterministic piecewise-linear interpolation through explicit authored knots. This remains a pure
    // environment-data query; neither render topology nor GPU data participates.
    const auto surfaceYAt = [&](const float x) {
        const auto right = std::upper_bound(
            profile.controlPoints.begin(), profile.controlPoints.end(), x,
            [](const float value, const SeabedProfileControlPoint& knot) { return value < knot.xMeters; });
        if (right == profile.controlPoints.begin()) return right->yMeters;
        if (right == profile.controlPoints.end()) return profile.controlPoints.back().yMeters;
        const auto left = right - 1;
        const float t = (x - left->xMeters) / (right->xMeters - left->xMeters);
        return left->yMeters + (right->yMeters - left->yMeters) * t;
    };

    auto assetId = Assets::AssetId::FromPath(std::string("environment/seabed/") + id.value + ".section");
    if (!assetId)
    {
        return std::unexpected("environment section id cannot form a valid asset path: " + assetId.error());
    }
    Assets::ModelAsset asset{.id = std::move(*assetId)};

    // One restrained scene-linear sediment color (linear space; the renderer applies the one SDR/HDR encode).
    // Muted tan-sand, clearly distinct from the dark underwater background so the floor reads as geometry.
    Assets::ModelMaterialData material;
    material.name = "SeabedSediment";
    material.baseColorFactor = {0.16F, 0.13F, 0.085F, 1.0F};
    material.metallicFactor = 0.0F;
    material.roughnessFactor = 0.95F;
    asset.materials.push_back(material);

    Assets::ModelMaterialData rockMaterial;
    rockMaterial.name = "SeabedRock";
    rockMaterial.baseColorFactor = {0.075F, 0.095F, 0.10F, 1.0F};
    rockMaterial.metallicFactor = 0.0F;
    rockMaterial.roughnessFactor = 0.9F;
    asset.materials.push_back(rockMaterial);

    Assets::MeshPrimitiveData primitive;
    const std::uint32_t cells = static_cast<std::uint32_t>(sampleCount - 1);
    // 4 faces per cell (top, front, back, bottom), 4 verts each => 16 verts/cell, 24 indices/cell.
    primitive.vertices.reserve(cells * 16U);
    primitive.indices.reserve(cells * 24U);

    const auto pushVertex = [&](const float x, const float y, const float z, const Assets::ModelVector3 normal) {
        Assets::MeshVertex vertex;
        vertex.position = {x, y, z};
        vertex.normal = normal;
        primitive.vertices.push_back(vertex);
    };
    const auto pushCell = [&](const std::uint32_t i, const float x0, const float y0, const float x1,
                              const float y1) {
        const std::uint32_t base = i * 16U;
        const double dx = static_cast<double>(x1) - static_cast<double>(x0);
        const double dy = static_cast<double>(y1) - static_cast<double>(y0);
        const double normalLength = std::hypot(dx, dy);
        const Assets::ModelVector3 topNormal{
            static_cast<float>(-dy / normalLength),
            static_cast<float>(dx / normalLength),
            0.0F};
        // TOP face: duplicated per cell so each deterministic slope receives its geometric normal.
        pushVertex(x0, y0, zMin, topNormal); // 0
        pushVertex(x1, y1, zMin, topNormal); // 1
        pushVertex(x1, y1, zMax, topNormal); // 2
        pushVertex(x0, y0, zMax, topNormal); // 3
        // FRONT wall (z = zMax, toward the +Z camera, normal +Z): top follows surface Y, bottom at fillBottom.
        pushVertex(x0, y0, zMax, {0.0F, 0.0F, +1.0F}); // 4
        pushVertex(x1, y1, zMax, {0.0F, 0.0F, +1.0F}); // 5
        pushVertex(x1, profile.fillBottomYMeters, zMax, {0.0F, 0.0F, +1.0F}); // 6
        pushVertex(x0, profile.fillBottomYMeters, zMax, {0.0F, 0.0F, +1.0F}); // 7
        // BACK wall (z = zMin, normal -Z).
        pushVertex(x0, y0, zMin, {0.0F, 0.0F, -1.0F}); // 8
        pushVertex(x1, y1, zMin, {0.0F, 0.0F, -1.0F}); // 9
        pushVertex(x1, profile.fillBottomYMeters, zMin, {0.0F, 0.0F, -1.0F}); // 10
        pushVertex(x0, profile.fillBottomYMeters, zMin, {0.0F, 0.0F, -1.0F}); // 11
        // BOTTOM face (y = fillBottom, normal -Y): closes the lower part of the visible box.
        pushVertex(x0, profile.fillBottomYMeters, zMin, {0.0F, -1.0F, 0.0F}); // 12
        pushVertex(x1, profile.fillBottomYMeters, zMin, {0.0F, -1.0F, 0.0F}); // 13
        pushVertex(x1, profile.fillBottomYMeters, zMax, {0.0F, -1.0F, 0.0F}); // 14
        pushVertex(x0, profile.fillBottomYMeters, zMax, {0.0F, -1.0F, 0.0F}); // 15

        // 8 triangles (2 per face), counter-clockwise as seen from the outward normal so the existing
        // BACK-face cull mode keeps every outward-facing face.
        auto tri = [&](const std::uint32_t a, const std::uint32_t b, const std::uint32_t c) {
            primitive.indices.push_back(base + a);
            primitive.indices.push_back(base + b);
            primitive.indices.push_back(base + c);
        };
        // Top (slope-aware outward normal has positive Y):
        tri(0, 2, 1);
        tri(0, 3, 2);
        // Front (zMax), viewer sees from +Z:
        tri(4, 6, 5);
        tri(4, 7, 6);
        // Back (zMin):
        tri(8, 9, 10);
        tri(8, 10, 11);
        // Bottom (fillBottom).
        tri(12, 13, 14);
        tri(12, 14, 15);
    };

    for (int cell = 0; cell < sampleCount - 1; ++cell)
    {
        const float x0 = profile.minX + (spanX * static_cast<float>(cell)) / static_cast<float>(sampleCount - 1);
        const float x1 = profile.minX + (spanX * static_cast<float>(cell + 1)) / static_cast<float>(sampleCount - 1);
        const float y0 = surfaceYAt(x0);
        const float y1 = surfaceYAt(x1);
        if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1) || !(x1 > x0))
        {
            return std::unexpected("seabed section sample positions are invalid or not representable");
        }
        pushCell(static_cast<std::uint32_t>(cell), x0, y0, x1, y1);
    }

    if (primitive.vertices.empty() || primitive.indices.empty())
    {
        return std::unexpected("seabed section produced no geometry");
    }

    // Validate the render geometry contract before exposing it: finite vertices, in-range indices,
    // non-degenerate triangles.
    Assets::ModelVector3 boundsMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max()};
    Assets::ModelVector3 boundsMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                   std::numeric_limits<float>::lowest()};
    for (const auto& v : primitive.vertices)
    {
        if (!IsFiniteVec(v.position) || !IsFiniteVec(v.normal))
        {
            return std::unexpected("seabed section vertex is not finite");
        }
        ExtendBounds(boundsMin, boundsMax, v.position);
    }
    const uint32_t lastVertex = static_cast<uint32_t>(primitive.vertices.size() - 1U);
    for (const auto& index : primitive.indices)
    {
        if (index > lastVertex)
        {
            return std::unexpected("seabed section index out of range");
        }
    }
    for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
    {
        const auto& a = primitive.vertices[primitive.indices[i]].position;
        const auto& b = primitive.vertices[primitive.indices[i + 1]].position;
        const auto& c = primitive.vertices[primitive.indices[i + 2]].position;
        const double u0 = static_cast<double>(b.x) - static_cast<double>(a.x);
        const double v0 = static_cast<double>(b.y) - static_cast<double>(a.y);
        const double w0 = static_cast<double>(b.z) - static_cast<double>(a.z);
        const double u1 = static_cast<double>(c.x) - static_cast<double>(a.x);
        const double v1 = static_cast<double>(c.y) - static_cast<double>(a.y);
        const double w1 = static_cast<double>(c.z) - static_cast<double>(a.z);
        const double crossX = v0 * w1 - w0 * v1;
        const double crossY = w0 * u1 - u0 * w1;
        const double crossZ = u0 * v1 - v0 * u1;
        const double areaSquared = crossX * crossX + crossY * crossY + crossZ * crossZ;
        if (!std::isfinite(areaSquared) || !(areaSquared > 0.0))
        {
            return std::unexpected("seabed section contains a degenerate triangle");
        }
    }

    const Assets::ModelBounds terrainBounds{boundsMin, boundsMax};

    // Seven stable, deliberately bounded rock placements. They are authored environment data first;
    // the single combined low-poly rock primitive below is only their inexpensive presentation consumer.
    struct RockAuthoring final
    {
        std::string_view id;
        float xMeters;
        Assets::ModelVector3 halfExtents;
        float rotationRadians;
        bool hasCoarseCollision;
    };
    constexpr std::array<RockAuthoring, 7> RockAuthoringData{{
        {"ridge-west", -300.0F, {11.0F, 7.0F, 3.0F}, 0.10F, true},
        {"ridge-east", -220.0F, {8.0F, 5.0F, 2.5F}, -0.16F, false},
        {"ridge-crown", -155.0F, {14.0F, 10.0F, 3.5F}, 0.08F, true},
        {"trench-west", 110.0F, {8.0F, 6.0F, 2.5F}, -0.18F, false},
        {"basin-outcrop", 175.0F, {13.0F, 9.0F, 3.5F}, 0.14F, true},
        {"slope-small", 230.0F, {7.0F, 5.0F, 2.0F}, -0.12F, false},
        {"slope-east", 280.0F, {10.0F, 8.0F, 3.0F}, 0.20F, false}}};
    std::vector<EnvironmentRockInstance> rocks;
    rocks.reserve(RockAuthoringData.size());
    for (const RockAuthoring& authored : RockAuthoringData)
    {
        rocks.push_back(EnvironmentRockInstance{
            .id = std::string(authored.id),
            .position = {authored.xMeters, surfaceYAt(authored.xMeters), 0.0F},
            .halfExtents = authored.halfExtents,
            .rotationRadians = authored.rotationRadians,
            .hasCoarseCollision = authored.hasCoarseCollision});
    }

    Assets::MeshPrimitiveData rockPrimitive;
    rockPrimitive.vertices.reserve(rocks.size() * 38U);
    rockPrimitive.indices.reserve(rocks.size() * 72U);
    Assets::ModelVector3 rockBoundsMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                       std::numeric_limits<float>::max()};
    Assets::ModelVector3 rockBoundsMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                       std::numeric_limits<float>::lowest()};
    const std::array<Assets::ModelVector3, 6> rockContour{{
        {-0.90F, 0.00F, 0.0F}, {0.80F, 0.00F, 0.0F}, {1.00F, 0.35F, 0.0F},
        {0.35F, 1.00F, 0.0F}, {-0.50F, 0.85F, 0.0F}, {-1.00F, 0.25F, 0.0F}}};
    const auto pushRockVertex = [&](const Assets::ModelVector3 position, const Assets::ModelVector3 normal) {
        rockPrimitive.vertices.push_back({.position = position, .normal = normal});
        ExtendBounds(rockBoundsMin, rockBoundsMax, position);
        ExtendBounds(boundsMin, boundsMax, position);
    };
    for (const EnvironmentRockInstance& rock : rocks)
    {
        const float cosine = std::cos(rock.rotationRadians);
        const float sine = std::sin(rock.rotationRadians);
        std::array<Assets::ModelVector3, 6> ring{};
        float minimumLocalY = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < rockContour.size(); ++index)
        {
            const float localX = rockContour[index].x * rock.halfExtents.x;
            const float localY = rockContour[index].y * (2.0F * rock.halfExtents.y);
            ring[index] = {localX * cosine - localY * sine, localX * sine + localY * cosine, 0.0F};
            minimumLocalY = std::min(minimumLocalY, ring[index].y);
        }
        for (auto& point : ring)
        {
            point.x += rock.position.x;
            point.y += rock.position.y - minimumLocalY; // exactly terrain-contacted, never floated.
        }

        Assets::ModelVector3 center{};
        for (const auto& point : ring)
        {
            center.x += point.x / static_cast<float>(ring.size());
            center.y += point.y / static_cast<float>(ring.size());
        }
        const std::uint32_t frontBase = static_cast<std::uint32_t>(rockPrimitive.vertices.size());
        const float zFront = rock.halfExtents.z;
        const float zBack = -rock.halfExtents.z;
        pushRockVertex({center.x, center.y, zFront}, {0.0F, 0.0F, 1.0F});
        for (const auto& point : ring) pushRockVertex({point.x, point.y, zFront}, {0.0F, 0.0F, 1.0F});
        const std::uint32_t backBase = static_cast<std::uint32_t>(rockPrimitive.vertices.size());
        pushRockVertex({center.x, center.y, zBack}, {0.0F, 0.0F, -1.0F});
        for (const auto& point : ring) pushRockVertex({point.x, point.y, zBack}, {0.0F, 0.0F, -1.0F});
        for (std::uint32_t index = 0; index < ring.size(); ++index)
        {
            const std::uint32_t next = (index + 1U) % static_cast<std::uint32_t>(ring.size());
            rockPrimitive.indices.insert(rockPrimitive.indices.end(),
                {frontBase, frontBase + 1U + index, frontBase + 1U + next,
                 backBase, backBase + 1U + next, backBase + 1U + index});

            const float edgeX = ring[next].x - ring[index].x;
            const float edgeY = ring[next].y - ring[index].y;
            const float edgeLength = std::hypot(edgeX, edgeY);
            const Assets::ModelVector3 sideNormal{edgeY / edgeLength, -edgeX / edgeLength, 0.0F};
            const std::uint32_t sideBase = static_cast<std::uint32_t>(rockPrimitive.vertices.size());
            pushRockVertex({ring[index].x, ring[index].y, zFront}, sideNormal);
            pushRockVertex({ring[next].x, ring[next].y, zFront}, sideNormal);
            pushRockVertex({ring[next].x, ring[next].y, zBack}, sideNormal);
            pushRockVertex({ring[index].x, ring[index].y, zBack}, sideNormal);
            rockPrimitive.indices.insert(rockPrimitive.indices.end(),
                {sideBase, sideBase + 2U, sideBase + 1U, sideBase, sideBase + 3U, sideBase + 2U});
        }
    }
    if (rockPrimitive.vertices.empty() || rockPrimitive.indices.empty())
    {
        return std::unexpected("seabed section produced no representative rock geometry");
    }
    for (const auto& vertex : rockPrimitive.vertices)
    {
        if (!IsFiniteVec(vertex.position) || !IsFiniteVec(vertex.normal))
        {
            return std::unexpected("representative rock vertex is not finite");
        }
    }
    for (const std::uint32_t index : rockPrimitive.indices)
    {
        if (index >= rockPrimitive.vertices.size())
        {
            return std::unexpected("representative rock index out of range");
        }
    }
    for (std::size_t index = 0; index + 2U < rockPrimitive.indices.size(); index += 3U)
    {
        const auto& a = rockPrimitive.vertices[rockPrimitive.indices[index]].position;
        const auto& b = rockPrimitive.vertices[rockPrimitive.indices[index + 1U]].position;
        const auto& c = rockPrimitive.vertices[rockPrimitive.indices[index + 2U]].position;
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double uz = static_cast<double>(b.z) - a.z;
        const double vx = static_cast<double>(c.x) - a.x;
        const double vy = static_cast<double>(c.y) - a.y;
        const double vz = static_cast<double>(c.z) - a.z;
        const double areaSquared = (uy * vz - uz * vy) * (uy * vz - uz * vy) +
                                   (uz * vx - ux * vz) * (uz * vx - ux * vz) +
                                   (ux * vy - uy * vx) * (ux * vy - uy * vx);
        if (!std::isfinite(areaSquared) || !(areaSquared > 0.0))
        {
            return std::unexpected("representative rock geometry contains a degenerate triangle");
        }
    }
    rockPrimitive.localBounds = Assets::ModelBounds{rockBoundsMin, rockBoundsMax};
    rockPrimitive.materialIndex = 1U;
    rockPrimitive.hasNormals = true;

    // World-space bounds derived from the generated geometry (deterministic; guaranteed to contain it).
    const EnvironmentBounds bounds{boundsMin, boundsMax};
    if (!bounds.IsFiniteAndValid())
    {
        return std::unexpected("seabed section produced invalid bounds");
    }
    for (const auto& vertex : primitive.vertices)
    {
        const Assets::ModelVector3& position = vertex.position;
        if (position.x < boundsMin.x || position.x > boundsMax.x || position.y < boundsMin.y ||
            position.y > boundsMax.y || position.z < boundsMin.z || position.z > boundsMax.z)
        {
            return std::unexpected("seabed section bounds do not contain generated geometry");
        }
    }

    primitive.localBounds = terrainBounds;
    primitive.materialIndex = 0U;
    primitive.hasNormals = true;
    asset.primitives.push_back(std::move(primitive));
    asset.primitives.push_back(std::move(rockPrimitive));

    // Single world-space node with an identity localToModel: the renderer's PrepareModelDraws therefore
    // produces a modelToWorld == identity draw, so the world-space vertices upload verbatim through the
    // existing classic indexed D3D12 path.
    Assets::MeshNodeData node;
    node.name = "Seabed";
    node.primitiveIndices = {0U};
    asset.nodes.push_back(node);
    Assets::MeshNodeData rockNode;
    rockNode.name = "RepresentativeRocks";
    rockNode.primitiveIndices = {1U};
    asset.nodes.push_back(rockNode);
    asset.bounds = Assets::ModelBounds{boundsMin, boundsMax};

    // Feature-aware collision stays independent of presentation tessellation. It respects every authored
    // knot, uses modest 25 m columns on ordinary slopes, and narrows only where a slope would otherwise
    // exceed the small fixed 6 m profile-error budget.
    // Each filled column conservatively uses the higher endpoint so no gap can exist beneath the visible floor.
    std::vector<Physics::StaticBoxBodyCreateInfo> collisionBoxes;
    for (std::size_t knot = 0; knot + 1U < profile.controlPoints.size(); ++knot)
    {
        const auto& left = profile.controlPoints[knot];
        const auto& right = profile.controlPoints[knot + 1U];
        const float slopeMagnitude = std::abs((right.yMeters - left.yMeters) / (right.xMeters - left.xMeters));
        const float maximumWidth = slopeMagnitude > 0.0F ? std::min(25.0F, 6.0F / slopeMagnitude) : 25.0F;
        const int columns = static_cast<int>(std::ceil((right.xMeters - left.xMeters) / maximumWidth));
        for (int column = 0; column < columns; ++column)
        {
            const float x0 = left.xMeters + (right.xMeters - left.xMeters) *
                              (static_cast<float>(column) / static_cast<float>(columns));
            const float x1 = left.xMeters + (right.xMeters - left.xMeters) *
                              (static_cast<float>(column + 1) / static_cast<float>(columns));
            const float top = std::max(surfaceYAt(x0), surfaceYAt(x1));
            const float halfHeight = (top - profile.fillBottomYMeters) * 0.5F;
            Physics::StaticBoxBodyCreateInfo box{
                .halfExtents = {(x1 - x0) * 0.5F, halfHeight, zMax},
                .position = {(x0 + x1) * 0.5F, profile.fillBottomYMeters + halfHeight, 0.0F}};
            if (!box.position.IsFinite() || !box.halfExtents.IsFinite() ||
                box.halfExtents.x <= 0.0F || box.halfExtents.y <= 0.0F)
            {
                return std::unexpected("seabed collision samples are not representable");
            }
            collisionBoxes.push_back(box);
        }
    }
    for (const EnvironmentRockInstance& rock : rocks)
    {
        if (!rock.hasCoarseCollision) continue;
        // Conservative AABB from the authored footprint/rotation, calculated independently from the
        // presentation vertices. It contains the obstructing low-poly rock without triangle collision.
        const float sineMagnitude = std::abs(std::sin(rock.rotationRadians));
        const float cosineMagnitude = std::abs(std::cos(rock.rotationRadians));
        const float halfWidth = rock.halfExtents.x * cosineMagnitude + rock.halfExtents.y * sineMagnitude;
        const float halfHeight = rock.halfExtents.y + rock.halfExtents.x * sineMagnitude;
        Physics::StaticBoxBodyCreateInfo box{
            .halfExtents = {halfWidth, halfHeight, rock.halfExtents.z},
            .position = {rock.position.x, rock.position.y + halfHeight, rock.position.z}};
        if (!box.position.IsFinite() || !box.halfExtents.IsFinite())
        {
            return std::unexpected("representative rock collision is not representable");
        }
        collisionBoxes.push_back(box);
    }
    return EnvironmentSection{.id = id, .bounds = bounds, .renderGeometry = std::move(asset),
                              .rocks = std::move(rocks), .collisionBoxes = std::move(collisionBoxes)};
}
} // namespace DeepRun::Game
