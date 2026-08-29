#include "Game/PhysicalPlayground.h"

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Game/PhysicsRenderSync.h"

#include <cmath>
#include <sstream>
#include <string_view>

namespace DeepRun::Game
{
namespace
{
constexpr std::string_view SubmarineModelPath = "submarines/prototype/submarine_prototype.glb";
constexpr float M2GameplayCameraHorizontalSpanMeters = 600.0F;

// Game-owned M2 prototype tuning (ADR-0008). This is gameplay/prototype tuning only: it is not yet a
// neutral-buoyancy calibration and is not derived from classified or precise real vessel data. While the body
// falls freely, mass does not change its motion under constant gravity; the real mass / displaced-water volume
// relationship is calibrated in the buoyancy slice. It must never move into generic PhysicsWorld.
constexpr float M2PrototypeMassKg = 12'000'000.0F;

// Diagnostics-only logger for the playground; the Engine's logger is not reachable through the generic API.
Diagnostics::Logger& PlaygroundLog() noexcept
{
    static Diagnostics::Logger logger;
    return logger;
}

std::string FormatVector(const Physics::PhysicsVector3& value)
{
    std::ostringstream stream;
    stream << '(' << value.x << ", " << value.y << ", " << value.z << ')';
    return stream.str();
}
} // namespace

std::expected<void, std::string> PhysicalPlayground::Initialize(
    Assets::AssetManager& assets,
    Physics::PhysicsWorld& physics,
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

    const Assets::ModelBounds& bounds = (*model)->bounds;
    std::string validationMessage;
    if (!ValidateCollisionBounds(bounds, validationMessage))
    {
        return std::unexpected("physical playground collision proxy rejected: " + validationMessage);
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

    // Pivot contract (ADR-0008): the C1 box shape is centered on the body origin, so the body origin is the
    // bounds center and the model-to-body correction is translation(-boundsCenter). With the initial identity
    // orientation this makes the first visual frame identical to B2.1: T(c) * T(-c) = identity.
    const Assets::ModelVector3 boundsCenter = BoundsCenter(bounds);
    const Physics::PhysicsVector3 halfExtents{
        (bounds.maximum.x - bounds.minimum.x) * 0.5F,
        (bounds.maximum.y - bounds.minimum.y) * 0.5F,
        (bounds.maximum.z - bounds.minimum.z) * 0.5F};

    // M2 Slice C2.1: the canonical submarine is a 2.5D rigid body constrained to the gameplay plane (XY).
    // This axis choice is Game-owned knowledge and never enters generic PhysicsWorld: translation X/Y plus
    // rotation Z are allowed; translation Z, rotation X, and rotation Y are locked at creation time in the
    // backend's mass/motion configuration. The submarine can move forward/back, rise/sink, and pitch nose
    // up/down, but it cannot leave the plane along Z, roll around X, or yaw around Y (ADR-0008).
    Physics::PhysicsDegreesOfFreedom m2VesselDof;
    m2VesselDof.translationX = true;
    m2VesselDof.translationY = true;
    m2VesselDof.translationZ = false;
    m2VesselDof.rotationX = false;
    m2VesselDof.rotationY = false;
    m2VesselDof.rotationZ = true;

    Physics::DynamicBoxBodyCreateInfo bodyInfo;
    bodyInfo.halfExtents = halfExtents;
    bodyInfo.mass = M2PrototypeMassKg; // gameplay/prototype tuning, see constant comment
    bodyInfo.position = {boundsCenter.x, boundsCenter.y, boundsCenter.z};
    bodyInfo.orientation = {}; // identity
    bodyInfo.gravityEnabled = true;
    bodyInfo.linearDamping = 0.0F;
    bodyInfo.angularDamping = 0.0F;
    bodyInfo.initialLinearVelocity = {};
    bodyInfo.initialAngularVelocity = {};
    bodyInfo.degreesOfFreedom = m2VesselDof;

    Physics::PhysicsError physicsError;
    const Physics::PhysicsBodyHandle body = physics.CreateDynamicBoxBody(bodyInfo, &physicsError);
    if (!body.IsValid())
    {
        return std::unexpected(
            "physical playground rigid body creation failed: " + physicsError.message);
    }

    const auto initialState = physics.GetBodyState(body);
    if (!initialState)
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected("physical playground initial body state is unavailable");
    }

    // M2 Slice C2.1: the body-to-world matrix depends only on the physics pose; the asset pivot correction is
    // applied explicitly by the caller through modelToBody (see Render for the single-source-of-truth flow).
    const auto initialBodyToWorld = BuildBodyToWorld(*initialState);
    if (!initialBodyToWorld)
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected(
            "physical playground initial transform cannot be built: " + initialBodyToWorld.error());
    }

