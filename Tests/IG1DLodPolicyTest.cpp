#include "Game/Submarine/ProductionAnteyLodPolicy.h"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
using namespace DeepRun::Game::Submarine;

ProductionSubmarineAssetDefinition MakeDefinition()
{
    ProductionSubmarineAssetDefinition definition{};
    definition.assetFamilyId = "submarine.antey";
    constexpr std::array<std::string_view, 4> ids{
        "render.LOD0", "render.LOD1", "render.LOD2", "render.LOD3"};
    constexpr std::array<std::size_t, 4> vertices{65423U, 38283U, 23197U, 12227U};
    constexpr std::array<std::size_t, 4> triangles{118346U, 65009U, 35442U, 14311U};
    for (std::size_t i = 0; i < definition.renderLods.size(); ++i)
    {
        definition.renderLods[i].semanticId = std::string(ids[i]);
        definition.renderLods[i].objectCount = 66U;
        definition.renderLods[i].vertexCount = vertices[i];
        definition.renderLods[i].triangleCount = triangles[i];
    }
    const auto lod0 = DeepRun::Assets::AssetId::FromPath("submarines/Antey/Antey.glb");
    if (lod0)
    {
        definition.renderLods[0].stagedModelAssetId = *lod0;
    }
    return definition;
}

bool CurrentPackageFallsBackToLod0()
{
    const auto definition = MakeDefinition();
    for (const auto requested : {ProductionRenderLodLevel::Lod0, ProductionRenderLodLevel::Lod1,
                                 ProductionRenderLodLevel::Lod2, ProductionRenderLodLevel::Lod3})
    {
        const auto selected = SelectProductionAnteyRenderAsset(definition, requested);
        if (!selected || selected->selected != ProductionRenderLodLevel::Lod0 ||
            selected->assetId.Value() != "submarines/Antey/Antey.glb" ||
            selected->usedFallback != (requested != ProductionRenderLodLevel::Lod0))
        {
            return false;
        }
    }
    return true;
}

bool AvailableRequestedLodWins()
{
    auto definition = MakeDefinition();
    const auto lod2 = DeepRun::Assets::AssetId::FromPath("submarines/Antey/Antey_LOD2.glb");
    if (!lod2)
    {
        return false;
    }
    definition.renderLods[2].stagedModelAssetId = *lod2;
    const auto selected = SelectProductionAnteyRenderAsset(definition, ProductionRenderLodLevel::Lod2);
    return selected && selected->selected == ProductionRenderLodLevel::Lod2 && !selected->usedFallback &&
           selected->assetId.Value() == "submarines/Antey/Antey_LOD2.glb";
}

bool NearestMoreDetailedAvailableLodWinsFallback()
{
    auto definition = MakeDefinition();
    const auto lod1 = DeepRun::Assets::AssetId::FromPath("submarines/Antey/Antey_LOD1.glb");
    const auto lod2 = DeepRun::Assets::AssetId::FromPath("submarines/Antey/Antey_LOD2.glb");
    if (!lod1 || !lod2)
    {
        return false;
    }
    definition.renderLods[1].stagedModelAssetId = *lod1;
    definition.renderLods[2].stagedModelAssetId = *lod2;

    const auto selected = SelectProductionAnteyRenderAsset(definition, ProductionRenderLodLevel::Lod3);
    return selected && selected->selected == ProductionRenderLodLevel::Lod2 && selected->usedFallback &&
           selected->assetId.Value() == "submarines/Antey/Antey_LOD2.glb";
}

bool MissingMandatoryLod0Fails()
{
    auto definition = MakeDefinition();
    definition.renderLods[0].stagedModelAssetId.reset();
    return !ValidateProductionAnteyLodFamily(definition) &&
           !SelectProductionAnteyRenderAsset(definition, ProductionRenderLodLevel::Lod3);
}

bool InvalidFamilyFails()
{
    auto definition = MakeDefinition();
    definition.renderLods[2].triangleCount = definition.renderLods[1].triangleCount + 1U;
    return !ValidateProductionAnteyLodFamily(definition) &&
           !SelectProductionAnteyRenderAsset(definition, ProductionRenderLodLevel::Lod2);
}
} // namespace

int main()
{
    const bool ok = CurrentPackageFallsBackToLod0() && AvailableRequestedLodWins() &&
                    NearestMoreDetailedAvailableLodWinsFallback() && MissingMandatoryLod0Fails() &&
                    InvalidFamilyFails();
    std::cout << (ok ? "IG1-D LOD POLICY: PASS\n" : "IG1-D LOD POLICY: FAIL\n");
    return ok ? 0 : 1;
}
