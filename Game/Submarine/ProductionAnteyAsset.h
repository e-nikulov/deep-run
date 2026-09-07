#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace DeepRun::Game::Submarine
{
// Production semantic transforms use the same column-major affine layout as
// Assets::ModelTransform: values[column * 4 + row], with translation at
// values[12..14]. They are already converted from the staged authoring basis
// to the DeepRun runtime basis.
using ProductionLocalTransform = Assets::ModelTransform;

// The staged authoring sidecar uses +X bow, +Y port, +Z up. Runtime uses +X,
// +Y up, +Z toward camera. These helpers define the one production boundary.
[[nodiscard]] Assets::ModelVector3 ConvertAnteyAuthoringVector(const Assets::ModelVector3& source) noexcept;
[[nodiscard]] Assets::ModelVector3 ConvertAnteyAuthoringExtent(const Assets::ModelVector3& source) noexcept;
[[nodiscard]] ProductionLocalTransform ConvertAnteyAuthoringTransform(
    const std::array<float, 16>& sourceRowMajor) noexcept;
[[nodiscard]] std::array<float, 4> ConvertAnteyAuthoringQuaternionWxyz(
    const std::array<float, 4>& sourceWxyz);

struct ProductionRenderLod final
{
    std::string semanticId;
    std::size_t objectCount = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    std::optional<Assets::AssetId> stagedModelAssetId;
};

struct ProductionPropellerAnchor final
{
    std::string semanticId;
    Assets::ModelVector3 localOrigin{};
    std::string rotationAxis;
    // Opaque Assets-layer binding index; never a GLB node name contract.
    std::size_t presentationNodeBindingIndex = 0;
};

struct ProductionLaunchAnchor final
{
    std::string semanticId;
    ProductionLocalTransform localTransform{};
    Assets::ModelVector3 launchForward{};
};

struct ProductionCompartment final
{
    std::string semanticId;
    Assets::ModelVector3 localCenter{};
    std::array<float, 4> orientationQuaternionWxyz{};
    Assets::ModelVector3 halfExtents{};
};

struct ProductionSubmarineAssetDefinition final
{
    std::string assetFamilyId;
    Assets::AssetId metadataAssetId;
    std::array<ProductionRenderLod, 4> renderLods;
    std::vector<ProductionPropellerAnchor> propellers;
    std::vector<ProductionLaunchAnchor> torpedoLaunchAnchors;
    std::vector<ProductionLaunchAnchor> p700LaunchAnchors;
    std::vector<ProductionCompartment> compartments;
    std::vector<std::string> collisionSemanticIds;
    std::string buoyancySemanticId;
};

[[nodiscard]] std::expected<ProductionSubmarineAssetDefinition, std::string> LoadProductionAnteyAssetDefinition(
    Assets::AssetManager& assets);
}
