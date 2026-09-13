from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel: str, old: str, new: str) -> None:
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{rel}: expected one replacement, found {count}\n--- OLD ---\n{old[:800]}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# 1. Bow planes are deployment-only. Only the stern planes create pitch force
#    and visibly rotate. Remove bow deflection from player-facing telemetry.
# ---------------------------------------------------------------------------
replace_once(
    "Game/PhysicalPlayground.cpp",
    '''// H2 Game-owned prototype tuning for exactly two independently evaluated diving-plane groups. These values
// are not measured/classified vessel data, mesh- or collision-derived, or universal submarine constants.
constexpr std::size_t M2BowPlaneIndex = 0;
constexpr std::size_t M2SternPlaneIndex = 1;
constexpr std::array<Marine::ControlSurfaceComponent, 2> M2ControlSurfaces{{
    {.bodyLocalPositionMeters = {32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F},
    {.bodyLocalPositionMeters = {-32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F}}};

constexpr float M2MaximumPlaneDeflection = 0.5F;
''',
    '''// Corrective M5 closure: production bow planes are deployment-only in Deep Run. They may be housed or
// extended, but never rotate with Depth input and never contribute hydrodynamic control force. The stern
// horizontal planes are the sole plane-based pitch authority; low/zero-speed vertical control remains ballast.
constexpr std::size_t M2BowPlaneIndex = 0;
constexpr std::size_t M2SternPlaneIndex = 1;
constexpr std::array<Marine::ControlSurfaceComponent, 2> M2ControlSurfaces{{
    {.bodyLocalPositionMeters = {32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F}, // retained only as production spatial/deployment context
    {.bodyLocalPositionMeters = {-32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F}}};

constexpr float M2MaximumPlaneDeflection = 0.5F;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''constexpr float DepthPlaneVisualRadians(
    const float committedDeflectionFraction,
    const bool sternPlane) noexcept
{
    const float normalized = M2MaximumPlaneDeflection > 0.0F
        ? std::clamp(committedDeflectionFraction / M2MaximumPlaneDeflection, -1.0F, 1.0F)
        : 0.0F;
    // H2 intentionally commits opposite force signs for bow and stern so both forces create the same pitch
    // moment. The production meshes do not share the same authored hinge basis, so presentation must account
    // for that distinction rather than blindly mapping the simulation sign to the same local rotation sign.
    const float authoredBasisSign = sternPlane ? 1.0F : -1.0F;
    return authoredBasisSign * normalized * M5DepthPlaneVisualMaximumRadians;
}

// Regression contract for direct depth control. Surface/nose-up commits +bow/-stern force deflection; dive is
// the inverse. Both production plane groups must therefore rotate coherently in the runtime presentation basis.
static_assert(DepthPlaneVisualRadians(M2MaximumPlaneDeflection, false) < 0.0F);
static_assert(DepthPlaneVisualRadians(-M2MaximumPlaneDeflection, true) < 0.0F);
static_assert(DepthPlaneVisualRadians(-M2MaximumPlaneDeflection, false) > 0.0F);
static_assert(DepthPlaneVisualRadians(M2MaximumPlaneDeflection, true) > 0.0F);

Assets::ModelTransform DepthPlanePostTransform(
    const float committedDeflectionFraction,
    const bool sternPlane) noexcept
{
    const float radians = DepthPlaneVisualRadians(committedDeflectionFraction, sternPlane);
''',
    '''constexpr float SternPlaneVisualRadians(const float committedDeflectionFraction) noexcept
{
    const float normalized = M2MaximumPlaneDeflection > 0.0F
        ? std::clamp(committedDeflectionFraction / M2MaximumPlaneDeflection, -1.0F, 1.0F)
        : 0.0F;
    return normalized * M5DepthPlaneVisualMaximumRadians;
}

static_assert(SternPlaneVisualRadians(-M2MaximumPlaneDeflection) < 0.0F);
static_assert(SternPlaneVisualRadians(M2MaximumPlaneDeflection) > 0.0F);

Assets::ModelTransform SternPlanePostTransform(const float committedDeflectionFraction) noexcept
{
    const float radians = SternPlaneVisualRadians(committedDeflectionFraction);
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    // Both H2 surfaces consume the SAME beginning-of-tick pose/velocity. H1 alone owns conversion to body
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


    const float vesselWeightNewtons = M2GameAnteyMassTuningKg * *gravityMagnitude;
''',
    '''    // Corrective M5 closure: bow planes never receive an angular command. They are deployment-only hardware.
    // The stern horizontal planes alone consume the semantic Depth command and create hydrodynamic pitch force.
    const Marine::ControlSurfaceKinematics controlKinematics{
        .bodyWorldPositionMeters = state->position,
        .worldOrientation = state->orientation,
        .worldLinearVelocityMetersPerSecond = state->linearVelocity};
    const std::array<float, 2> controlDeflections{
        0.0F,
        M2MaximumPlaneDeflection * command.depthCommandFraction};
    std::array<Marine::ControlSurfaceResult, 2> controlResults{};
    const auto sternControl = Marine::ControlSurfaceSystem::Calculate(
        *water_,
        controlSurfaces_[M2SternPlaneIndex],
        controlKinematics,
        controlDeflections[M2SternPlaneIndex]);
    if (!sternControl)
    {
        return std::unexpected(
            "physical playground stern control-surface calculation failed: " + sternControl.error().message);
    }
    controlResults[M2SternPlaneIndex] = *sternControl;

    const float vesselWeightNewtons = M2GameAnteyMassTuningKg * *gravityMagnitude;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''        controlResults[M2BowPlaneIndex].bodyForwardSpeedMetersPerSecond,
''',
    '''        controlResults[M2SternPlaneIndex].bodyForwardSpeedMetersPerSecond,
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    // Apply the two published H1 outputs independently. Their near-zero linear sum must never be collapsed
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
''',
    '''    // Only the stern horizontal planes are an active hydrodynamic control surface. Bow planes never publish
    // or apply control force; their separate deployment state is presentation/content state only.
    Physics::PhysicsError controlForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_,
            controlResults[M2SternPlaneIndex].forceNewtons,
            controlResults[M2SternPlaneIndex].worldPositionMeters,
            &controlForceError))
    {
        return std::unexpected(
            "physical playground stern control-surface force application failed: " + controlForceError.message);
    }
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''                std::to_string(propulsionResult->thrustNewtons) + " N, bow deflection " +
                std::to_string(controlDeflections[M2BowPlaneIndex]) +
                ", bow force " + FormatVector(controlResults[M2BowPlaneIndex].forceNewtons) +
                ", bow point " + FormatVector(controlResults[M2BowPlaneIndex].worldPositionMeters) +
                ", stern deflection " +
''',
    '''                std::to_string(propulsionResult->thrustNewtons) + " N, bow planes DEPLOYMENT_ONLY, stern deflection " +
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    // M5-V2-B composes dynamic production depth-plane articulation after the already accepted submerged
    // sail-device overrides. Both consume committed Game state; neither mutates ModelAsset or physics.
    // The current submerged playground represents bow planes in their deployed hydrodynamic state. A future
    // housed/retracted state must come from explicit production authoring/state, never by zeroing their forces.
    std::vector<Render::ModelNodeTransformOverride> submarineNodeOverrides = submergedSailDeviceOverrides_;
    submarineNodeOverrides.reserve(
        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +
        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size());
    for (std::size_t group = 0; group < depthPlaneMeshNodeIndices_.size(); ++group)
    {
        const Assets::ModelTransform postTransform = DepthPlanePostTransform(
            committedControlSurfaceDeflections_[group],
            group == M2SternPlaneIndex);
        for (const std::size_t meshNodeIndex : depthPlaneMeshNodeIndices_[group])
        {
            submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = postTransform});
        }
    }
''',
    '''    // Production bow planes are deployment-only and therefore receive no rotation override. Stern planes
    // alone articulate from committed simulation state. The primary periscope replaces its default stowed
    // sail-device transform with the bounded deployment animation driven by gameplay periscope state.
    std::vector<Render::ModelNodeTransformOverride> submarineNodeOverrides = submergedSailDeviceOverrides_;
    submarineNodeOverrides.reserve(
        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size());
    const Assets::ModelTransform sternPostTransform =
        SternPlanePostTransform(committedControlSurfaceDeflections_[M2SternPlaneIndex]);
    for (const std::size_t meshNodeIndex : depthPlaneMeshNodeIndices_[M2SternPlaneIndex])
    {
        submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = sternPostTransform});
    }
    if (primaryPeriscopeNodeIndex_.has_value())
    {
        Assets::ModelTransform periscopeTransform = primaryPeriscopeStowedTransform_;
        for (std::size_t element = 0; element < periscopeTransform.values.size(); ++element)
        {
            periscopeTransform.values[element] = primaryPeriscopeStowedTransform_.values[element] +
                (primaryPeriscopeDeployedTransform_.values[element] - primaryPeriscopeStowedTransform_.values[element]) *
                    primaryPeriscopeDeploymentProgress_;
        }
        const auto existing = std::ranges::find_if(submarineNodeOverrides, [&](const auto& value) {
            return value.nodeIndex == *primaryPeriscopeNodeIndex_;
        });
        if (existing == submarineNodeOverrides.end())
            return std::unexpected("physical playground primary periscope stowed override disappeared");
        existing->nodeLocalPostTransform = periscopeTransform;
    }
''')

