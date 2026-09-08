#pragma once

#include "Game/Submarine/ProductionAnteyAsset.h"

#include <array>
#include <cstddef>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
enum class ProductionRenderLodLevel : std::size_t
{
    Lod0 = 0,
    Lod1 = 1,
    Lod2 = 2,
    Lod3 = 3,
};

struct ProductionRenderLodSelection final
{
    ProductionRenderLodLevel requested = ProductionRenderLodLevel::Lod0;
    ProductionRenderLodLevel selected = ProductionRenderLodLevel::Lod0;
    Assets::AssetId assetId;
    bool usedFallback = false;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateProductionAnteyLodFamily(
    const ProductionSubmarineAssetDefinition& definition)
{
    constexpr std::array<const char*, 4> ExpectedSemanticIds{
        "render.LOD0", "render.LOD1", "render.LOD2", "render.LOD3"};

    if (definition.assetFamilyId != "submarine.antey")
    {
        return std::unexpected("Antey render LOD validation received an unexpected asset family");
    }

    for (std::size_t index = 0; index < definition.renderLods.size(); ++index)
    {
        const ProductionRenderLod& lod = definition.renderLods[index];
        if (lod.semanticId != ExpectedSemanticIds[index])
        {
            return std::unexpected("Antey render LOD semantic identity/order is invalid");
        }
        if (lod.objectCount == 0U || lod.vertexCount == 0U || lod.triangleCount == 0U)
        {
            return std::unexpected("Antey render LOD metadata must contain non-zero topology counts");
        }
        if (index > 0U)
        {
            const ProductionRenderLod& previous = definition.renderLods[index - 1U];
            if (lod.vertexCount > previous.vertexCount || lod.triangleCount > previous.triangleCount)
            {
                return std::unexpected("Antey render LOD topology must not become denser at a coarser level");
            }
        }
    }

    // IG1-D requires one guaranteed normal-path render asset. The canonical package currently stages only
    // LOD0; metadata for LOD1-LOD3 remains source-family truth and must not be converted into fabricated files.
    if (!definition.renderLods[0].stagedModelAssetId.has_value())
    {
        return std::unexpected("Antey LOD0 must always be staged and available");
    }
    return {};
}

[[nodiscard]] inline std::expected<ProductionRenderLodSelection, std::string> SelectProductionAnteyRenderAsset(
    const ProductionSubmarineAssetDefinition& definition,
    const ProductionRenderLodLevel requested)
{
    if (const auto validated = ValidateProductionAnteyLodFamily(definition); !validated)
    {
        return std::unexpected(validated.error());
    }

    const std::size_t requestedIndex = static_cast<std::size_t>(requested);
    if (requestedIndex >= definition.renderLods.size())
    {
        return std::unexpected("Antey requested render LOD is outside the production family");
    }

    // Prefer the requested variant. If it is unavailable, fall back toward a more detailed available LOD
    // first. This preserves geometry correctness at the cost of performance and never invents an asset path.
    for (std::size_t candidateCount = requestedIndex + 1U; candidateCount > 0U; --candidateCount)
    {
        const std::size_t index = candidateCount - 1U;
        const ProductionRenderLod& candidate = definition.renderLods[index];
        if (candidate.stagedModelAssetId.has_value())
        {
            return ProductionRenderLodSelection{
                .requested = requested,
                .selected = static_cast<ProductionRenderLodLevel>(index),
                .assetId = *candidate.stagedModelAssetId,
                .usedFallback = index != requestedIndex};
        }
    }

    // This branch is defensive for future packages where LOD0 might intentionally cease to be mandatory.
    // It is unreachable for the accepted IG1 contract because validation above requires staged LOD0.
    for (std::size_t index = requestedIndex + 1U; index < definition.renderLods.size(); ++index)
    {
        const ProductionRenderLod& candidate = definition.renderLods[index];
        if (candidate.stagedModelAssetId.has_value())
        {
            return ProductionRenderLodSelection{
                .requested = requested,
                .selected = static_cast<ProductionRenderLodLevel>(index),
                .assetId = *candidate.stagedModelAssetId,
                .usedFallback = true};
        }
    }

    return std::unexpected("Antey production render family has no staged render variant");
}
} // namespace DeepRun::Game::Submarine
