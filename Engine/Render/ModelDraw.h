#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Render/Camera.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Render
{
struct NormalTransform final
{
    // Three row vectors padded to float4 for root-constant packing.
    std::array<float, 12> rows{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F};
};

struct ModelDrawInstance final
{
    std::size_t nodeIndex = 0;
    std::size_t primitiveIndex = 0;
    Assets::ModelTransform modelToWorld{};
    NormalTransform normalToWorld{};
    Assets::ModelMaterialData material;
};

struct ModelDrawStats final
{
    std::uint32_t drawCalls = 0;
    std::uint32_t submittedPrimitives = 0;
    std::uint64_t submittedIndices = 0;
};

// Optional presentation-only transform for one model node. The post-transform is composed after the
// immutable authored node transform; ModelAsset data is never mutated.
struct ModelNodeTransformOverride final
{
    std::size_t nodeIndex = 0;
    Assets::ModelTransform nodeLocalPostTransform{};
};

[[nodiscard]] Assets::ModelMaterialData DefaultModelMaterial();
[[nodiscard]] Assets::ModelTransform Multiply(
    const Assets::ModelTransform& left,
    const Assets::ModelTransform& right) noexcept;
[[nodiscard]] std::expected<NormalTransform, std::string> BuildNormalTransform(
    const Assets::ModelTransform& transform);
[[nodiscard]] std::array<float, 3> TransformNormal(
    const NormalTransform& transform,
    const std::array<float, 3>& normal) noexcept;
[[nodiscard]] std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(
    const Assets::ModelAsset& model,
    const Assets::ModelTransform& modelToWorld = {},
    std::span<const ModelNodeTransformOverride> nodeTransformOverrides = {});
}
