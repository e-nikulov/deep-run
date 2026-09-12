#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Combat/CombatPlaygroundView.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Game/Submarine/ProductionAnteyAsset.h"
#include "Game/Weapons/P700CarrierLaunchContract.h"

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
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false,
        const float destroyerCruiseVelocityXMetersPerSecond = M5CombatDestroyerCruiseVelocityXMetersPerSecond)
    {
        auto view = CombatPlaygroundView::Create(renderer, assets);
        if (!view)
        {
            return std::unexpected("M5-H.1 windowed combat view creation failed: " + view.error());
        }

        const auto anteyDefinition = Submarine::LoadProductionAnteyAssetDefinition(assets);
        if (!anteyDefinition)
        {
            return std::unexpected(
                "M5 P-700 carrier production Antey definition load failed: " + anteyDefinition.error());
        }
        if (anteyDefinition->collisionProxies.size() != 1U)
        {
            return std::unexpected("M5 P-700 carrier requires exactly one accepted production collision proxy");
        }
        auto p700Carrier = Armament::P700CarrierLaunchContract::Create(
            anteyDefinition->p700LaunchAnchors,
            anteyDefinition->collisionProxies.front().localCenter);
        if (!p700Carrier)
        {
            return std::unexpected("M5 P-700 carrier launch contract creation failed: " + p700Carrier.error());
        }
        return CombatPlaygroundWindowedComposition(
            std::move(*view), std::move(*p700Carrier), destroyerInitialXMeters, p700AcceptanceMode,
            destroyerCruiseVelocityXMetersPerSecond);
    }

    // Deterministic H/H.1 smoke path retained unchanged in behavior.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
        Physics::PhysicsWorld& physicsWorld,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, playerCollisionProxy, physicsWorld, simulationTimeSeconds);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);
        if (!synced)
        {
            return std::unexpected("M5-I.2 windowed player physical proxy update failed: " + synced.error());
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
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
        Physics::PhysicsWorld& physicsWorld,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, playerCollisionProxy, physicsWorld, simulationTimeSeconds);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);
        if (!synced)
        {
            return std::unexpected("M5-I.2 windowed player physical proxy update failed: " + synced.error());
        }
        const auto frame = runtime_->AdvancePlayerControlled(playerSnapshot, commands, simulationTimeSeconds);
        if (!frame)
        {
            return std::unexpected("M5-J2 windowed player-controlled combat advance failed: " + frame.error());
        }
        return *frame;
    }

    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvanceP700Acceptance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
        Physics::PhysicsWorld& physicsWorld,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, playerCollisionProxy, physicsWorld, simulationTimeSeconds);
        if (!ready) return std::unexpected(ready.error());
        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);
        if (!synced) return std::unexpected("M5 P-700 acceptance physical proxy update failed: " + synced.error());
        const auto frame = runtime_->AdvanceP700Acceptance(playerSnapshot, simulationTimeSeconds);
        if (!frame) return std::unexpected("M5 P-700 acceptance advance failed: " + frame.error());
        return *frame;
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(
        Render::D3D12Renderer& renderer,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        if (!runtime_.has_value())
        {
            return std::unexpected("M5-H.1 windowed combat runtime is not initialized yet");
        }
        return view_.RenderWithPresentation(renderer, *runtime_, physicsWorld, camera, simulationTimeSeconds);
    }

    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const double simulationTimeSeconds) const
    {
        const auto frame = RenderWithPresentation(renderer, physicsWorld, camera, simulationTimeSeconds);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        return frame->stats;
    }

    [[nodiscard]] const std::optional<CombatPlaygroundRuntime>& Runtime() const noexcept
    {
        return runtime_;
    }

    // Read-only resize acceptance hook. The view remains the owner of the opaque upload; callers only get
    // the renderer's validity result and cannot inspect or replace the GPU handle.
    [[nodiscard]] bool PresentationModelValid(const Render::D3D12Renderer& renderer) const noexcept
    {
        return view_.ModelsValid(renderer);
    }

private:
    CombatPlaygroundWindowedComposition(
        CombatPlaygroundView view,
        Armament::P700CarrierLaunchContract p700CarrierLaunchContract,
        const float destroyerInitialXMeters,
        const bool p700AcceptanceMode,
        const float destroyerCruiseVelocityXMetersPerSecond)
        : view_(std::move(view)),
          p700CarrierLaunchContract_(std::move(p700CarrierLaunchContract)),
          destroyerInitialXMeters_(destroyerInitialXMeters),
          p700AcceptanceMode_(p700AcceptanceMode),
          destroyerCruiseVelocityXMetersPerSecond_(destroyerCruiseVelocityXMetersPerSecond)
    {
    }

    [[nodiscard]] std::expected<void, std::string> EnsureRuntime(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
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
        auto p700Inventory = p700CarrierLaunchContract_.CreateInventory();
        if (!p700Inventory)
        {
            return std::unexpected("M5 P-700 windowed launcher inventory creation failed: " + p700Inventory.error());
        }
        auto runtime = CombatPlaygroundRuntime::Create(
            physicsWorld,
            static_cast<float>(surfaceLevel),
            simulationTimeSeconds,
            p700CarrierLaunchContract_,
            std::move(*p700Inventory),
            destroyerInitialXMeters_,
            p700AcceptanceMode_,
            destroyerCruiseVelocityXMetersPerSecond_);
        if (!runtime)
        {
            return std::unexpected("M5-H.1 windowed combat runtime creation failed: " + runtime.error());
        }
        const auto boundPlayer = runtime->BindPlayerPhysicalProxy(
            playerCollisionProxy, playerSnapshot, simulationTimeSeconds);
        if (!boundPlayer)
        {
            return std::unexpected("M5-I.2 windowed player physical proxy binding failed: " + boundPlayer.error());
        }
        runtime_ = std::move(*runtime);
        return {};
    }

    CombatPlaygroundView view_;
    Armament::P700CarrierLaunchContract p700CarrierLaunchContract_;
    float destroyerInitialXMeters_ = M5CombatDestroyerInitialXMeters;
    bool p700AcceptanceMode_ = false;
    float destroyerCruiseVelocityXMetersPerSecond_ = M5CombatDestroyerCruiseVelocityXMetersPerSecond;
    std::optional<CombatPlaygroundRuntime> runtime_{};
};
} // namespace DeepRun::Game::Combat
