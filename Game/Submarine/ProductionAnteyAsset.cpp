#include "Game/Submarine/ProductionAnteyAsset.h"
#include "Game/Submarine/ProductionAnteyLodPolicy.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace DeepRun::Game::Submarine
{
namespace
{
using Json = nlohmann::json;

constexpr std::string_view AnteyMetadataPath = "submarines/Antey/Antey.asset.json";
constexpr std::string_view AnteyAuthoringPath = "submarines/Antey/Antey.authoring.json";
constexpr std::string_view AnteyLod0ModelPath = "submarines/Antey/Antey.glb";
constexpr std::string_view CoordinateContract = "+X bow; +Y port; +Z up; 1 BU = 1 m";

[[nodiscard]] bool IsFinite(const float value)
{
    return std::isfinite(value);
}

[[nodiscard]] float ReadFiniteFloat(const Json& value, const std::string_view label)
{
    const float result = value.get<float>();
    if (!IsFinite(result))
    {
        throw std::runtime_error(std::format("Antey metadata {} must be finite", label));
    }
    return result;
}

[[nodiscard]] Assets::ModelVector3 ReadVector3(const Json& value, const std::string_view label)
{
    if (!value.is_array() || value.size() != 3U)
    {
        throw std::runtime_error(std::format("Antey metadata {} must be a three-component vector", label));
    }
    return {.x = ReadFiniteFloat(value[0], label), .y = ReadFiniteFloat(value[1], label),
            .z = ReadFiniteFloat(value[2], label)};
}

[[nodiscard]] ProductionLocalTransform ReadTransform(const Json& value, const std::string_view label)
{
    if (!value.is_array() || value.size() != 4U)
    {
        throw std::runtime_error(std::format("Antey metadata {} must be a 4x4 transform", label));
    }

    std::array<float, 16> sourceRowMajor{};
    for (std::size_t row = 0; row < 4U; ++row)
    {
        if (!value[row].is_array() || value[row].size() != 4U)
        {
            throw std::runtime_error(std::format("Antey metadata {} must be a 4x4 transform", label));
        }
        for (std::size_t column = 0; column < 4U; ++column)
        {
            sourceRowMajor[row * 4U + column] = ReadFiniteFloat(value[row][column], label);
        }
    }
    return ConvertAnteyAuthoringTransform(sourceRowMajor);
}

} // namespace

Assets::ModelVector3 ConvertAnteyAuthoringVector(const Assets::ModelVector3& source) noexcept
{
    return {.x = source.x, .y = source.z, .z = -source.y};
}

Assets::ModelVector3 ConvertAnteyAuthoringExtent(const Assets::ModelVector3& source) noexcept
{
    return {.x = source.x, .y = source.z, .z = source.y};
}

ProductionLocalTransform ConvertAnteyAuthoringTransform(const std::array<float, 16>& sourceRowMajor) noexcept
{
    // C maps authoring vectors (X, Y, Z) to runtime vectors (X, Z, -Y).
    constexpr std::array<float, 16> basis{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, -1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    constexpr std::array<float, 16> inverseBasis{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, -1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};

    ProductionLocalTransform source{};
    for (std::size_t row = 0; row < 4U; ++row)
    {
        for (std::size_t column = 0; column < 4U; ++column)
        {
            source.values[column * 4U + row] = sourceRowMajor[row * 4U + column];
        }
    }
    const auto multiply = [](const std::array<float, 16>& left, const std::array<float, 16>& right)
    {
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4U; ++row)
        {
            for (std::size_t column = 0; column < 4U; ++column)
            {
                for (std::size_t inner = 0; inner < 4U; ++inner)
                {
                    result[column * 4U + row] += left[inner * 4U + row] * right[column * 4U + inner];
                }
            }
        }
        return result;
    };
    return {.values = multiply(multiply(basis, source.values), inverseBasis)};
}

std::array<float, 4> ConvertAnteyAuthoringQuaternionWxyz(const std::array<float, 4>& sourceWxyz)
{
    const float lengthSquared = sourceWxyz[0] * sourceWxyz[0] + sourceWxyz[1] * sourceWxyz[1] +
                                sourceWxyz[2] * sourceWxyz[2] + sourceWxyz[3] * sourceWxyz[3];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F)
    {
        throw std::runtime_error("Antey compartment orientation must be a finite non-zero quaternion");
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const float w = sourceWxyz[0] * inverseLength;
    const float x = sourceWxyz[1] * inverseLength;
    const float y = sourceWxyz[2] * inverseLength;
    const float z = sourceWxyz[3] * inverseLength;
    const std::array<float, 16> sourceRotationRowMajor{
        1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - z * w), 2.0F * (x * z + y * w), 0.0F,
        2.0F * (x * y + z * w), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z - x * w), 0.0F,
        2.0F * (x * z - y * w), 2.0F * (y * z + x * w), 1.0F - 2.0F * (x * x + y * y), 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    const ProductionLocalTransform runtime = ConvertAnteyAuthoringTransform(sourceRotationRowMajor);
    const auto element = [&runtime](const std::size_t row, const std::size_t column)
    {
        return runtime.values[column * 4U + row];
    };
    const float trace = element(0U, 0U) + element(1U, 1U) + element(2U, 2U);
    std::array<float, 4> result{};
    if (trace > 0.0F)
    {
        const float s = 2.0F * std::sqrt(trace + 1.0F);
        result = {0.25F * s, (element(2U, 1U) - element(1U, 2U)) / s,
                  (element(0U, 2U) - element(2U, 0U)) / s, (element(1U, 0U) - element(0U, 1U)) / s};
    }
    else if (element(0U, 0U) > element(1U, 1U) && element(0U, 0U) > element(2U, 2U))
    {
        const float s = 2.0F * std::sqrt(1.0F + element(0U, 0U) - element(1U, 1U) - element(2U, 2U));
        result = {(element(2U, 1U) - element(1U, 2U)) / s, 0.25F * s,
                  (element(0U, 1U) + element(1U, 0U)) / s, (element(0U, 2U) + element(2U, 0U)) / s};
    }
    else if (element(1U, 1U) > element(2U, 2U))
    {
        const float s = 2.0F * std::sqrt(1.0F + element(1U, 1U) - element(0U, 0U) - element(2U, 2U));
        result = {(element(0U, 2U) - element(2U, 0U)) / s, (element(0U, 1U) + element(1U, 0U)) / s,
                  0.25F * s, (element(1U, 2U) + element(2U, 1U)) / s};
    }
    else
    {
        const float s = 2.0F * std::sqrt(1.0F + element(2U, 2U) - element(0U, 0U) - element(1U, 1U));
        result = {(element(1U, 0U) - element(0U, 1U)) / s, (element(0U, 2U) + element(2U, 0U)) / s,
                  (element(1U, 2U) + element(2U, 1U)) / s, 0.25F * s};
    }
    return result;
}

