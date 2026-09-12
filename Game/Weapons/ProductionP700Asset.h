#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Render/ModelDraw.h"

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game::Weapons
{
struct ProductionP700MovableSurface final
{
    std::string semanticId;
    std::size_t presentationNodeBindingIndex = 0;
    float stowedRotationXRadians = 0.0F;
    float deployedRotationXRadians = 0.0F;
};

struct ProductionP700AssetDefinition final
{
    Assets::AssetId modelAssetId;
    std::string deploymentAnimationName;
    float authoredDeploymentDurationSeconds = 0.0F;
    std::vector<std::size_t> lod0MeshNodeIndices;
    std::vector<ProductionP700MovableSurface> movableSurfaces;
};

// Loads only the staged, hash-validated production P-700 package. The canonical GLB may carry one bounded
// LINEAR rotation-only clip; semantic surface names are resolved once here into opaque Assets binding indices.
[[nodiscard]] std::expected<ProductionP700AssetDefinition, std::string> LoadProductionP700AssetDefinition(
    Assets::AssetManager& assets);

// The canonical GLB rest pose is STOWED. Runtime deployment is a presentation-only post rotation at each fixed
// physical hinge; progress must come from the authoritative P700Granit lifecycle and never drives simulation.
[[nodiscard]] std::expected<std::vector<Render::ModelBindingTransformOverride>, std::string>
BuildProductionP700DeploymentOverrides(
    const ProductionP700AssetDefinition& definition,
    float deploymentProgress);

[[nodiscard]] bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    std::size_t meshNodeIndex) noexcept;
} // namespace DeepRun::Game::Weapons