# Public/player telemetry no longer describes a bow deflection angle.
replace_once(
    "Game/PhysicalPlayground.cpp",
    '''        .throttleFraction = committedThrottleFraction_,
        .bowPlaneDeflectionFraction = committedControlSurfaceDeflections_[M2BowPlaneIndex],
        .sternPlaneDeflectionFraction = committedControlSurfaceDeflections_[M2SternPlaneIndex]};
''',
    '''        .throttleFraction = committedThrottleFraction_,
        .bowPlanesDeployed = true,
        .sternPlaneDeflectionFraction = committedControlSurfaceDeflections_[M2SternPlaneIndex]};
''')

replace_once(
    "Game/PhysicalPlayground.h",
    '''    float throttleFraction = 0.0F;
    float bowPlaneDeflectionFraction = 0.0F;
    float sternPlaneDeflectionFraction = 0.0F;
''',
    '''    float throttleFraction = 0.0F;
    bool bowPlanesDeployed = true;
    float sternPlaneDeflectionFraction = 0.0F;
''')

replace_once(
    "Game/Combat/CombatCommandUi.h",
    '''    float throttleFraction = 0.0F;
    float bowPlaneDeflectionFraction = 0.0F;
    float sternPlaneDeflectionFraction = 0.0F;
''',
    '''    float throttleFraction = 0.0F;
    bool bowPlanesDeployed = true;
    float sternPlaneDeflectionFraction = 0.0F;
''')

