#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"
#include "Simulation/Marine/BuoyancyComponent.h"
#include "Simulation/Marine/WaterBody.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Assets
{
struct ModelAsset;
}

namespace DeepRun::Physics
{
class PhysicsWorld;
}

namespace DeepRun::Render
{
class D3D12Renderer;
}

namespace DeepRun::Game
{
// M2 Slice C2: the canonical submarine is rendered from authoritative Jolt rigid-body state.
//
// Authority flow (the only allowed direction):
//   PhysicsWorld (Jolt) -> GetBodyState(handle) value copy -> modelToWorld -> renderer.
// The playground owns a non-owning PhysicsBodyHandle plus a non-owning PhysicsWorld reference.
// Lifetime assumption: the Engine owns the PhysicsWorld and outlives all playground rendering
// during Application::Run; no shared ownership is created here (ADR-0008).
//
// M2 Slice D2/E3: the scenario owns its authoritative Marine::WaterBody as a plain value — the single
// source of truth for sea level and signed depth. The Game reads it (surface level, body-center depth) and
// derives presentation from those values; the renderer never sees a WaterBody, and the WaterBody never
// knows about the renderer, camera or submarine. E3 composes that water with a Game-owned buoyancy point
// configuration and the generic PhysicsWorld force API during the fixed phase.
class PhysicalPlayground final
{
public:
    // verifyDistinctUploads keeps the B2 GPU-handle regression check active in smoke runs.
    [[nodiscard]] std::expected<void, std::string> Initialize(
        Assets::AssetManager& assets,
        Physics::PhysicsWorld& physics,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads);

    // Produces and applies transient buoyancy point forces for one authoritative fixed tick. The Engine
    // calls this before PhysicsWorld::Step; this function never steps physics and never scales force by dt.
    [[nodiscard]] std::expected<void, std::string> FixedUpdate(float fixedDeltaSeconds);

    // Reads one body state copy and feeds it to all node draws. Must be called after the engine's
    // fixed-step update for the frame; never steps physics itself. Also paints the D2 flat-water
    // cross-section (generic clear-rect below the projected authoritative surface) before the submarine.
    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer) const;

    [[nodiscard]] Render::GpuModelHandle SubmarineModel() const noexcept;

private:
    Assets::AssetHandle<Assets::ModelAsset> modelAsset_;
    Render::GpuModelHandle submarineModel_;
    Physics::PhysicsBodyHandle physicsBody_;
    // Non-owning: the Engine owns the world and outlives this playground (see class comment).
    Physics::PhysicsWorld* physics_ = nullptr;

    // M2 scenario composition (D2): the authoritative water body, owned by value as part of this concrete
    // scenario. No MarineEnvironment/global/singleton — just a member of the playground that composes it.
    std::optional<Marine::WaterBody> water_;
    // Explicit Game-owned M2 prototype tuning. Marine owns only the generic point/component data types.
    Marine::BuoyancyComponent buoyancy_;

    // Asset-space pivot: (bounds.min + bounds.max) * 0.5. Used ONLY for modelToBody = T(-assetBoundsCenter).
    Assets::ModelVector3 assetBoundsCenter_{};
    // World-space initial body center: X/Z from the asset bounds center, Y from WaterBody surface level and
    // M2InitialSubmarineDepthMeters (never from the asset Y center). Also the fixed B2.1 camera target.
    Physics::PhysicsVector3 initialBodyWorldCenter_{};
    Assets::ModelTransform modelToBody_{};

    // Bounded E3 diagnostics: physics in FixedUpdate; camera/water presentation in Render.
    std::uint64_t fixedTickCount_ = 0;
    bool loggedFirstFixedSample_ = false;
    bool loggedLaterFixedSample_ = false;
    mutable bool loggedRenderPresentation_ = false;
};
}
