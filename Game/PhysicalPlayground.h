#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Environment/EnvironmentSection.h"
#include "Game/Environment/UnderwaterFaunaField.h"
#include "Game/Environment/UnderwaterFloraField.h"
#include "Game/Environment/UnderwaterIceField.h"
#include "Game/Haptics/HapticEvent.h"
#include "Game/PhysicsRenderSync.h"
#include "Game/Submarine/AnteyAcousticRuntimeBridge.h"
#include "Game/Submarine/VesselCommandState.h"
#include "Simulation/Marine/BuoyancyComponent.h"
#include "Simulation/Marine/BuoyancySystem.h"
#include "Simulation/Marine/ControlSurfaceComponent.h"
#include "Simulation/Marine/HydroDragComponent.h"
#include "Simulation/Marine/PropulsionComponent.h"
#include "Simulation/Marine/PropulsionSystem.h"
#include "Simulation/Marine/WaterBody.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
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
// M2 Slice D2/E3/F2/G2/H2/I1: the scenario owns its authoritative Marine::WaterBody as a plain value — the single
// source of truth for sea level and signed depth. The Game reads it (surface level, body-center depth) and
// derives presentation from those values; the renderer never sees a WaterBody, and the WaterBody never
// knows about the renderer, camera or submarine. Game composes that water with Game-owned buoyancy/drag
// tuning and generic PhysicsWorld force/torque operations during the fixed phase. G2 adds one authoritative
// shaft state; propeller angle is presentation-only and can never feed the simulation. H2 composes two
// Game-owned control surfaces through their pure Marine calculation and the existing world-point force API. I1
// receives only a Game-owned VesselCommandState; it never sees a physical key, gamepad field, or backend type.
// I2 publishes a Game-owned semantic feedback event after authoritative propulsion state commit; it never sees
// a motor value or platform backend, and feedback success never participates in the simulation transaction.
class PhysicalPlayground final
{
public:
    using HapticEventSink = std::function<void(const HapticEvent&)>;

    // verifyDistinctUploads keeps the B2 GPU-handle regression check active in smoke runs.
    [[nodiscard]] std::expected<void, std::string> Initialize(
        Assets::AssetManager& assets,
        Physics::PhysicsWorld& physics,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads);

    // Produces and applies transient marine forces/torques from one authoritative body snapshot for one
    // fixed tick at beginning-of-step SimulationTime. The Engine calls this before PhysicsWorld::Step; this
    // function never steps physics and never scales force or torque by dt.
    [[nodiscard]] std::expected<void, std::string> FixedUpdate(
        float fixedDeltaSeconds,
        double simulationTimeSeconds,
        const VesselCommandState& command,
        const HapticEventSink& hapticEventSink = {});

    // M4 live read-only bridge. Call only after a successful FixedUpdate transaction: propulsionState_ then
    // contains the committed shaft RPM, while the current Jolt body copy and WaterBody sample remain the sole
    // position/velocity/depth authorities. Acoustics cannot write back into physics, water or propulsion.
    [[nodiscard]] std::expected<Submarine::AnteyAcousticSnapshot, std::string> BuildAcousticSnapshot(
        const Acoustics::AcousticSpectrum& ambientNoiseLevelDb) const
    {
        if (physics_ == nullptr || !physicsBody_.IsValid() || !water_.has_value())
        {
            return std::unexpected("physical playground live acoustic authorities are unavailable");
        }
        const auto bodyState = physics_->GetBodyState(physicsBody_);
        if (!bodyState)
        {
            return std::unexpected("physical playground live acoustic body state is unavailable");
        }
        const auto waterSample = water_->Sample(bodyState->position);
        if (!waterSample)
        {
            return std::unexpected("physical playground live acoustic water sample failed: " +
                                   waterSample.error().message);
        }
        const auto runtimeState = Submarine::ComposeAnteyAcousticRuntimeState(
            *bodyState, *waterSample, propulsionState_);
        if (!runtimeState)
        {
            return std::unexpected("physical playground live acoustic composition failed: " + runtimeState.error());
        }
        const auto snapshot = Submarine::BuildAnteyAcousticSnapshot(*runtimeState, ambientNoiseLevelDb);
        if (!snapshot)
        {
            return std::unexpected("physical playground live acoustic snapshot failed: " + snapshot.error());
        }
        return *snapshot;
    }