replace_once(
    "DeepRun/Main.cpp",
    '''                            .throttleFraction = navigationTelemetry->throttleFraction,
                            .bowPlaneDeflectionFraction = navigationTelemetry->bowPlaneDeflectionFraction,
                            .sternPlaneDeflectionFraction = navigationTelemetry->sternPlaneDeflectionFraction});
''',
    '''                            .throttleFraction = navigationTelemetry->throttleFraction,
                            .bowPlanesDeployed = navigationTelemetry->bowPlanesDeployed,
                            .sternPlaneDeflectionFraction = navigationTelemetry->sternPlaneDeflectionFraction});
''')

replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    '''    if (snapshot.bowPlaneDeflectionFraction > commandThreshold)
    {
        return "INCREASING BUOYANCY";
    }
    if (snapshot.bowPlaneDeflectionFraction < -commandThreshold)
    {
        return "DECREASING BUOYANCY";
    }
''',
    '''    // Bow planes are deployment-only; stern deflection is the only hydrodynamic depth/pitch command cue.
    // Canonical Depth -1 (surface) produces negative stern deflection, +1 (dive) positive deflection.
    if (snapshot.sternPlaneDeflectionFraction < -commandThreshold)
    {
        return "INCREASING BUOYANCY";
    }
    if (snapshot.sternPlaneDeflectionFraction > commandThreshold)
    {
        return "DECREASING BUOYANCY";
    }
''')

