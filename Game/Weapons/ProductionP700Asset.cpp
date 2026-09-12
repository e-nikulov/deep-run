#include "Game/Weapons/ProductionP700Asset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace DeepRun::Game::Weapons
{
namespace
{
using Json = nlohmann::json;

constexpr std::string_view MetadataPath = "Weapons/P700/P700.asset.json";
constexpr std::string_view AuthoringPath = "Weapons/P700/P700.authoring.json";
constexpr std::string_view ModelPath = "Weapons/P700/P700_Granit.glb";
constexpr std::string_view CoordinateContract = "+X forward; +Y port; +Z up; 1 BU = 1 m";
constexpr std::string_view DeploymentAnimation = "P700_Deploy";
constexpr std::string_view Lod0Prefix = "SM_P700_LOD0_";
constexpr std::size_t ExpectedLod0ObjectCount = 8U;
constexpr std::size_t ExpectedMovableSurfaceCount = 6U;
constexpr std::size_t ExpectedLod0TriangleCount = 25'172U;

[[nodiscard]] float ReadFiniteFloat(const Json& value, const std::string_view label)
{
    const float result = value.get<float>();
    if (!std::isfinite(result))
    {
        throw std::runtime_error(std::format("P-700 {} must be finite", label));
    }
    return result;
}

[[nodiscard]] float ReadRotationX(const Json& state, const std::string_view label)
{
    const Json& rotation = state.at("rotationEulerXYZ");
    if (!rotation.is_array() || rotation.size() != 3U)
    {
        throw std::runtime_error(std::format("P-700 {} rotation must have three components", label));
    }
    const float x = ReadFiniteFloat(rotation.at(0), label);
    const float y = ReadFiniteFloat(rotation.at(1), label);
    const float z = ReadFiniteFloat(rotation.at(2), label);
    if (std::abs(y) > 1.0e-5F || std::abs(z) > 1.0e-5F)
    {
        throw std::runtime_error(std::format("P-700 {} must remain rotation-only around its fixed local +X hinge", label));
    }
    return x;
}

[[nodiscard]] std::array<float, 3> ReadLocation(const Json& state, const std::string_view label)
{
    const Json& location = state.at("location");
    if (!location.is_array() || location.size() != 3U)
    {
        throw std::runtime_error(std::format("P-700 {} location must have three components", label));
    }
    return {ReadFiniteFloat(location.at(0), label), ReadFiniteFloat(location.at(1), label),
            ReadFiniteFloat(location.at(2), label)};
}

[[nodiscard]] bool SameLocation(const std::array<float, 3>& first, const std::array<float, 3>& second) noexcept
{
    constexpr float tolerance = 1.0e-5F;
    return std::abs(first[0] - second[0]) <= tolerance &&
           std::abs(first[1] - second[1]) <= tolerance &&
           std::abs(first[2] - second[2]) <= tolerance;
}

[[nodiscard]] std::optional<std::size_t> FindUniqueBinding(
    const Assets::ModelAsset& model,
    const std::string_view name)
{
    std::optional<std::size_t> result{};
    for (std::size_t index = 0; index < model.nodeBindings.size(); ++index)
    {
        if (model.nodeBindings[index].name != name)
        {
            continue;
        }
        if (result.has_value())
        {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

[[nodiscard]] Assets::ModelTransform RotationX(const float radians) noexcept
{
    const float sine = static_cast<float>(std::sin(static_cast<double>(radians)));
    const float cosine = static_cast<float>(std::cos(static_cast<double>(radians)));
    return Assets::ModelTransform{.values = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, cosine, sine, 0.0F,
        0.0F, -sine, cosine, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F}};
}
} // namespace

std::expected<ProductionP700AssetDefinition, std::string> LoadProductionP700AssetDefinition(
    Assets::AssetManager& assets)
{
    const auto metadataText = assets.LoadText(MetadataPath);
    if (!metadataText)
    {
        return std::unexpected(std::format("unable to load staged P-700 metadata: {}", metadataText.error().message));
    }
    const auto authoringText = assets.LoadText(AuthoringPath);
    if (!authoringText)
    {
        return std::unexpected(std::format("unable to load staged P-700 authoring contract: {}", authoringText.error().message));
    }
    const auto modelHandle = assets.LoadModel(ModelPath);
    if (!modelHandle)
    {
        return std::unexpected(std::format("unable to load staged P-700 model: {}", modelHandle.error().message));
    }
    const Assets::ModelAsset* model = modelHandle->Get();
    if (model == nullptr)
    {
        return std::unexpected("staged P-700 model handle expired during production contract load");
    }

    try
    {
        const Json metadata = Json::parse(metadataText->Get()->text);
        const Json authoring = Json::parse(authoringText->Get()->text);
        if (metadata.at("schemaVersion").get<int>() != 1 || metadata.at("assetId").get<std::string>() != "P700" ||
            metadata.at("coordinateContract").get<std::string>() != CoordinateContract)
        {
            return std::unexpected("staged P-700 metadata identity/coordinate contract is invalid");
        }
        if (metadata.at("licenseStatus").get<std::string>() != "SHIPPING BLOCKED PENDING LEGAL REVIEW")
        {
            return std::unexpected("staged P-700 metadata must preserve the explicit unresolved shipping-license gate");
        }
        const Json& lod0 = metadata.at("lods").at("LOD0");
        if (lod0.at("objects").get<std::size_t>() != ExpectedLod0ObjectCount ||
            lod0.at("triangles").get<std::size_t>() != ExpectedLod0TriangleCount)
        {
            return std::unexpected("staged P-700 LOD0 accounting no longer matches the accepted production contract");
        }
        if (authoring.at("schemaVersion").get<int>() != 1 || authoring.at("root").get<std::string>() != "P700_ROOT" ||
            authoring.at("animation").at("name").get<std::string>() != DeploymentAnimation ||
            authoring.at("animation").at("interpolation").get<std::string>().find("LINEAR") == std::string::npos)
        {
            return std::unexpected("staged P-700 authoring deployment contract is invalid");
        }

        if (model->animations.size() != 1U || model->animations.front().name != DeploymentAnimation ||
            !std::isfinite(model->animations.front().durationSeconds) || model->animations.front().durationSeconds <= 0.0F)
        {
            return std::unexpected("staged P-700 GLB must expose exactly one bounded P700_Deploy rotation clip");
        }
        const Assets::ModelAnimationClipData& animation = model->animations.front();

        std::vector<std::size_t> lod0MeshNodes;
        std::unordered_set<std::size_t> uniqueLod0MeshNodes;
        std::size_t lod0BindingCount = 0U;
        for (const Assets::ModelNodeBindingData& binding : model->nodeBindings)
        {
            if (!binding.name.starts_with(Lod0Prefix))
            {
                continue;
            }
            ++lod0BindingCount;
            if (binding.drawableMeshNodeIndices.empty())
            {
                return std::unexpected("staged P-700 LOD0 binding has no drawable geometry");
            }
            for (const std::size_t nodeIndex : binding.drawableMeshNodeIndices)
            {
                if (nodeIndex >= model->nodes.size() || !uniqueLod0MeshNodes.insert(nodeIndex).second)
                {
                    return std::unexpected("staged P-700 LOD0 drawable-node partition is invalid or overlapping");
                }
                lod0MeshNodes.push_back(nodeIndex);
            }
        }
        if (lod0BindingCount != ExpectedLod0ObjectCount || lod0MeshNodes.size() != ExpectedLod0ObjectCount)
        {
            return std::unexpected("staged P-700 GLB does not expose the accepted eight-object LOD0 partition");
        }

        const Json& surfaceJson = authoring.at("movableSurfaces");
        if (!surfaceJson.is_array() || surfaceJson.size() != ExpectedMovableSurfaceCount)
        {
            return std::unexpected("staged P-700 authoring must contain exactly six movable surfaces");
        }
        std::vector<ProductionP700MovableSurface> surfaces;
        surfaces.reserve(surfaceJson.size());
        std::unordered_set<std::string> uniqueSurfaceNames;
        for (const Json& surface : surfaceJson)
        {
            const std::string name = surface.at("name").get<std::string>();
            if (!uniqueSurfaceNames.insert(name).second || !name.starts_with(Lod0Prefix) ||
                std::find(animation.targetNodeNames.begin(), animation.targetNodeNames.end(), name) == animation.targetNodeNames.end())
            {
                return std::unexpected(std::format("P-700 movable surface '{}' is duplicated, non-LOD0, or absent from P700_Deploy", name));
            }
            if (surface.at("pivotContract").get<std::string>().find("ROTATION_ONLY") == std::string::npos)
            {
                return std::unexpected(std::format("P-700 movable surface '{}' lost its fixed rotation-only hinge contract", name));
            }
            const auto stowedLocation = ReadLocation(surface.at("stowed"), name + " STOWED");
            const auto deployedLocation = ReadLocation(surface.at("deployed"), name + " DEPLOYED");
            if (!SameLocation(stowedLocation, deployedLocation))
            {
                return std::unexpected(std::format("P-700 movable surface '{}' changes hinge location during deployment", name));
            }
            const auto bindingIndex = FindUniqueBinding(*model, name);
            if (!bindingIndex.has_value() || model->nodeBindings[*bindingIndex].drawableMeshNodeIndices.empty())
            {
                return std::unexpected(std::format("P-700 movable surface '{}' has no unique drawable semantic binding", name));
            }
            surfaces.push_back(ProductionP700MovableSurface{
                .semanticId = name,
                .presentationNodeBindingIndex = *bindingIndex,
                .stowedRotationXRadians = ReadRotationX(surface.at("stowed"), name + " STOWED"),
                .deployedRotationXRadians = ReadRotationX(surface.at("deployed"), name + " DEPLOYED")});
        }
        if (animation.targetNodeNames.size() != ExpectedMovableSurfaceCount)
        {
            return std::unexpected("P700_Deploy targets nodes outside the accepted six-surface production contract");
        }

        return ProductionP700AssetDefinition{
            .modelAssetId = model->id,
            .deploymentAnimationName = animation.name,
            .authoredDeploymentDurationSeconds = animation.durationSeconds,
            .lod0MeshNodeIndices = std::move(lod0MeshNodes),
            .movableSurfaces = std::move(surfaces)};
    }
    catch (const std::exception& exception)
    {
        return std::unexpected(std::string("invalid staged P-700 production metadata: ") + exception.what());
    }
}

std::expected<std::vector<Render::ModelBindingTransformOverride>, std::string>
BuildProductionP700DeploymentOverrides(
    const ProductionP700AssetDefinition& definition,
    const float deploymentProgress)
{
    if (!std::isfinite(deploymentProgress) || deploymentProgress < 0.0F || deploymentProgress > 1.0F ||
        definition.movableSurfaces.size() != ExpectedMovableSurfaceCount)
    {
        return std::unexpected("P-700 deployment presentation progress/definition is invalid");
    }

    std::vector<Render::ModelBindingTransformOverride> result;
    result.reserve(definition.movableSurfaces.size());
    std::unordered_set<std::size_t> uniqueBindings;
    for (const ProductionP700MovableSurface& surface : definition.movableSurfaces)
    {
        if (!std::isfinite(surface.stowedRotationXRadians) || !std::isfinite(surface.deployedRotationXRadians) ||
            !uniqueBindings.insert(surface.presentationNodeBindingIndex).second)
        {
            return std::unexpected("P-700 deployment surface binding/rotation contract is invalid");
        }
        const float deltaRadians =
            (surface.deployedRotationXRadians - surface.stowedRotationXRadians) * deploymentProgress;
        result.push_back(Render::ModelBindingTransformOverride{
            .bindingIndex = surface.presentationNodeBindingIndex,
            .bindingLocalPostTransform = RotationX(deltaRadians)});
    }
    return result;
}

bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    const std::size_t meshNodeIndex) noexcept
{
    return std::find(definition.lod0MeshNodeIndices.begin(), definition.lod0MeshNodeIndices.end(), meshNodeIndex) !=
           definition.lod0MeshNodeIndices.end();
}
} // namespace DeepRun::Game::Weapons
