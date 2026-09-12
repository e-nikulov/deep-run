#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"

#include <algorithm>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Combat
{
struct CombatPlaygroundRenderFrame final
{
    CombatPlaygroundPresentationSnapshot presentation{};
    Render::ModelDrawStats stats{};
    bool explosionDrawn = false;
};

// M5-H.1-B renderer composition for the bounded combat playground. The view owns only opaque GPU uploads.
// Simulation, PhysicsWorld, TrackManager, target selection, damage and weapon lifecycle all remain external
// authorities. Each frame is rebuilt from the read-only presentation snapshot defined by H.1-A.
class CombatPlaygroundView final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundView, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets)
    {
        if (!renderer.IsInitialized() || !renderer.IsModelPipelineReady())
        {
            return std::unexpected("M5-H.1 combat view requires an initialized model renderer");
        }

        const auto proxyModel = BuildCombatPlaygroundPresentationModel();
        if (!proxyModel)
        {
            return std::unexpected("M5-H.1 combat view model creation failed: " + proxyModel.error());
        }
        const auto proxyUploaded = renderer.UploadModel(*proxyModel);
        if (!proxyUploaded || !proxyUploaded->handle.IsValid() || !proxyUploaded->stats.uploadCompleted ||
            proxyUploaded->stats.primitiveCount != 1U || proxyUploaded->stats.indexCount != 36U)
        {
            return std::unexpected(proxyUploaded
                ? "M5-H.1 combat view upload produced an invalid GPU model contract"
                : "M5-H.1 combat view upload failed: " + proxyUploaded.error());
        }

        // M5-V2 review torpedo GLBs are optional presentation candidates, not simulation or startup authority.
        // A malformed/unpromoted review asset must never prevent the accepted M5 combat playground from starting.
        // When a candidate cannot be loaded/uploaded, that torpedo keeps the existing readable proxy draw.
        Assets::AssetHandle<Assets::ModelAsset> uset80Asset{};
        Render::GpuModelHandle uset80GpuModel{};
        const auto uset80 = assets.LoadModel("Weapons/Torpedoes/USET80/USET80_review.glb");
        if (uset80 && uset80->IsValid() && uset80->Get() != nullptr && !(*uset80)->primitives.empty())
        {
            const auto uploaded = renderer.UploadModel(**uset80);
            if (uploaded && uploaded->handle.IsValid() && uploaded->stats.uploadCompleted &&
                uploaded->stats.primitiveCount > 0U)
            {
                uset80Asset = *uset80;
                uset80GpuModel = uploaded->handle;
            }
        }

        Assets::AssetHandle<Assets::ModelAsset> kit6576Asset{};
        Render::GpuModelHandle kit6576GpuModel{};
        const auto kit6576 = assets.LoadModel("Weapons/Torpedoes/65-76A/65-76A_Kit_review.glb");
        if (kit6576 && kit6576->IsValid() && kit6576->Get() != nullptr && !(*kit6576)->primitives.empty())
        {
            const auto uploaded = renderer.UploadModel(**kit6576);
            if (uploaded && uploaded->handle.IsValid() && uploaded->stats.uploadCompleted &&
                uploaded->stats.primitiveCount > 0U)
            {
                kit6576Asset = *kit6576;
                kit6576GpuModel = uploaded->handle;
            }
        }

        return CombatPlaygroundView(
            proxyUploaded->handle,
            std::move(uset80Asset),
            uset80GpuModel,
            std::move(kit6576Asset),
            kit6576GpuModel);
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(
        Render::D3D12Renderer& renderer,
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        if (!ModelsValid(renderer))
        {
            return std::unexpected("M5-H.1 combat view GPU proxy model is unavailable");
        }

        const auto snapshot = BuildCombatPlaygroundPresentationSnapshot(
            runtime, physicsWorld, simulationTimeSeconds);
        if (!snapshot)
        {
            return std::unexpected("M5-H.1 combat view snapshot failed: " + snapshot.error());
        }
        const auto presentationDraws = BuildCombatPlaygroundPresentationDraws(*snapshot);
        if (!presentationDraws)
        {
            return std::unexpected("M5-H.1 combat view draw composition failed: " + presentationDraws.error());
        }

        const bool useUset80Review =
            uset80Asset_.IsValid() && uset80Asset_.Get() != nullptr && uset80GpuModel_.IsValid() &&
            renderer.IsGpuModelValid(uset80GpuModel_);
        const bool useKit6576Review =
            kit6576Asset_.IsValid() && kit6576Asset_.Get() != nullptr && kit6576GpuModel_.IsValid() &&
            renderer.IsGpuModelValid(kit6576GpuModel_);

        std::vector<Render::ModelDrawInstance> proxyDraws;
        proxyDraws.reserve(presentationDraws->size());
        std::optional<Assets::ModelTransform> playerTorpedoTransform{};
        std::optional<Assets::ModelTransform> destroyerTorpedoTransform{};
        for (const auto& presentationDraw : *presentationDraws)
        {
            if (presentationDraw.element == CombatPlaygroundPresentationElement::PlayerTorpedo && useUset80Review)
            {
                playerTorpedoTransform = presentationDraw.draw.modelToWorld;
            }
            else if (presentationDraw.element == CombatPlaygroundPresentationElement::DestroyerTorpedo && useKit6576Review)
            {
                destroyerTorpedoTransform = presentationDraw.draw.modelToWorld;
            }
            else
            {
                proxyDraws.push_back(presentationDraw.draw);
            }
        }
        if (proxyDraws.empty())
        {
            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");
        }

        Render::ModelDrawStats totalStats{};
        const auto accumulate = [&totalStats](const Render::ModelDrawStats& stats)
        {
            totalStats.drawCalls += stats.drawCalls;
            totalStats.submittedPrimitives += stats.submittedPrimitives;
            totalStats.submittedIndices += stats.submittedIndices;
        };

        const auto proxyStats = renderer.DrawModel(
            proxyGpuModel_, std::span<const Render::ModelDrawInstance>(proxyDraws.data(), proxyDraws.size()), camera);
        if (!proxyStats)
        {
            return std::unexpected("M5-H.1 combat proxy view draw failed: " + proxyStats.error());
        }
        accumulate(*proxyStats);

        const auto drawTorpedoModel = [&](const std::optional<Assets::ModelTransform>& transform,
                                          const Assets::AssetHandle<Assets::ModelAsset>& asset,
                                          const Render::GpuModelHandle gpuModel,
                                          const std::string_view label) -> std::expected<void, std::string>
        {
            if (!transform)
            {
                return {};
            }
            const Assets::ModelAsset* model = asset.Get();
            if (model == nullptr || !gpuModel.IsValid() || !renderer.IsGpuModelValid(gpuModel))
            {
                return std::unexpected(std::string(label) + " playground model is unavailable");
            }
            const auto draws = Render::PrepareModelDraws(*model, *transform);
            if (!draws || draws->empty())
            {
                return std::unexpected(draws
                    ? std::string(label) + " produced no presentation draws"
                    : std::string(label) + " draw preparation failed: " + draws.error());
            }
            const auto stats = renderer.DrawModel(
                gpuModel, std::span<const Render::ModelDrawInstance>(draws->data(), draws->size()), camera);
            if (!stats)
            {
                return std::unexpected(std::string(label) + " draw failed: " + stats.error());
            }
            if (stats->drawCalls != draws->size() || stats->submittedPrimitives != draws->size() ||
                stats->submittedIndices == 0U)
            {
                return std::unexpected(std::string(label) + " draw statistics are invalid");
            }
            accumulate(*stats);
            return {};
        };

        // When the review candidates are valid, exercise them at authored metre scale. Otherwise the torpedo
        // remains in proxyDraws above. This fallback affects presentation only; weapon simulation is identical.
        if (const auto playerDraw = drawTorpedoModel(
                playerTorpedoTransform, uset80Asset_, uset80GpuModel_, "M5-V2 USET-80"); !playerDraw)
        {
            return std::unexpected(playerDraw.error());
        }
        if (const auto hostileDraw = drawTorpedoModel(
                destroyerTorpedoTransform, kit6576Asset_, kit6576GpuModel_, "M5-V2 65-76A"); !hostileDraw)
        {
            return std::unexpected(hostileDraw.error());
        }
        if (totalStats.drawCalls < proxyDraws.size() ||
            totalStats.submittedPrimitives != totalStats.drawCalls || totalStats.submittedIndices < 72U)
        {
            return std::unexpected("M5-V2 combat view aggregate draw statistics are invalid");
        }
        const bool explosionDrawn = std::ranges::any_of(
            *presentationDraws, [](const CombatPlaygroundPresentationDraw& draw) {
                return draw.element == CombatPlaygroundPresentationElement::Explosion;
            });
        return CombatPlaygroundRenderFrame{
            .presentation = *snapshot,
            .stats = totalStats,
            .explosionDrawn = explosionDrawn};
    }

    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        const auto frame = RenderWithPresentation(renderer, runtime, physicsWorld, camera, simulationTimeSeconds);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        return frame->stats;
    }

    [[nodiscard]] Render::GpuModelHandle Model() const noexcept
    {
        return proxyGpuModel_;
    }

    [[nodiscard]] bool ModelsValid(const Render::D3D12Renderer& renderer) const noexcept
    {
        // Review torpedo candidates are intentionally optional. The canonical combat proxy model is the only
        // presentation resource required for M5 startup and smoke stability.
        return proxyGpuModel_.IsValid() && renderer.IsGpuModelValid(proxyGpuModel_);
    }

private:
    CombatPlaygroundView(
        const Render::GpuModelHandle proxyModel,
        Assets::AssetHandle<Assets::ModelAsset> uset80Asset,
        const Render::GpuModelHandle uset80Model,
        Assets::AssetHandle<Assets::ModelAsset> kit6576Asset,
        const Render::GpuModelHandle kit6576Model) noexcept
        : proxyGpuModel_(proxyModel),
          uset80Asset_(std::move(uset80Asset)),
          uset80GpuModel_(uset80Model),
          kit6576Asset_(std::move(kit6576Asset)),
          kit6576GpuModel_(kit6576Model)
    {
    }

    Render::GpuModelHandle proxyGpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> uset80Asset_{};
    Render::GpuModelHandle uset80GpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> kit6576Asset_{};
    Render::GpuModelHandle kit6576GpuModel_{};
};
} // namespace DeepRun::Game::Combat
