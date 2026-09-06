#include "Game/Environment/UnderwaterFaunaField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
using DeepRun::Assets::MeshPrimitiveData;
using DeepRun::Assets::MeshVertex;
using DeepRun::Assets::ModelTransform;
using DeepRun::Assets::ModelVector3;

constexpr std::uint32_t LayoutSeed = 0x4D334831U;
constexpr float Pi = 3.14159265358979323846F;

[[nodiscard]] bool IsFinite(const ModelVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinite(const DeepRun::Game::UnderwaterFishSchoolPresentationParameters& parameters) noexcept
{
    return std::isfinite(parameters.travelMinimumX) && std::isfinite(parameters.travelMaximumX) &&
           std::isfinite(parameters.centerY) && std::isfinite(parameters.centerZ) &&
           std::isfinite(parameters.horizontalSpeedMetersPerSecond) &&
           std::isfinite(parameters.verticalAmplitudeMeters) &&
           std::isfinite(parameters.verticalAngularFrequencyRadiansPerSecond);
}

void ExtendBounds(ModelVector3& minimum, ModelVector3& maximum, const ModelVector3 position) noexcept
{
    minimum.x = (std::min)(minimum.x, position.x);
    minimum.y = (std::min)(minimum.y, position.y);
    minimum.z = (std::min)(minimum.z, position.z);
    maximum.x = (std::max)(maximum.x, position.x);
    maximum.y = (std::max)(maximum.y, position.y);
    maximum.z = (std::max)(maximum.z, position.z);
}

[[nodiscard]] float NextUnitFloat(std::uint32_t& state) noexcept
{
    state = state * 1664525U + 1013904223U;
    return static_cast<float>((state >> 8U) & 0x00FFFFFFU) / 16777215.0F;
}

[[nodiscard]] ModelVector3 RotateOffset(
    const ModelVector3 center,
    const ModelVector3 offset,
    const float headingRadians) noexcept
{
    const float cosine = std::cos(headingRadians);
    const float sine = std::sin(headingRadians);
    return {
        center.x + offset.x * cosine - offset.y * sine,
        center.y + offset.x * sine + offset.y * cosine,
        center.z + offset.z};
}

[[nodiscard]] bool ValidMotionParameters(
    const DeepRun::Game::UnderwaterFishSchoolPresentationParameters& parameters) noexcept
{
    return IsFinite(parameters) && parameters.travelMaximumX > parameters.travelMinimumX &&
           parameters.horizontalSpeedMetersPerSecond > 0.0F && parameters.verticalAmplitudeMeters >= 0.0F &&
           parameters.verticalAngularFrequencyRadiansPerSecond >= 0.0F;
}
} // namespace

