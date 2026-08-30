#include "Game/PhysicalPlayground.h"

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Game/PhysicsRenderSync.h"
#include "Game/WaterPresentation.h"
#include "Simulation/Marine/BuoyancySystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>

namespace DeepRun::Game
{
namespace
{
constexpr std::string_view SubmarineModelPath = "submarines/prototype/submarine_prototype.glb";
constexpr float M2GameplayCameraHorizontalSpanMeters = 600.0F;

// Game-owned M2 prototype tuning (ADR-0008). This is gameplay/prototype tuning only, not classified or
// precise real-vessel hydrostatic data. Collision bounds configure only the collision proxy; displaced-water
// volume is derived independently from mass / water density below. These values never move into Marine or
// generic PhysicsWorld.
constexpr float M2PrototypeMassKg = 12'000'000.0F;

// Game-owned M2 environment tuning (Slice D2). These are scenario values for this concrete playground, not
// properties of the generic WaterBody: they stay here and never move into Simulation/Marine.
constexpr float M2SeaSurfaceLevelMeters = 0.0F;
constexpr float M2SeaWaterDensityKgPerCubicMeter = 1025.0F;
constexpr float M2InitialSubmarineDepthMeters = 100.0F;

// E3 prototype buoyancy layout, in BODY-LOCAL meters relative to the rigid-body origin/COM. Four explicit
// points distribute force along the prototype length without deriving hydrostatics from mesh/collision
// geometry or claiming CFD fidelity. The +2 m vertical offset creates a small restoring pitch moment.
constexpr std::array<float, 4> M2BuoyancyPointXMeters = {36.0F, 12.0F, -12.0F, -36.0F};
constexpr float M2BuoyancyPointYMeters = 2.0F;
constexpr float M2BuoyancySubmersionHalfHeightMeters = 6.0F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
constexpr float M2GravityAlignmentRelativeTolerance = 1.0e-4F;
constexpr std::uint64_t M2LaterDiagnosticFixedTick = 90;

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

bool NearlyEqualRelative(const double actual, const double expected, const double relativeTolerance)
{
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::abs(actual - expected) <= relativeTolerance * (std::max)(1.0, std::abs(expected));
}

std::expected<float, std::string> GravityMagnitudeForWater(
    const Physics::PhysicsWorld& physics,
    const Marine::WaterBody& water,
    const Physics::PhysicsVector3& samplePosition)
{
    const auto gravity = physics.Gravity();
    if (!gravity || !gravity->IsFinite())
    {
        return std::unexpected("authoritative physics gravity is unavailable or non-finite");
    }

    const double gx = static_cast<double>(gravity->x);
    const double gy = static_cast<double>(gravity->y);
    const double gz = static_cast<double>(gravity->z);
    const double magnitude = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (!std::isfinite(magnitude) || magnitude <= 0.0 ||
        magnitude > static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return std::unexpected("authoritative physics gravity magnitude must be finite and positive");
    }

    const auto waterSample = water.Sample(samplePosition);
    if (!waterSample || !waterSample->surfaceNormal.IsFinite())
    {
        return std::unexpected("authoritative water surface normal is unavailable or non-finite");
    }

    const Physics::PhysicsVector3& normal = waterSample->surfaceNormal;
    const double alongOutwardNormal = gx * normal.x + gy * normal.y + gz * normal.z;
    const double lateralSquared =
        (std::max)(0.0, magnitude * magnitude - alongOutwardNormal * alongOutwardNormal);
    const double lateralMagnitude = std::sqrt(lateralSquared);
    if (alongOutwardNormal >= 0.0 ||
        lateralMagnitude > magnitude * static_cast<double>(M2GravityAlignmentRelativeTolerance))
    {
        return std::unexpected("M2 gravity must point opposite the WaterBody surface normal");
    }
    return static_cast<float>(magnitude);
}

Marine::BuoyancyComponent BuildM2Buoyancy(const Marine::WaterBody& water)
{
    const float totalDisplacedVolume = M2PrototypeMassKg / water.Config().densityKgPerCubicMeter;
    const float pointVolume = totalDisplacedVolume / static_cast<float>(M2BuoyancyPointXMeters.size());

    Marine::BuoyancyComponent component;
    component.points.reserve(M2BuoyancyPointXMeters.size());
    for (const float x : M2BuoyancyPointXMeters)
    {
        component.points.push_back(Marine::BuoyancyPoint{
            .bodyLocalPositionMeters = {x, M2BuoyancyPointYMeters, 0.0F},
            .displacedVolumeCubicMeters = pointVolume,
            .submersionHalfHeightMeters = M2BuoyancySubmersionHalfHeightMeters});
    }
    return component;
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

    // D2/E3 scenario composition: this playground owns its authoritative water body as a plain value, created
    // through the D1 validated factory with Game-owned M2 tuning. No MarineEnvironment/global/singleton —
    // Engine/Core stays unaware of water (architecture scan). The WaterBody knows nothing about the renderer,
    // camera or submarine; it only answers surface/depth queries.
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = M2SeaSurfaceLevelMeters,
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter});
    if (!water)
    {
        return std::unexpected("physical playground water body creation failed: " + water.error().message);
    }

