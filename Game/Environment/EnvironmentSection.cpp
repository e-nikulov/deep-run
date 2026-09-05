#include "Game/Environment/EnvironmentSection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace
{
constexpr float kPi = 3.14159265358979323846F;
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
                        std::isfinite(profile.baselineYMeters) &&
                        std::isfinite(profile.fillBottomYMeters) && std::isfinite(profile.primaryAmplitudeMeters) &&
                        std::isfinite(profile.secondaryAmplitudeMeters) && std::isfinite(profile.primaryWavePeriodMeters) &&
                        std::isfinite(profile.secondaryWavePeriodMeters) && std::isfinite(profile.zThicknessMeters);
    if (!finite || profile.maxX <= profile.minX || profile.sampleCount < 2 ||
        profile.sampleCount > MaxSeabedSamples || profile.primaryAmplitudeMeters < 0.0F ||
        profile.secondaryAmplitudeMeters < 0.0F ||
        profile.primaryWavePeriodMeters <= 0.0F || profile.secondaryWavePeriodMeters <= 0.0F ||
        profile.zThicknessMeters <= 0.0F)
    {
        err << "seabed profile config is invalid (minX=" << profile.minX << ", maxX=" << profile.maxX
            << ", samples=" << profile.sampleCount << ", baselineY=" << profile.baselineYMeters
            << ", fillBottomY=" << profile.fillBottomYMeters << ")";
        return std::unexpected(err.str());
    }

    const int sampleCount = profile.sampleCount;
    const float spanX = profile.maxX - profile.minX;
    const float zMax = 0.5F * profile.zThicknessMeters;
    const float zMin = -zMax;
    const float sampleStep = spanX / static_cast<float>(sampleCount - 1);
    const float minSurfaceY = profile.baselineYMeters - profile.primaryAmplitudeMeters - profile.secondaryAmplitudeMeters;
    const float maxSurfaceY = profile.baselineYMeters + profile.primaryAmplitudeMeters + profile.secondaryAmplitudeMeters;
    const float primaryPhaseAtSpan = (spanX / profile.primaryWavePeriodMeters) * (2.0F * kPi);
    const float secondaryPhaseAtSpan = (spanX / profile.secondaryWavePeriodMeters) * (2.0F * kPi);
    if (!std::isfinite(spanX) || !std::isfinite(zMax) || zMax <= 0.0F || !std::isfinite(sampleStep) ||
        sampleStep <= 0.0F || !(profile.minX + sampleStep > profile.minX) || !std::isfinite(minSurfaceY) ||
        !std::isfinite(maxSurfaceY) || profile.fillBottomYMeters >= minSurfaceY ||
        !std::isfinite(primaryPhaseAtSpan) || !std::isfinite(secondaryPhaseAtSpan))
    {
        return std::unexpected("seabed profile derived values are invalid or not representable");
    }

    // Deterministic surface Y at an X coordinate: two fixed sine terms (authored, NOT procedural noise).
    const auto surfaceYAt = [&](const float x) {
        const float u = x - profile.minX;
        const float primary = std::sin((2.0F * kPi * u) / profile.primaryWavePeriodMeters);
        const float secondary = std::sin((2.0F * kPi * u) / profile.secondaryWavePeriodMeters);
        return profile.baselineYMeters - profile.primaryAmplitudeMeters * primary -
               profile.secondaryAmplitudeMeters * secondary;
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

    primitive.localBounds = Assets::ModelBounds{boundsMin, boundsMax};
    primitive.materialIndex = 0U;
    primitive.hasNormals = true;
    asset.primitives.push_back(std::move(primitive));

    // Single world-space node with an identity localToModel: the renderer's PrepareModelDraws therefore
    // produces a modelToWorld == identity draw, so the world-space vertices upload verbatim through the
    // existing classic indexed D3D12 path.
    Assets::MeshNodeData node;
    node.name = "Seabed";
    node.primitiveIndices = {0U};
    asset.nodes.push_back(node);
    asset.bounds = Assets::ModelBounds{boundsMin, boundsMax};

    return EnvironmentSection{.id = id, .bounds = bounds, .renderGeometry = std::move(asset)};
}
} // namespace DeepRun::Game