    modelAsset_ = *model;
    submarineModel_ = upload->handle;
    physicsBody_ = body;
    physics_ = &physics;
    boundsCenter_ = boundsCenter;
    modelToBody_ = TranslationTransform({-boundsCenter.x, -boundsCenter.y, -boundsCenter.z});
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

    PlaygroundLog().Info(
        Diagnostics::LogCategory::Physics,
        "Physical playground body ready: box half extents (" + FormatVector(halfExtents) +
            "), prototype mass " + std::to_string(M2PrototypeMassKg) + " kg, initial position " +
            FormatVector(initialState->position));
    return {};
}

std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer) const
{
    if (!modelAsset_.IsValid() || !renderer.IsGpuModelValid(submarineModel_) || physics_ == nullptr ||
        !physicsBody_.IsValid())
    {
        return std::unexpected("physical playground model assets are no longer valid");
    }

    // One body snapshot per frame, taken after the engine's fixed-step update and before any D3D12 work. The
    // copy is the only physics data this function uses; no Jolt lock or reference survives into rendering.
    const auto state = physics_->GetBodyState(physicsBody_);
    if (!state)
    {
        return std::unexpected("physical playground body state became unavailable");
    }

    // M2 Slice C2.1: one snapshot -> bodyToWorld -> modelToWorld, and that single matrix is the source of
    // truth for BOTH draw preparation and the rendered world bounds. The physics pose (bodyToWorld) never
    // mixes with asset pivot correction (modelToBody); two slightly different transforms are never computed.
    const auto bodyToWorld = BuildBodyToWorld(*state);
    if (!bodyToWorld)
    {
        return std::unexpected(bodyToWorld.error());
    }
    const Assets::ModelTransform modelToWorld = Render::Multiply(*bodyToWorld, modelToBody_);

    // The single snapshot feeds every node draw: the body translation is applied exactly once here and each
    // node's local transform (including the propeller at (-49, 0, 0)) is applied exactly once by
    // PrepareModelDraws. Static GPU metadata is untouched; only per-draw transforms are rebuilt.
    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld);
    if (!draws)
    {
        return std::unexpected(draws.error());
    }

    // Camera policy for C2: the 600 m orthographic side view keeps its target at the INITIAL world/model
    // center so the falling body visibly moves inside a fixed viewport. The transformed world bounds only set
    // the near/far depth range; they must never change the horizontal zoom (covered by tests).
    const auto worldBounds = TransformBounds(modelAsset_->bounds, modelToWorld);
    if (!worldBounds)
    {
        return std::unexpected(worldBounds.error());
    }
    const Assets::ModelVector3 target = boundsCenter_;
    const auto camera = Render::BuildFixedWorldSideViewCamera(
        target,
        renderer.AspectRatio(),
        M2GameplayCameraHorizontalSpanMeters,
        *worldBounds);
    if (!camera || !Render::BoundsFitInCamera(*worldBounds, *camera))
    {
        return std::unexpected(camera ? "physical playground bounds do not fit the camera" : camera.error());
    }

    const auto stats = renderer.DrawModel(submarineModel_, *draws, *camera);
    if (!stats)
    {
        return stats;
    }

    // Bounded smoke diagnostics: log the first physical render sample and one later sample only.
    ++renderSampleCount_;
    if (!loggedFirstSample_)
    {
        loggedFirstSample_ = true;
        PlaygroundLog().Info(
            Diagnostics::LogCategory::Physics,
            "Physical playground first render sample: position " + FormatVector(state->position) +
                ", velocity " + FormatVector(state->linearVelocity));
    }
    if (!loggedLaterSample_ && renderSampleCount_ >= 60)
    {
        loggedLaterSample_ = true;
        PlaygroundLog().Info(
            Diagnostics::LogCategory::Physics,
            "Physical playground later render sample: position " + FormatVector(state->position) +
                ", velocity " + FormatVector(state->linearVelocity));
    }
    return stats;
}

Render::GpuModelHandle PhysicalPlayground::SubmarineModel() const noexcept
{
    return submarineModel_;
}
} // namespace DeepRun::Game
