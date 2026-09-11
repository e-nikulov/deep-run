#include "Game/PhysicalPlayground.h"

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Game/Haptics/HapticFeedbackSystem.h"
#include "Game/PhysicsRenderSync.h"
#include "Game/PropulsionPresentation.h"
#include "Game/Submarine/ProductionAnteyAsset.h"
#include "Game/Submarine/ProductionAnteyLodPolicy.h"
#include "Game/SurfaceFloatModel.h"
#include "Game/Environment/ScalableEnvironmentPresentation.h"
#include "Game/Environment/UnderwaterFaunaField.h"
#include "Game/Environment/UnderwaterFloraField.h"
#include "Game/Environment/UnderwaterIceField.h"
#include "Game/WaterPresentation.h"
#include "Simulation/Marine/BuoyancySystem.h"
#include "Simulation/Marine/ControlSurfaceSystem.h"
#include "Simulation/Marine/HydroDragSystem.h"
#include "Simulation/Marine/PropulsionSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>

namespace DeepRun::Game
{
namespace
{
constexpr float IG1BProductionLengthMinimumMeters = 150.0F;
constexpr float IG1BProductionLengthMaximumMeters = 158.0F;
constexpr float M2GameplayCameraHorizontalSpanMeters = 600.0F;
constexpr std::string_view M3SeabedSectionId = "m3_seabed_01";

// Temporary Game-owned Antey playground tuning (ADR-0008). This is not production mass authoring and is
// not a claim about Project 949A hydrostatics. Production collision/buoyancy proxies provide spatial
// authority only; effective neutral displacement remains mass / water density for this accepted M2/M3 scenario.
constexpr float M2GameAnteyMassTuningKg = 12'000'000.0F;

// Game-owned M2 environment tuning (Slice D2). These are scenario values for this concrete playground, not
// properties of the generic WaterBody: they stay here and never move into Simulation/Marine.
constexpr float M2SeaSurfaceLevelMeters = 0.0F;
constexpr float M2SeaWaterDensityKgPerCubicMeter = 1025.0F;
constexpr float M2InitialSubmarineDepthMeters = 100.0F;

// M3-C presentation-only depth-light tuning. Game supplies the authoritative WaterBody surface value each
// frame; the renderer receives only this small scene-linear snapshot. Red attenuates fastest, then green,
// then blue. The restrained material-modulated deep ambient preserves terrain/rock readability without fog.
constexpr std::array<float, 3> M3DepthAttenuationPerMeterRgb{0.012F, 0.006F, 0.003F};
constexpr std::array<float, 3> M3DeepAmbientRgb{0.02F, 0.075F, 0.12F};

// M3-C.1 fixed view-path presentation tuning. It deliberately approaches the existing scene-linear
// underwater clear colour; it does not alter WaterBody, simulation visibility, or depth-light coefficients.
constexpr float M3FogExtinctionPerMeter = 0.004F;
constexpr std::array<float, 3> M3FogColorRgb{
    M2UnderwaterBackgroundColor.r,
    M2UnderwaterBackgroundColor.g,
    M2UnderwaterBackgroundColor.b};

// M3-D fixed presentation tuning for the one canonical suspended-particulate field. These bounds cover the
// 600 m side view with a small margin and remain wholly below the Game-owned WaterBody surface. This is not
// environment authority, a simulation population, or an emitter configuration system.
constexpr Render::SuspendedParticleFieldParameters M3UnderwaterParticleField{
    .seed = 0x4D334430U,
    .particleCount = 256U,
    .minimumWorldPosition = {-320.0F, -260.0F, -20.0F},
    .maximumWorldPosition = {320.0F, -15.0F, 20.0F},
    .particleSizeMeters = 1.15F,
    .particleOpacity = 0.18F,
    .verticalDriftMetersPerSecond = 0.16F,
    .lateralOscillationAmplitudeMeters = 1.4F,
    .lateralOscillationAngularFrequency = 0.23F};

// M3-F's only wave-aware physical consumer. This is a Game-owned engineering float, not a ship or a
// reusable floating-body framework. Its collision proxy and displaced volume are intentionally independent.
constexpr float M3SurfaceFloatMassKg = 1000.0F;
constexpr Physics::PhysicsVector3 M3SurfaceFloatHalfExtents{1.5F, 0.5F, 0.5F};
constexpr Physics::PhysicsVector3 M3SurfaceFloatInitialReferencePosition{140.0F, 0.0F, 0.0F};
constexpr std::array<Physics::PhysicsVector3, 2> M3SurfaceFloatBuoyancyPointPositions{{
    {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}}};
constexpr float M3SurfaceFloatSubmersionHalfHeightMeters = 0.55F;
constexpr float M3SurfaceFloatLinearDamping = 0.9F;
constexpr float M3SurfaceFloatAngularDamping = 2.0F;
constexpr float M3SurfaceFloatBalanceRelativeTolerance = 1.0e-4F;

// IG1-C Game policy: sample the production buoyancy BOX at four deterministic normalized longitudinal
// positions. The fractions preserve the accepted M2 spacing while removing prototype/world-space metres.
constexpr std::array<float, 4> M2BuoyancyLongitudinalFractions = {0.5F, 1.0F / 6.0F, -1.0F / 6.0F, -0.5F};
// Production COB is spatial authority. This explicit Game-owned offset preserves the accepted M2 pitch
// stability behaviour; it is not a fabricated historical metacentric height.
constexpr float M2GameBuoyancyStabilityOffsetMeters = 2.0F;
constexpr float M2BuoyancySubmersionHalfHeightMeters = 6.0F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
constexpr float M2GravityAlignmentRelativeTolerance = 1.0e-4F;
constexpr std::uint64_t M2LaterDiagnosticFixedTick = 90;

// F2 Game-owned prototype drag tuning. These are gameplay coefficients, not measured/classified vessel
// hydrostatics and not values derived from the render mesh, collision box, or displaced-water model.
// Longitudinal X is intentionally much lower than vertical/lateral Y/Z. The current 2.5D body locks RX/RY,
// so only Z pitch damping needs a non-zero angular coefficient in this playground.
constexpr Physics::PhysicsVector3 M2LinearEffectiveAreaSquareMeters{150.0F, 1800.0F, 2200.0F};
constexpr Physics::PhysicsVector3 M2AngularEffectiveMomentMeters5{0.0F, 0.0F, 50'000'000.0F};

// G2 Game-owned one-shaft prototype tuning. These are gameplay values, not measured or classified vessel
// data, not mesh/collision-derived, and not universal submarine constants.
constexpr Marine::PropulsionComponent M2Propulsion{
    .maxForwardRpm = 180.0F,
    .maxReverseRpm = 120.0F,
    .maxForwardThrustNewtons = 12'000'000.0F,
    .maxReverseThrustNewtons = 4'800'000.0F,
    .spinUpRateRpmPerSecond = 30.0F,
    .spinDownRateRpmPerSecond = 45.0F};

// Temporary M2 scenario truth until the later power system exists. The direct throttle command remains the only
// source of requested drive in I1; this value must not become an M6 power-management system early.
constexpr float M2AvailablePropulsionPowerFraction = 1.0F;

// Authoritative Game tuning relative to the production collision body origin/COM. The asset only validates
// the production semantic contract; these points are not derived from render bounds.
constexpr Physics::PhysicsVector3 M2PropulsorBodyLocalPositionMeters{-50.0F, 0.0F, 0.0F};

// H2 Game-owned prototype tuning for exactly two independently evaluated diving-plane groups. These values
// are not measured/classified vessel data, mesh- or collision-derived, or universal submarine constants.
constexpr std::size_t M2BowPlaneIndex = 0;
constexpr std::size_t M2SternPlaneIndex = 1;
constexpr std::array<Marine::ControlSurfaceComponent, 2> M2ControlSurfaces{{
    {.bodyLocalPositionMeters = {32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F},
    {.bodyLocalPositionMeters = {-32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F}}};

constexpr float M2MaximumPlaneDeflection = 0.5F;
constexpr std::array<std::string_view, 2> M2ControlSurfaceNames{"bow", "stern"};

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

std::string FormatBounds(const EnvironmentBounds& bounds)
{
    std::ostringstream stream;
    stream << "min(" << bounds.minimum.x << ", " << bounds.minimum.y << ", " << bounds.minimum.z
           << "), max(" << bounds.maximum.x << ", " << bounds.maximum.y << ", " << bounds.maximum.z << ')';
    return stream.str();
}

std::string FormatBounds(const Assets::ModelBounds& bounds)
{
    std::ostringstream stream;
    stream << "min(" << bounds.minimum.x << ", " << bounds.minimum.y << ", " << bounds.minimum.z
           << "), max(" << bounds.maximum.x << ", " << bounds.maximum.y << ", " << bounds.maximum.z << ')';
    return stream.str();
}

Assets::ModelBounds CombineBounds(const Assets::ModelBounds& first, const Assets::ModelBounds& second) noexcept
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
}

bool AxisAlignedBoxesOverlap(
    const Physics::PhysicsVector3& firstPosition,
    const Physics::PhysicsVector3& firstHalfExtents,
    const Physics::PhysicsVector3& secondPosition,
    const Physics::PhysicsVector3& secondHalfExtents) noexcept
{
    return std::abs(firstPosition.x - secondPosition.x) < firstHalfExtents.x + secondHalfExtents.x &&
           std::abs(firstPosition.y - secondPosition.y) < firstHalfExtents.y + secondHalfExtents.y &&
           std::abs(firstPosition.z - secondPosition.z) < firstHalfExtents.z + secondHalfExtents.z;
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

Physics::PhysicsVector3 ToPhysicsVector(const Assets::ModelVector3& value) noexcept
{
    return {.x = value.x, .y = value.y, .z = value.z};
}

Physics::PhysicsQuaternion ToPhysicsQuaternionWxyz(const std::array<float, 4>& value) noexcept
{
    return {.x = value[1], .y = value[2], .z = value[3], .w = value[0]};
}

bool IsIdentityQuaternion(const std::array<float, 4>& value) noexcept
{
    return std::abs(value[0] - 1.0F) <= 1.0e-5F &&
           std::abs(value[1]) <= 1.0e-5F &&
           std::abs(value[2]) <= 1.0e-5F &&
           std::abs(value[3]) <= 1.0e-5F;
}

Physics::PhysicsVector3 ShiftProductionPointToBodyLocal(
    const Physics::PhysicsVector3& productionPoint,
    const Physics::PhysicsVector3& collisionCenter) noexcept
{
    return {
        .x = productionPoint.x - collisionCenter.x,
        .y = productionPoint.y - collisionCenter.y,
        .z = productionPoint.z - collisionCenter.z};
}

Marine::BuoyancyComponent BuildM2Buoyancy(
    const Marine::WaterBody& water,
    const Submarine::ProductionBuoyancyDefinition& productionBuoyancy,
    const Submarine::ProductionCollisionDefinition& collision)
{
    const float totalDisplacedVolume = M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter;
    const float pointVolume = totalDisplacedVolume / static_cast<float>(M2BuoyancyLongitudinalFractions.size());

    Marine::BuoyancyComponent component;
    component.points.reserve(M2BuoyancyLongitudinalFractions.size());
    for (const float fraction : M2BuoyancyLongitudinalFractions)
    {
        const Assets::ModelVector3 sourcePoint{
            .x = productionBuoyancy.localCenter.x + fraction * productionBuoyancy.halfExtents.x,
            .y = productionBuoyancy.centerOfBuoyancy.y + M2GameBuoyancyStabilityOffsetMeters,
            .z = productionBuoyancy.centerOfBuoyancy.z};
        component.points.push_back(Marine::BuoyancyPoint{
            .bodyLocalPositionMeters = {
                sourcePoint.x - collision.localCenter.x,
                sourcePoint.y - collision.localCenter.y,
                sourcePoint.z - collision.localCenter.z},
            .displacedVolumeCubicMeters = pointVolume,
            .submersionHalfHeightMeters = M2BuoyancySubmersionHalfHeightMeters});
    }
    return component;
}

Marine::BuoyancyComponent BuildM3SurfaceFloatBuoyancy(const Marine::WaterBody& water)
{
    // Potential displacement is twice neutral volume. At the reference plane each point is half submerged,
    // so the two 50% contributions sum to mass / density without inferring volume from the collision box.
    const float pointPotentialVolume = M3SurfaceFloatMassKg / water.Config().densityKgPerCubicMeter;
    Marine::BuoyancyComponent component;
    component.points.reserve(M3SurfaceFloatBuoyancyPointPositions.size());
    for (const Physics::PhysicsVector3 localPosition : M3SurfaceFloatBuoyancyPointPositions)
    {
        component.points.push_back({
            .bodyLocalPositionMeters = localPosition,
            .displacedVolumeCubicMeters = pointPotentialVolume,
            .submersionHalfHeightMeters = M3SurfaceFloatSubmersionHalfHeightMeters});
    }
    return component;
}

Marine::HydroDragComponent BuildM2HydroDrag() noexcept
{
    return Marine::HydroDragComponent{
        .linearEffectiveAreaSquareMeters = M2LinearEffectiveAreaSquareMeters,
        .angularEffectiveMomentMeters5 = M2AngularEffectiveMomentMeters5};
}
} // namespace

std::expected<void, std::string> PhysicalPlayground::Initialize(
    Assets::AssetManager& assets,
    Physics::PhysicsWorld& physics,
    Render::D3D12Renderer& renderer,
    const bool verifyDistinctUploads)
{
    const auto productionDefinition = Submarine::LoadProductionAnteyAssetDefinition(assets);
    if (!productionDefinition)
    {
        return std::unexpected("physical playground production Antey definition load failed: " +
                               productionDefinition.error());
    }

    const auto productionLodSelection = Submarine::SelectProductionAnteyRenderAsset(
        *productionDefinition, Submarine::ProductionRenderLodLevel::Lod0);
    if (!productionLodSelection)
    {
        return std::unexpected("physical playground production Antey LOD policy rejected the render family: " +
                               productionLodSelection.error());
    }

    const auto model = assets.LoadModel(std::filesystem::path(productionLodSelection->assetId.Value()));
    if (!model)
    {
        std::ostringstream message;
        message << "Physical playground production Antey visual load failed: " << model.error().message
                << " (" << model.error().path.string() << ')';
        return std::unexpected(message.str());
    }

    const Assets::ModelBounds& visualBounds = (*model)->bounds;
    std::string validationMessage;
    if (!ValidateProductionVisualBounds(visualBounds, validationMessage))
    {
        return std::unexpected("physical playground production Antey visual bounds rejected: " + validationMessage);
    }
    const float productionLengthMeters = visualBounds.maximum.x - visualBounds.minimum.x;
    if (productionLengthMeters < IG1BProductionLengthMinimumMeters ||
        productionLengthMeters > IG1BProductionLengthMaximumMeters || (*model)->nodes.empty() ||
        (*model)->primitives.empty() || (*model)->materials.size() != 2U)
    {
        return std::unexpected("physical playground production Antey visual contract validation failed");
    }
    for (const Assets::MeshPrimitiveData& primitive : (*model)->primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty() || !primitive.hasNormals)
        {
            return std::unexpected("physical playground production Antey has a non-renderable primitive");
        }
    }
    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;
    submergedSailDeviceOverrides.reserve(productionDefinition->retractableSailDevices.size());
    for (const Submarine::ProductionRetractableSailDevice& device : productionDefinition->retractableSailDevices)
    {
        if (device.defaultState != Submarine::RetractableSailDeviceState::Stowed ||
            device.presentationNodeBindingIndex >= (*model)->nodeBindings.size())
        {
            return std::unexpected("physical playground production sail-device state is invalid");
        }
        const auto meshNodeIndex = (*model)->nodeBindings[device.presentationNodeBindingIndex].meshNodeIndex;
        if (!meshNodeIndex.has_value())
        {
            return std::unexpected("physical playground production sail-device binding is not drawable");
        }
        submergedSailDeviceOverrides.push_back(
            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
    }

    if (productionDefinition->collisionProxies.size() != 1U)
    {
        return std::unexpected("physical playground requires exactly one production Antey collision BOX proxy");
    }
    const Submarine::ProductionCollisionDefinition& collisionProxy = productionDefinition->collisionProxies.front();
    const Submarine::ProductionBuoyancyDefinition& buoyancyProxy = productionDefinition->buoyancyProxy;
    if (collisionProxy.shape != Submarine::ProductionProxyShape::Box ||
        buoyancyProxy.shape != Submarine::ProductionProxyShape::Box ||
        collisionProxy.semanticId != "collision.primary" ||
        buoyancyProxy.semanticId != "buoyancy.primary")
    {
        return std::unexpected("physical playground production Antey physics proxy semantic contract is invalid");
    }
    // The current Engine box contract has no separate local-shape orientation field. Canonical production
    // proxies are axis-aligned; reject a rotated content proxy clearly instead of silently changing the
    // accepted environment/non-penetration checks.
    if (!IsIdentityQuaternion(collisionProxy.orientationQuaternionWxyz) ||
        !IsIdentityQuaternion(buoyancyProxy.orientationQuaternionWxyz))
    {
        return std::unexpected("physical playground production Antey BOX proxy rotation is unsupported");
    }
    const Physics::PhysicsQuaternion collisionProxyOrientation =
        ToPhysicsQuaternionWxyz(collisionProxy.orientationQuaternionWxyz);
    const Physics::PhysicsQuaternion buoyancyProxyOrientation =
        ToPhysicsQuaternionWxyz(buoyancyProxy.orientationQuaternionWxyz);
    if (!collisionProxyOrientation.IsFinite() || !buoyancyProxyOrientation.IsFinite() ||
        std::abs(collisionProxyOrientation.LengthSquared() - 1.0F) > 1.0e-4F ||
        std::abs(buoyancyProxyOrientation.LengthSquared() - 1.0F) > 1.0e-4F)
    {
        return std::unexpected("physical playground production Antey BOX proxy orientation is invalid");
    }
    const Physics::PhysicsVector3 collisionCenter = ToPhysicsVector(collisionProxy.localCenter);
    const Physics::PhysicsVector3 collisionHalfExtents = ToPhysicsVector(collisionProxy.halfExtents);
    if (!collisionCenter.IsFinite() || !collisionHalfExtents.IsFinite() ||
        collisionHalfExtents.x <= 0.0F || collisionHalfExtents.y <= 0.0F || collisionHalfExtents.z <= 0.0F)
    {
        return std::unexpected("physical playground production Antey collision BOX values are invalid");
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
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter,
         .waves = Marine::M3WaterWaveField});
    if (!water)
    {
        return std::unexpected("physical playground water body creation failed: " + water.error().message);
    }
    const Render::GerstnerSurfacePresentationParameters gerstnerSurface =
        BuildGerstnerSurfacePresentation(*water);
    if (const auto configured = renderer.ConfigureGerstnerSurface(gerstnerSurface); !configured)
    {
        return std::unexpected("physical playground Gerstner surface configuration failed: " + configured.error());
    }
    if (!renderer.IsGerstnerSurfaceReady())
    {
        return std::unexpected("physical playground Gerstner surface did not become renderer-ready");
    }
    if (M3UnderwaterParticleField.maximumWorldPosition[1] >= water->Config().surfaceLevelY)
    {
        return std::unexpected("physical playground particle field must remain below the WaterBody surface");
    }
    if (const auto particles = renderer.ConfigureSuspendedParticleField(M3UnderwaterParticleField); !particles)
    {
        return std::unexpected("physical playground particle field configuration failed: " + particles.error());
    }
    if (!renderer.IsSuspendedParticleFieldReady())
    {
        return std::unexpected("physical playground particle field did not become renderer-ready");
    }

    // M3-B environment composition: Game owns the stable section and uses the renderer solely as a consumer
    // of its existing ModelAsset. The world-space section remains independent of WaterBody/physics truth;
    // this one check only confirms the canonical presentation floor is below the authoritative water surface.
    const SeabedProfileConfig seabedProfile{};
    auto seabed = BuildSeabedSection(EnvironmentSectionId{std::string(M3SeabedSectionId)}, seabedProfile);
    if (!seabed)
    {
        return std::unexpected("physical playground seabed construction failed: " + seabed.error());
    }
    if (seabed->bounds.maximum.y >= water->Config().surfaceLevelY)
    {
        return std::unexpected("physical playground canonical seabed must remain below the WaterBody surface");
    }

    // M3-G derives all root contact from the same authored profile that just built the Game-owned section.
    // It has no WaterBody, physics, simulation, or renderer dependency; the explicit Game reference level
    // merely keeps this canonical decorative field inside the established underwater presentation region.
    auto flora = BuildUnderwaterFloraField(seabed->id, seabedProfile, M2SeaSurfaceLevelMeters);
    if (!flora)
    {
        return std::unexpected("physical playground underwater flora construction failed: " + flora.error());
    }
    const auto floraUpload = renderer.UploadModel(flora->renderGeometry);
    const auto floraDraws = Render::PrepareModelDraws(flora->renderGeometry);
    const std::size_t floraVertexCount = flora->renderGeometry.primitives.empty()
                                             ? 0U
                                             : flora->renderGeometry.primitives.front().vertices.size();
    const std::size_t floraIndexCount = flora->renderGeometry.primitives.empty()
                                            ? 0U
                                            : flora->renderGeometry.primitives.front().indices.size();
    if (!floraUpload || !floraDraws || flora->patchCount != M3UnderwaterFloraPatchCount ||
        flora->plants.size() != M3UnderwaterFloraPlantCount || flora->renderGeometry.materials.size() != 1U ||
        flora->renderGeometry.primitives.size() != 1U || flora->renderGeometry.nodes.size() != 1U ||
        floraDraws->size() != 1U || floraVertexCount != 960U || floraIndexCount != 1'440U ||
        floraIndexCount / 3U > M3UnderwaterFloraTriangleBudget || !floraUpload->stats.uploadCompleted ||
        !floraUpload->handle.IsValid() || !renderer.IsGpuModelValid(floraUpload->handle) ||
        floraUpload->stats.primitiveCount != 1U || floraUpload->stats.vertexCount != floraVertexCount ||
        floraUpload->stats.indexCount != floraIndexCount ||
        floraDraws->front().modelToWorld.values != Assets::ModelTransform{}.values)
    {
        return std::unexpected("physical playground underwater flora presentation initialization validation failed" +
                               (!floraUpload ? ": " + floraUpload.error() :
                                !floraDraws ? ": " + floraDraws.error() : std::string{}));
    }

    // M3-H uses the same fixed Game reference level only as authored composition input. Its immutable field
    // owns both faceted render geometry and independent coarse-box descriptions; neither is derived from the
    // other or from the water/visual-wave presentation.
    auto ice = BuildUnderwaterIceField(seabed->id, M2SeaSurfaceLevelMeters);
    if (!ice)
    {
        return std::unexpected("physical playground underwater ice construction failed: " + ice.error());
    }
    const auto iceUpload = renderer.UploadModel(ice->renderGeometry);
    const auto iceDraws = Render::PrepareModelDraws(ice->renderGeometry);
    const std::size_t iceVertexCount = ice->renderGeometry.primitives.empty()
                                           ? 0U
                                           : ice->renderGeometry.primitives.front().vertices.size();
    const std::size_t iceIndexCount = ice->renderGeometry.primitives.empty()
                                          ? 0U
                                          : ice->renderGeometry.primitives.front().indices.size();
    if (!iceUpload || !iceDraws || ice->formations.size() != M3UnderwaterIceFormationCount ||
        ice->collisionBoxes.size() != M3UnderwaterIceCollisionCount || ice->renderGeometry.materials.size() != 1U ||
        ice->renderGeometry.primitives.size() != 1U || ice->renderGeometry.nodes.size() != 1U ||
        iceDraws->size() != 1U || iceVertexCount != 126U || iceIndexCount != 240U ||
        iceIndexCount / 3U > M3UnderwaterIceTriangleBudget || !iceUpload->stats.uploadCompleted ||
        !iceUpload->handle.IsValid() || !renderer.IsGpuModelValid(iceUpload->handle) ||
        iceUpload->stats.primitiveCount != 1U || iceUpload->stats.vertexCount != iceVertexCount ||
        iceUpload->stats.indexCount != iceIndexCount ||
        iceDraws->front().modelToWorld.values != Assets::ModelTransform{}.values)
    {
        return std::unexpected("physical playground underwater ice presentation initialization validation failed" +
                               (!iceUpload ? ": " + iceUpload.error() :
                                !iceDraws ? ": " + iceDraws.error() : std::string{}));
    }

    // M3-H.1 is one immutable, local low-poly school. Its only animated value is one presentation-time
    // translation computed in Render; no per-fish transforms, geometry rebuild, or world query is involved.
    auto fauna = BuildUnderwaterFaunaField(seabed->id, M2SeaSurfaceLevelMeters);
    if (!fauna)
    {
        return std::unexpected("physical playground underwater fauna construction failed: " + fauna.error());
    }
    const auto faunaUpload = renderer.UploadModel(fauna->renderGeometry);
    const auto faunaDraws = Render::PrepareModelDraws(fauna->renderGeometry);
    const std::size_t faunaVertexCount = fauna->renderGeometry.primitives.empty()
                                             ? 0U
                                             : fauna->renderGeometry.primitives.front().vertices.size();
    const std::size_t faunaIndexCount = fauna->renderGeometry.primitives.empty()
                                            ? 0U
                                            : fauna->renderGeometry.primitives.front().indices.size();
    if (!faunaUpload || !faunaDraws || fauna->fish.size() != M3UnderwaterFishCount ||
        fauna->renderGeometry.materials.size() != 1U || fauna->renderGeometry.primitives.size() != 1U ||
        fauna->renderGeometry.nodes.size() != 1U || faunaDraws->size() != 1U || faunaVertexCount != 168U ||
        faunaIndexCount != 216U || faunaIndexCount / 3U != M3UnderwaterFishCount * M3UnderwaterFishTrianglesPerFish ||
        faunaIndexCount / 3U > M3UnderwaterFishTriangleBudget || !faunaUpload->stats.uploadCompleted ||
        !faunaUpload->handle.IsValid() || !renderer.IsGpuModelValid(faunaUpload->handle) ||
        faunaUpload->stats.primitiveCount != 1U || faunaUpload->stats.vertexCount != faunaVertexCount ||
        faunaUpload->stats.indexCount != faunaIndexCount ||
        faunaDraws->front().modelToWorld.values != Assets::ModelTransform{}.values)
    {
        return std::unexpected("physical playground underwater fauna presentation initialization validation failed" +
                               (!faunaUpload ? ": " + faunaUpload.error() :
                                !faunaDraws ? ": " + faunaDraws.error() : std::string{}));
    }

    const auto seabedUpload = renderer.UploadModel(seabed->renderGeometry);
    if (!seabedUpload)
    {
        return std::unexpected("physical playground seabed GPU upload failed: " + seabedUpload.error());
    }
    const auto seabedDraws = Render::PrepareModelDraws(seabed->renderGeometry);
    if (seabed->renderGeometry.primitives.size() != 2U)
    {
        return std::unexpected("physical playground representative environment must contain terrain and rock primitives");
    }
    const std::size_t terrainVertexCount = seabed->renderGeometry.primitives[0].vertices.size();
    const std::size_t terrainIndexCount = seabed->renderGeometry.primitives[0].indices.size();
    const std::size_t rockVertexCount = seabed->renderGeometry.primitives[1].vertices.size();
    const std::size_t rockIndexCount = seabed->renderGeometry.primitives[1].indices.size();
    const std::size_t environmentVertexCount = terrainVertexCount + rockVertexCount;
    const std::size_t environmentIndexCount = terrainIndexCount + rockIndexCount;
    if (!seabedUpload->stats.uploadCompleted || !seabedUpload->handle.IsValid() ||
        !renderer.IsGpuModelValid(seabedUpload->handle) || !seabedDraws || seabedDraws->size() != 2U ||
        seabedUpload->stats.primitiveCount != seabed->renderGeometry.primitives.size() ||
        seabedUpload->stats.vertexCount != environmentVertexCount ||
        seabedUpload->stats.indexCount != environmentIndexCount ||
        seabedDraws->at(0).modelToWorld.values != Assets::ModelTransform{}.values ||
        seabedDraws->at(1).modelToWorld.values != Assets::ModelTransform{}.values)
    {
        return std::unexpected("physical playground seabed presentation initialization validation failed");
    }

    auto strategicSeabed = BuildStrategicSeabedPresentationModel();
    if (!strategicSeabed)
    {
        return std::unexpected("physical playground strategic seabed construction failed: " + strategicSeabed.error());
    }
    const auto strategicSeabedUpload = renderer.UploadModel(*strategicSeabed);
    const auto strategicSeabedDraws = Render::PrepareModelDraws(*strategicSeabed);
    if (!strategicSeabedUpload || !strategicSeabedDraws || strategicSeabed->materials.size() != 1U ||
        strategicSeabed->primitives.size() != 1U || strategicSeabed->nodes.size() != 1U ||
        strategicSeabedDraws->size() != 1U || !strategicSeabedUpload->stats.uploadCompleted ||
        !strategicSeabedUpload->handle.IsValid() || !renderer.IsGpuModelValid(strategicSeabedUpload->handle) ||
        strategicSeabedUpload->stats.primitiveCount != 1U ||
        strategicSeabedUpload->stats.indexCount != strategicSeabed->primitives.front().indices.size() ||
        strategicSeabedDraws->front().modelToWorld.values != Assets::ModelTransform{}.values)
    {
        return std::unexpected(
            "physical playground strategic seabed presentation initialization validation failed" +
            (!strategicSeabedUpload ? ": " + strategicSeabedUpload.error() :
             !strategicSeabedDraws ? ": " + strategicSeabedDraws.error() : std::string{}));
    }

    // IG1-C body origin follows the production collision contract. The render bounds center is not used for
    // collision size, center, initial placement, or model-to-body composition.
    const Assets::ModelVector3 collisionCenterModel = collisionProxy.localCenter;

    // D2 world placement: the authoritative collision/reference point starts exactly M2InitialSubmarineDepthMeters
    // below the WaterBody surface. X/Z come from the production collision center; Y comes exclusively from WaterBody.
    const Physics::PhysicsVector3 initialBodyWorldCenter = ComputeInitialBodyWorldCenter(
        water->Config().surfaceLevelY, M2InitialSubmarineDepthMeters, collisionCenterModel);

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

    std::array<Marine::ControlSurfaceComponent, 2> controlSurfaces = M2ControlSurfaces;
    for (auto& control : controlSurfaces)
    {
        control.bodyLocalPositionMeters = ShiftProductionPointToBodyLocal(
            control.bodyLocalPositionMeters, collisionCenter);
    }
    const Physics::PhysicsVector3 propulsorBodyLocalPosition =
        ShiftProductionPointToBodyLocal(M2PropulsorBodyLocalPositionMeters, collisionCenter);

    // The canonical identity-oriented vessel must start above every coarse column it overlaps.
    for (const auto& box : seabed->collisionBoxes)
    {
        if (std::abs(initialBodyWorldCenter.x - box.position.x) < collisionHalfExtents.x + box.halfExtents.x &&
            initialBodyWorldCenter.y - collisionHalfExtents.y <= box.position.y + box.halfExtents.y)
            return std::unexpected("canonical submarine starts penetrating seabed collision");
    }
    // M3-H coarse ice is deliberately authored well above the canonical vessel. Validate the real static
    // proxy descriptions before creating the submarine body; no mesh/GPU bounds participate in this test.
    for (const auto& box : ice->collisionBoxes)
    {
        if (AxisAlignedBoxesOverlap(initialBodyWorldCenter, collisionHalfExtents, box.position, box.halfExtents))
        {
            return std::unexpected("canonical submarine starts penetrating underwater ice collision");
        }
    }

    Physics::DynamicBoxBodyCreateInfo bodyInfo;
    bodyInfo.halfExtents = collisionHalfExtents;
    bodyInfo.mass = M2GameAnteyMassTuningKg; // temporary Game-owned neutral-mass tuning
    bodyInfo.position = initialBodyWorldCenter;
    bodyInfo.orientation = {}; // canonical production BOX orientation is identity
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

    // IG1-C: production buoyancy proxy supplies spatial extent/COB only. Effective neutral displacement
    // remains the accepted Game-owned mass / density policy and is split across four bounded points.
    Marine::BuoyancyComponent buoyancy = BuildM2Buoyancy(*water, buoyancyProxy, collisionProxy);
    const double expectedVolume = static_cast<double>(M2GameAnteyMassTuningKg) /
                                  static_cast<double>(water->Config().densityKgPerCubicMeter);
    double configuredPotentialVolume = 0.0;
    const Physics::PhysicsVector3 proxyCenter = ToPhysicsVector(buoyancyProxy.localCenter);
    const Physics::PhysicsVector3 proxyHalfExtents = ToPhysicsVector(buoyancyProxy.halfExtents);
    for (const Marine::BuoyancyPoint& point : buoyancy.points)
    {
        if (!point.bodyLocalPositionMeters.IsFinite() || !std::isfinite(point.displacedVolumeCubicMeters) ||
            point.displacedVolumeCubicMeters <= 0.0F)
        {
            (void)physics.DestroyBody(body, &physicsError);
            return std::unexpected("physical playground production buoyancy point is invalid");
        }
        const Physics::PhysicsVector3 productionPoint{
            point.bodyLocalPositionMeters.x + collisionCenter.x,
            point.bodyLocalPositionMeters.y + collisionCenter.y,
            point.bodyLocalPositionMeters.z + collisionCenter.z};
        if (std::abs(productionPoint.x - proxyCenter.x) > proxyHalfExtents.x + 1.0e-4F ||
            std::abs(productionPoint.y - proxyCenter.y) > proxyHalfExtents.y + 1.0e-4F ||
            std::abs(productionPoint.z - proxyCenter.z) > proxyHalfExtents.z + 1.0e-4F)
        {
            (void)physics.DestroyBody(body, &physicsError);
            return std::unexpected("physical playground production buoyancy point is outside its BOX proxy");
        }
        configuredPotentialVolume += static_cast<double>(point.displacedVolumeCubicMeters);
    }
    if (!NearlyEqualRelative(configuredPotentialVolume, expectedVolume, M2InitialBalanceRelativeTolerance))
    {
        (void)physics.DestroyBody(body, &physicsError);
        return std::unexpected("physical playground production buoyancy potential volume violates Game policy");
    }
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

    const double expectedWeight = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;
    const Physics::PhysicsVector3& initialForce = initialBuoyancy->totalForceNewtons;
    const bool allFullySubmerged = std::ranges::all_of(initialBuoyancy->points, [](const auto& point) {
        return std::abs(point.submergedFraction - 1.0F) <= M2InitialBalanceRelativeTolerance;
    });
    if (initialBuoyancy->points.size() != M2BuoyancyLongitudinalFractions.size() || !allFullySubmerged ||
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

    // H2 content/configuration gate: validate exactly two independent Game-owned groups against the initial
    // pose with benign zero deflection. At rest their published application points must be finite and their
    // forces exactly zero. No mesh node or asset bound supplies either physics position.
    const Marine::ControlSurfaceKinematics initialControlKinematics{
        .bodyWorldPositionMeters = initialState->position,
        .worldOrientation = initialState->orientation,
        .worldLinearVelocityMetersPerSecond = initialState->linearVelocity};
    for (std::size_t index = 0; index < controlSurfaces.size(); ++index)
    {
        const auto control = Marine::ControlSurfaceSystem::Calculate(
            *water, controlSurfaces[index], initialControlKinematics, 0.0F);
        if (!control || !control->worldPositionMeters.IsFinite() ||
            control->forceNewtons != Physics::PhysicsVector3{})
        {
            (void)physics.DestroyBody(body, &physicsError);
            return std::unexpected(
                "physical playground " + std::string(M2ControlSurfaceNames[index]) +
                " control-surface initialization validation failed" +
                (control ? std::string{} : ": " + control.error().message));
        }
    }

    std::vector<Physics::PhysicsBodyHandle> seabedBodies;
    seabedBodies.reserve(seabed->collisionBoxes.size());
    for (const auto& box : seabed->collisionBoxes)
    {
        const auto handle = physics.CreateStaticBoxBody(box, &physicsError);
        if (!handle.IsValid())
        {
            for (const auto previous : seabedBodies) (void)physics.DestroyBody(previous);
            (void)physics.DestroyBody(body);
            return std::unexpected("seabed static collision creation failed: " + physicsError.message);
        }
        seabedBodies.push_back(handle);
    }
    std::vector<Physics::PhysicsBodyHandle> iceBodies;
    iceBodies.reserve(ice->collisionBoxes.size());
    for (const auto& box : ice->collisionBoxes)
    {
        const auto handle = physics.CreateStaticBoxBody(box, &physicsError);
        if (!handle.IsValid())
        {
            for (const auto previous : iceBodies) (void)physics.DestroyBody(previous);
            for (const auto previous : seabedBodies) (void)physics.DestroyBody(previous);
            (void)physics.DestroyBody(body);
            return std::unexpected("underwater ice static collision creation failed: " + physicsError.message);
        }
        iceBodies.push_back(handle);
    }
    seabedBodies_ = std::move(seabedBodies);
    iceBodies_ = std::move(iceBodies);
    PlaygroundLog().Info(Diagnostics::LogCategory::Physics,
        "Environment collision ready: " + seabed->id.value + ", seabed static bodies " +
        std::to_string(seabedBodies_.size()) + ", ice static bodies " + std::to_string(iceBodies_.size()));

    // M3-F presentation is one Game-owned indexed box whose model origin intentionally matches its Jolt
    // body origin. It uses the existing opaque model path; there is no specialized primitive renderer.
    Assets::ModelAsset surfaceFloatModel = BuildM3SurfaceFloatModel();
    const auto surfaceFloatUpload = renderer.UploadModel(surfaceFloatModel);
    const auto surfaceFloatDraws = Render::PrepareModelDraws(surfaceFloatModel);
    if (!surfaceFloatUpload || !surfaceFloatDraws || surfaceFloatDraws->size() != 1U ||
        surfaceFloatModel.primitives.size() != 1U || surfaceFloatModel.primitives.front().vertices.size() != 24U ||
        surfaceFloatModel.primitives.front().indices.size() != 36U || !surfaceFloatUpload->stats.uploadCompleted ||
        surfaceFloatUpload->stats.primitiveCount != 1U || surfaceFloatUpload->stats.vertexCount != 24U ||
        surfaceFloatUpload->stats.indexCount != 36U || !surfaceFloatUpload->handle.IsValid() ||
        !renderer.IsGpuModelValid(surfaceFloatUpload->handle))
    {
        return std::unexpected(
            "physical playground M3-F surface-float model initialization failed" +
            (!surfaceFloatUpload ? ": " + surfaceFloatUpload.error() :
             !surfaceFloatDraws ? ": " + surfaceFloatDraws.error() : std::string{}));
    }

    // Initial Y is sampled from the authoritative local free surface, never snapped subsequently. The body
    // remains dynamic and can settle under forces; Z remains the locked gameplay-plane coordinate.
    const auto initialFloatSurface = water->SampleWaveSurface(M3SurfaceFloatInitialReferencePosition, 0.0);
    if (!initialFloatSurface)
    {
        return std::unexpected("physical playground M3-F initial free-surface sample failed: " +
                               initialFloatSurface.error().message);
    }
    Physics::PhysicsDegreesOfFreedom m3FloatDof;
    m3FloatDof.translationX = true;
    m3FloatDof.translationY = true;
    m3FloatDof.translationZ = false;
    m3FloatDof.rotationX = false;
    m3FloatDof.rotationY = false;
    m3FloatDof.rotationZ = true;
    Physics::DynamicBoxBodyCreateInfo surfaceFloatInfo{
        .halfExtents = M3SurfaceFloatHalfExtents,
        .mass = M3SurfaceFloatMassKg,
        .position = {M3SurfaceFloatInitialReferencePosition.x, initialFloatSurface->surfaceLevelY,
                     M3SurfaceFloatInitialReferencePosition.z},
        .orientation = {},
        .gravityEnabled = true,
        .linearDamping = M3SurfaceFloatLinearDamping,
        .angularDamping = M3SurfaceFloatAngularDamping,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {},
        .degreesOfFreedom = m3FloatDof};
    for (const auto& box : ice->collisionBoxes)
    {
        if (AxisAlignedBoxesOverlap(
                surfaceFloatInfo.position, surfaceFloatInfo.halfExtents, box.position, box.halfExtents))
        {
            return std::unexpected("M3-F surface float starts penetrating underwater ice collision");
        }
    }
    const Physics::PhysicsBodyHandle surfaceFloatBody = physics.CreateDynamicBoxBody(surfaceFloatInfo, &physicsError);
    if (!surfaceFloatBody.IsValid())
    {
        return std::unexpected("physical playground M3-F surface-float body creation failed: " +
                               physicsError.message);
    }
    const auto initialFloatState = physics.GetBodyState(surfaceFloatBody);
    const Marine::BuoyancyComponent surfaceFloatBuoyancy = BuildM3SurfaceFloatBuoyancy(*water);
    if (!initialFloatState || surfaceFloatBuoyancy.points.size() != M3SurfaceFloatBuoyancyPointPositions.size())
    {
        (void)physics.DestroyBody(surfaceFloatBody);
        return std::unexpected("physical playground M3-F initial float state or buoyancy configuration failed");
    }

    // Check the Game tuning at the reference plane independently from the current crest/trough. This proves
    // 50% point submersion yields neutral volume/weight without claiming collision-box volume is buoyancy.
    const Marine::BuoyancyPose referenceFloatPose{
        .worldPositionMeters = M3SurfaceFloatInitialReferencePosition,
        .worldOrientation = initialFloatState->orientation};
    const auto referenceFloatBuoyancy = Marine::BuoyancySystem::Calculate(
        *water, surfaceFloatBuoyancy, referenceFloatPose, *gravityMagnitude);
    const double expectedFloatNeutralVolume = static_cast<double>(M3SurfaceFloatMassKg) /
                                               static_cast<double>(water->Config().densityKgPerCubicMeter);
    const double expectedFloatPotentialVolume = 2.0 * expectedFloatNeutralVolume;
    const double expectedFloatWeight = static_cast<double>(M3SurfaceFloatMassKg) * *gravityMagnitude;
    double configuredFloatPotentialVolume = 0.0;
    for (const Marine::BuoyancyPoint& point : surfaceFloatBuoyancy.points)
    {
        configuredFloatPotentialVolume += point.displacedVolumeCubicMeters;
    }
    if (!referenceFloatBuoyancy ||
        !NearlyEqualRelative(configuredFloatPotentialVolume, expectedFloatPotentialVolume,
                             M3SurfaceFloatBalanceRelativeTolerance) ||
        !NearlyEqualRelative(referenceFloatBuoyancy->totalSubmergedVolumeCubicMeters,
                             expectedFloatNeutralVolume, M3SurfaceFloatBalanceRelativeTolerance) ||
        !NearlyEqualRelative(referenceFloatBuoyancy->totalForceNewtons.y, expectedFloatWeight,
                             M3SurfaceFloatBalanceRelativeTolerance))
    {
        (void)physics.DestroyBody(surfaceFloatBody);
        return std::unexpected("physical playground M3-F reference-plane float balance validation failed");
    }
    Marine::BuoyancyResult initialFloatWaveBuoyancy;
    initialFloatWaveBuoyancy.points.reserve(surfaceFloatBuoyancy.points.size());
    if (const auto waveCalculated = Marine::BuoyancySystem::CalculateWaveSurface(
            *water,
            surfaceFloatBuoyancy,
            {.worldPositionMeters = initialFloatState->position,
             .worldOrientation = initialFloatState->orientation},
            *gravityMagnitude,
            0.0,
            initialFloatWaveBuoyancy);
        !waveCalculated)
    {
        (void)physics.DestroyBody(surfaceFloatBody);
        return std::unexpected("physical playground M3-F initial wave buoyancy calculation failed: " +
                               waveCalculated.error().message);
    }

    modelAsset_ = *model;
    submarineModel_ = upload->handle;
    seabedSection_ = std::move(*seabed);
    seabedModel_ = seabedUpload->handle;
    seabedDraws_ = std::move(*seabedDraws);
    strategicSeabedModel_ = strategicSeabedUpload->handle;
    strategicSeabedDraws_ = std::move(*strategicSeabedDraws);
    floraField_ = std::move(*flora);
    floraModel_ = floraUpload->handle;
    floraDraws_ = std::move(*floraDraws);
    iceField_ = std::move(*ice);
    iceModel_ = iceUpload->handle;
    iceDraws_ = std::move(*iceDraws);
    faunaField_ = std::move(*fauna);
    faunaModel_ = faunaUpload->handle;
    faunaBaseDraw_ = faunaDraws->front();
    physicsBody_ = body;
    submarineCollisionHalfExtents_ = collisionHalfExtents;
    surfaceFloatModel_ = std::move(surfaceFloatModel);
    surfaceFloatModelGpu_ = surfaceFloatUpload->handle;
    surfaceFloatBaseDraw_ = surfaceFloatDraws->front();
    surfaceFloatBody_ = surfaceFloatBody;
    physics_ = &physics;
    water_ = *water;
    buoyancy_ = std::move(buoyancy);
    surfaceFloatBuoyancy_ = surfaceFloatBuoyancy;
    surfaceFloatBuoyancyResult_.points.reserve(surfaceFloatBuoyancy_.points.size());
    hydroDrag_ = BuildM2HydroDrag();
    propulsion_ = M2Propulsion;
    propulsionState_ = {};
    controlSurfaces_ = std::move(controlSurfaces);
    propellerPresentationAngleRadians_ = 0.0F;
    propulsorBodyLocalPosition_ = propulsorBodyLocalPosition;
    initialBodyWorldCenter_ = initialBodyWorldCenter;
    modelToBody_ = TranslationTransform(
        {-collisionCenter.x, -collisionCenter.y, -collisionCenter.z});
    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);

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
        "IG1-C production physics ready: collision center " + FormatVector(collisionCenter) +
            ", collision half extents " + FormatVector(collisionHalfExtents) +
            ", buoyancy center " + FormatVector(ToPhysicsVector(buoyancyProxy.localCenter)) +
            ", buoyancy half extents " + FormatVector(ToPhysicsVector(buoyancyProxy.halfExtents)) +
            ", COB " + FormatVector(ToPhysicsVector(buoyancyProxy.centerOfBuoyancy)) +
            ", effective Game displacement " + std::to_string(expectedVolume) + " m^3, mass " +
            std::to_string(M2GameAnteyMassTuningKg) + " kg, initial buoyancy " +
            std::to_string(initialForce.y) + " N, weight " + std::to_string(expectedWeight) + " N");
    PlaygroundLog().Info(
        Diagnostics::LogCategory::Render,
        "IG1-D production Antey visual ready: requested LOD " +
            std::to_string(static_cast<std::size_t>(productionLodSelection->requested)) +
            ", selected LOD " + std::to_string(static_cast<std::size_t>(productionLodSelection->selected)) +
            ", fallback " + std::string(productionLodSelection->usedFallback ? "yes" : "no") +
            ", asset " + std::string(productionLodSelection->assetId.Value()) +
            ", bounds " + FormatBounds(visualBounds) + ", length " + std::to_string(productionLengthMeters) +
            " m, nodes " + std::to_string((*model)->nodes.size()) + ", primitives " +
            std::to_string((*model)->primitives.size()) + ", materials " + std::to_string((*model)->materials.size()) +
            ", vertices " + std::to_string(upload->stats.vertexCount) + ", indices " +
            std::to_string(upload->stats.indexCount));
    PlaygroundLog().Info(
        Diagnostics::LogCategory::Render,
        "Environment section ready: " + seabedSection_->id.value + ", bounds " +
            FormatBounds(seabedSection_->bounds) + ", terrain vertices " + std::to_string(terrainVertexCount) +
            ", terrain triangles " + std::to_string(terrainIndexCount / 3U) + ", rock vertices " +
            std::to_string(rockVertexCount) + ", rock triangles " + std::to_string(rockIndexCount / 3U) +
            ", rocks " + std::to_string(seabedSection_->rocks.size()) + ", environment draws " +
            std::to_string(seabedDraws_.size()) + ", flora plants " + std::to_string(floraField_->plants.size()) +
            ", flora triangles " + std::to_string(floraIndexCount / 3U) + ", ice formations " +
            std::to_string(iceField_->formations.size()) + ", ice triangles " + std::to_string(iceIndexCount / 3U) +
            ", GPU upload success");
    return {};
}

std::expected<void, std::string> PhysicalPlayground::FixedUpdate(
    const float fixedDeltaSeconds,
    const double simulationTimeSeconds,
    const VesselCommandState& command,
    const HapticEventSink& hapticEventSink)
{
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected("physical playground fixed delta must be finite and positive");
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("physical playground fixed SimulationTime must be finite and non-negative");
    }
    if (physics_ == nullptr)
    {
        return std::unexpected("physical playground physics world is unavailable");
    }
    if (!physicsBody_.IsValid() || !surfaceFloatBody_.IsValid())
    {
        return std::unexpected("physical playground physics body handle is invalid");
    }
    if (!water_.has_value())
    {
        return std::unexpected("physical playground water body is unavailable");
    }
    const auto validatedCommand = ValidateVesselCommandState(command);
    if (!validatedCommand)
    {
        return std::unexpected("physical playground command validation failed: " + validatedCommand.error());
    }
    if (buoyancy_.points.empty() || surfaceFloatBuoyancy_.points.size() != M3SurfaceFloatBuoyancyPointPositions.size())
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
    const auto surfaceFloatState = physics_->GetBodyState(surfaceFloatBody_);
    if (!surfaceFloatState)
    {
        return std::unexpected("physical playground M3-F float state is unavailable during fixed update");
    }
    const auto gravityMagnitude = GravityMagnitudeForWater(*physics_, *water_, state->position);
    if (!gravityMagnitude)
    {
        return std::unexpected("physical playground fixed gravity validation failed: " + gravityMagnitude.error());
    }