namespace
{

[[nodiscard]] std::array<float, 4> ReadQuaternion(const Json& value, const std::string_view label)
{
    if (!value.is_array() || value.size() != 4U)
    {
        throw std::runtime_error(std::format("Antey metadata {} must be a quaternion", label));
    }
    return {ReadFiniteFloat(value[0], label), ReadFiniteFloat(value[1], label), ReadFiniteFloat(value[2], label),
            ReadFiniteFloat(value[3], label)};
}

[[nodiscard]] std::size_t ReadCount(const Json& value, const std::string_view label)
{
    const std::int64_t result = value.get<std::int64_t>();
    if (result < 0)
    {
        throw std::runtime_error(std::format("Antey metadata {} must be non-negative", label));
    }
    return static_cast<std::size_t>(result);
}

// The production sidecar's node-reference field is private import metadata.
// It is resolved once into an opaque binding index and never reaches the
// public semantic definition as a raw GLB name.
[[nodiscard]] std::size_t ResolvePresentationNodeBindingIndex(
    const Assets::ModelAsset& model,
    const std::string_view privateNodeReference)
{
    for (std::size_t index = 0; index < model.nodeBindings.size(); ++index)
    {
        if (model.nodeBindings[index].name == privateNodeReference)
        {
            return index;
        }
    }
    throw std::runtime_error(std::format("Antey GLB has no node for a required propeller presentation binding"));
}

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ProductionProxyShape ReadProxyShape(const Json& value, const std::string_view label)
{
    Require(value.is_string() && value.get<std::string>() == "BOX",
            std::format("Antey {} shapeType must be BOX", label));
    return ProductionProxyShape::Box;
}

[[nodiscard]] ProductionCollisionDefinition ReadCollisionProxy(const Json& value, const std::size_t ordinal)
{
    Require(value.is_object(), "Antey collision proxy must be an object");
    const std::string label = std::format("collision proxy {}", ordinal);
    const std::string semanticId = value.at("semanticId").get<std::string>();
    Require(!semanticId.empty(), std::format("{} semanticId must be non-empty", label));
    const Assets::ModelVector3 sourceDimensions = ReadVector3(value.at("dimensions"), label + " dimensions");
    const Assets::ModelVector3 sourceHalfExtents = ReadVector3(value.at("halfExtents"), label + " halfExtents");
    Require(sourceHalfExtents.x > 0.0F && sourceHalfExtents.y > 0.0F && sourceHalfExtents.z > 0.0F,
            std::format("{} halfExtents must be positive", label));
    Require(std::abs(sourceDimensions.x - 2.0F * sourceHalfExtents.x) <= 1.0e-4F &&
                std::abs(sourceDimensions.y - 2.0F * sourceHalfExtents.y) <= 1.0e-4F &&
                std::abs(sourceDimensions.z - 2.0F * sourceHalfExtents.z) <= 1.0e-4F,
            std::format("{} dimensions and halfExtents disagree", label));
    const auto orientation = ConvertAnteyAuthoringQuaternionWxyz(
        ReadQuaternion(value.at("orientationQuaternionWXYZ"), label + " orientation"));
    return {
        .semanticId = semanticId,
        .shape = ReadProxyShape(value.at("shapeType"), label),
        .localCenter = ConvertAnteyAuthoringVector(ReadVector3(value.at("center"), label + " center")),
        .orientationQuaternionWxyz = orientation,
        .halfExtents = ConvertAnteyAuthoringExtent(sourceHalfExtents)};
}

[[nodiscard]] ProductionBuoyancyDefinition ReadBuoyancyProxy(const Json& value)
{
    Require(value.is_object(), "Antey buoyancy proxy must be an object");
    const std::string semanticId = value.at("semanticId").get<std::string>();
    Require(!semanticId.empty(), "buoyancy proxy semanticId must be non-empty");
    const Assets::ModelVector3 sourceDimensions = ReadVector3(value.at("dimensions"), "buoyancy proxy dimensions");
    const Assets::ModelVector3 sourceHalfExtents = ReadVector3(value.at("halfExtents"), "buoyancy proxy halfExtents");
    Require(sourceHalfExtents.x > 0.0F && sourceHalfExtents.y > 0.0F && sourceHalfExtents.z > 0.0F,
            "buoyancy proxy halfExtents must be positive");
    Require(std::abs(sourceDimensions.x - 2.0F * sourceHalfExtents.x) <= 1.0e-4F &&
                std::abs(sourceDimensions.y - 2.0F * sourceHalfExtents.y) <= 1.0e-4F &&
                std::abs(sourceDimensions.z - 2.0F * sourceHalfExtents.z) <= 1.0e-4F,
            "buoyancy proxy dimensions and halfExtents disagree");
    const Assets::ModelVector3 sourceCenter = ReadVector3(value.at("center"), "buoyancy proxy center");
    const Assets::ModelVector3 sourceCob = ReadVector3(value.at("centerOfBuoyancy"), "buoyancy proxy COB");
    const Assets::ModelVector3 runtimeCenter = ConvertAnteyAuthoringVector(sourceCenter);
    const Assets::ModelVector3 runtimeHalfExtents = ConvertAnteyAuthoringExtent(sourceHalfExtents);
    const Assets::ModelVector3 runtimeCob = ConvertAnteyAuthoringVector(sourceCob);
    const auto associated = [runtimeCenter, runtimeHalfExtents](const Assets::ModelVector3& point)
    {
        return std::abs(point.x - runtimeCenter.x) <= runtimeHalfExtents.x * 1.25F &&
               std::abs(point.y - runtimeCenter.y) <= runtimeHalfExtents.y * 1.25F &&
               std::abs(point.z - runtimeCenter.z) <= runtimeHalfExtents.z * 1.25F;
    };
    Require(associated(runtimeCob), "buoyancy proxy COB is not inside or plausibly associated with the proxy");
    return {
        .semanticId = semanticId,
        .shape = ReadProxyShape(value.at("shapeType"), "buoyancy proxy"),
        .localCenter = runtimeCenter,
        .orientationQuaternionWxyz = ConvertAnteyAuthoringQuaternionWxyz(
            ReadQuaternion(value.at("orientationQuaternionWXYZ"), "buoyancy proxy orientation")),
        .halfExtents = runtimeHalfExtents,
        .centerOfBuoyancy = runtimeCob};
}
}

