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

struct ModelAsset final
{
    AssetId id;
    std::vector<ModelMaterialData> materials;
    std::vector<MeshPrimitiveData> primitives;
    std::vector<MeshNodeData> nodes;
    ModelBounds bounds{};
};
}
