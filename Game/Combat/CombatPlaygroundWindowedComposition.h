#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Combat/CombatPlaygroundView.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <cmath>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Game::Combat
{
// M5-H.1-B windowed Game composition. This object only joins already-bounded simulation and presentation
// contracts inside the Engine-owned PhysicsWorld/renderer lifetime. It does not introduce a scene graph,
// command bus, target shortcut, second physics clock or renderer-owned gameplay state.
class CombatPlaygroundWindowedComposition final
{
public:
    [[nodiscard]] static std::expected<CombatPlaygroundWindowedComposition, std::string> Create(
        Render::D3D12Renderer& renderer)
    {
        auto view = CombatPlaygroundView::Create(renderer);
        if (!view)
        {
            return std::unexpected("M5-H.1 windowed combat view creation failed: " + view.error());
        }
        return CombatPlaygroundWindowedComposition(std::move(*view));
    }

    // Deterministic H/H.1 smoke path retained unchanged in behavior.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        Physics::PhysicsWorld& physicsWorld,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, physicsWorld, simulationTimeSeconds);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
        const auto frame = runtime_->Advance(playerSnapshot, simulationTimeSeconds);
        if (!frame)
        {
            return std::unexpected("M5-H.1 windowed combat advance failed: " + frame.error());
        }
        return *frame;
    }

    // Normal-play J2 path. Semantic commands have already crossed the Input boundary; the composition does not
    // know physical bindings and does not retain a command queue between fixed ticks.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvancePlayerControlled(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        Physics::PhysicsWorld& physicsWorld,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, physicsWorld, simulationTimeSeconds);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
        const auto frame = runtime_->AdvancePlayerControlled(playerSnapshot, commands, simulationTimeSeconds);
        if (!frame)
        {
            return std::unexpected("M5-J2 windowed player-controlled combat advance failed: " + frame.error());
        }
        return *frame;
    }

    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        if (!runtime_.has_value())
        {
            return std::unexpected("M5-H.1 windowed combat runtime is not initialized yet");
        }
        return view_.Render(renderer, *runtime_, physicsWorld, camera, simulationTimeSeconds);
    }

    [[nodiscard]] const std::optional<CombatPlaygroundRuntime>& Runtime() const noexcept
    {
        return runtime_;
    }

    // Read-only resize acceptance hook. The view remains the owner of the opaque upload; callers only get
    // the renderer's validity result and cannot inspect or replace the GPU handle.
    [[nodiscard]] bool PresentationModelValid(const Render::D3D12Renderer& renderer) const noexcept
    {
        return view_.Model().IsValid() && renderer.IsGpuModelValid(view_.Model());
    }

private:
    explicit CombatPlaygroundWindowedComposition(CombatPlaygroundView view)
        : view_(std::move(view))
    {
    }

    [[nodiscard]] std::expected<void, std::string> EnsureRuntime(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        Physics::PhysicsWorld& physicsWorld,
        const double simulationTimeSeconds)
    {
        if (!physicsWorld.IsInitialized() || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        {
            return std::unexpected("M5-H.1 windowed combat advance input is invalid");
        }
        if (runtime_.has_value())
        {
            return {};
        }

        const double surfaceLevel = static_cast<double>(playerSnapshot.emitter.positionMeters.y) +
                                    static_cast<double>(playerSnapshot.signedDepthMeters);
        if (!std::isfinite(surfaceLevel))
        {
            return std::unexpected("M5-H.1 windowed combat could not derive the live surface level");
        }
        auto runtime = CombatPlaygroundRuntime::Create(
            physicsWorld, static_cast<float>(surfaceLevel), simulationTimeSeconds);
        if (!runtime)
        {
            return std::unexpected("M5-H.1 windowed combat runtime creation failed: " + runtime.error());
        }
        runtime_ = std::move(*runtime);
        return {};
    }

    CombatPlaygroundView view_;
    std::optional<CombatPlaygroundRuntime> runtime_{};
};
} // namespace DeepRun::Game::Combat
