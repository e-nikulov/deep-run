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
#include "Game/SurfaceFloatModel.h"
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
#include <sstream>
#include <span>
#include <string_view>

namespace DeepRun::Game
{
namespace
{
constexpr std::string_view SubmarineModelPath = "submarines/prototype/submarine_prototype.glb";
constexpr float M2GameplayCameraHorizontalSpanMeters = 600.0F;
constexpr std::string_view M3SeabedSectionId = "m3_seabed_01";

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

// E3 prototype buoyancy layout, in BODY-LOCAL meters relative to the rigid-body origin/COM. Four explicit
// points distribute force along the prototype length without deriving hydrostatics from mesh/collision
// geometry or claiming CFD fidelity. The +2 m vertical offset creates a small restoring pitch moment.
constexpr std::array<float, 4> M2BuoyancyPointXMeters = {36.0F, 12.0F, -12.0F, -36.0F};
constexpr float M2BuoyancyPointYMeters = 2.0F;
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

// Authoritative physics tuning relative to the rigid-body origin/COM. The asset only validates alignment.
constexpr Physics::PhysicsVector3 M2PropulsorBodyLocalPositionMeters{-50.0F, 0.0F, 0.0F};
// The AABB-center body origin is 1.7 m above the authored hub because the sail raises the model bounds.
// Two meters is still a narrow content-alignment gate for this approximately 100 m prototype, while the
// simulation point remains the explicit centerline tuning above rather than being mesh-derived.
constexpr float M2PropulsorAlignmentToleranceMeters = 2.0F;

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

    const Assets::ModelVector3 assetBoundsCenter = BoundsCenter(bounds);
    const auto propellerNodeIndex = ResolveM2PrototypePropellerNode(
        **model,
        assetBoundsCenter,
        M2PropulsorBodyLocalPositionMeters,
        M2PropulsorAlignmentToleranceMeters);
    if (!propellerNodeIndex)
    {
        return std::unexpected("physical playground propeller node validation failed: " +
                               propellerNodeIndex.error());
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

    // Asset-space pivot (ADR-0008): the C1 box shape is centered on the body origin, so modelToBody =
    // T(-assetBoundsCenter). The asset bounds center is used ONLY for this pivot correction — it must never
    // double as a world position or camera target.
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

    // The canonical identity-oriented vessel must start above every coarse column it overlaps.
    for (const auto& box : seabed->collisionBoxes)
    {
        if (std::abs(initialBodyWorldCenter.x - box.position.x) < halfExtents.x + box.halfExtents.x &&
            initialBodyWorldCenter.y - halfExtents.y <= box.position.y + box.halfExtents.y)
            return std::unexpected("canonical submarine starts penetrating seabed collision");
    }
    // M3-H coarse ice is deliberately authored well above the canonical vessel. Validate the real static
    // proxy descriptions before creating the submarine body; no mesh/GPU bounds participate in this test.
    for (const auto& box : ice->collisionBoxes)
    {
        if (AxisAlignedBoxesOverlap(initialBodyWorldCenter, halfExtents, box.position, box.halfExtents))
        {
            return std::unexpected("canonical submarine starts penetrating underwater ice collision");
        }
    }

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

    // H2 content/configuration gate: validate exactly two independent Game-owned groups against the initial
    // pose with benign zero deflection. At rest their published application points must be finite and their
    // forces exactly zero. No mesh node or asset bound supplies either physics position.
    const Marine::ControlSurfaceKinematics initialControlKinematics{
        .bodyWorldPositionMeters = initialState->position,
        .worldOrientation = initialState->orientation,
        .worldLinearVelocityMetersPerSecond = initialState->linearVelocity};
    for (std::size_t index = 0; index < M2ControlSurfaces.size(); ++index)
    {
        const auto control = Marine::ControlSurfaceSystem::Calculate(
            *water, M2ControlSurfaces[index], initialControlKinematics, 0.0F);
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
    controlSurfaces_ = M2ControlSurfaces;
    propellerPresentationAngleRadians_ = 0.0F;
    propellerNodeIndex_ = *propellerNodeIndex;
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
        state->position, state->orientation, M2PropulsorBodyLocalPositionMeters);
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
        const double weightMagnitude = static_cast<double>(M2PrototypeMassKg) * *gravityMagnitude;
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

    if (!propellerNodeIndex_.has_value())
    {
        return std::unexpected("physical playground propeller node index is unavailable");
    }
    const auto propellerRotation = RotationXTransform(propellerPresentationAngleRadians_);
    if (!propellerRotation)
    {
        return std::unexpected("physical playground propeller rotation failed: " + propellerRotation.error());
    }
    const std::array<Render::ModelNodeTransformOverride, 1> nodeOverrides{{
        {.nodeIndex = *propellerNodeIndex_, .nodeLocalPostTransform = *propellerRotation}}};

    // The generic post-transform preserves the authored hub translation and mutates no ModelAsset/GPU data.
    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, nodeOverrides);
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
    if (!camera || !Render::BoundsFitInCamera(*worldBounds, *camera) ||
        !Render::BoundsFitInCamera(floraField_->renderGeometry.bounds, *camera) ||
        !Render::BoundsFitInCamera(iceField_->renderGeometry.bounds, *camera) ||
        !Render::BoundsFitInCamera(*faunaWorldBounds, *camera) ||
        !Render::BoundsFitInCamera(*surfaceFloatWorldBounds, *camera))
    {
        return std::unexpected(camera ? "physical playground bounds do not fit the camera" : camera.error());
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

    // M3-E keeps the Game-owned full above-water clear, then draws its one displaced presentation backdrop.
    // Its profile is centered on the authoritative level but never feeds back into WaterBody, simulation,
    // depth lighting, fog, collision, or particles. The renderer receives only bounded visual tuning.
    const auto aboveWaterCleared = renderer.ClearViewportRect(
        Render::ViewportRect{}, // full viewport: default {0, 0, 1, 1}
        M2AboveWaterBackgroundColor);
    if (!aboveWaterCleared)
    {
        return std::unexpected(aboveWaterCleared.error());
    }

    const auto gerstnerStats = renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds);
    if (!gerstnerStats)
    {
        return std::unexpected("physical playground Gerstner surface draw failed: " + gerstnerStats.error());
    }