# ---------------------------------------------------------------------------
# 2. Surface hydrostatics: reserve buoyancy gives a natural ~3/4-hull surfaced
#    equilibrium, while a simulation-owned trim force preserves neutral
#    submerged operation and releases reserve buoyancy for surfacing.
# ---------------------------------------------------------------------------
replace_once(
    "Game/PhysicalPlayground.cpp",
    '''constexpr float M2BuoyancySubmersionHalfHeightMeters = 6.0F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
''',
    '''constexpr float M2BuoyancySubmersionHalfHeightMeters = 6.0F;
// Public Project 949A references give roughly 32% reserve buoyancy. In this Game-owned point model that makes
// the untrimmed surface equilibrium about 1/1.32 = 75.8% submerged while submerged trim cancels the reserve.
constexpr float M5AnteyReserveBuoyancyFraction = 0.32F;
constexpr float M5MaximumReserveBuoyancyReleaseFractionOfWeight = 0.04F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const float totalDisplacedVolume = M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter;
''',
    '''    const float neutralDisplacedVolume = M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter;
    const float totalDisplacedVolume = neutralDisplacedVolume * (1.0F + M5AnteyReserveBuoyancyFraction);
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const double expectedVolume = static_cast<double>(M2GameAnteyMassTuningKg) /
                                  static_cast<double>(water->Config().densityKgPerCubicMeter);
''',
    '''    const double neutralVolume = static_cast<double>(M2GameAnteyMassTuningKg) /
                                 static_cast<double>(water->Config().densityKgPerCubicMeter);
    const double expectedVolume = neutralVolume * (1.0 + static_cast<double>(M5AnteyReserveBuoyancyFraction));
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const double expectedWeight = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;
    const Physics::PhysicsVector3& initialForce = initialBuoyancy->totalForceNewtons;
''',
    '''    const double expectedWeight = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;
    const double expectedFullySubmergedBuoyancy =
        expectedWeight * (1.0 + static_cast<double>(M5AnteyReserveBuoyancyFraction));
    const Physics::PhysicsVector3& initialForce = initialBuoyancy->totalForceNewtons;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''        !NearlyEqualRelative(initialForce.y, expectedWeight, M2InitialBalanceRelativeTolerance) ||
''',
    '''        !NearlyEqualRelative(initialForce.y, expectedFullySubmergedBuoyancy, M2InitialBalanceRelativeTolerance) ||
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    // Apply exactly the published wave-aware point forces. No force is reconstructed at COM and no dt scale,
    // wave velocity, drag, or visual displacement enters this path; Jolt derives the two-point pitch moment.
    // Variable ballast/trim is a real bounded simulation force at COM. It intentionally produces translation
    // without inventing a pitch moment; bow/stern planes remain the separate pitch mechanism when flow exists.
    Physics::PhysicsError ballastForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_, variableBallast->forceNewtons, state->position, &ballastForceError))
''',
    '''    // Submerged trim cancels only reserve buoyancy above vessel weight; this preserves neutral submerged
    // operation. A surface command releases a bounded part of that compensation so the boat rises physically.
    // Near the surface the buoyancy model itself loses submerged volume, producing a stable surfaced equilibrium.
    const float reserveExcessBuoyancyNewtons =
        (std::max)(0.0F, buoyancyResult->totalForceNewtons.y - vesselWeightNewtons);
    const float reserveReleaseNewtons = (std::max)(0.0F, -command.depthCommandFraction) *
        (std::min)(reserveExcessBuoyancyNewtons,
                   vesselWeightNewtons * M5MaximumReserveBuoyancyReleaseFractionOfWeight);
    Physics::PhysicsVector3 combinedBallastTrimForce = variableBallast->forceNewtons;
    combinedBallastTrimForce.y += -reserveExcessBuoyancyNewtons + reserveReleaseNewtons;

    Physics::PhysicsError ballastForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_, combinedBallastTrimForce, state->position, &ballastForceError))
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''                " m/s, ballast force " + FormatVector(variableBallast->forceNewtons) + ", drag force " +
''',
    '''                " m/s, ballast/trim force " + FormatVector(combinedBallastTrimForce) +
                ", reserve buoyancy excess " + std::to_string(reserveExcessBuoyancyNewtons) +
                " N, released reserve " + std::to_string(reserveReleaseNewtons) + " N, drag force " +
''')