    // Asset-space pivot (ADR-0008): the C1 box shape is centered on the body origin, so modelToBody =
    // T(-assetBoundsCenter). The asset bounds center is used ONLY for this pivot correction — it must never
    // double as a world position or camera target.
    const Assets::ModelVector3 assetBoundsCenter = BoundsCenter(bounds);
    const Physics::PhysicsVector3 halfExtents{
        (bounds.maximum.x - bounds.minimum.x) * 0.5F,
        (bounds.maximum.y - bounds.minimum.y) * 0.5F,
        (bounds.maximum.z - bounds.minimum.z) * 0.5F};

    // D2 world placement: the body's model-space bounds center starts exactly M2InitialSubmarineDepthMeters
    // below the authoritative surface level; X/Z come from the asset bounds center, Y comes exclusively from
    // WaterBody truth (never from the asset Y center). For the canonical prototype this is ~(1, -100, 0).
    const Physics::PhysicsVector3 initialBodyWorldCenter = ComputeInitialBodyWorldCenter(
        water->Config().surfaceLevelY, M2InitialSubmarineDepthMeters, assetBoundsCenter);

    // Verify placement against the authoritative water body before any physics/render work: sampling the
    // initial world center must report exactly the desired signed depth.
    const auto initialDepthSample = water->Sample(initialBodyWorldCenter);
    if (!initialDepthSample ||
        std::abs(initialDepthSample->signedDepthMeters - M2InitialSubmarineDepthMeters) > 1.0e-3F)
    {
        return std::unexpected(
            "physical playground initial placement does not match the authoritative water depth");
    }

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
    bodyInfo.position = initialBodyWorldCenter;
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

    // E3 displaced-water model: total potential displacement is derived from Game-owned prototype mass and
    // authoritative water density, then split equally across four explicit body-local points. It is never
    // derived from the render mesh, asset bounds, or the collision box dimensions.
    Marine::BuoyancyComponent buoyancy = BuildM2Buoyancy(*water);
    const auto gravityMagnitude = GravityMagnitudeForWater(physics, *water, initialState->position);
    if (!gravityMagnitude)
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected("physical playground gravity validation failed: " + gravityMagnitude.error());
    }

    const auto initialBuoyancy = Marine::BuoyancySystem::Calculate(
        *water,
        buoyancy,
        Marine::BuoyancyPose{
            .worldPositionMeters = initialState->position,
            .worldOrientation = initialState->orientation},
        *gravityMagnitude);
    if (!initialBuoyancy)
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected("physical playground initial buoyancy calculation failed: " +
                               initialBuoyancy.error().message);
    }

    const double expectedVolume = static_cast<double>(M2PrototypeMassKg) /
                                  static_cast<double>(water->Config().densityKgPerCubicMeter);
    const double expectedWeight = static_cast<double>(M2PrototypeMassKg) * *gravityMagnitude;
    const Physics::PhysicsVector3& initialForce = initialBuoyancy->totalForceNewtons;
    const bool allFullySubmerged = std::ranges::all_of(initialBuoyancy->points, [](const auto& point) {
        return std::abs(point.submergedFraction - 1.0F) <= M2InitialBalanceRelativeTolerance;
    });
    if (initialBuoyancy->points.size() != M2BuoyancyPointXMeters.size() || !allFullySubmerged ||
        !NearlyEqualRelative(initialBuoyancy->totalSubmergedVolumeCubicMeters,
                             expectedVolume,
                             M2InitialBalanceRelativeTolerance) ||
        !NearlyEqualRelative(initialForce.y, expectedWeight, M2InitialBalanceRelativeTolerance) ||
        std::abs(initialForce.x) > expectedWeight * M2InitialBalanceRelativeTolerance ||
        std::abs(initialForce.z) > expectedWeight * M2InitialBalanceRelativeTolerance || initialForce.y <= 0.0F)
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected("physical playground initial hydrostatic balance validation failed");
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
    water_ = *water;
    buoyancy_ = std::move(buoyancy);
    assetBoundsCenter_ = assetBoundsCenter;
    initialBodyWorldCenter_ = initialBodyWorldCenter;
    modelToBody_ = TranslationTransform(
        {-assetBoundsCenter.x, -assetBoundsCenter.y, -assetBoundsCenter.z});

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
            FormatVector(initialState->position) + ", displaced volume " +
            std::to_string(initialBuoyancy->totalSubmergedVolumeCubicMeters) + " m^3, gravity " +
            std::to_string(*gravityMagnitude) + " m/s^2, buoyancy " +
            std::to_string(initialForce.y) + " N, weight " + std::to_string(expectedWeight) + " N");
    return {};
}

