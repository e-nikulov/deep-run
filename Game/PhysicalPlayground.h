#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"

#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Render
{
class D3D12Renderer;
}

namespace DeepRun::Game
{
class PhysicalPlayground final
{
public:
    [[nodiscard]] std::expected<void, std::string> Initialize(
        Assets::AssetManager& assets,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads);
    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer) const;

    [[nodiscard]] Render::GpuModelHandle SubmarineModel() const noexcept;

private:
    Assets::AssetHandle<Assets::ModelAsset> modelAsset_;
    Render::GpuModelHandle submarineModel_;
    std::vector<Render::ModelDrawInstance> draws_;
};
}