    const auto seabedStats = renderer.DrawModel(seabedModel_, seabedDraws_, *camera);
    if (!seabedStats)
    {
        return seabedStats;
    }
    const auto floraStats = renderer.DrawModel(floraModel_, floraDraws_, *camera);
    if (!floraStats)
    {
        return floraStats;
    }
    const auto iceStats = renderer.DrawModel(iceModel_, iceDraws_, *camera);
    if (!iceStats)
    {
        return iceStats;
    }
    Render::ModelDrawInstance faunaDraw = faunaBaseDraw_;
    faunaDraw.modelToWorld = *faunaModelToWorld;
    const auto faunaStats = renderer.DrawModel(
        faunaModel_, std::span<const Render::ModelDrawInstance>(&faunaDraw, 1U), *camera);
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
    // M3-D runs after opaque terrain and submarine draws, so its deliberately approximate transparent quads
    // still fail the existing depth test when they are behind opaque geometry. The renderer reuses the exact
    // immutable M3-C.1 scene presentation CBV rather than accepting another Game snapshot for this pass.
    const auto particleStats = renderer.DrawSuspendedParticleField(*camera);
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
        .drawCalls = gerstnerStats->drawCalls + seabedStats->drawCalls + floraStats->drawCalls + iceStats->drawCalls +
                     faunaStats->drawCalls + submarineStats->drawCalls + surfaceFloatStats->drawCalls +
                     particleStats->drawCalls,
        // ModelDrawStats::submittedPrimitives counts ModelDrawInstance primitives only. The Gerstner surface
        // and suspended field are non-model batches. The fauna field is an ordinary one-primitive model draw,
        // so include that submitted model primitive in the established model-only diagnostic.
        .submittedPrimitives = seabedStats->submittedPrimitives + floraStats->submittedPrimitives + iceStats->submittedPrimitives +
                              faunaStats->submittedPrimitives + submarineStats->submittedPrimitives +
                              surfaceFloatStats->submittedPrimitives,
        .submittedIndices = gerstnerStats->indexCount + seabedStats->submittedIndices + floraStats->submittedIndices +
                            iceStats->submittedIndices + submarineStats->submittedIndices + surfaceFloatStats->submittedIndices +
                            faunaStats->submittedIndices + particleStats->indexCount};
}

Render::GpuModelHandle PhysicalPlayground::SubmarineModel() const noexcept
{
    return submarineModel_;
}
} // namespace DeepRun::Game
