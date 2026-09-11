#pragma once

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

// M5-H.1-B renderer composition for the bounded combat playground. The view owns only an opaque GPU upload.
// Simulation, PhysicsWorld, TrackManager, target selection, damage and weapon lifecycle all remain external
// authorities. Each frame is rebuilt from the read-only presentation snapshot defined by H.1-A.
class CombatPlaygroundView final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundView, std::string> Create(
        Render::D3D12Renderer& renderer)
    {
        if (!renderer.IsInitialized() || !renderer.IsModelPipelineReady())
        {
            return std::unexpected("M5-H.1 combat view requires an initialized model renderer");
        }

        const auto model = BuildCombatPlaygroundPresentationModel();
        if (!model)
        {
            return std::unexpected("M5-H.1 combat view model creation failed: " + model.error());
        }
        const auto uploaded = renderer.UploadModel(*model);
        if (!uploaded || !uploaded->handle.IsValid() || !uploaded->stats.uploadCompleted ||
            uploaded->stats.primitiveCount != 1U || uploaded->stats.indexCount != 36U)
        {
            return std::unexpected(uploaded
                ? "M5-H.1 combat view upload produced an invalid GPU model contract"
                : "M5-H.1 combat view upload failed: " + uploaded.error());
        }
        return CombatPlaygroundView(uploaded->handle);
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(
        Render::D3D12Renderer& renderer,
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        if (!gpuModel_.IsValid() || !renderer.IsGpuModelValid(gpuModel_))
        {
            return std::unexpected("M5-H.1 combat view GPU model is unavailable");
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

        std::vector<Render::ModelDrawInstance> draws;
        draws.reserve(presentationDraws->size());
        for (const auto& presentationDraw : *presentationDraws)
        {
            draws.push_back(presentationDraw.draw);
        }
        if (draws.empty())
        {
            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");
        }

        const auto stats = renderer.DrawModel(
            gpuModel_, std::span<const Render::ModelDrawInstance>(draws.data(), draws.size()), camera);
        if (!stats)
        {
            return std::unexpected("M5-H.1 combat view draw failed: " + stats.error());
        }
        if (stats->drawCalls != draws.size() || stats->submittedPrimitives != draws.size() ||
            stats->submittedIndices != draws.size() * 36U)
        {
            return std::unexpected("M5-H.1 combat view renderer statistics violated the one-cube-per-element contract");
        }
        const bool explosionDrawn = std::ranges::any_of(
            *presentationDraws, [](const CombatPlaygroundPresentationDraw& draw) {
                return draw.element == CombatPlaygroundPresentationElement::Explosion;
            });
        return CombatPlaygroundRenderFrame{
            .presentation = *snapshot,
            .stats = *stats,
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
        return gpuModel_;
    }

private:
    explicit CombatPlaygroundView(const Render::GpuModelHandle model) noexcept
        : gpuModel_(model)
    {
    }

    Render::GpuModelHandle gpuModel_{};
};
} // namespace DeepRun::Game::Combat