    const auto buoyancyResult = Marine::BuoyancySystem::Calculate(
        *water_,
        buoyancy_,
        Marine::BuoyancyPose{
            .worldPositionMeters = state->position,
            .worldOrientation = state->orientation},
        *gravityMagnitude);
    if (!buoyancyResult)
    {
        return std::unexpected("physical playground buoyancy calculation failed: " + buoyancyResult.error().message);
    }

    // M3-F is the sole opt-in physical consumer. This uses the Engine-owned beginning-of-step SimulationTime
    // supplied by Game composition; the submarine's Calculate() above remains flat/reference-plane only.
    const auto surfaceFloatCalculated = Marine::BuoyancySystem::CalculateWaveSurface(
        *water_,
        surfaceFloatBuoyancy_,
        {.worldPositionMeters = surfaceFloatState->position,
         .worldOrientation = surfaceFloatState->orientation},
        *gravityMagnitude,
        simulationTimeSeconds,
        surfaceFloatBuoyancyResult_);
    if (!surfaceFloatCalculated)
    {
        return std::unexpected("physical playground M3-F wave buoyancy calculation failed: " +
                               surfaceFloatCalculated.error().message);
    }

    // F2 consumes the SAME beginning-of-tick body snapshot as buoyancy. Both force producers finish before
    // any output is applied, so neither observes state affected by the other in this fixed tick.
    const auto dragResult = Marine::HydroDragSystem::Calculate(
        *water_,
        hydroDrag_,
        Marine::HydroDragState{
            .worldOrientation = state->orientation,
            .worldLinearVelocityMetersPerSecond = state->linearVelocity,
            .worldAngularVelocityRadiansPerSecond = state->angularVelocity});
    if (!dragResult)
    {
        return std::unexpected("physical playground hydrodynamic drag calculation failed: " +
                               dragResult.error().message);
    }

