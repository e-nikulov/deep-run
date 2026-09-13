from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    s = p.read_text(encoding="utf-8")
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f"{path}: expected one exact match, found {n}: {old[:220]!r}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8", newline="\n")


def sub_once(path: str, pattern: str, repl: str) -> None:
    p = Path(path)
    s = p.read_text(encoding="utf-8")
    s2, n = re.subn(pattern, repl, s, count=1, flags=re.S)
    if n != 1:
        raise RuntimeError(f"{path}: expected one regex match, found {n}: {pattern[:220]!r}")
    p.write_text(s2, encoding="utf-8", newline="\n")


# PhysicsWorld: generic dynamic-mass API backed by Jolt MotionProperties::ScaleToMass.
replace_once(
    "Engine/Physics/PhysicsWorld.h",
    """    bool DestroyBody(PhysicsBodyHandle handle, PhysicsError* error = nullptr);\n\n    // Returns a copy of the body state.""",
    """    bool DestroyBody(PhysicsBodyHandle handle, PhysicsError* error = nullptr);\n\n    // Changes the physical mass of an existing dynamic body and scales its inertia consistently.\n    // This is a generic physics operation; callers own the domain reason (fuel, ballast, cargo, etc.).\n    // Position and velocity are not teleported or rewritten. Static bodies are rejected.\n    bool SetDynamicBodyMass(PhysicsBodyHandle handle, float massKg, PhysicsError* error = nullptr);\n\n    // Returns a copy of the body state.""",
)
replace_once(
    "Engine/Physics/PhysicsWorld.cpp",
    "#include <Jolt/Physics/Body/BodyInterface.h>\n",
    "#include <Jolt/Physics/Body/BodyInterface.h>\n#include <Jolt/Physics/Body/BodyLock.h>\n#include <Jolt/Physics/Body/MotionProperties.h>\n",
)
replace_once(
    "Engine/Physics/PhysicsWorld.cpp",
    """        logger.Info(Diagnostics::LogCategory::Physics, "Physics body destroyed (slot " + std::to_string(handle.Slot()) + ")");\n        return true;\n    }\n\n    std::optional<PhysicsBodyState> GetBodyState""",
    """        logger.Info(Diagnostics::LogCategory::Physics, "Physics body destroyed (slot " + std::to_string(handle.Slot()) + ")");\n        return true;\n    }\n\n    bool SetDynamicBodyMass(PhysicsBodyHandle handle, const float massKg, PhysicsError* error)\n    {\n        const auto fail = [this, error](const PhysicsErrorCode code, const std::string& message) -> bool {\n            if (error != nullptr)\n                *error = PhysicsError{code, message};\n            logger.Warning(Diagnostics::LogCategory::Physics, "Dynamic body mass change rejected: " + message);\n            return false;\n        };\n        if (!initialized)\n            return fail(PhysicsErrorCode::NotInitialized, "physics world is not initialized");\n        if (!std::isfinite(massKg) || massKg <= 0.0F)\n            return fail(PhysicsErrorCode::InvalidInput, "dynamic body mass must be finite and positive");\n        BodySlot* slot = Resolve(handle);\n        if (slot == nullptr)\n            return fail(PhysicsErrorCode::InvalidHandle, "handle is invalid, foreign, or stale");\n        if (slot->kind != BodyKind::Dynamic)\n            return fail(PhysicsErrorCode::InvalidInput, "mass can only be changed on a dynamic body");\n\n        {\n            JPH::BodyLockWrite lock(physicsSystem->GetBodyLockInterface(), slot->bodyId);\n            if (!lock.Succeeded())\n                return fail(PhysicsErrorCode::InvalidHandle, "Jolt body lock failed");\n            JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();\n            if (motion == nullptr)\n                return fail(PhysicsErrorCode::InvalidInput, "dynamic body has no motion properties");\n            motion->ScaleToMass(massKg);\n        }\n        physicsSystem->GetBodyInterface().ActivateBody(slot->bodyId);\n        return true;\n    }\n\n    std::optional<PhysicsBodyState> GetBodyState""",
)
replace_once(
    "Engine/Physics/PhysicsWorld.cpp",
    """bool PhysicsWorld::DestroyBody(PhysicsBodyHandle handle, PhysicsError* error)\n{\n    assert(impl_ != nullptr);\n    return impl_->DestroyBody(handle, error);\n}\n\nstd::optional<PhysicsBodyState> PhysicsWorld::GetBodyState""",
    """bool PhysicsWorld::DestroyBody(PhysicsBodyHandle handle, PhysicsError* error)\n{\n    assert(impl_ != nullptr);\n    return impl_->DestroyBody(handle, error);\n}\n\nbool PhysicsWorld::SetDynamicBodyMass(PhysicsBodyHandle handle, const float massKg, PhysicsError* error)\n{\n    assert(impl_ != nullptr);\n    return impl_->SetDynamicBodyMass(handle, massKg, error);\n}\n\nstd::optional<PhysicsBodyState> PhysicsWorld::GetBodyState""",
)

