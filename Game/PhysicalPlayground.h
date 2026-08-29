#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

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
class PhysicalPlayground final
{
public:
    // verifyDistinctUploads keeps the B2 GPU-handle regression check active in smoke runs.
    [[nodiscard]] std::expected<void, std::string> Initialize(
        Assets::AssetManager& assets,
        Physics::PhysicsWorld& physics,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads);

    // Reads one body state copy and feeds it to all node draws. Must be called after the engine's
    // fixed-step update for the frame; never steps physics itself.
    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer) const;

    [[nodiscard]] Render::GpuModelHandle SubmarineModel() const noexcept;

private:
    Assets::AssetHandle<Assets::ModelAsset> modelAsset_;
    Render::GpuModelHandle submarineModel_;
    Physics::PhysicsBodyHandle physicsBody_;
    // Non-owning: the Engine owns the world and outlives this playground (see class comment).
    Physics::PhysicsWorld* physics_ = nullptr;

    // M2 physical tuning, derived once from ModelAsset::bounds at startup.
    Assets::ModelVector3 boundsCenter_{};
    Assets::ModelTransform modelToBody_{};
    std::vector<Render::ModelDrawInstance> draws_;

    // Bounded smoke diagnostics: the first and a later physical render sample.
    mutable std::uint64_t renderSampleCount_ = 0;
    mutable bool loggedFirstSample_ = false;
    mutable bool loggedLaterSample_ = false;
};
}