# ---------------------------------------------------------------------------
# 3. Primary periscope is the inspected production SailDevice_08 / existing
#    semantic sail.retractable.03. Animate its authored stowed/deployed transform.
# ---------------------------------------------------------------------------
replace_once(
    "Game/PhysicalPlayground.cpp",
    '''constexpr std::string_view M3SeabedSectionId = "m3_seabed_01";
''',
    '''constexpr std::string_view M3SeabedSectionId = "m3_seabed_01";
// Production GLB inspection + public 949A retractable-device layout: SailDevice_08 is the tall/slender primary
// periscope candidate used by gameplay; SailDevice_17 remains the secondary periscope and stays stowed here.
constexpr std::string_view M5PrimaryPeriscopeSemanticId = "sail.retractable.03";
constexpr float M5PrimaryPeriscopeDeploymentSeconds = 2.5F; // explicit GAME POLICY, not hardware timing data
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;
    submergedSailDeviceOverrides.reserve(productionDefinition->retractableSailDevices.size());
    for (const Submarine::ProductionRetractableSailDevice& device : productionDefinition->retractableSailDevices)
''',
    '''    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;
    submergedSailDeviceOverrides.reserve(productionDefinition->retractableSailDevices.size());
    std::optional<std::size_t> primaryPeriscopeNodeIndex;
    Assets::ModelTransform primaryPeriscopeStowedTransform{};
    Assets::ModelTransform primaryPeriscopeDeployedTransform{};
    for (const Submarine::ProductionRetractableSailDevice& device : productionDefinition->retractableSailDevices)
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''        submergedSailDeviceOverrides.push_back(
            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
    }
    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices;
''',
    '''        submergedSailDeviceOverrides.push_back(
            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
        if (device.semanticId == M5PrimaryPeriscopeSemanticId)
        {
            if (primaryPeriscopeNodeIndex.has_value())
                return std::unexpected("physical playground has duplicate primary periscope semantic binding");
            primaryPeriscopeNodeIndex = *meshNodeIndex;
            primaryPeriscopeStowedTransform = device.stowedLocalPostTransform;
            primaryPeriscopeDeployedTransform = device.deployedLocalPostTransform;
        }
    }
    if (!primaryPeriscopeNodeIndex.has_value())
        return std::unexpected("physical playground primary production periscope semantic binding is missing");
    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);
''',
    '''    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);
    primaryPeriscopeNodeIndex_ = primaryPeriscopeNodeIndex;
    primaryPeriscopeStowedTransform_ = primaryPeriscopeStowedTransform;
    primaryPeriscopeDeployedTransform_ = primaryPeriscopeDeployedTransform;
    primaryPeriscopeRequestedRaised_ = false;
    primaryPeriscopeDeploymentProgress_ = 0.0F;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    // Transaction boundary: state advances only after every calculation and force/torque application,
    // including both H2 surface forces, succeeds.
    propulsionState_ = propulsionResult->nextState;
''',
    '''    // Presentation animation follows committed gameplay periscope state but never feeds physics/sensors.
    const float periscopeStep = fixedDeltaSeconds / M5PrimaryPeriscopeDeploymentSeconds;
    if (primaryPeriscopeRequestedRaised_)
        primaryPeriscopeDeploymentProgress_ = std::clamp(primaryPeriscopeDeploymentProgress_ + periscopeStep, 0.0F, 1.0F);
    else
        primaryPeriscopeDeploymentProgress_ = std::clamp(primaryPeriscopeDeploymentProgress_ - periscopeStep, 0.0F, 1.0F);

    // Transaction boundary: state advances only after every calculation and force/torque application succeeds.
    propulsionState_ = propulsionResult->nextState;
''')

replace_once(
    "Game/PhysicalPlayground.h",
    '''    [[nodiscard]] float PresentationCameraHorizontalSpanMeters() const noexcept
    {
        return M2GameplayCameraHorizontalSpanMeters;
    }

    // Presentation-only bridge from the P-700 lifecycle.
''',
    '''    [[nodiscard]] float PresentationCameraHorizontalSpanMeters() const noexcept
    {
        return M2GameplayCameraHorizontalSpanMeters;
    }

    [[nodiscard]] std::expected<void, std::string> SetPeriscopePresentation(const bool raised)
    {
        if (!primaryPeriscopeNodeIndex_.has_value())
            return std::unexpected("primary production periscope presentation binding is unavailable");
        primaryPeriscopeRequestedRaised_ = raised;
        return {};
    }

    [[nodiscard]] float PeriscopeDeploymentProgress() const noexcept
    {
        return primaryPeriscopeDeploymentProgress_;
    }

    // Presentation-only bridge from the P-700 lifecycle.
''')