    // M5 combat framing is presentation policy only. initialBodyWorldCenter_ is the stored fixed camera target
    // after Initialize; the physical body was already created from the local initialization value and does not
    // read this member again. Applying an offset therefore cannot move Jolt/world truth. Zero restores the
    // accepted M2/M3 centered camera contract used by the dedicated M3 benchmark.
    [[nodiscard]] std::expected<void, std::string> SetPresentationCameraTargetOffsetXMeters(const float offsetMeters)
    {
        if (!std::isfinite(offsetMeters))
        {
            return std::unexpected("physical playground presentation camera offset must be finite");
        }
        initialBodyWorldCenter_.x += offsetMeters - presentationCameraTargetOffsetXMeters_;
        presentationCameraTargetOffsetXMeters_ = offsetMeters;
        return {};
    }

    // M5-H.1-B read-only camera bridge for additional Game presentation consumers. This deliberately mirrors
    // the same target/span/depth policy consumed by PhysicalPlayground::Render, so combat proxies and the
    // production scene share one projection rather than drifting through separate camera transforms.
    [[nodiscard]] std::expected<Render::OrthographicCamera, std::string> BuildPresentationCamera(
        Render::D3D12Renderer& renderer,
        const double presentationTimeSeconds) const
    {
        if (!modelAsset_.IsValid() || !surfaceFloatModel_.has_value() || !faunaField_.has_value() ||
            !seabedSection_.has_value() || !floraField_.has_value() || !iceField_.has_value() ||
            physics_ == nullptr || !physicsBody_.IsValid() || !surfaceFloatBody_.IsValid())
        {
            return std::unexpected("physical playground presentation camera authorities are unavailable");
        }

        const auto bodyState = physics_->GetBodyState(physicsBody_);
        const auto surfaceFloatState = physics_->GetBodyState(surfaceFloatBody_);
        if (!bodyState || !surfaceFloatState)
        {
            return std::unexpected("physical playground presentation camera body state is unavailable");
        }
        const auto bodyToWorld = BuildBodyToWorld(*bodyState);
        const auto surfaceFloatToWorld = BuildBodyToWorld(*surfaceFloatState);
        const auto faunaToWorld = EvaluateUnderwaterFishSchoolPresentation(
            faunaField_->presentation, presentationTimeSeconds);
        if (!bodyToWorld || !surfaceFloatToWorld || !faunaToWorld)
        {
            return std::unexpected("physical playground presentation camera transforms are unavailable");
        }

        const Assets::ModelTransform modelToWorld = Render::Multiply(*bodyToWorld, modelToBody_);
        const auto worldBounds = TransformBounds(modelAsset_->bounds, modelToWorld);
        const auto surfaceFloatWorldBounds = TransformBounds(surfaceFloatModel_->bounds, *surfaceFloatToWorld);
        const auto faunaWorldBounds = TransformBounds(faunaField_->renderGeometry.bounds, *faunaToWorld);
        if (!worldBounds || !surfaceFloatWorldBounds || !faunaWorldBounds)
        {
            return std::unexpected("physical playground presentation camera bounds transform failed");
        }

        const auto combineBounds = [](const Assets::ModelBounds& first, const Assets::ModelBounds& second) noexcept
        {
            return Assets::ModelBounds{
                .minimum = {
                    (std::min)(first.minimum.x, second.minimum.x),
                    (std::min)(first.minimum.y, second.minimum.y),
                    (std::min)(first.minimum.z, second.minimum.z)},
                .maximum = {
                    (std::max)(first.maximum.x, second.maximum.x),
                    (std::max)(first.maximum.y, second.maximum.y),
                    (std::max)(first.maximum.z, second.maximum.z)}};
        };
        const Assets::ModelBounds cameraDepthBounds = combineBounds(
            combineBounds(
                combineBounds(
                    combineBounds(
                        combineBounds(*worldBounds, seabedSection_->renderGeometry.bounds),
                        floraField_->renderGeometry.bounds),
                    iceField_->renderGeometry.bounds),
                *faunaWorldBounds),
            *surfaceFloatWorldBounds);
        const Assets::ModelVector3 target{
            initialBodyWorldCenter_.x, initialBodyWorldCenter_.y, initialBodyWorldCenter_.z};
        constexpr float fixedHorizontalSpanMeters = 600.0F; // accepted B2.1 span; M5 changes framing only
        return Render::BuildFixedWorldSideViewCamera(
            target, renderer.AspectRatio(), fixedHorizontalSpanMeters, cameraDepthBounds);
    }

    // Reads one body state copy and feeds it to all node draws. Must be called after the engine's
    // fixed-step update for the frame; never steps physics itself. presentationTimeSeconds is the Engine's
    // existing frame clock supplied for presentation-only motion; it is not simulation time.
    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        double simulationTimeSeconds,
        double presentationTimeSeconds) const;

    [[nodiscard]] Render::GpuModelHandle SubmarineModel() const noexcept;

