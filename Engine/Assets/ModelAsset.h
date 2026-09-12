#pragma once

#include "Engine/Assets/AssetManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DeepRun::Assets
{
struct ModelVector3 final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct ModelBounds final
{
    ModelVector3 minimum{};
    ModelVector3 maximum{};
};

// Column-major affine transform matching the glTF matrix convention.
struct ModelTransform final
{
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
};

struct MeshVertex final
{
    ModelVector3 position{};
    ModelVector3 normal{};
};

struct ModelMaterialData final
{
    std::string name;
    std::array<float, 4> baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F};
    float metallicFactor = 1.0F;
    float roughnessFactor = 1.0F;
};

struct MeshPrimitiveData final
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::optional<std::size_t> materialIndex;
    ModelBounds localBounds{};
    bool hasNormals = false;
};

struct MeshNodeData final
{
    std::string name;
    ModelTransform localToModel{};
    std::vector<std::size_t> primitiveIndices;
};

// Imported scene-node data available to the asset/import layer. Unlike
// MeshNodeData, this retains transform-only hierarchy nodes without making
// them render draws or a gameplay-facing naming contract.
struct ModelNodeBindingData final
{
    std::string name;
    ModelTransform localToModel{};
    // Present only when this imported node contributes a drawable mesh node.
    // This remains an Assets-layer resolution detail; semantic consumers retain
    // only the opaque nodeBindings index.
    std::optional<std::size_t> meshNodeIndex;
    // Mesh nodes contributed by this binding's complete imported subtree. This lets
    // presentation animate a semantic transform-only root without leaking child GLB
    // names into Game code. Direct drawable bindings include their own mesh node.
    std::vector<std::size_t> drawableMeshNodeIndices;
};

// C0.1 bounded animation metadata. The importer still does not evaluate arbitrary glTF animation.
// It accepts only validated LINEAR rotation-only clips with no skins and exposes just enough immutable
// metadata for Game to bind a semantic production animation contract such as P700_Deploy.
struct ModelAnimationClipData final
{
    std::string name;
    float durationSeconds = 0.0F;
    std::vector<std::string> targetNodeNames;
};

struct ModelAsset final
{
    AssetId id;
    std::vector<ModelMaterialData> materials;
    std::vector<MeshPrimitiveData> primitives;
    std::vector<MeshNodeData> nodes;
    std::vector<ModelNodeBindingData> nodeBindings;
    std::vector<ModelAnimationClipData> animations;
    ModelBounds bounds{};
};
}