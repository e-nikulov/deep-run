#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Render/DepthLighting.h"
#include "Engine/Render/TransientVfx.h"
#include "Game/Combat/CivilianVesselPresentation.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Weapons/P700LaunchVfx.h"
#include "Game/Weapons/ProductionP700Asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <filesystem>
#include <optional>
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
    Render::TransientVfxDrawStats p700VfxStats{};
    Armament::P700LaunchVfxFrame p700Vfx{};
    bool explosionDrawn = false;
};

// M5-H.1-B renderer composition for the bounded combat playground. Simulation, PhysicsWorld, TrackManager,
// target selection, damage and weapon lifecycle remain external authorities. P-700 launch VFX observes the
// same immutable runtime state and writes only scene-linear presentation plus semantic audio/camera hooks.
class CombatPlaygroundView final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundView, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets)
    {
        if (!renderer.IsInitialized() || !renderer.IsModelPipelineReady())
            return std::unexpected("M5-H.1 combat view requires an initialized model renderer");

        const auto proxyModel = BuildCombatPlaygroundPresentationModel();
        if (!proxyModel)
            return std::unexpected("M5-H.1 combat view model creation failed: " + proxyModel.error());
        const auto proxyUploaded = renderer.UploadModel(*proxyModel);
        if (!proxyUploaded || !proxyUploaded->handle.IsValid() || !proxyUploaded->stats.uploadCompleted ||
            proxyUploaded->stats.primitiveCount != 1U || proxyUploaded->stats.indexCount != 36U)
        {
            return std::unexpected(proxyUploaded
                ? "M5-H.1 combat view upload produced an invalid GPU model contract"
                : "M5-H.1 combat view upload failed: " + proxyUploaded.error());
        }

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

        const auto p700Definition = Armament::LoadProductionP700AssetDefinition(assets);
        if (!p700Definition)
            return std::unexpected("M5 production P-700 definition load failed: " + p700Definition.error());
        const auto p700 = assets.LoadModel(p700Definition->modelAssetId.Value());
        if (!p700 || !p700->IsValid() || p700->Get() == nullptr)
            return std::unexpected("M5 production P-700 GLB load failed");
        const auto p700Uploaded = renderer.UploadModel(**p700);
        if (!p700Uploaded || !p700Uploaded->handle.IsValid() || !p700Uploaded->stats.uploadCompleted)
        {
            return std::unexpected(p700Uploaded
                ? "M5 production P-700 upload produced an invalid GPU model"
                : "M5 production P-700 upload failed: " + p700Uploaded.error());
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

        Render::GpuTransientVfx transientVfx;
        if (const auto initialized = transientVfx.Initialize(renderer, "Shaders"); !initialized)
            return std::unexpected("P-700 transient VFX renderer initialization failed: " + initialized.error());

        Armament::P700LaunchVfxTuning tuning = Armament::DefaultP700LaunchVfxTuning();
        if (const auto authored = Armament::LoadP700LaunchVfxTuning(std::filesystem::path("Config") / "p700_vfx.json"); authored)
            tuning = *authored;

        return CombatPlaygroundView(
            proxyUploaded->handle,
            std::move(uset80Asset),
            uset80GpuModel,
            std::move(kit6576Asset),
            kit6576GpuModel,
            *p700Definition,
            *p700,
            p700Uploaded->handle,
            std::move(transientVfx),
            Armament::P700LaunchVfxSystem{tuning});
    }

    void SetP700VfxEnvironment(const Armament::P700LaunchVfxEnvironment& environment) noexcept
    {
        p700VfxEnvironment_ = environment;
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(
        Render::D3D12Renderer& renderer,
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        if (!ModelsValid(renderer) || !p700TransientVfx_.IsReady())
            return std::unexpected("M5 combat presentation GPU resources are unavailable");

        const auto snapshot = BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, simulationTimeSeconds);
        if (!snapshot)
            return std::unexpected("M5-H.1 combat view snapshot failed: " + snapshot.error());
        const auto presentationDraws = BuildCombatPlaygroundPresentationDraws(*snapshot);
        if (!presentationDraws)
            return std::unexpected("M5-H.1 combat view draw composition failed: " + presentationDraws.error());

        std::expected<std::vector<Render::ModelDrawInstance>, std::string> civilianDraws =
            std::vector<Render::ModelDrawInstance>{};
        if (!runtime.PlayerFogOfWarActive() ||
            runtime.PlayerHasVisualClassification(Perception::ContactClassification::CivilianSurfaceVessel))
            civilianDraws = BuildCivilianVesselPresentationDraws(runtime, physicsWorld);
        if (!civilianDraws)
            return std::unexpected("civilian surface-vessel presentation failed: " + civilianDraws.error());

        const bool useUset80Review = uset80Asset_.IsValid() && uset80Asset_.Get() != nullptr &&
            uset80GpuModel_.IsValid() && renderer.IsGpuModelValid(uset80GpuModel_);
        const bool useKit6576Review = kit6576Asset_.IsValid() && kit6576Asset_.Get() != nullptr &&
            kit6576GpuModel_.IsValid() && renderer.IsGpuModelValid(kit6576GpuModel_);

        std::vector<Render::ModelDrawInstance> proxyDraws;
        proxyDraws.reserve(presentationDraws->size() + civilianDraws->size());
        std::optional<Assets::ModelTransform> playerTorpedoTransform{};
        std::optional<Assets::ModelTransform> destroyerTorpedoTransform{};
        for (const auto& presentationDraw : *presentationDraws)
        {
            if (presentationDraw.element == CombatPlaygroundPresentationElement::PlayerTorpedo && useUset80Review)
                playerTorpedoTransform = presentationDraw.draw.modelToWorld;
            else if (presentationDraw.element == CombatPlaygroundPresentationElement::DestroyerTorpedo && useKit6576Review)
                destroyerTorpedoTransform = presentationDraw.draw.modelToWorld;
            else if (presentationDraw.element != CombatPlaygroundPresentationElement::P700LaunchBoosterPlume &&
                     presentationDraw.element != CombatPlaygroundPresentationElement::P700MainEnginePlume)
                proxyDraws.push_back(presentationDraw.draw);
        }
        proxyDraws.insert(proxyDraws.end(), civilianDraws->begin(), civilianDraws->end());
        if (proxyDraws.empty() && !runtime.PlayerFogOfWarActive())
            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");

        Render::ModelDrawStats totalStats{};
        const auto accumulate = [&totalStats](const Render::ModelDrawStats& stats)
        {
            totalStats.drawCalls += stats.drawCalls;
            totalStats.submittedPrimitives += stats.submittedPrimitives;
            totalStats.submittedIndices += stats.submittedIndices;
        };

        if (!proxyDraws.empty())
        {
            const auto proxyStats = renderer.DrawModel(
                proxyGpuModel_, std::span<const Render::ModelDrawInstance>(proxyDraws.data(), proxyDraws.size()), camera);
            if (!proxyStats)
                return std::unexpected("M5-H.1 combat proxy view draw failed: " + proxyStats.error());
            accumulate(*proxyStats);
        }

        const auto drawTorpedoModel = [&](const std::optional<Assets::ModelTransform>& transform,
                                          const Assets::AssetHandle<Assets::ModelAsset>& asset,
                                          const Render::GpuModelHandle gpuModel,
                                          const std::string_view label) -> std::expected<void, std::string>
        {
            if (!transform)
                return {};
            const Assets::ModelAsset* model = asset.Get();
            if (model == nullptr || !gpuModel.IsValid() || !renderer.IsGpuModelValid(gpuModel))
                return std::unexpected(std::string(label) + " playground model is unavailable");
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
                return std::unexpected(std::string(label) + " draw failed: " + stats.error());
            if (stats->drawCalls != draws->size() || stats->submittedPrimitives != draws->size() ||
                stats->submittedIndices == 0U)
                return std::unexpected(std::string(label) + " draw statistics are invalid");
            accumulate(*stats);
            return {};
        };

        if (const auto playerDraw = drawTorpedoModel(
                playerTorpedoTransform, uset80Asset_, uset80GpuModel_, "M5-V2 USET-80"); !playerDraw)
            return std::unexpected(playerDraw.error());
        if (const auto hostileDraw = drawTorpedoModel(
                destroyerTorpedoTransform, kit6576Asset_, kit6576GpuModel_, "M5-V2 65-76A"); !hostileDraw)
            return std::unexpected(hostileDraw.error());

        const auto drawP700 = [&](const CombatPlaygroundP700Presentation& p700) -> std::expected<void, std::string>
        {
            if (!p700Asset_.IsValid() || p700Asset_.Get() == nullptr || !p700GpuModel_.IsValid() ||
                !renderer.IsGpuModelValid(p700GpuModel_))
                return std::unexpected("M5 production P-700 presentation model is unavailable");
            const auto modelToWorld = CombatPlaygroundPresentationDetail::BodyPoseTransform(
                p700.positionMeters, Weapons::P700HeadingQuaternion(p700.headingRadians));
            if (!modelToWorld)
                return std::unexpected("M5 production P-700 pose failed: " + modelToWorld.error());
            const auto overrides = Armament::BuildProductionP700DeploymentOverrides(
                p700Definition_, p700.deploymentProgress);
            if (!overrides)
                return std::unexpected("M5 production P-700 deployment override failed: " + overrides.error());
            const auto prepared = Render::PrepareModelDraws(*p700Asset_, *modelToWorld, {}, *overrides);
            if (!prepared)
                return std::unexpected("M5 production P-700 draw preparation failed: " + prepared.error());
            std::vector<Render::ModelDrawInstance> p700Draws;
            for (const auto& draw : *prepared)
            {
                if (Armament::IsProductionP700Lod0MeshNode(p700Definition_, draw.nodeIndex) &&
                    (p700.launchBoosterAttached ||
                     !Armament::IsProductionP700BoosterLod0MeshNode(p700Definition_, draw.nodeIndex)))
                    p700Draws.push_back(draw);
            }
            if (p700Draws.empty())
                return std::unexpected("M5 production P-700 LOD0 produced no presentation draws");
            const auto stats = renderer.DrawModel(
                p700GpuModel_, std::span<const Render::ModelDrawInstance>(p700Draws.data(), p700Draws.size()), camera);
            if (!stats || stats->drawCalls != p700Draws.size() || stats->submittedIndices == 0U)
            {
                return std::unexpected(stats
                    ? "M5 production P-700 draw statistics are invalid"
                    : "M5 production P-700 draw failed: " + stats.error());
            }
            accumulate(*stats);

            // The authored booster remains a real mesh after separation. Presentation adds a small physical-looking
            // tumble and ballistic drop instead of hiding it; no gameplay collision/damage authority is created.
            if (!p700.launchBoosterAttached && p700.phase == Weapons::P700GranitPhase::PostExitTransition &&
                p700.postExitTransitionProgress < 0.95F)
            {
                const float p = p700.postExitTransitionProgress;
                const float angle = p * 0.55F;
                Assets::ModelTransform separation{};
                separation.values[0] = std::cos(angle);
                separation.values[1] = std::sin(angle);
                separation.values[4] = -std::sin(angle);
                separation.values[5] = std::cos(angle);
                separation.values[12] = -4.2F * p;
                separation.values[13] = -1.35F * p - 2.8F * p * p;
                separation.values[14] = 0.65F * p;
                const Assets::ModelTransform detachedToWorld = Render::Multiply(*modelToWorld, separation);
                const auto detachedPrepared = Render::PrepareModelDraws(*p700Asset_, detachedToWorld);
                if (!detachedPrepared)
                    return std::unexpected("M5 detached P-700 booster draw preparation failed: " + detachedPrepared.error());
                std::vector<Render::ModelDrawInstance> detached;
                for (const auto& draw : *detachedPrepared)
                {
                    if (Armament::IsProductionP700BoosterLod0MeshNode(p700Definition_, draw.nodeIndex))
                        detached.push_back(draw);
                }
                if (detached.empty())
                    return std::unexpected("M5 detached P-700 booster has no LOD0 draw");
                const auto detachedStats = renderer.DrawModel(
                    p700GpuModel_, std::span<const Render::ModelDrawInstance>(detached.data(), detached.size()), camera);
                if (!detachedStats)
                    return std::unexpected("M5 detached P-700 booster draw failed: " + detachedStats.error());
                accumulate(*detachedStats);
            }
            return {};
        };

        if (snapshot->playerP700)
            if (auto r = drawP700(*snapshot->playerP700); !r) return std::unexpected(r.error());
        for (const auto& wingman : snapshot->playerP700Wingmen)
            if (auto r = drawP700(wingman); !r) return std::unexpected(r.error());

        std::vector<const Weapons::P700GranitRuntimeState*> liveP700;
        liveP700.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());
        if (const auto& leader = runtime.PlayerP700(); leader)
            liveP700.push_back(&*leader);
        for (const auto& wingman : runtime.PlayerP700Wingmen())
            liveP700.push_back(&wingman);
        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles())
            liveP700.push_back(&ripple);

        auto p700VfxFrame = p700VfxSystem_.BuildFrame(liveP700, camera, p700VfxEnvironment_, simulationTimeSeconds);
        if (!p700VfxFrame)
            return std::unexpected("P-700 launch VFX frame build failed: " + p700VfxFrame.error());

        float surfaceLevel = 0.0F;
        if (!liveP700.empty() && liveP700.front() != nullptr)
            surfaceLevel = liveP700.front()->surfaceLevelYMeters;
        constexpr std::array<float, 3> OpenOceanAbsorption{0.045F, 0.020F, 0.010F};
        constexpr std::array<float, 3> OpenOceanDeepAmbient{0.006F, 0.024F, 0.050F};
        const Render::DepthLightingParameters vfxDepthLighting{
            .surfaceLevelYMeters = surfaceLevel,
            .attenuationPerMeterRgb = OpenOceanAbsorption,
            .deepAmbientRgb = OpenOceanDeepAmbient};
        const auto p700VfxStats = p700TransientVfx_.Draw(
            renderer,
            std::span<const Render::TransientVfxEmitter>(p700VfxFrame->emitters.data(), p700VfxFrame->emitters.size()),
            camera,
            vfxDepthLighting);
        if (!p700VfxStats)
            return std::unexpected("P-700 transient VFX draw failed: " + p700VfxStats.error());

        const bool historicalPresentationGate = !runtime.PlayerFogOfWarActive();
        if (totalStats.drawCalls < proxyDraws.size() || totalStats.submittedPrimitives != totalStats.drawCalls ||
            (historicalPresentationGate && totalStats.submittedIndices < 72U))
            return std::unexpected("M5-V2 combat view aggregate draw statistics are invalid");

        const bool explosionDrawn = std::ranges::any_of(
            *presentationDraws, [](const CombatPlaygroundPresentationDraw& draw) {
                return draw.element == CombatPlaygroundPresentationElement::Explosion;
            });
        return CombatPlaygroundRenderFrame{
            .presentation = *snapshot,
            .stats = totalStats,
            .p700VfxStats = *p700VfxStats,
            .p700Vfx = std::move(*p700VfxFrame),
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
            return std::unexpected(frame.error());
        return frame->stats;
    }

    [[nodiscard]] Render::GpuModelHandle Model() const noexcept
    {
        return proxyGpuModel_;
    }

    [[nodiscard]] bool ModelsValid(const Render::D3D12Renderer& renderer) const noexcept
    {
        return proxyGpuModel_.IsValid() && renderer.IsGpuModelValid(proxyGpuModel_) &&
               p700Asset_.IsValid() && p700Asset_.Get() != nullptr && p700GpuModel_.IsValid() &&
               renderer.IsGpuModelValid(p700GpuModel_);
    }

