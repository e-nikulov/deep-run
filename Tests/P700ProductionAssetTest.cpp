#include "Engine/Assets/AssetManager.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Weapons/ProductionP700Asset.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <unordered_set>

namespace
{
constexpr std::string_view P700ModelPath = "Weapons/P700/P700_Granit.glb";
constexpr std::size_t ExpectedLod0ObjectCount = 8U;
constexpr std::size_t ExpectedMovableSurfaceCount = 6U;
constexpr std::size_t ExpectedProductionLodCount = 4U;
constexpr std::size_t ExpectedAnimationTargetCount = ExpectedMovableSurfaceCount * ExpectedProductionLodCount;
constexpr std::size_t ExpectedLod0TriangleCount = 25'172U;

[[nodiscard]] bool IsIdentity(const DeepRun::Assets::ModelTransform& transform) noexcept
{
    constexpr DeepRun::Assets::ModelTransform identity{};
    constexpr float tolerance = 1.0e-5F;
    for (std::size_t index = 0; index < transform.values.size(); ++index)
    {
        if (std::abs(transform.values[index] - identity.values[index]) > tolerance)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RunProductionP700AssetChecks()
{
    using namespace DeepRun;

    Assets::AssetManager assets("Assets");
    const auto definition = Game::Weapons::LoadProductionP700AssetDefinition(assets);
    if (!definition)
    {
        std::cerr << "P-700 production definition load failed: " << definition.error() << '\n';
        return false;
    }
    if (definition->modelAssetId.Value() != P700ModelPath ||
        definition->deploymentAnimationName != "P700_Deploy" ||
        !std::isfinite(definition->authoredDeploymentDurationSeconds) ||
        definition->authoredDeploymentDurationSeconds <= 0.0F ||
        definition->lod0MeshNodeIndices.size() != ExpectedLod0ObjectCount ||
        definition->movableSurfaces.size() != ExpectedMovableSurfaceCount)
    {
        std::cerr << "P-700 production definition accounting is invalid\n";
        return false;
    }

    const auto modelHandle = assets.LoadModel(P700ModelPath);
    if (!modelHandle || !modelHandle->IsValid() || modelHandle->Get() == nullptr)
    {
        std::cerr << "P-700 staged GLB did not load through AssetManager\n";
        return false;
    }
    const Assets::ModelAsset& model = **modelHandle;
    if (model.animations.size() != 1U || model.animations.front().name != "P700_Deploy")
    {
        std::cerr << "P-700 bounded animation metadata is invalid\n";
        return false;
    }
    const std::unordered_set<std::string> animationTargets(
        model.animations.front().targetNodeNames.begin(), model.animations.front().targetNodeNames.end());
    if (animationTargets.size() != ExpectedAnimationTargetCount ||
        animationTargets.size() != model.animations.front().targetNodeNames.size())
    {
        std::cerr << "P-700 deployment clip must expose 24 unique movable-surface LOD targets\n";
        return false;
    }

    const auto stowedOverrides = Game::Weapons::BuildProductionP700DeploymentOverrides(*definition, 0.0F);
    const auto deployedOverrides = Game::Weapons::BuildProductionP700DeploymentOverrides(*definition, 1.0F);
    if (!stowedOverrides || !deployedOverrides ||
        stowedOverrides->size() != ExpectedMovableSurfaceCount ||
        deployedOverrides->size() != ExpectedMovableSurfaceCount)
    {
        std::cerr << "P-700 deployment presentation overrides failed\n";
        return false;
    }
    for (const Render::ModelBindingTransformOverride& overrideValue : *stowedOverrides)
    {
        if (!IsIdentity(overrideValue.bindingLocalPostTransform))
        {
            std::cerr << "P-700 STOWED deployment override must be identity\n";
            return false;
        }
    }

    const auto draws = Render::PrepareModelDraws(model, {}, {}, *deployedOverrides);
    if (!draws)
    {
        std::cerr << "P-700 deployed draw preparation failed: " << draws.error() << '\n';
        return false;
    }

    std::size_t lod0DrawCount = 0U;
    std::size_t lod0TriangleCount = 0U;
    for (const Render::ModelDrawInstance& draw : *draws)
    {
        if (!Game::Weapons::IsProductionP700Lod0MeshNode(*definition, draw.nodeIndex))
        {
            continue;
        }
        if (draw.primitiveIndex >= model.primitives.size())
        {
            std::cerr << "P-700 LOD0 draw references invalid primitive\n";
            return false;
        }
        ++lod0DrawCount;
        lod0TriangleCount += model.primitives[draw.primitiveIndex].indices.size() / 3U;
    }
    if (lod0DrawCount != ExpectedLod0ObjectCount || lod0TriangleCount != ExpectedLod0TriangleCount)
    {
        std::cerr << "P-700 LOD0 draw/triangle accounting mismatch: draws=" << lod0DrawCount
                  << " triangles=" << lod0TriangleCount << '\n';
        return false;
    }

    if (Game::Weapons::BuildProductionP700DeploymentOverrides(*definition, -0.01F) ||
        Game::Weapons::BuildProductionP700DeploymentOverrides(*definition, 1.01F))
    {
        std::cerr << "P-700 deployment progress accepted values outside [0,1]\n";
        return false;
    }
    return true;
}
} // namespace

int main()
{
    if (!RunProductionP700AssetChecks())
    {
        return 1;
    }
    std::cout << "P-700 production asset contract: PASS\n";
    return 0;
}