    const Marine::PropulsionCommand propulsionCommand{
        .requestedDriveFraction = command.throttleFraction,
        .availablePowerFraction = M2AvailablePropulsionPowerFraction};
    const auto propulsionResult =
        Marine::PropulsionSystem::Advance(propulsion_, propulsionState_, propulsionCommand, fixedDeltaSeconds);
    if (!propulsionResult)
    {
        return std::unexpected("physical playground propulsion calculation failed: " +
                               propulsionResult.error().message);
    }

    // Both H2 surfaces consume the SAME beginning-of-tick pose/velocity. H1 alone owns conversion to body
    // flow, force magnitude/sign, orientation back to world, and the published world application point.
    const Marine::ControlSurfaceKinematics controlKinematics{
        .bodyWorldPositionMeters = state->position,
        .worldOrientation = state->orientation,
            .worldLinearVelocityMetersPerSecond = state->linearVelocity};
    // I1 intentionally maps direct semantic Depth to the existing H2 prototype *actual* deflection range.
    // There is no actuator state, target depth, vertical-velocity command, or stabilization layer here.
    const std::array<float, 2> controlDeflections{
        -M2MaximumPlaneDeflection * command.depthCommandFraction,
        M2MaximumPlaneDeflection * command.depthCommandFraction};
    std::array<Marine::ControlSurfaceResult, 2> controlResults{};
    for (std::size_t index = 0; index < controlSurfaces_.size(); ++index)
    {
        const auto control = Marine::ControlSurfaceSystem::Calculate(
            *water_,
            controlSurfaces_[index],
            controlKinematics,
            controlDeflections[index]);
        if (!control)
        {
            return std::unexpected(
                "physical playground " + std::string(M2ControlSurfaceNames[index]) +
                " control-surface calculation failed: " + control.error().message);
        }
        controlResults[index] = *control;
    }