namespace DeepRun::Game
{
std::expected<UnderwaterFaunaField, std::string> BuildUnderwaterFaunaField(
    const EnvironmentSectionId& sectionId,
    const float referenceSurfaceLevelYMeters)
{
    if (!sectionId.IsValid())
    {
        return std::unexpected("underwater fauna requires a valid environment section id");
    }
    if (!std::isfinite(referenceSurfaceLevelYMeters))
    {
        return std::unexpected("underwater fauna reference surface level must be finite");
    }

    const auto assetId = Assets::AssetId::FromPath("environment/fauna/" + sectionId.value + ".fish-school");
    if (!assetId)
    {
        return std::unexpected("underwater fauna could not form a stable asset id: " + assetId.error());
    }

    UnderwaterFaunaField field{
        .renderGeometry = Assets::ModelAsset{.id = std::move(*assetId)}, .fish = {}, .presentation = {}};
    field.renderGeometry.materials.push_back({
        .name = "UnderwaterFish",
        .baseColorFactor = {0.22F, 0.34F, 0.38F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 0.88F});
    field.fish.reserve(M3UnderwaterFishCount);

    MeshPrimitiveData primitive;
    primitive.vertices.reserve(M3UnderwaterFishCount * 7U);
    primitive.indices.reserve(M3UnderwaterFishCount * M3UnderwaterFishTrianglesPerFish * 3U);
    ModelVector3 boundsMinimum{
        (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)()};
    ModelVector3 boundsMaximum{
        (std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)()};
    std::uint32_t randomState = LayoutSeed;

    const auto pushVertex = [&](const ModelVector3 position) {
        if (!IsFinite(position))
        {
            return false;
        }
        primitive.vertices.push_back({.position = position, .normal = {0.0F, 0.0F, 1.0F}});
        ExtendBounds(boundsMinimum, boundsMaximum, position);
        return true;
    };

    for (std::size_t index = 0U; index < M3UnderwaterFishCount; ++index)
    {
        const std::size_t row = index / 8U;
        const std::size_t column = index % 8U;
        const float x = (static_cast<float>(column) - 3.5F) * 9.0F + (NextUnitFloat(randomState) - 0.5F) * 2.0F;
        const float y = (static_cast<float>(row) - 1.0F) * 8.0F + (NextUnitFloat(randomState) - 0.5F) * 1.5F;
        const float z = M3UnderwaterFishMinimumLocalZ +
                        NextUnitFloat(randomState) * (M3UnderwaterFishMaximumLocalZ - M3UnderwaterFishMinimumLocalZ);
        const float bodyLength = M3UnderwaterFishMinimumBodyLengthMeters +
                                 NextUnitFloat(randomState) *
                                     (M3UnderwaterFishMaximumBodyLengthMeters - M3UnderwaterFishMinimumBodyLengthMeters);
        const float bodyHeight = M3UnderwaterFishMinimumBodyHeightMeters +
                                 NextUnitFloat(randomState) *
                                     (M3UnderwaterFishMaximumBodyHeightMeters - M3UnderwaterFishMinimumBodyHeightMeters);
        const float heading = (NextUnitFloat(randomState) - 0.5F) * 0.36F;
        const ModelVector3 center{x, y, z};
        field.fish.push_back({
            .localPosition = center,
            .bodyLengthMeters = bodyLength,
            .bodyHeightMeters = bodyHeight,
            .headingRadians = heading});

        const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertices.size());
        const std::array<ModelVector3, 7> offsets{{
            {-0.80F * bodyLength, 0.0F, 0.0F},
            {0.0F, -bodyHeight, 0.0F},
            {bodyLength, 0.0F, 0.0F},
            {0.0F, bodyHeight, 0.0F},
            {-1.35F * bodyLength, -0.65F * bodyHeight, 0.0F},
            {-0.80F * bodyLength, 0.0F, 0.0F},
            {-1.35F * bodyLength, 0.65F * bodyHeight, 0.0F}}};
        for (const ModelVector3 offset : offsets)
        {
            if (!pushVertex(RotateOffset(center, offset, heading)))
            {
                return std::unexpected("underwater fauna generated a non-finite fish vertex");
            }
        }
        primitive.indices.insert(
            primitive.indices.end(),
            {base, base + 1U, base + 2U, base, base + 2U, base + 3U, base + 4U, base + 5U, base + 6U});
    }

    if (primitive.vertices.empty() || primitive.indices.empty() ||
        primitive.indices.size() / 3U > M3UnderwaterFishTriangleBudget ||
        boundsMaximum.y + field.presentation.centerY + field.presentation.verticalAmplitudeMeters >=
            referenceSurfaceLevelYMeters ||
        boundsMinimum.z < M3UnderwaterFishMinimumLocalZ || boundsMaximum.z > M3UnderwaterFishMaximumLocalZ)
    {
        return std::unexpected("underwater fauna authored layout is outside its bounded presentation contract");
    }

    primitive.materialIndex = 0U;
    primitive.localBounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    primitive.hasNormals = true;
    field.renderGeometry.primitives.push_back(std::move(primitive));
    field.renderGeometry.nodes.push_back({.name = "UnderwaterFishSchool", .localToModel = {}, .primitiveIndices = {0U}});
    field.renderGeometry.bounds = {.minimum = boundsMinimum, .maximum = boundsMaximum};
    return field;
}

std::expected<Assets::ModelTransform, std::string> EvaluateUnderwaterFishSchoolPresentation(
    const UnderwaterFishSchoolPresentationParameters& parameters,
    const double presentationTimeSeconds)
{
    if (!ValidMotionParameters(parameters) || !std::isfinite(presentationTimeSeconds) || presentationTimeSeconds < 0.0)
    {
        return std::unexpected("underwater fauna presentation requires finite non-negative time and valid motion parameters");
    }

    const double routeSpan = static_cast<double>(parameters.travelMaximumX) - parameters.travelMinimumX;
    const double travelPeriod = routeSpan / parameters.horizontalSpeedMetersPerSecond;
    if (!std::isfinite(routeSpan) || !std::isfinite(travelPeriod) || travelPeriod <= 0.0)
    {
        return std::unexpected("underwater fauna presentation route is invalid");
    }
    const double wrappedTravelTime = std::fmod(presentationTimeSeconds, travelPeriod);
    if (!std::isfinite(wrappedTravelTime))
    {
        return std::unexpected("underwater fauna presentation travel phase is non-finite");
    }
    const double x = static_cast<double>(parameters.travelMinimumX) +
                     wrappedTravelTime * parameters.horizontalSpeedMetersPerSecond;

    double verticalPhase = 0.0;
    if (parameters.verticalAngularFrequencyRadiansPerSecond > 0.0F)
    {
        const double verticalPeriod = 2.0 * static_cast<double>(Pi) /
                                      parameters.verticalAngularFrequencyRadiansPerSecond;
        const double wrappedVerticalTime = std::fmod(presentationTimeSeconds, verticalPeriod);
        if (!std::isfinite(verticalPeriod) || verticalPeriod <= 0.0 || !std::isfinite(wrappedVerticalTime))
        {
            return std::unexpected("underwater fauna presentation vertical phase is non-finite");
        }
        verticalPhase = wrappedVerticalTime * parameters.verticalAngularFrequencyRadiansPerSecond;
    }
    const double y = static_cast<double>(parameters.centerY) +
                     static_cast<double>(parameters.verticalAmplitudeMeters) * std::sin(verticalPhase);
    const double z = parameters.centerZ;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        x < parameters.travelMinimumX || x >= parameters.travelMaximumX)
    {
        return std::unexpected("underwater fauna presentation transform is non-finite or out of bounds");
    }

    Assets::ModelTransform transform{};
    transform.values[12] = static_cast<float>(x);
    transform.values[13] = static_cast<float>(y);
    transform.values[14] = static_cast<float>(z);
    return transform;
}
} // namespace DeepRun::Game