# PhysicalPlayground hydrostatics / drag constants.
sub_once(
    "Game/PhysicalPlayground.cpp",
    r"// Surface-waterplane calibration is explicit Game policy\..*?constexpr float M5SurfaceHydrostaticModeArmDepthMeters = 2\.8F;\n",
    """// One physical displacement model serves both surfaced and submerged states. Fully immersed displacement\n// supports 19,400 t; empty main ballast leaves the public 14,700 t surfaced mass. With the calibrated\n// waterplane half-height the resulting flat-water equilibrium is 2.242 m body-centre depth.\nconstexpr float M2GameBuoyancyStabilityOffsetMeters = 0.0F;\nconstexpr float M2BuoyancySubmersionHalfHeightMeters = 4.35F;\nconstexpr float M5AnteySurfaceMassKg = Submarine::AnteyPublicSurfaceDisplacementMassKg;\nconstexpr float M5AnteySubmergedMassKg = Submarine::AnteyPublicSubmergedDisplacementMassKg;\nconstexpr float M5AnteyMainBallastCapacityKg = Submarine::AnteyMainBallastWaterCapacityKg;\nconstexpr float M5SurfaceEquilibriumSubmergedFraction = M5AnteySurfaceMassKg / M5AnteySubmergedMassKg;\nconstexpr float M5SurfaceEquilibriumBodyCenterDepthMeters =\n    M2GameBuoyancyStabilityOffsetMeters +\n    M2BuoyancySubmersionHalfHeightMeters * (2.0F * M5SurfaceEquilibriumSubmergedFraction - 1.0F);\nstatic_assert(M5SurfaceEquilibriumBodyCenterDepthMeters > 2.23F &&\n              M5SurfaceEquilibriumBodyCenterDepthMeters < 2.25F);\n// Exact 949A flood/blow timing is not asserted from public data. Full empty<->full is a 40 s GAME POLICY.\nconstexpr float M5MainBallastFillRateFractionPerSecond = 0.025F;\nconstexpr float M5MainBallastBlowRateFractionPerSecond = 0.025F;\nconstexpr float M5MaximumTrimMassFractionOfSubmergedMass = 0.015F;\n// Effective Cd*A calibrated with AnteyGameplayPropulsion: terminal full ahead is 32 kn submerged / 15 kn surfaced.\nconstexpr float M5SubmergedLongitudinalEffectiveAreaSquareMeters = 24.11986F;\nconstexpr float M5SurfacedLongitudinalEffectiveAreaSquareMeters = 109.77216F;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "constexpr Physics::PhysicsVector3 M2LinearEffectiveAreaSquareMeters{150.0F, 1800.0F, 2200.0F};\n",
    "constexpr Physics::PhysicsVector3 M2LinearEffectiveAreaSquareMeters{\n    M5SubmergedLongitudinalEffectiveAreaSquareMeters, 1800.0F, 2200.0F};\n",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    const float neutralDisplacedVolume = M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter;\n    const float totalDisplacedVolume = neutralDisplacedVolume * (1.0F + M5AnteyReserveBuoyancyFraction);\n""",
    """    const float totalDisplacedVolume =\n        M5AnteySubmergedMassKg / water.Config().densityKgPerCubicMeter;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    bodyInfo.mass = M2GameAnteyMassTuningKg; // temporary Game-owned neutral-mass tuning\n",
    "    bodyInfo.mass = M5AnteySubmergedMassKg; // fully flooded main ballast: neutral submerged condition\n",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    // IG1-C: production buoyancy proxy supplies spatial extent/COB only. Effective neutral displacement\n    // remains the accepted Game-owned mass / density policy and is split across four bounded points.\n    Marine::BuoyancyComponent buoyancy = BuildM2Buoyancy(*water, buoyancyProxy, collisionProxy);\n    const double neutralVolume = static_cast<double>(M2GameAnteyMassTuningKg) /\n                                 static_cast<double>(water->Config().densityKgPerCubicMeter);\n    const double expectedVolume = neutralVolume * (1.0 + static_cast<double>(M5AnteyReserveBuoyancyFraction));\n""",
    """    // Production buoyancy proxy supplies spatial extent/COB only. Potential displaced volume is the public\n    // 19,400 t submerged displacement divided by seawater density; ballast changes physical mass, not volume.\n    Marine::BuoyancyComponent buoyancy = BuildM2Buoyancy(*water, buoyancyProxy, collisionProxy);\n    const double expectedVolume = static_cast<double>(M5AnteySubmergedMassKg) /\n                                  static_cast<double>(water->Config().densityKgPerCubicMeter);\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    const double expectedWeight = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;\n    const double expectedFullySubmergedBuoyancy =\n        expectedWeight * (1.0 + static_cast<double>(M5AnteyReserveBuoyancyFraction));\n""",
    """    const double expectedWeight = static_cast<double>(M5AnteySubmergedMassKg) * *gravityMagnitude;\n    const double expectedFullySubmergedBuoyancy = expectedWeight;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    // F2 consumes the SAME beginning-of-tick body snapshot as buoyancy. Both force producers finish before\n    // any output is applied, so neither observes state affected by the other in this fixed tick.\n    const auto dragResult = Marine::HydroDragSystem::Calculate(\n        *water_,\n        hydroDrag_,\n        Marine::HydroDragState{\n            .worldOrientation = state->orientation,\n            .worldLinearVelocityMetersPerSecond = state->linearVelocity,\n            .worldAngularVelocityRadiansPerSecond = state->angularVelocity});\n""",
    """    // Longitudinal resistance rises continuously as the hull emerges. There is no hidden speed clamp.\n    float meanSubmergedFraction = 0.0F;\n    for (const Marine::BuoyancyPointResult& point : buoyancyResult->points)\n        meanSubmergedFraction += point.submergedFraction;\n    meanSubmergedFraction /= static_cast<float>(buoyancyResult->points.size());\n    const float surfacedExposureFraction = std::clamp(\n        (1.0F - meanSubmergedFraction) / (1.0F - M5SurfaceEquilibriumSubmergedFraction), 0.0F, 1.0F);\n    Marine::HydroDragComponent liveHydroDrag = hydroDrag_;\n    liveHydroDrag.linearEffectiveAreaSquareMeters.x =\n        M5SubmergedLongitudinalEffectiveAreaSquareMeters +\n        (M5SurfacedLongitudinalEffectiveAreaSquareMeters - M5SubmergedLongitudinalEffectiveAreaSquareMeters) *\n            surfacedExposureFraction;\n    const auto dragResult = Marine::HydroDragSystem::Calculate(\n        *water_,\n        liveHydroDrag,\n        Marine::HydroDragState{\n            .worldOrientation = state->orientation,\n            .worldLinearVelocityMetersPerSecond = state->linearVelocity,\n            .worldAngularVelocityRadiansPerSecond = state->angularVelocity});\n""",
)