    // Validate every remaining derived output before applying any tick output. Thrust and both H1 surfaces
    // use the SAME beginning-of-tick pose as buoyancy/drag. Signed thrust maps to body-local +X.
    const auto propulsionForceWorld = RotateBodyLocalVectorToWorld(
        state->orientation,
        {propulsionResult->thrustNewtons, 0.0F, 0.0F});
    if (!propulsionForceWorld)
    {
        return std::unexpected("physical playground propulsion force transform failed: " +
                               propulsionForceWorld.error());
    }
    const auto propulsorWorldPosition = TransformBodyLocalPointToWorld(
        state->position, state->orientation, propulsorBodyLocalPosition_);
    if (!propulsorWorldPosition)
    {
        return std::unexpected("physical playground propulsor position transform failed: " +
                               propulsorWorldPosition.error());
    }
    const auto nextPresentationAngle = AdvancePropellerPresentationAngle(
        propellerPresentationAngleRadians_,
        propulsionState_.shaftRpm,
        propulsionResult->nextState.shaftRpm,
        fixedDeltaSeconds);
    if (!nextPresentationAngle)
    {
        return std::unexpected("physical playground propeller presentation advance failed: " +
                               nextPresentationAngle.error());
    }

    // E2 published output is E1 input, point for point and in order. Do not recompute force, aggregate at
    // COM, derive torque, special-case dry points, or multiply by fixedDeltaSeconds.
    for (std::size_t index = 0; index < buoyancyResult->points.size(); ++index)
    {
        const Marine::BuoyancyPointResult& point = buoyancyResult->points[index];
        Physics::PhysicsError error;
        if (!physics_->AddForceAtWorldPosition(
                physicsBody_, point.forceNewtons, point.worldPositionMeters, &error))
        {
            return std::unexpected("physical playground buoyancy force application failed at point " +
                                   std::to_string(index) + ": " + error.message);
        }
    }