std::expected<void, std::string> PhysicalPlayground::FixedUpdate(const float fixedDeltaSeconds)
{
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected("physical playground fixed delta must be finite and positive");
    }
    if (physics_ == nullptr)
    {
        return std::unexpected("physical playground physics world is unavailable");
    }
    if (!physicsBody_.IsValid())
    {
        return std::unexpected("physical playground physics body handle is invalid");
    }
    if (!water_.has_value())
    {
        return std::unexpected("physical playground water body is unavailable");
    }
    if (buoyancy_.points.empty())
    {
        return std::unexpected("physical playground buoyancy configuration is unavailable");
    }

    // Exactly one authoritative body snapshot per fixed tick. E2 transforms all four body-local points from
    // this copy; no point performs another body query and no render state participates.
    const auto state = physics_->GetBodyState(physicsBody_);
    if (!state)
    {
        return std::unexpected("physical playground body state is unavailable during fixed update");
    }
    const auto gravityMagnitude = GravityMagnitudeForWater(*physics_, *water_, state->position);
    if (!gravityMagnitude)
    {
        return std::unexpected("physical playground fixed gravity validation failed: " + gravityMagnitude.error());
    }

    const auto result = Marine::BuoyancySystem::Calculate(
        *water_,
        buoyancy_,
        Marine::BuoyancyPose{
            .worldPositionMeters = state->position,
            .worldOrientation = state->orientation},
        *gravityMagnitude);
    if (!result)
    {
        return std::unexpected("physical playground buoyancy calculation failed: " + result.error().message);
    }

    // E2 published output is E1 input, point for point and in order. Do not recompute force, aggregate at
    // COM, derive torque, special-case dry points, or multiply by fixedDeltaSeconds.
    for (std::size_t index = 0; index < result->points.size(); ++index)
    {
        const Marine::BuoyancyPointResult& point = result->points[index];
        Physics::PhysicsError error;
        if (!physics_->AddForceAtWorldPosition(
                physicsBody_, point.forceNewtons, point.worldPositionMeters, &error))
        {
            return std::unexpected("physical playground buoyancy force application failed at point " +
                                   std::to_string(index) + ": " + error.message);
        }
    }

    ++fixedTickCount_;
    if (!loggedFirstFixedSample_ ||
        (!loggedLaterFixedSample_ && fixedTickCount_ >= M2LaterDiagnosticFixedTick))
    {
        const auto bodyDepth = water_->Sample(state->position);
        if (!bodyDepth)
        {
            return std::unexpected("physical playground body-center water sample failed: " + bodyDepth.error().message);
        }

        float minimumFraction = 1.0F;
        float maximumFraction = 0.0F;
        for (const auto& point : result->points)
        {
            minimumFraction = (std::min)(minimumFraction, point.submergedFraction);
            maximumFraction = (std::max)(maximumFraction, point.submergedFraction);
        }
        const double weightMagnitude = static_cast<double>(M2PrototypeMassKg) * *gravityMagnitude;
        const float pitchDegrees = 2.0F * std::atan2(state->orientation.z, state->orientation.w) *
                                   (180.0F / 3.14159265358979323846F);
        const bool first = !loggedFirstFixedSample_;
        loggedFirstFixedSample_ = true;
        loggedLaterFixedSample_ = loggedLaterFixedSample_ || fixedTickCount_ >= M2LaterDiagnosticFixedTick;
        PlaygroundLog().Info(
            Diagnostics::LogCategory::Physics,
            std::string(first ? "Physical playground first E3 fixed sample: "
                              : "Physical playground later E3 fixed sample: ") +
                "tick " + std::to_string(fixedTickCount_) + ", position " + FormatVector(state->position) +
                ", velocity " + FormatVector(state->linearVelocity) + ", pitch Z " +
                std::to_string(pitchDegrees) + " deg, angular velocity " +
                FormatVector(state->angularVelocity) + ", signed depth " +
                std::to_string(bodyDepth->signedDepthMeters) + " m, submerged volume " +
                std::to_string(result->totalSubmergedVolumeCubicMeters) + " m^3, buoyancy force " +
                FormatVector(result->totalForceNewtons) + ", gravity magnitude " +
                std::to_string(*gravityMagnitude) + " m/s^2, weight " +
                std::to_string(weightMagnitude) + " N, point fraction range [" +
                std::to_string(minimumFraction) + ", " + std::to_string(maximumFraction) + ']');
    }
    return {};
}