private:
    Assets::AssetHandle<Assets::ModelAsset> modelAsset_;
    Render::GpuModelHandle submarineModel_;
    // M3-B authoritative environment data remains Game-owned. The GPU model and prepared draws are
    // presentation-only state; neither replaces the stable section ID, bounds, or renderGeometry data.
    std::optional<EnvironmentSection> seabedSection_;
    // Non-owning handles; PhysicsWorld destroys the static bodies at world shutdown.
    std::vector<Physics::PhysicsBodyHandle> seabedBodies_;
    Render::GpuModelHandle seabedModel_;
    std::vector<Render::ModelDrawInstance> seabedDraws_;
    // M3-G immutable presentation-only vegetation. Its profile-derived roots and ModelAsset stay Game-owned;
    // the renderer owns only this opaque GPU upload and does not feed environment truth back into Game.
    std::optional<UnderwaterFloraField> floraField_;
    Render::GpuModelHandle floraModel_;
    std::vector<Render::ModelDrawInstance> floraDraws_;
    // M3-H static authored environment: one Game-owned ice field supplies independent faceted presentation
    // and two deliberately coarse static-box descriptions. PhysicsWorld owns only the resulting body handles.
    std::optional<UnderwaterIceField> iceField_;
    std::vector<Physics::PhysicsBodyHandle> iceBodies_;
    Render::GpuModelHandle iceModel_;
    std::vector<Render::ModelDrawInstance> iceDraws_;
    // M3-H.1 immutable fish layout and one prepared opaque model draw. The per-frame school transform is a
    // stack value, so rendering does not regenerate geometry or allocate per-fish state.
    std::optional<UnderwaterFaunaField> faunaField_;
    Render::GpuModelHandle faunaModel_;
    Render::ModelDrawInstance faunaBaseDraw_{};
    Physics::PhysicsBodyHandle physicsBody_;
    // M3-F representative surface float: Game owns the small model, opaque physics handle and explicit
    // wave-aware buoyancy configuration. It is separate from every canonical submarine member.
    std::optional<Assets::ModelAsset> surfaceFloatModel_;
    Render::GpuModelHandle surfaceFloatModelGpu_;
    Render::ModelDrawInstance surfaceFloatBaseDraw_{};
    Physics::PhysicsBodyHandle surfaceFloatBody_;
    // Non-owning: the Engine owns the world and outlives this playground (see class comment).
    Physics::PhysicsWorld* physics_ = nullptr;

    // M2 scenario composition (D2): the authoritative water body, owned by value as part of this concrete
    // scenario. No MarineEnvironment/global/singleton — just a member of the playground that composes it.
    std::optional<Marine::WaterBody> water_;
    // Explicit Game-owned M2 Antey playground tuning. Marine owns only the generic point/component data types;
    // production proxy geometry supplies spatial layout, not displaced volume or mass.
    Marine::BuoyancyComponent buoyancy_;
    Marine::BuoyancyComponent surfaceFloatBuoyancy_;
    // Pre-reserved at initialization; CalculateWaveSurface reuses this storage in every fixed tick.
    Marine::BuoyancyResult surfaceFloatBuoyancyResult_;
    Marine::HydroDragComponent hydroDrag_;
    Marine::PropulsionComponent propulsion_;
    Marine::PropulsionState propulsionState_{};
    std::array<Marine::ControlSurfaceComponent, 2> controlSurfaces_{};

    // World-space fixed camera target initialized from the production body's initial center. The body itself is
    // already created before this member is retained; M5 may shift this stored presentation target without
    // changing physics authority.
    Physics::PhysicsVector3 initialBodyWorldCenter_{};
    float presentationCameraTargetOffsetXMeters_ = 0.0F;
    Assets::ModelTransform modelToBody_{};
    // Game tuning points are authored in the production vessel frame and shifted once into the production
    // collision body's local frame. The canonical collision center is source origin, so this is unchanged.
    Physics::PhysicsVector3 propulsorBodyLocalPosition_{};
    // IG1-B.1 fixed submerged presentation state. These per-node post transforms are built from opaque
    // IG1 production bindings once at initialization and never mutate ModelAsset or physics.
    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;

    // Bounded H2 diagnostics: physics in FixedUpdate; camera/water/propeller presentation in Render.
    std::uint64_t fixedTickCount_ = 0;
    bool loggedFirstFixedSample_ = false;
    bool loggedLaterFixedSample_ = false;
    bool loggedHapticFailure_ = false;
    mutable bool loggedRenderPresentation_ = false;

    // Presentation state derived only from authoritative shaft RPM. Production propeller hierarchy animation
    // is intentionally deferred beyond IG1-B; this state remains M2 simulation-compatible but is not drawn.
    float propellerPresentationAngleRadians_ = 0.0F;
};
}