sub_once(
    "Game/PhysicalPlayground.cpp",
    r"    const float vesselWeightNewtons = M2GameAnteyMassTuningKg \* \*gravityMagnitude;\n.*?    if \(!variableBallast\)\n    \{\n        return std::unexpected\(\"physical playground variable-ballast evaluation failed: \" \+ variableBallast\.error\(\)\);\n    \}\n",
    """    const float mainBallastRate = command.depthCommandFraction >= 0.0F\n        ? M5MainBallastFillRateFractionPerSecond\n        : M5MainBallastBlowRateFractionPerSecond;\n    const float nextMainBallastFillFraction = std::clamp(\n        mainBallastFillFraction_ + command.depthCommandFraction * mainBallastRate * fixedDeltaSeconds,\n        0.0F, 1.0F);\n    const float baseMassKg = M5AnteySurfaceMassKg + M5AnteyMainBallastCapacityKg * nextMainBallastFillFraction;\n    const float baseWeightNewtons = baseMassKg * *gravityMagnitude;\n    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(\n        M5LowSpeedBallastDepthControl, command.depthCommandFraction,\n        sternControl->bodyForwardSpeedMetersPerSecond, state->linearVelocity.y, baseWeightNewtons);\n    if (!variableBallast)\n        return std::unexpected(\"physical playground variable-ballast evaluation failed: \" + variableBallast.error());\n    // The low-speed controller requests trim by changing equivalent water mass, never by injecting vertical force.\n    const float requestedTrimMassDeltaKg = -variableBallast->forceNewtons.y / *gravityMagnitude;\n    const float maximumTrimMassKg = M5AnteySubmergedMassKg * M5MaximumTrimMassFractionOfSubmergedMass;\n    const float trimMassDeltaKg = std::clamp(requestedTrimMassDeltaKg, -maximumTrimMassKg, maximumTrimMassKg);\n    const float nextDynamicMassKg = std::clamp(\n        baseMassKg + trimMassDeltaKg, M5AnteySurfaceMassKg, M5AnteySubmergedMassKg + maximumTrimMassKg);\n    const float vesselWeightNewtons = nextDynamicMassKg * *gravityMagnitude;\n""",
)
sub_once(
    "Game/PhysicalPlayground.cpp",
    r"    // Submerged mode cancels reserve buoyancy above vessel weight, preserving neutral deep/periscope-depth\n.*?    if \(!physics_->AddForceAtWorldPosition\(\n            physicsBody_, combinedBallastTrimForce, state->position, &ballastForceError\)\)\n    \{\n        return std::unexpected\(\n            \"physical playground variable-ballast force application failed: \" \+ ballastForceError\.message\);\n    \}\n",
    """    // Ballast changes the Jolt rigid body's real mass/inertia. Buoyancy remains purely Archimedean.\n    if (std::abs(nextDynamicMassKg - committedDynamicMassKg_) > 0.5F)\n    {\n        Physics::PhysicsError massError;\n        if (!physics_->SetDynamicBodyMass(physicsBody_, nextDynamicMassKg, &massError))\n            return std::unexpected(\"physical playground ballast mass update failed: \" + massError.message);\n    }\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    surfacedHydrostaticMode_ = nextSurfacedHydrostaticMode;\n    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;\n""",
    """    mainBallastFillFraction_ = nextMainBallastFillFraction;\n    committedDynamicMassKg_ = nextDynamicMassKg;\n    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    surfacedHydrostaticMode_ = false;\n",
    "    mainBallastFillFraction_ = 1.0F;\n    committedDynamicMassKg_ = M5AnteySubmergedMassKg;\n",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "        const double weightMagnitude = static_cast<double>(M2GameAnteyMassTuningKg) * *gravityMagnitude;\n",
    "        const double weightMagnitude = static_cast<double>(nextDynamicMassKg) * *gravityMagnitude;\n",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """                \" m/s, ballast/trim force \" + FormatVector(combinedBallastTrimForce) +\n                \", reserve buoyancy excess \" + std::to_string(reserveExcessBuoyancyNewtons) +\n                \" N, reserve trim \" + std::to_string(reserveTrimCompensationNewtons) +\n                \" N, released reserve \" + std::to_string(reserveReleaseNewtons) +\n                \" N, hydrostatic mode \" + std::string(nextSurfacedHydrostaticMode ? \"SURFACED\" : \"SUBMERGED_TRIM\") +\n                \", drag force \" +\n""",
    """                \" m/s, main ballast fill \" + std::to_string(nextMainBallastFillFraction) +\n                \", trim mass delta \" + std::to_string(trimMassDeltaKg) +\n                \" kg, dynamic mass \" + std::to_string(nextDynamicMassKg) +\n                \" kg, surface exposure \" + std::to_string(surfacedExposureFraction) +\n                \", drag force \" +\n""",
)
replace_once(
    "Game/PhysicalPlayground.h",
    "    bool surfacedHydrostaticMode_ = false;\n",
    "    float mainBallastFillFraction_ = 1.0F;\n    float committedDynamicMassKg_ = 0.0F;\n",
)