replace_once(
    "Game/PhysicalPlayground.h",
    '''    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;

    // Bounded H2 diagnostics:
''',
    '''    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;
    std::optional<std::size_t> primaryPeriscopeNodeIndex_{};
    Assets::ModelTransform primaryPeriscopeStowedTransform_{};
    Assets::ModelTransform primaryPeriscopeDeployedTransform_{};
    bool primaryPeriscopeRequestedRaised_ = false;
    float primaryPeriscopeDeploymentProgress_ = 0.0F;

    // Bounded H2 diagnostics:
''')

replace_once(
    "DeepRun/Main.cpp",
    '''                    combatUiSnapshot = combatFrame->playerCombat;
                    if (combatPlayground->Runtime().has_value())
''',
    '''                    combatUiSnapshot = combatFrame->playerCombat;
                    const auto periscopePresentation =
                        playground.SetPeriscopePresentation(combatFrame->playerCombat.periscopeRaised);
                    if (!periscopePresentation)
                    {
                        std::cerr << "[Game][ERROR] production periscope presentation failed: "
                                  << periscopePresentation.error() << '\\n';
                        return false;
                    }
                    if (combatPlayground->Runtime().has_value())
''')

# ---------------------------------------------------------------------------
# 4. Continuous waves: keep the accepted immutable M3 mesh but map it around
#    the live camera in the shader, preserving absolute-world wave phase.
# ---------------------------------------------------------------------------
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    '''        const GerstnerDrawConstants constants{
            .viewProjection = camera.viewProjection.values,
            .referenceLevelAndTime = {parameters.referenceLevelY, static_cast<float>(simulationTimeSeconds), 0.0F, 0.0F},
''',
    '''        const float authoredSpanMeters = parameters.maximumX - parameters.minimumX;
        const float horizontalScale = (std::max)(1.0F, camera.width * 1.05F / authoredSpanMeters);
        const GerstnerDrawConstants constants{
            .viewProjection = camera.viewProjection.values,
            // z/w remap the immutable local M3 mesh around the live camera; wave phase still uses absolute world X.
            .referenceLevelAndTime = {parameters.referenceLevelY, static_cast<float>(simulationTimeSeconds),
                                      camera.target.x, horizontalScale},
''')

replace_once(
    "Shaders/GerstnerSurface.hlsl",
    '''    const float timeSeconds = ReferenceLevelAndTime.y;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave0, HorizontalSteepness.x);
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave1, HorizontalSteepness.y);
        displacement += EvaluateComponent(input.basePosition.x, timeSeconds, Wave2, HorizontalSteepness.z);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = input.basePosition.x + displacement.x * surfaceWeight;
''',
    '''    const float timeSeconds = ReferenceLevelAndTime.y;
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, HorizontalSteepness.x);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, HorizontalSteepness.y);
        displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, HorizontalSteepness.z);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = baseWorldX + displacement.x * surfaceWeight;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const bool gerstnerCoversView = HorizontalPresentationBoundsCoverView(
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
''',
    '''    // The renderer remaps the immutable Gerstner mesh around the current camera. Waves therefore remain
    // continuous while the boat travels or the camera zooms; absolute world X still owns phase continuity.
    std::expected<Render::GerstnerSurfaceDrawStats, std::string> gerstnerStats =
        renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds);
''')

# ---------------------------------------------------------------------------
# 5. Known M5 combat-region seabed stays readable while travelling. It remains
#    presentation-only and does not fabricate collision/nav/acoustic authority.
# ---------------------------------------------------------------------------
replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    '''    const float distanceFromLocalMeters = (std::max)(0.0F, std::abs(xMeters) - 400.0F);
    const float baseDepthMeters = 165.0F + (std::min)(3'600.0F, distanceFromLocalMeters * 0.055F);
    const float reliefWeight = std::clamp(distanceFromLocalMeters / 2'000.0F, 0.0F, 1.0F);
    const float regionalReliefMeters = reliefWeight * (
        85.0F * std::sin(xMeters / 1'900.0F) +
        55.0F * std::sin(xMeters / 710.0F + 0.8F) +
        35.0F * std::sin(xMeters / 3'300.0F + 1.6F));
    const float depthMeters = std::clamp(baseDepthMeters + regionalReliefMeters, 150.0F, 4'800.0F);
''',
    '''    const float distanceFromLocalMeters = (std::max)(0.0F, std::abs(xMeters) - 400.0F);
    // M5's current authored combat region is a readable continental/shelf theatre, not an automatic descent
    // into a 4.8 km abyss merely because ownship moved away from x=0. Keep broad deterministic relief visible
    // in normal underwater framing; future world data will replace this presentation-only regional profile.
    const float baseDepthMeters = 165.0F + (std::min)(45.0F, distanceFromLocalMeters * 0.006F);
    const float reliefWeight = std::clamp(distanceFromLocalMeters / 2'000.0F, 0.0F, 1.0F);
    const float regionalReliefMeters = reliefWeight * (
        32.0F * std::sin(xMeters / 1'900.0F) +
        20.0F * std::sin(xMeters / 710.0F + 0.8F) +
        14.0F * std::sin(xMeters / 3'300.0F + 1.6F));
    const float depthMeters = std::clamp(baseDepthMeters + regionalReliefMeters, 150.0F, 280.0F);
''')

replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    '''    constexpr int ExtentKilometers = 300;
''',
    '''    constexpr int ExtentKilometers = 1'000;
''')

replace_once(
    "Tests/M5ScalableEnvironmentPresentationChecks.h",
    '''    if (tacticalProfile.size() < 120U || tacticalProfile.front().xMeters > -299'000.0F ||
        tacticalProfile.back().xMeters < 299'000.0F || !leftLocal || !centerLocal || !rightLocal || !nearRight ||
        !regionalRight || !deepRight || leftLocal->yMeters != -160.0F || centerLocal->yMeters != -220.0F ||
        rightLocal->yMeters != -165.0F || nearRight->yMeters > -150.0F || nearRight->yMeters < -350.0F ||
        regionalRight->yMeters >= nearRight->yMeters || deepRight->yMeters >= -700.0F ||
        std::abs(SampleM5TacticalBathymetryYMeters(6'000.0F) - SampleM5TacticalBathymetryYMeters(8'000.0F)) < 5.0F)
''',
    '''    if (tacticalProfile.size() < 2'000U || tacticalProfile.front().xMeters > -999'000.0F ||
        tacticalProfile.back().xMeters < 999'000.0F || !leftLocal || !centerLocal || !rightLocal || !nearRight ||
        !regionalRight || !deepRight || leftLocal->yMeters != -160.0F || centerLocal->yMeters != -220.0F ||
        rightLocal->yMeters != -165.0F || nearRight->yMeters > -150.0F || nearRight->yMeters < -280.0F ||
        regionalRight->yMeters > -150.0F || regionalRight->yMeters < -280.0F ||
        deepRight->yMeters > -150.0F || deepRight->yMeters < -280.0F ||
        std::abs(SampleM5TacticalBathymetryYMeters(6'000.0F) - SampleM5TacticalBathymetryYMeters(8'000.0F)) < 2.0F)
''')

# ---------------------------------------------------------------------------
# 6. Authoring/runtime contract: bow planes are deployment-only everywhere.
# ---------------------------------------------------------------------------
replace_once(
    "Tools/Blender/build_antey_source_first.py",
    '''        obj["CONTROL_SURFACE_ROLE"] = "BOW_DEPTH_PLANE" if role.startswith("BOW") else "STERN_DEPTH_PLANE"
        obj["ARTICULATION"] = "ROTATION"
        obj["HINGE_AXIS"] = "LOCAL_Y"
        obj["SIMULATION_OWNS_ANGLE"] = True
''',
    '''        obj["CONTROL_SURFACE_ROLE"] = "BOW_DEPTH_PLANE" if role.startswith("BOW") else "STERN_DEPTH_PLANE"
        if role.startswith("BOW"):
            obj["ARTICULATION"] = "DEPLOYMENT_ONLY"
            obj["HINGE_AXIS"] = "NONE"
            obj["SIMULATION_OWNS_ANGLE"] = False
        else:
            obj["ARTICULATION"] = "ROTATION"
            obj["HINGE_AXIS"] = "LOCAL_Y"
            obj["SIMULATION_OWNS_ANGLE"] = True
''')