std::expected<ProductionSubmarineAssetDefinition, std::string> LoadProductionAnteyAssetDefinition(
    Assets::AssetManager& assets)
{
    const auto metadataText = assets.LoadText(AnteyMetadataPath);
    if (!metadataText)
    {
        return std::unexpected(std::format("unable to load staged Antey metadata: {}", metadataText.error().message));
    }
    const auto authoringText = assets.LoadText(AnteyAuthoringPath);
    if (!authoringText)
    {
        return std::unexpected(std::format("unable to load staged Antey semantic metadata: {}", authoringText.error().message));
    }
    const auto model = assets.LoadModel(AnteyLod0ModelPath);
    if (!model)
    {
        return std::unexpected(std::format("unable to load staged Antey LOD0 model: {}", model.error().message));
    }

    try
    {
        const Json metadata = Json::parse((*metadataText)->text);
        const Json authoring = Json::parse((*authoringText)->text);
        Require(metadata.at("schemaVersion").get<int>() == 1, "Antey metadata schemaVersion must be 1");
        Require(metadata.at("assetId").get<std::string>() == "C0 Player Submarine", "Antey assetId is unexpected");
        Require(metadata.at("name").get<std::string>() == "Antey", "Antey name is unexpected");
        Require(metadata.at("coordinateContract").get<std::string>() == CoordinateContract,
                "Antey coordinate contract is unexpected");
        Require(metadata.at("technicalAssetStatus").get<std::string>() == "ACCEPTED",
                "Antey technical asset status is not accepted");
        Require(metadata.at("userVisualApproval").get<std::string>() == "PASS",
                "Antey visual approval is not pass");
        Require(authoring.at("schemaVersion").get<int>() == 1, "Antey authoring schemaVersion must be 1");
        Require(authoring.at("coordinateContract").get<std::string>() == CoordinateContract,
                "Antey authoring coordinate contract is unexpected");

        auto metadataAssetId = Assets::AssetId::FromPath(AnteyMetadataPath);
        auto lod0AssetId = Assets::AssetId::FromPath(AnteyLod0ModelPath);
        if (!metadataAssetId || !lod0AssetId)
        {
            return std::unexpected("Antey runtime asset paths are invalid");
        }

        ProductionSubmarineAssetDefinition definition{
            .assetFamilyId = "submarine.antey",
            .metadataAssetId = std::move(*metadataAssetId),
            .renderLods = {},
            .propellers = {},
            .retractableSailDevices = {},
            .depthPlanes = {},
            .torpedoLaunchAnchors = {},
            .p700LaunchAnchors = {},
            .compartments = {},
            .collisionProxies = {},
            .buoyancyProxy = {}};

        for (std::size_t index = 0; index < definition.renderLods.size(); ++index)
        {
            const std::string lodName = std::format("LOD{}", index);
            const Json& lod = metadata.at("lods").at(lodName);
            ProductionRenderLod& output = definition.renderLods[index];
            output.semanticId = std::string("render.") + lodName;
            output.objectCount = ReadCount(lod.at("objects"), lodName);
            output.vertexCount = ReadCount(lod.at("vertices"), lodName);
            output.triangleCount = ReadCount(lod.at("triangles"), lodName);
            if (index == 0U)
            {
                output.stagedModelAssetId = *lod0AssetId;
            }
        }

        const Json& propellers = authoring.at("propellers");
        Require(propellers.is_array() && propellers.size() == 2U, "Antey must have two propeller records");
        std::unordered_set<std::string> propellerSemanticIds;
        for (const Json& propeller : propellers)
        {
            const std::string privateNodeReference = propeller.at("nodeReference").get<std::string>();
            const Assets::ModelVector3 localOrigin = ReadVector3(propeller.at("origin"), "propeller origin");
            const std::string semanticId = propeller.at("semanticId").get<std::string>();
            Require(semanticId == "propeller.port" || semanticId == "propeller.starboard",
                    "Antey propeller semantic ID is unexpected");
            Require(propellerSemanticIds.insert(semanticId).second,
                    "Antey propeller semantic IDs must be unique");
            definition.propellers.push_back({
                .semanticId = semanticId,
                .localOrigin = ConvertAnteyAuthoringVector(localOrigin),
                .rotationAxis = propeller.at("axis").get<std::string>(),
                .presentationNodeBindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference)});
        }

        // M5-V2-B: exact node references are private source-first authoring metadata. Resolve them once here
        // and expose only semantic group + opaque model binding index to Game/runtime code.
        const Json& controlSurfaces = authoring.at("controlSurfaces");
        Require(controlSurfaces.is_array() && controlSurfaces.size() == 4U,
                "Antey authoring must contain four production depth-plane records");
        std::unordered_set<std::size_t> depthPlaneBindingIndices;
        std::size_t bowPlaneCount = 0U;
        std::size_t sternPlaneCount = 0U;
        for (const Json& record : controlSurfaces)
        {
            Require(record.is_object(), "Antey depth-plane authoring record must be an object");
            const std::string semanticId = record.at("semanticId").get<std::string>();
            const std::string groupValue = record.at("group").get<std::string>();
            const std::string privateNodeReference = record.at("nodeReference").get<std::string>();
            Require(record.at("articulation").get<std::string>() == "ROTATION" &&
                    record.at("hingeAxisSource").get<std::string>() == "LOCAL_Y" &&
                    record.at("simulationOwnsAngle").get<bool>(),
                    "Antey depth-plane articulation authoring contract is invalid");
            const ProductionDepthPlaneGroup group = groupValue == "BOW"
                ? ProductionDepthPlaneGroup::Bow
                : groupValue == "STERN"
                    ? ProductionDepthPlaneGroup::Stern
                    : throw std::runtime_error("Antey depth-plane group must be BOW or STERN");
            if (group == ProductionDepthPlaneGroup::Bow) ++bowPlaneCount;
            else ++sternPlaneCount;
            const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
            Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value(),
                    "Antey depth-plane binding must resolve to a drawable mesh node");
            Require(depthPlaneBindingIndices.insert(bindingIndex).second,
                    "Antey depth-plane bindings must be unique");
            definition.depthPlanes.push_back({
                .semanticId = semanticId,
                .group = group,
                .presentationNodeBindingIndex = bindingIndex});
        }
        Require(bowPlaneCount == 2U && sternPlaneCount == 2U,
                "Antey must expose two bow and two stern production depth planes");

        const Json& retractableSailDevices = authoring.at("retractableSailDevices");
        Require(retractableSailDevices.is_array() && !retractableSailDevices.empty(),
                "Antey must have retractable sail-device records");
        std::unordered_set<std::string> sailDeviceSemanticIds;
        std::unordered_set<std::size_t> sailDeviceBindingIndices;
        for (const Json& device : retractableSailDevices)
        {
            const std::string privateNodeReference = device.at("nodeReference").get<std::string>();
            const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
            Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value(),
                    "Antey retractable sail-device binding must resolve to a mesh node");
            Require(device.at("classification").get<std::string>() == "RETRACTABLE",
                    "Antey sail-device classification is unexpected");
            Require(device.at("defaultState").get<std::string>() == "STOWED",
                    "Antey submerged sail-device default must be stowed");
            const std::string semanticId = device.at("semanticId").get<std::string>();
            Require(sailDeviceSemanticIds.insert(semanticId).second,
                    "Antey retractable sail-device semantic IDs must be unique");
            Require(sailDeviceBindingIndices.insert(bindingIndex).second,
                    "Antey retractable sail-device bindings must be unique");
            const Assets::ModelVector3 sourceEnvelopeTop = ReadVector3(
                device.at("stowedSailEnvelopeMaximumSource"), "sail-device stowed envelope maximum");
            definition.retractableSailDevices.push_back({
                .semanticId = std::move(semanticId),
                .presentationNodeBindingIndex = bindingIndex,
                .deployedLocalPostTransform = ReadTransform(
                    device.at("deployedLocalPostTransform"), "sail-device deployed transform"),
                .stowedLocalPostTransform = ReadTransform(
                    device.at("stowedLocalPostTransform"), "sail-device stowed transform"),
                .defaultState = RetractableSailDeviceState::Stowed,
                .stowedSailEnvelopeMaximumY = ConvertAnteyAuthoringVector(sourceEnvelopeTop).y});
        }

        const Json& torpedoes = authoring.at("torpedoTubes");
        Require(torpedoes.is_array() && torpedoes.size() == 6U, "Antey must have six torpedo records");
        std::size_t torpedoOrdinal = 0;
        for (const Json& torpedo : torpedoes)
        {
            ++torpedoOrdinal;
            definition.torpedoLaunchAnchors.push_back({
                .semanticId = std::format("torpedo.{}.{}", torpedo.at("tubeClass").get<std::string>(), torpedoOrdinal),
                .localTransform = ReadTransform(torpedo.at("transform"), "torpedo transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(torpedo.at("launchForward"), "torpedo launch forward"))});
        }

        const Json& p700Launchers = authoring.at("p700Launchers");
        Require(p700Launchers.is_array() && p700Launchers.size() == 24U, "Antey must have 24 P700 launcher records");
        for (const Json& launcher : p700Launchers)
        {
            const std::string hatchGroup = launcher.at("hatchGroup").get<std::string>();
            const std::size_t pairIndex = ReadCount(launcher.at("pairIndex"), "P700 pair index");
            definition.p700LaunchAnchors.push_back({
                .semanticId = std::format("p700.{}.{}", hatchGroup, pairIndex),
                .localTransform = ReadTransform(launcher.at("transform"), "P700 transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(launcher.at("launchForward"), "P700 launch forward"))});
        }

        const Json& compartments = authoring.at("compartments");
        Require(compartments.is_array() && compartments.size() == 10U, "Antey must have ten compartment records");
        std::size_t compartmentOrdinal = 0;
        std::unordered_set<std::string> compartmentReferences;
        std::unordered_set<std::string> compartmentSemanticIds;
        std::vector<Assets::ModelVector3> compartmentCenters;
        for (const Json& compartment : compartments)
        {
            ++compartmentOrdinal;
            const std::string privateReference = compartment.at("name").get<std::string>();
            Require(compartmentReferences.insert(privateReference).second,
                    "Antey compartment source references must be unique");
            const std::string semanticId = compartment.at("semanticId").get<std::string>();
            Require(semanticId == std::format("compartment.{:02}", compartmentOrdinal),
                    "Antey compartment semantic IDs must be stable and ordered 01..10");
            Require(compartmentSemanticIds.insert(semanticId).second,
                    "Antey compartment semantic IDs must be unique");
            const std::string displayNameRu = compartment.at("displayNameRu").get<std::string>();
            const std::string functionalRole = compartment.at("functionalRole").get<std::string>();
            const std::string functionalRoleStatus = compartment.at("functionalRoleStatus").get<std::string>();
            Require(!displayNameRu.empty() && !functionalRole.empty() && !functionalRoleStatus.empty(),
                    "Antey compartment functional metadata must be non-empty");
            std::vector<std::string> systemTags;
            const Json& sourceSystemTags = compartment.at("systemTags");
            Require(sourceSystemTags.is_array(), "Antey compartment systemTags must be an array");
            for (const Json& tag : sourceSystemTags)
            {
                const std::string value = tag.get<std::string>();
                Require(!value.empty(), "Antey compartment systemTags must not contain empty values");
                systemTags.push_back(value);
            }
            const Assets::ModelVector3 sourceCenter = ReadVector3(compartment.at("center"), "compartment center");
            const Assets::ModelVector3 halfExtents = ReadVector3(compartment.at("halfExtents"), "compartment half extents");
            Require(halfExtents.x > 0.0F && halfExtents.y > 0.0F && halfExtents.z > 0.0F,
                    "Antey compartment half extents must be positive");
            const Json& xRange = compartment.at("xRangeMeters");
            Require(xRange.is_array() && xRange.size() == 2U, "Antey compartment xRangeMeters must contain two values");
            const float xMinimum = ReadFiniteFloat(xRange[0], "compartment x range minimum");
            const float xMaximum = ReadFiniteFloat(xRange[1], "compartment x range maximum");
            Require(xMaximum > xMinimum, "Antey compartment x range must be ordered");
            Require(std::abs(sourceCenter.x - 0.5F * (xMinimum + xMaximum)) <= 1.0e-4F &&
                        std::abs(halfExtents.x - 0.5F * (xMaximum - xMinimum)) <= 1.0e-4F,
                    "Antey compartment center/extents must agree with xRangeMeters");
            const Assets::ModelVector3 runtimeCenter = ConvertAnteyAuthoringVector(sourceCenter);
            compartmentCenters.push_back(runtimeCenter);
            definition.compartments.push_back({
                .semanticId = semanticId,
                .displayNameRu = displayNameRu,
                .functionalRole = functionalRole,
                .functionalRoleStatus = functionalRoleStatus,
                .systemTags = std::move(systemTags),
                .localCenter = runtimeCenter,
                .orientationQuaternionWxyz = ConvertAnteyAuthoringQuaternionWxyz(
                    ReadQuaternion(compartment.at("orientationQuaternionWXYZ"), "compartment orientation")),
                .halfExtents = ConvertAnteyAuthoringExtent(halfExtents)});
        }
        bool hasDistinctCompartmentCenters = false;
        for (std::size_t left = 0; left < compartmentCenters.size(); ++left)
        {
            for (std::size_t right = left + 1U; right < compartmentCenters.size(); ++right)
            {
                const Assets::ModelVector3 delta{
                    .x = compartmentCenters[left].x - compartmentCenters[right].x,
                    .y = compartmentCenters[left].y - compartmentCenters[right].y,
                    .z = compartmentCenters[left].z - compartmentCenters[right].z};
                hasDistinctCompartmentCenters = hasDistinctCompartmentCenters ||
                    delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 1.0e-6F;
            }
        }
        Require(hasDistinctCompartmentCenters, "Antey compartment centers must not collapse to one point");

        const Json& collision = authoring.at("collision");
        Require(collision.is_array() && !collision.empty(), "Antey collision records must be present");
        std::unordered_set<std::string> collisionSemanticIds;
        for (std::size_t index = 0; index < collision.size(); ++index)
        {
            ProductionCollisionDefinition proxy = ReadCollisionProxy(collision[index], index + 1U);
            Require(collisionSemanticIds.insert(proxy.semanticId).second,
                    "Antey collision proxy semantic IDs must be unique");
            definition.collisionProxies.push_back(std::move(proxy));
        }
        definition.buoyancyProxy = ReadBuoyancyProxy(authoring.at("buoyancyProxy"));
        Require(collisionSemanticIds.find(definition.buoyancyProxy.semanticId) == collisionSemanticIds.end(),
                "Antey collision and buoyancy proxy semantic IDs must be unique across physics roles");

        return definition;
    }
    catch (const std::exception& exception)
    {
        return std::unexpected(std::format("invalid staged Antey metadata: {}", exception.what()));
    }
}

}