private:
    CombatPlaygroundView(
        const Render::GpuModelHandle proxyModel,
        Assets::AssetHandle<Assets::ModelAsset> uset80Asset,
        const Render::GpuModelHandle uset80Model,
        Assets::AssetHandle<Assets::ModelAsset> kit6576Asset,
        const Render::GpuModelHandle kit6576Model,
        Armament::ProductionP700AssetDefinition p700Definition,
        Assets::AssetHandle<Assets::ModelAsset> p700Asset,
        const Render::GpuModelHandle p700Model,
        Render::GpuTransientVfx p700TransientVfx,
        Armament::P700LaunchVfxSystem p700VfxSystem) noexcept
        : proxyGpuModel_(proxyModel),
          uset80Asset_(std::move(uset80Asset)),
          uset80GpuModel_(uset80Model),
          kit6576Asset_(std::move(kit6576Asset)),
          kit6576GpuModel_(kit6576Model),
          p700Definition_(std::move(p700Definition)),
          p700Asset_(std::move(p700Asset)),
          p700GpuModel_(p700Model),
          p700TransientVfx_(std::move(p700TransientVfx)),
          p700VfxSystem_(std::move(p700VfxSystem))
    {
    }

    Render::GpuModelHandle proxyGpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> uset80Asset_{};
    Render::GpuModelHandle uset80GpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> kit6576Asset_{};
    Render::GpuModelHandle kit6576GpuModel_{};
    Armament::ProductionP700AssetDefinition p700Definition_;
    Assets::AssetHandle<Assets::ModelAsset> p700Asset_{};
    Render::GpuModelHandle p700GpuModel_{};
    mutable Render::GpuTransientVfx p700TransientVfx_{};
    mutable Armament::P700LaunchVfxSystem p700VfxSystem_{};
    Armament::P700LaunchVfxEnvironment p700VfxEnvironment_{};
};
} // namespace DeepRun::Game::Combat
