#include "Game/PhysicalPlayground.h"

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Render/D3D12Renderer.h"

#include <sstream>
#include <string_view>

namespace DeepRun::Game
{
namespace
{
constexpr std::string_view SubmarineModelPath = "submarines/prototype/submarine_prototype.glb";
}

std::expected<void, std::string> PhysicalPlayground::Initialize(
    Assets::AssetManager& assets,
    Render::D3D12Renderer& renderer,
    const bool verifyDistinctUploads)
{
    const auto model = assets.LoadModel(SubmarineModelPath);
    if (!model)
    {
        std::ostringstream message;
        message << "Physical playground model load failed: " << model.error().message
                << " (" << model.error().path.string() << ')';
        return std::unexpected(message.str());
    }

    const auto upload = renderer.UploadModel(**model);
    if (!upload)
    {
        return std::unexpected(upload.error());
    }
    if (!upload->stats.uploadCompleted || !upload->handle.IsValid() ||
        !renderer.IsGpuModelValid(upload->handle))
    {
        return std::unexpected("physical playground GPU model handle validation failed");
    }
    const auto draws = Render::PrepareModelDraws(**model);
    if (!draws)
    {
        return std::unexpected(draws.error());
    }
    if (!renderer.IsModelPipelineReady() || !renderer.IsDepthBufferReady())
    {
        return std::unexpected("physical playground model pipeline or depth buffer is not ready");
    }

    modelAsset_ = *model;
    submarineModel_ = upload->handle;
    draws_ = *draws;

    if (verifyDistinctUploads)
    {
        const auto duplicateUpload = renderer.UploadModel(**model);
        if (!duplicateUpload)
        {
            return std::unexpected(duplicateUpload.error());
        }
        if (!duplicateUpload->handle.IsValid() || duplicateUpload->handle == submarineModel_ ||
            !renderer.IsGpuModelValid(duplicateUpload->handle) ||
            duplicateUpload->stats != upload->stats)
        {
            return std::unexpected("physical playground distinct GPU handle validation failed");
        }
    }
    return {};
}

std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer) const
{
    if (!modelAsset_.IsValid() || !renderer.IsGpuModelValid(submarineModel_))
    {
        return std::unexpected("physical playground model assets are no longer valid");
    }

    const auto camera = Render::BuildSideViewCamera(modelAsset_->bounds, renderer.AspectRatio());
    if (!camera || !Render::BoundsFitInCamera(modelAsset_->bounds, *camera))
    {
        return std::unexpected(camera ? "physical playground bounds do not fit the camera" : camera.error());
    }
    return renderer.DrawModel(submarineModel_, draws_, *camera);
}

Render::GpuModelHandle PhysicalPlayground::SubmarineModel() const noexcept
{
    return submarineModel_;
}
}