# Regression contract for canonical mass/waterline/speed values.
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    '#include "Game/Submarine/VariableBallastDepthControl.h"\n',
    '#include "Game/Submarine/AnteyHandlingModel.h"\n#include "Game/Submarine/VariableBallastDepthControl.h"\n',
)
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    "    constexpr float WeightNewtons = 100'000'000.0F;\n",
    """    constexpr float WeightNewtons = 100'000'000.0F;\n    constexpr float SeaWaterDensity = 1025.0F;\n    constexpr float SurfaceWaterplaneHalfHeight = 4.35F;\n    const float reserve = AnteyMainBallastWaterCapacityKg / AnteyPublicSurfaceDisplacementMassKg;\n    const float surfacedFraction = AnteyPublicSurfaceDisplacementMassKg / AnteyPublicSubmergedDisplacementMassKg;\n    const float surfaceCenterDepth = SurfaceWaterplaneHalfHeight * (2.0F * surfacedFraction - 1.0F);\n    const float submergedTerminal = std::sqrt(\n        2.0F * AnteyGameplayPropulsion.maxForwardThrustNewtons / (SeaWaterDensity * 24.11986F));\n    const float surfacedTerminal = std::sqrt(\n        2.0F * AnteyGameplayPropulsion.maxForwardThrustNewtons / (SeaWaterDensity * 109.77216F));\n    if (std::abs(reserve - 0.32F) > 0.001F || std::abs(surfaceCenterDepth - 2.242268F) > 0.01F ||\n        std::abs(submergedTerminal - AnteyPublicMaximumSubmergedSpeedMetersPerSecond) > 0.02F ||\n        std::abs(surfacedTerminal - AnteyPublicMaximumSurfacedSpeedMetersPerSecond) > 0.02F)\n        return false;\n""",
)