    // Apply exactly the published wave-aware point forces. No force is reconstructed at COM and no dt scale,
    // wave velocity, drag, or visual displacement enters this path; Jolt derives the two-point pitch moment.
    for (std::size_t index = 0; index < surfaceFloatBuoyancyResult_.points.size(); ++index)
    {
        const Marine::BuoyancyPointResult& point = surfaceFloatBuoyancyResult_.points[index];
        Physics::PhysicsError error;
        if (!physics_->AddForceAtWorldPosition(
                surfaceFloatBody_, point.forceNewtons, point.worldPositionMeters, &error))
        {
            return std::unexpected("physical playground M3-F float force application failed at point " +
                                   std::to_string(index) + ": " + error.message);
        }
    }

    // The current M2 box body's origin is its center of mass (ADR-0008), so apply F1's one net world force
    // there through the existing E1 operation. Do not distribute it or derive another torque from it.
    Physics::PhysicsError dragForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_, dragResult->forceNewtons, state->position, &dragForceError))
    {
        return std::unexpected("physical playground hydrodynamic drag force application failed: " +
                               dragForceError.message);
    }

    Physics::PhysicsError dragTorqueError;
    if (!physics_->AddTorque(physicsBody_, dragResult->torqueNewtonMeters, &dragTorqueError))
    {
        return std::unexpected("physical playground hydrodynamic drag torque application failed: " +
                               dragTorqueError.message);
    }

    Physics::PhysicsError propulsionForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_, *propulsionForceWorld, *propulsorWorldPosition, &propulsionForceError))
    {
        return std::unexpected("physical playground propulsion force application failed: " +
                               propulsionForceError.message);
    }

    // Apply the two published H1 outputs independently. Their near-zero linear sum must never be collapsed
    // at COM: opposite forces at +/-32 m generate the physical pitch moment through PhysicsWorld/Jolt.
    for (std::size_t index = 0; index < controlResults.size(); ++index)
    {
        Physics::PhysicsError controlForceError;
        if (!physics_->AddForceAtWorldPosition(
                physicsBody_,
                controlResults[index].forceNewtons,
                controlResults[index].worldPositionMeters,
                &controlForceError))
        {
            return std::unexpected(
                "physical playground " + std::string(M2ControlSurfaceNames[index]) +
                " control-surface force application failed: " + controlForceError.message);
        }
    }

    // Transaction boundary: state advances only after every calculation and force/torque application,
    // including both H2 surface forces, succeeds.
    propulsionState_ = propulsionResult->nextState;
    propellerPresentationAngleRadians_ = *nextPresentationAngle;

    // I2 presentation producer: derive semantic intensity only from the newly committed authoritative shaft
    // RPM. A malformed impossible state is validated and diagnosed once, but haptic presentation can never
    // roll back or fail the already-successful simulation transaction.
    float engineVibrationIntensity = 0.0F;
    const HapticFeedbackSystem hapticFeedback;
    const auto engineVibration = hapticFeedback.EngineVibrationFromShaftRpm(propulsion_, propulsionState_);
    if (engineVibration)
    {
        engineVibrationIntensity = engineVibration->intensity;
        if (hapticEventSink)
        {
            try
            {
                hapticEventSink(*engineVibration);
            }
            catch (const std::exception& exception)
            {
                if (!loggedHapticFailure_)
                {
                    loggedHapticFailure_ = true;
                    PlaygroundLog().Warning(
                        Diagnostics::LogCategory::Input,
                        "Haptic presentation callback failed and was suppressed: " + std::string(exception.what()));
                }
            }
            catch (...)
            {
                if (!loggedHapticFailure_)
                {
                    loggedHapticFailure_ = true;
                    PlaygroundLog().Warning(
                        Diagnostics::LogCategory::Input,
                        "Haptic presentation callback failed and was suppressed");
                }
            }
        }
    }
    else if (!loggedHapticFailure_)
    {
        loggedHapticFailure_ = true;
        PlaygroundLog().Warning(
            Diagnostics::LogCategory::Input,
            "Engine-vibration semantic feedback rejected and suppressed: " + engineVibration.error());
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
        for (const auto& point : buoyancyResult->points)
        {
            minimumFraction = (std::min)(minimumFraction, point.submergedFraction);
            maximumFraction = (std::max)(maximumFraction, point.submergedFraction);
        }
        const double weightMagnitude = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;
        const float pitchDegrees = 2.0F * std::atan2(state->orientation.z, state->orientation.w) *
                                   (180.0F / 3.14159265358979323846F);
        const bool first = !loggedFirstFixedSample_;
        loggedFirstFixedSample_ = true;
        loggedLaterFixedSample_ = loggedLaterFixedSample_ || fixedTickCount_ >= M2LaterDiagnosticFixedTick;
        PlaygroundLog().Info(
            Diagnostics::LogCategory::Physics,
            std::string(first ? "Physical playground first M2/I2 fixed sample: "
                              : "Physical playground later M2/I2 fixed sample: ") +
                "tick " + std::to_string(fixedTickCount_) + ", position " + FormatVector(state->position) +
                ", velocity " + FormatVector(state->linearVelocity) + ", pitch Z " +
                std::to_string(pitchDegrees) + " deg, angular velocity " +
                FormatVector(state->angularVelocity) + ", signed depth " +
                std::to_string(bodyDepth->signedDepthMeters) + " m, submerged volume " +
                std::to_string(buoyancyResult->totalSubmergedVolumeCubicMeters) + " m^3, buoyancy force " +
                FormatVector(buoyancyResult->totalForceNewtons) + ", drag force " +
                FormatVector(dragResult->forceNewtons) + ", drag torque " +
                FormatVector(dragResult->torqueNewtonMeters) + ", requested drive " +
                std::to_string(command.throttleFraction) + ", depth command " +
                std::to_string(command.depthCommandFraction) +
                ", available power " +
                std::to_string(M2AvailablePropulsionPowerFraction) + ", shaft RPM " +
                std::to_string(propulsionState_.shaftRpm) + ", target RPM " +
                std::to_string(propulsionResult->targetRpm) + ", engine haptic intensity " +
                std::to_string(engineVibrationIntensity) + ", thrust " +
                std::to_string(propulsionResult->thrustNewtons) + " N, bow deflection " +
                std::to_string(controlDeflections[M2BowPlaneIndex]) +
                ", bow force " + FormatVector(controlResults[M2BowPlaneIndex].forceNewtons) +
                ", bow point " + FormatVector(controlResults[M2BowPlaneIndex].worldPositionMeters) +
                ", stern deflection " +
                std::to_string(controlDeflections[M2SternPlaneIndex]) +
                ", stern force " + FormatVector(controlResults[M2SternPlaneIndex].forceNewtons) +
                ", stern point " + FormatVector(controlResults[M2SternPlaneIndex].worldPositionMeters) +
                ", gravity magnitude " +
                std::to_string(*gravityMagnitude) + " m/s^2, weight " +
                std::to_string(weightMagnitude) + " N, point fraction range [" +
                std::to_string(minimumFraction) + ", " + std::to_string(maximumFraction) + ']');
        const float floatPitchDegrees = 2.0F * std::atan2(surfaceFloatState->orientation.z,
                                                           surfaceFloatState->orientation.w) *
                                      (180.0F / 3.14159265358979323846F);
        PlaygroundLog().Info(
            Diagnostics::LogCategory::Physics,
            "Physical playground M3-F float fixed sample: beginning SimulationTime " +
                std::to_string(simulationTimeSeconds) + ", position " +
                FormatVector(surfaceFloatState->position) + ", pitch Z " +
                std::to_string(floatPitchDegrees) + " deg, wave buoyancy force " +
                FormatVector(surfaceFloatBuoyancyResult_.totalForceNewtons) + " N, submerged volume " +
                std::to_string(surfaceFloatBuoyancyResult_.totalSubmergedVolumeCubicMeters) + " m^3");
    }
    return {};
}