replace_once(
    "Tools/Blender/write_production_sidecars.py",
    '''        role = obj.get("CONTROL_SURFACE_ROLE")
        if obj.get("ARTICULATION") != "ROTATION" or obj.get("HINGE_AXIS") != "LOCAL_Y" or not bool(obj.get("SIMULATION_OWNS_ANGLE", False)):
            raise RuntimeError(f"Depth plane has invalid articulation contract: {obj.name}")
        group = "BOW" if role == "BOW_DEPTH_PLANE" else "STERN"
        group_counts[group] += 1
        records.append({
            "semanticId": f"depth-plane.{group.lower()}.{group_counts[group]:02d}",
            "group": group,
            "nodeReference": obj.name,
            "articulation": "ROTATION",
            "hingeAxisSource": "LOCAL_Y",
            "simulationOwnsAngle": True,
        })
''',
    '''        role = obj.get("CONTROL_SURFACE_ROLE")
        group = "BOW" if role == "BOW_DEPTH_PLANE" else "STERN"
        if group == "BOW":
            if obj.get("ARTICULATION") != "DEPLOYMENT_ONLY" or obj.get("HINGE_AXIS") != "NONE" or bool(obj.get("SIMULATION_OWNS_ANGLE", True)):
                raise RuntimeError(f"Bow plane must be deployment-only: {obj.name}")
        elif obj.get("ARTICULATION") != "ROTATION" or obj.get("HINGE_AXIS") != "LOCAL_Y" or not bool(obj.get("SIMULATION_OWNS_ANGLE", False)):
            raise RuntimeError(f"Stern plane has invalid articulation contract: {obj.name}")
        group_counts[group] += 1
        records.append({
            "semanticId": f"depth-plane.{group.lower()}.{group_counts[group]:02d}",
            "group": group,
            "nodeReference": obj.name,
            "articulation": "DEPLOYMENT_ONLY" if group == "BOW" else "ROTATION",
            "hingeAxisSource": "NONE" if group == "BOW" else "LOCAL_Y",
            "simulationOwnsAngle": group == "STERN",
        })
''')

replace_once(
    "Game/Submarine/ProductionAnteyAsset.cpp",
    '''                        {"articulation", "ROTATION"},
                        {"hingeAxisSource", "LOCAL_Y"},
                        {"simulationOwnsAngle", true}});
''',
    '''                        {"articulation", semanticGroup == "bow" ? "DEPLOYMENT_ONLY" : "ROTATION"},
                        {"hingeAxisSource", semanticGroup == "bow" ? "NONE" : "LOCAL_Y"},
                        {"simulationOwnsAngle", semanticGroup != "bow"}});
''')

replace_once(
    "Game/Submarine/ProductionAnteyAsset.cpp",
    '''            Require(record.at("articulation").get<std::string>() == "ROTATION" &&
                    record.at("hingeAxisSource").get<std::string>() == "LOCAL_Y" &&
                    record.at("simulationOwnsAngle").get<bool>(),
                    "Antey depth-plane articulation authoring contract is invalid");
            const ProductionDepthPlaneGroup group = groupValue == "BOW"
''',
    '''            const bool bowDeploymentOnly = groupValue == "BOW" &&
                record.at("articulation").get<std::string>() == "DEPLOYMENT_ONLY" &&
                record.at("hingeAxisSource").get<std::string>() == "NONE" &&
                !record.at("simulationOwnsAngle").get<bool>();
            const bool sternRotating = groupValue == "STERN" &&
                record.at("articulation").get<std::string>() == "ROTATION" &&
                record.at("hingeAxisSource").get<std::string>() == "LOCAL_Y" &&
                record.at("simulationOwnsAngle").get<bool>();
            Require(bowDeploymentOnly || sternRotating,
                    "Antey depth-plane articulation authoring contract is invalid");
            const ProductionDepthPlaneGroup group = groupValue == "BOW"
''')

for rel in [
    "Content/submarines/Antey/Antey.authoring.json",
    "Engine/Assets/submarines/Antey/Antey.authoring.json",
]:
    path = ROOT / rel
    data = json.loads(path.read_text(encoding="utf-8"))
    controls = data.get("controlSurfaces", [])
    if len(controls) != 4:
        raise RuntimeError(f"{rel}: expected four controlSurfaces")
    for item in controls:
        if item.get("group") == "BOW":
            item["articulation"] = "DEPLOYMENT_ONLY"
            item["hingeAxisSource"] = "NONE"
            item["simulationOwnsAngle"] = False
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

print("M5 final closure core patch applied")