# Documentation summary.
p = Path("docs/development/periscope-ballast-gameplay.md")
s = p.read_text(encoding="utf-8")
anchor = "## Scope A — low/zero-speed depth control\n"
if anchor not in s:
    raise RuntimeError("ballast doc scope heading missing")
insert = """## Physical ballast / hydrostatic authority\n\nDeep Run uses one Archimedean model across surfaced and submerged operation. The public-source gameplay canonical is 14,700 t surfaced and 19,400 t submerged; the 4,700 t difference is main-ballast water (~31.97% reserve buoyancy relative to surfaced displacement). Jolt rigid-body mass and inertia change with ballast fill. With main ballast empty, the calibrated waterplane settles the body reference at ~2.242 m below mean sea level in flat water; with main ballast full, 19,400 t is neutrally buoyant when fully submerged. There is no surfaced-mode buoyancy switch and no reserve-compensation vertical force.\n\nExact Project 949A flood/blow timing is not asserted from public data; the current 40 s full-range transition is explicit GAME POLICY. Small low-speed trim authority is represented as bounded equivalent water mass, not as a direct vertical force. Full-ahead propulsion and immersion-dependent quadratic drag are calibrated to the public 32 kn submerged / 15 kn surfaced canonical without hard velocity clamps. Public sources do not provide a trustworthy Project-949A-specific maximum vertical rate; generic open literature for nuclear submarines quotes roughly 6–9 m/s, so Deep Run does not label that range as an Antey-specific TTX.\n\n"""
p.write_text(s.replace(anchor, insert + anchor, 1), encoding="utf-8", newline="\n")

playground = Path("Game/PhysicalPlayground.cpp").read_text(encoding="utf-8")
for forbidden in ("surfacedHydrostaticMode_", "reserveTrimCompensationNewtons", "combinedBallastTrimForce", "M5SurfaceHydrostaticModeArmDepthMeters"):
    if forbidden in playground:
        raise RuntimeError(f"stale hydrostatic-force hack remains: {forbidden}")