std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer,
    const double simulationTimeSeconds,
    const double presentationTimeSeconds) const
{
    if (!modelAsset_.IsValid() || !renderer.IsGpuModelValid(submarineModel_) || !seabedSection_.has_value() ||
        !renderer.IsGpuModelValid(seabedModel_) || seabedDraws_.size() != 2U || !floraField_.has_value() ||
        !renderer.IsGpuModelValid(strategicSeabedModel_) || strategicSeabedDraws_.size() != 1U ||
        floraField_->renderGeometry.primitives.size() != 1U || !renderer.IsGpuModelValid(floraModel_) ||
        floraDraws_.size() != 1U || !iceField_.has_value() || iceField_->renderGeometry.primitives.size() != 1U ||
        iceBodies_.size() != M3UnderwaterIceCollisionCount || !renderer.IsGpuModelValid(iceModel_) ||
        iceDraws_.size() != 1U || !faunaField_.has_value() || faunaField_->fish.size() != M3UnderwaterFishCount ||
        faunaField_->renderGeometry.primitives.size() != 1U || !renderer.IsGpuModelValid(faunaModel_) ||
        faunaBaseDraw_.primitiveIndex != 0U || physics_ == nullptr ||
        !physicsBody_.IsValid() || !surfaceFloatModel_.has_value() || surfaceFloatModel_->primitives.size() != 1U ||
        !renderer.IsGpuModelValid(surfaceFloatModelGpu_) || !surfaceFloatBody_.IsValid() || !water_.has_value())
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
    const auto surfaceFloatState = physics_->GetBodyState(surfaceFloatBody_);
    if (!surfaceFloatState)
    {
        return std::unexpected("physical playground M3-F float state is unavailable during render");
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

    // The procedural float model origin is its body origin, so its physics-to-render transform is direct.
    // The rendered pose always comes from Jolt after the preceding fixed step, never from SampleWaveSurface.
    const auto surfaceFloatModelToWorld = BuildBodyToWorld(*surfaceFloatState);
    if (!surfaceFloatModelToWorld)
    {
        return std::unexpected("physical playground M3-F float transform failed: " +
                               surfaceFloatModelToWorld.error());
    }
    const auto faunaModelToWorld = EvaluateUnderwaterFishSchoolPresentation(
        faunaField_->presentation, presentationTimeSeconds);
    if (!faunaModelToWorld)
    {
        return std::unexpected("physical playground underwater fauna presentation transform failed: " +
                               faunaModelToWorld.error());
    }
    Render::ModelDrawInstance surfaceFloatDraw = surfaceFloatBaseDraw_;
    surfaceFloatDraw.modelToWorld = *surfaceFloatModelToWorld;
    const auto surfaceFloatNormal = Render::BuildNormalTransform(*surfaceFloatModelToWorld);
    if (!surfaceFloatNormal)
    {
        return std::unexpected(surfaceFloatNormal.error());
    }
    surfaceFloatDraw.normalToWorld = *surfaceFloatNormal;

    // IG1-B deliberately leaves production propellers static. Their semantic anchors resolve through IG1-A,
    // but the existing M2 override addresses a prototype mesh node and must not leak raw GLB names into Game.
    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submergedSailDeviceOverrides_);
    if (!draws)
    {
        return std::unexpected(draws.error());
    }

    // Camera policy (B2.1 fixed-world contract, D2/E3 target): the retained presentation target starts at the
    // INITIAL body world center. The default benchmark remains the accepted 600 m fixed view; normal M5 play
    // may explicitly opt into wider tactical framing or narrower close inspection without moving simulation.
    const auto worldBounds = TransformBounds(modelAsset_->bounds, modelToWorld);
    if (!worldBounds)
    {
        return std::unexpected(worldBounds.error());
    }
    const auto surfaceFloatWorldBounds = TransformBounds(surfaceFloatModel_->bounds, *surfaceFloatModelToWorld);
    if (!surfaceFloatWorldBounds)
    {
        return std::unexpected("physical playground M3-F float bounds transform failed: " +
                               surfaceFloatWorldBounds.error());
    }
    const auto faunaWorldBounds = TransformBounds(faunaField_->renderGeometry.bounds, *faunaModelToWorld);
    if (!faunaWorldBounds)
    {
        return std::unexpected("physical playground underwater fauna bounds transform failed: " +
                               faunaWorldBounds.error());
    }
    const Assets::ModelVector3 target{initialBodyWorldCenter_.x, initialBodyWorldCenter_.y,
                                      initialBodyWorldCenter_.z};
    const Assets::ModelBounds cameraDepthBounds = CombineBounds(
        CombineBounds(
            CombineBounds(
                CombineBounds(
                    CombineBounds(*worldBounds, seabedSection_->renderGeometry.bounds),
                    floraField_->renderGeometry.bounds),
                iceField_->renderGeometry.bounds),
            *faunaWorldBounds),
        *surfaceFloatWorldBounds);
    const auto camera = Render::BuildFixedWorldSideViewCamera(
        target,
        renderer.AspectRatio(),
        M2GameplayCameraHorizontalSpanMeters,
        cameraDepthBounds);
    if (!camera)
    {
        return std::unexpected(camera.error());
    }

    // Keep the historical M2/M3 full-scene invariant strict for untouched benchmark instances. Normal M5
    // free-presentation framing explicitly permits crop for close inspection and player-directed tactical pan;
    // this changes presentation only and never relaxes physics/environment authority.
    if (!freePresentationCameraFraming_)
    {
        if (!Render::BoundsFitInCamera(*worldBounds, *camera))
        {
            return std::unexpected("physical playground production Antey bounds do not fit the fixed camera");
        }
        if (!Render::BoundsFitInCamera(floraField_->renderGeometry.bounds, *camera) ||
            !Render::BoundsFitInCamera(iceField_->renderGeometry.bounds, *camera) ||
            !Render::BoundsFitInCamera(*faunaWorldBounds, *camera) ||
            !Render::BoundsFitInCamera(*surfaceFloatWorldBounds, *camera))
        {
            return std::unexpected("physical playground existing environment bounds do not fit the fixed camera");
        }
    }

    // M3-C/C.1 authority boundary: WaterBody remains in Game/Simulation. Game derives only the authoritative
    // surface Y, fixed presentation tuning, plus the orthographic camera-plane center and view direction. The
    // renderer receives no WaterBody, physics state, seabed authority, or gameplay visibility state.
    const Render::ScenePresentationParameters scenePresentation{
        .depthLighting = {
            .surfaceLevelYMeters = water_->Config().surfaceLevelY,
            .attenuationPerMeterRgb = M3DepthAttenuationPerMeterRgb,
            .deepAmbientRgb = M3DeepAmbientRgb},
        .cameraPlaneCenterWorldPosition = {camera->position.x, camera->position.y, camera->position.z},
        .cameraViewDirection = {camera->viewDirection.x, camera->viewDirection.y, camera->viewDirection.z},
        .fogExtinctionPerMeter = M3FogExtinctionPerMeter,
        .fogColorRgb = M3FogColorRgb};
    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)
    {
        return std::unexpected("physical playground scene presentation configuration failed: " + configured.error());
    }

    // Legacy M2/M3 benchmark presentation keeps its original dark above-water clear. M5 free-presentation
    // gameplay uses a deterministic DAY baseline instead: a scene-linear sky gradient plus a small sun cue.
    // This is presentation-only and intentionally not a time-of-day simulation; future weather/day-night work
    // may replace it without touching WaterBody or combat authority.
    const auto waterlineViewportY = ProjectWorldSurfaceToViewportY(*camera, water_->Config().surfaceLevelY);
    if (!waterlineViewportY)
    {
        return std::unexpected("physical playground waterline projection failed: " + waterlineViewportY.error());
    }

    if (freePresentationCameraFraming_)
    {
        const auto skyBands = BuildM5DaySkyPresentationBands(*waterlineViewportY);
        if (!skyBands)
        {
            return std::unexpected("physical playground day-sky presentation failed: " + skyBands.error());
        }
        for (const auto& band : *skyBands)
        {
            const auto cleared = renderer.ClearViewportRect(band.viewport, band.color);
            if (!cleared)
            {
                return std::unexpected("physical playground day-sky clear failed: " + cleared.error());
            }
        }

        const auto sunStrips = BuildM5DaySunPresentationStrips(*waterlineViewportY, renderer.AspectRatio());
        if (!sunStrips)
        {
            return std::unexpected("physical playground day-sun presentation failed: " + sunStrips.error());
        }
        for (const auto& strip : *sunStrips)
        {
            const auto cleared = renderer.ClearViewportRect(strip.viewport, strip.color);
            if (!cleared)
            {
                return std::unexpected("physical playground day-sun clear failed: " + cleared.error());
            }
        }
    }
    else
    {
        const auto aboveWaterCleared = renderer.ClearViewportRect(
            Render::ViewportRect{}, // full viewport: default {0, 0, 1, 1}
            M2AboveWaterBackgroundColor);
        if (!aboveWaterCleared)
        {
            return std::unexpected(aboveWaterCleared.error());
        }
    }

    const auto underwaterRegion = UnderwaterRegionForSurface(*camera, water_->Config().surfaceLevelY);
    if (!underwaterRegion)
    {
        return std::unexpected("physical playground scalable underwater region failed: " + underwaterRegion.error());
    }
    if (underwaterRegion->has_value())
    {
        const auto underwaterCleared = renderer.ClearViewportRect(**underwaterRegion, M2UnderwaterBackgroundColor);
        if (!underwaterCleared)
        {
            return std::unexpected("physical playground scalable underwater clear failed: " + underwaterCleared.error());
        }
    }

    // M5-V1.2 scalable presentation: the accepted M3 section remains the sole detailed local environment
    // section and is never tiled. Once span/pan no longer fits that section, the temporary strategic silhouette
    // supplies render-only bathymetry. Flora/fauna remain their single authored local instances and naturally
    // leave the view as the camera moves away; generic M5 combat never inherits the Arctic ice field.
    std::span<const Render::ModelDrawInstance> seabedPresentationDraws{seabedDraws_};
    std::span<const Render::ModelDrawInstance> strategicSeabedPresentationDraws{};
    std::span<const Render::ModelDrawInstance> floraPresentationDraws{floraDraws_};
    std::span<const Render::ModelDrawInstance> icePresentationDraws{iceDraws_};
    Render::ModelDrawInstance faunaDraw = faunaBaseDraw_;
    faunaDraw.modelToWorld = *faunaModelToWorld;
    std::span<const Render::ModelDrawInstance> faunaPresentationDraws(&faunaDraw, 1U);

    if (freePresentationCameraFraming_)
    {
        icePresentationDraws = {};

        constexpr float LocalSeabedBottomSafetyMarginMeters = 1.0F;
        const float cameraBottomWorldYMeters = camera->target.y - 0.5F * camera->height;
        const bool localSeabedCoversView =
            UseDetailedEnvironmentPresentation(camera->width) &&
            HorizontalPresentationBoundsCoverView(
                seabedSection_->renderGeometry.bounds.minimum.x,
                seabedSection_->renderGeometry.bounds.maximum.x,
                camera->target.x,
                camera->width) &&
            seabedSection_->renderGeometry.bounds.minimum.y <
                cameraBottomWorldYMeters - LocalSeabedBottomSafetyMarginMeters;

        if (!localSeabedCoversView)
        {
            seabedPresentationDraws = {};
            // This concrete M5 combat playground owns an authored tactical-context bathymetry extension whose
            // central knots match the accepted local M3 seabed. It is presentation-only (no Jolt/nav/acoustic
            // authority) but zoom must not make a known shallow seabed disappear. Other future regions may
            // explicitly choose unknown/deep bathymetry and fall through to the abyss presentation.
            constexpr bool M5CombatRegionWideAreaBathymetryKnown = true;
            if (UseStrategicSeabedPresentation(camera->width, M5CombatRegionWideAreaBathymetryKnown))
            {
                strategicSeabedPresentationDraws = strategicSeabedDraws_;
            }
            else
            {
                // Flora is rooted in the local seabed profile. Once neither detailed nor tactical-context
                // bathymetry is rendered, retaining it would leave plants visibly suspended in deep water.
                floraPresentationDraws = {};
            }
        }
    }

    // When neither local nor explicitly-known wide bathymetry is available, present a deliberate deep-ocean
    // abyss below the canonical 700 m submarine gameplay band. This is screen-space atmosphere only: it does
    // not create seabed, collision, navigation or acoustic terrain authority.
    if (freePresentationCameraFraming_ && seabedPresentationDraws.empty() &&
        strategicSeabedPresentationDraws.empty())
    {
        const auto gameplayBandBottom = ProjectWorldSurfaceToViewportY(
            *camera, water_->Config().surfaceLevelY - NormalGameplayMaximumVisibleDepthMeters);
        if (!gameplayBandBottom)
        {
            return std::unexpected("physical playground deep-water boundary projection failed: " +
                                   gameplayBandBottom.error());
        }
        const auto abyssBands = BuildDeepWaterAbyssPresentationBands(*gameplayBandBottom);
        if (!abyssBands)
        {
            return std::unexpected("physical playground deep-water presentation failed: " + abyssBands.error());
        }
        for (const auto& band : *abyssBands)
        {
            const auto cleared = renderer.ClearViewportRect(band.viewport, band.color);
            if (!cleared)
            {
                return std::unexpected("physical playground deep-water clear failed: " + cleared.error());
            }
        }
    }

    // The M3 Gerstner surface and suspended-particle field are authored as bounded local-detail envelopes.
    // Draw them only while that envelope fully covers the viewport. At wider/panned framing the full-width
    // WaterBody-derived underlay + depth/fog presentation remains, so no differently shaded rectangle can
    // reveal the local mesh/field bounds. Untouched 600 m M3 still takes the original draw path exactly once.
    const Render::GerstnerSurfacePresentationParameters gerstnerPresentation =
        BuildGerstnerSurfacePresentation(*water_);
    const bool gerstnerCoversView = HorizontalPresentationBoundsCoverView(
        gerstnerPresentation.minimumX,
        gerstnerPresentation.maximumX,
        camera->target.x,
        camera->width);
    std::expected<Render::GerstnerSurfaceDrawStats, std::string> gerstnerStats =
        Render::GerstnerSurfaceDrawStats{};
    if (gerstnerCoversView)
    {
        gerstnerStats = renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds);
    }
    if (!gerstnerStats)
    {
        return std::unexpected("physical playground Gerstner surface draw failed: " + gerstnerStats.error());
    }

    std::expected<Render::ModelDrawStats, std::string> strategicSeabedStats = Render::ModelDrawStats{};
    if (!strategicSeabedPresentationDraws.empty())
    {
        strategicSeabedStats = renderer.DrawModel(
            strategicSeabedModel_, strategicSeabedPresentationDraws, *camera);
    }
    if (!strategicSeabedStats)
    {
        return strategicSeabedStats;
    }

    std::expected<Render::ModelDrawStats, std::string> seabedStats = Render::ModelDrawStats{};
    if (!seabedPresentationDraws.empty())
    {
        seabedStats = renderer.DrawModel(seabedModel_, seabedPresentationDraws, *camera);
    }
    if (!seabedStats)
    {
        return seabedStats;
    }

    std::expected<Render::ModelDrawStats, std::string> floraStats = Render::ModelDrawStats{};
    if (!floraPresentationDraws.empty())
    {
        floraStats = renderer.DrawModel(floraModel_, floraPresentationDraws, *camera);
    }
    if (!floraStats)
    {
        return floraStats;
    }

    std::expected<Render::ModelDrawStats, std::string> iceStats = Render::ModelDrawStats{};
    if (!icePresentationDraws.empty())
    {
        iceStats = renderer.DrawModel(iceModel_, icePresentationDraws, *camera);
    }
    if (!iceStats)
    {
        return iceStats;
    }

    std::expected<Render::ModelDrawStats, std::string> faunaStats = Render::ModelDrawStats{};
    if (!faunaPresentationDraws.empty())
    {
        faunaStats = renderer.DrawModel(faunaModel_, faunaPresentationDraws, *camera);
    }
    if (!faunaStats)
    {
        return faunaStats;
    }

    const auto submarineStats = renderer.DrawModel(submarineModel_, *draws, *camera);
    if (!submarineStats)
    {
        return submarineStats;
    }
    const auto surfaceFloatStats = renderer.DrawModel(
        surfaceFloatModelGpu_, std::span<const Render::ModelDrawInstance>(&surfaceFloatDraw, 1U), *camera);
    if (!surfaceFloatStats)
    {
        return surfaceFloatStats;
    }

    const bool particleFieldCoversView = HorizontalPresentationBoundsCoverView(
        M3UnderwaterParticleField.minimumWorldPosition[0],
        M3UnderwaterParticleField.maximumWorldPosition[0],
        camera->target.x,
        camera->width);
    std::expected<Render::SuspendedParticleDrawStats, std::string> particleStats =
        Render::SuspendedParticleDrawStats{};
    if (particleFieldCoversView)
    {
        particleStats = renderer.DrawSuspendedParticleField(*camera);
    }
    if (!particleStats)
    {
        return std::unexpected("physical playground particle draw failed: " + particleStats.error());
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
                "Physical playground M3-E presentation: authoritative surface Y " +
                    std::to_string(water_->Config().surfaceLevelY) + ", camera target Y " +
                    std::to_string(camera->target.y) + ", horizontal span " +
                    std::to_string(camera->width) + ", vertical span " +
                    std::to_string(camera->height) + ", normalized waterline from top " +
                    std::to_string(*waterline));
        }
    }
    return Render::ModelDrawStats{
        .drawCalls = gerstnerStats->drawCalls + strategicSeabedStats->drawCalls + seabedStats->drawCalls +
                     floraStats->drawCalls + iceStats->drawCalls +
                     faunaStats->drawCalls + submarineStats->drawCalls + surfaceFloatStats->drawCalls +
                     particleStats->drawCalls,
        // ModelDrawStats::submittedPrimitives counts ModelDrawInstance primitives only. The Gerstner surface
        // and suspended field are non-model batches. The fauna field is an ordinary one-primitive model draw,
        // so include that submitted model primitive in the established model-only diagnostic.
        .submittedPrimitives = strategicSeabedStats->submittedPrimitives + seabedStats->submittedPrimitives +
                              floraStats->submittedPrimitives + iceStats->submittedPrimitives +
                              faunaStats->submittedPrimitives + submarineStats->submittedPrimitives +
                              surfaceFloatStats->submittedPrimitives,
        .submittedIndices = gerstnerStats->indexCount + strategicSeabedStats->submittedIndices +
                            seabedStats->submittedIndices + floraStats->submittedIndices +
                            iceStats->submittedIndices + submarineStats->submittedIndices + surfaceFloatStats->submittedIndices +
                            faunaStats->submittedIndices + particleStats->indexCount};
}

Render::GpuModelHandle PhysicalPlayground::SubmarineModel() const noexcept
{
    return submarineModel_;
}
} // namespace DeepRun::Game