std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer) const
{
    if (!modelAsset_.IsValid() || !renderer.IsGpuModelValid(submarineModel_) || physics_ == nullptr ||
        !physicsBody_.IsValid() || !water_.has_value())
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

    // Camera policy (B2.1 fixed-world contract, D2/E3 target): the 600 m orthographic side view keeps its
    // target at the INITIAL body world center — a world-space point derived from WaterBody truth, not an
    // asset-space value — so any physical drift remains visible and the surface stays a
    // constant ~100 m above camera center. The transformed world bounds only set the near/far depth range;
    // they must never change the horizontal zoom (covered by tests). No follow/smoothing in D2.
    const auto worldBounds = TransformBounds(modelAsset_->bounds, modelToWorld);
    if (!worldBounds)
    {
        return std::unexpected(worldBounds.error());
    }
    const Assets::ModelVector3 target{initialBodyWorldCenter_.x, initialBodyWorldCenter_.y,
                                      initialBodyWorldCenter_.z};
    const auto camera = Render::BuildFixedWorldSideViewCamera(
        target,
        renderer.AspectRatio(),
        M2GameplayCameraHorizontalSpanMeters,
        *worldBounds);
    if (!camera || !Render::BoundsFitInCamera(*worldBounds, *camera))
    {
        return std::unexpected(camera ? "physical playground bounds do not fit the camera" : camera.error());
    }

    // D2 flat-water cross-section presentation: BOTH presentation colors belong to the Game. The full
    // viewport is first painted with the above-water color, then everything below the AUTHORITATIVE surface
    // level (projected through the actual gameplay camera) is overpainted with the underwater color via the
    // generic renderer clear-rect API. The renderer receives only normalized rects + RGBA — no WaterBody,
    // no marine semantics; its BeginFrame default clear is a generic fallback this path fully covers.
    // Camera clipping happens in the Game projection helper; the authoritative water state is never touched
    // by what is visible. This temporary M2 path must run before the submarine draw so the hull renders on
    // top of the water background (ImGui stays above everything via the engine overlay).
    const auto aboveWaterCleared = renderer.ClearViewportRect(
        Render::ViewportRect{}, // full viewport: default {0, 0, 1, 1}
        M2AboveWaterBackgroundColor);
    if (!aboveWaterCleared)
    {
        return std::unexpected(aboveWaterCleared.error());
    }

    const auto underwaterRegion = UnderwaterRegionForSurface(*camera, water_->Config().surfaceLevelY);
    if (!underwaterRegion)
    {
        return std::unexpected(underwaterRegion.error());
    }
    if (underwaterRegion->has_value())
    {
        const auto cleared = renderer.ClearViewportRect(**underwaterRegion, M2UnderwaterBackgroundColor);
        if (!cleared)
        {
            return std::unexpected(cleared.error());
        }
    }

    const auto stats = renderer.DrawModel(submarineModel_, *draws, *camera);
    if (!stats)
    {
        return stats;
    }

    // Physics diagnostics live in FixedUpdate. Render logs only the bounded presentation contract once, so
    // camera/waterline evidence is not duplicated with authoritative physical samples.
    if (!loggedRenderPresentation_)
    {
        const auto waterline = ProjectWorldSurfaceToViewportY(*camera, water_->Config().surfaceLevelY);
        if (waterline)
        {
            loggedRenderPresentation_ = true;
            PlaygroundLog().Info(
                Diagnostics::LogCategory::Render,
                "Physical playground E3 presentation: surface Y " +
                    std::to_string(water_->Config().surfaceLevelY) + ", camera target Y " +
                    std::to_string(camera->target.y) + ", horizontal span " +
                    std::to_string(camera->width) + ", vertical span " +
                    std::to_string(camera->height) + ", normalized waterline from top " +
                    std::to_string(*waterline));
        }
    }
    return stats;
}

Render::GpuModelHandle PhysicalPlayground::SubmarineModel() const noexcept
{
    return submarineModel_;
}
} // namespace DeepRun::Game
