from pathlib import Path

root = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

header = root / "Game" / "PhysicalPlayground.h"
replace_once(
    header,
    '#include "Simulation/Marine/PropulsionSystem.h"\n#include "Simulation/Marine/WaterBody.h"\n',
    '#include "Simulation/Marine/PropulsionSystem.h"\n#include "Simulation/Marine/SurfaceImpactSystem.h"\n#include "Simulation/Marine/WaterBody.h"\n',
)
replace_once(
    header,
    """    Marine::BuoyancyResult surfaceVesselBuoyancyResult_;\n    Marine::BuoyancyResult surfaceFloatBuoyancyResult_;\n""",
    """    Marine::BuoyancyResult surfaceVesselBuoyancyResult_;\n    // W1-D detector state is double-buffered so a failed fixed tick can never partially commit impact history.\n    std::vector<Marine::SurfaceImpactPointState> surfaceImpactPointStates_;\n    std::vector<Marine::SurfaceImpactPointState> pendingSurfaceImpactPointStates_;\n    Marine::BuoyancyResult surfaceFloatBuoyancyResult_;\n""",
)

cpp = root / "Game" / "PhysicalPlayground.cpp"
replace_once(
    cpp,
    """constexpr float M5SurfaceEquilibriumBodyCenterDepthMeters =\n    M2BuoyancySubmersionHalfHeightMeters * (2.0F * M5SurfaceEquilibriumSubmergedFraction - 1.0F);\nstatic_assert(M5SurfaceEquilibriumBodyCenterDepthMeters > 2.3463F &&\n              M5SurfaceEquilibriumBodyCenterDepthMeters < 2.3467F);\n""",
    """constexpr float M5SurfaceEquilibriumBodyCenterDepthMeters =\n    M2BuoyancySubmersionHalfHeightMeters * (2.0F * M5SurfaceEquilibriumSubmergedFraction - 1.0F);\nstatic_assert(M5SurfaceEquilibriumBodyCenterDepthMeters > 2.3463F &&\n              M5SurfaceEquilibriumBodyCenterDepthMeters < 2.3467F);\n// W1-D GAME POLICY: dimensional wetting speed and q=1/2*rho*v^2 remain in Marine evidence; only the\n// hysteresis and presentation-severity endpoints are authored here. No structural damage threshold is implied.\nconstexpr Marine::SurfaceImpactConfig W1DSurfaceImpactConfig{\n    .rearmSubmergedFraction = 0.55F,\n    .triggerSubmergedFraction = 0.65F,\n    .minimumRelativeWettingSpeedMetersPerSecond = 1.0F,\n    .severeRelativeWettingSpeedMetersPerSecond = 6.0F};\n""",
)
replace_once(
    cpp,
    """    buoyancy_ = std::move(buoyancy);\n    surfaceVesselBuoyancyResult_.points.reserve(buoyancy_.points.size());\n    surfaceFloatBuoyancy_ = surfaceFloatBuoyancy;\n""",
    """    buoyancy_ = std::move(buoyancy);\n    surfaceVesselBuoyancyResult_.points.reserve(buoyancy_.points.size());\n    surfaceImpactPointStates_.assign(buoyancy_.points.size(), {});\n    pendingSurfaceImpactPointStates_.assign(buoyancy_.points.size(), {});\n    surfaceFloatBuoyancy_ = surfaceFloatBuoyancy;\n""",
)
replace_once(
    cpp,
    """    const Marine::BuoyancyResult* buoyancyResult = &surfaceVesselBuoyancyResult_;\n\n    // M3-F retains its accepted small-float surface-normal response independently from W1-C vessel hydrostatics.\n""",
    """    const Marine::BuoyancyResult* buoyancyResult = &surfaceVesselBuoyancyResult_;\n\n    // W1-D derives impact exposure from the SAME local free-surface samples already used by W1-C. The detector\n    // neither applies another force nor owns damage. Its signed-depth derivative naturally includes hull heave,\n    // pitch/forward traversal and wave motion. State remains pending until the fixed-tick transaction commits.\n    if (surfaceImpactPointStates_.size() != buoyancyResult->points.size() ||\n        pendingSurfaceImpactPointStates_.size() != buoyancyResult->points.size())\n    {\n        return std::unexpected(\"physical playground W1-D impact-state cardinality mismatch\");\n    }\n    std::optional<Marine::SurfaceImpactEvent> strongestSurfaceImpact;\n    for (std::size_t index = 0U; index < buoyancyResult->points.size(); ++index)\n    {\n        const Marine::BuoyancyPointResult& point = buoyancyResult->points[index];\n        const Marine::SurfaceImpactPointSample sample{\n            .worldXMeters = point.worldPositionMeters.x,\n            .worldYMeters = point.worldPositionMeters.y,\n            .worldZMeters = point.worldPositionMeters.z,\n            .signedDepthMeters = point.signedDepthMeters,\n            .submergedFraction = point.submergedFraction};\n        const auto impactAdvance = Marine::SurfaceImpactSystem::AdvancePoint(\n            W1DSurfaceImpactConfig, water_->Config().densityKgPerCubicMeter, index, sample,\n            surfaceImpactPointStates_[index], fixedDeltaSeconds);\n        if (!impactAdvance)\n        {\n            return std::unexpected(\"physical playground W1-D surface-impact evaluation failed at point \" +\n                                   std::to_string(index) + \": \" + impactAdvance.error().message);\n        }\n        pendingSurfaceImpactPointStates_[index] = impactAdvance->nextState;\n        if (impactAdvance->event &&\n            (!strongestSurfaceImpact || impactAdvance->event->severity > strongestSurfaceImpact->severity))\n        {\n            strongestSurfaceImpact = *impactAdvance->event;\n        }\n    }\n\n    // M3-F retains its accepted small-float surface-normal response independently from W1-C vessel hydrostatics.\n""",
)
replace_once(
    cpp,
    """    // Transaction boundary: state advances only after every calculation and force/torque application succeeds.\n    propulsionState_ = propulsionResult->nextState;\n""",
    """    // Transaction boundary: state advances only after every calculation and force/torque application succeeds.\n    surfaceImpactPointStates_.swap(pendingSurfaceImpactPointStates_);\n    propulsionState_ = propulsionResult->nextState;\n""",
)
replace_once(
    cpp,
    """    // I2 presentation producer: derive semantic intensity only from the newly committed authoritative shaft\n    // RPM. A malformed impossible state is validated and diagnosed once, but haptic presentation can never\n    // roll back or fail the already-successful simulation transaction.\n    float engineVibrationIntensity = 0.0F;\n    const HapticFeedbackSystem hapticFeedback;\n""",
    """    // W1-D presentation bridge: only a committed impact may produce controller feedback. The Marine event keeps\n    // dimensional load evidence; Game exposes only its bounded severity to the semantic haptic layer. A callback\n    // failure is presentation-only and can never roll back the already-successful simulation transaction.\n    if (strongestSurfaceImpact && strongestSurfaceImpact->severity > 0.0F && hapticEventSink)\n    {\n        try\n        {\n            hapticEventSink(HapticEvent{\n                .type = HapticEventType::WaveSlam,\n                .intensity = strongestSurfaceImpact->severity});\n        }\n        catch (const std::exception& exception)\n        {\n            if (!loggedHapticFailure_)\n            {\n                loggedHapticFailure_ = true;\n                PlaygroundLog().Warning(\n                    Diagnostics::LogCategory::Input,\n                    \"Wave-slam haptic callback failed and was suppressed: \" + std::string(exception.what()));\n            }\n        }\n        catch (...)\n        {\n            if (!loggedHapticFailure_)\n            {\n                loggedHapticFailure_ = true;\n                PlaygroundLog().Warning(\n                    Diagnostics::LogCategory::Input,\n                    \"Wave-slam haptic callback failed and was suppressed\");\n            }\n        }\n    }\n\n    // I2 presentation producer: derive semantic intensity only from the newly committed authoritative shaft\n    // RPM. A malformed impossible state is validated and diagnosed once, but haptic presentation can never\n    // roll back or fail the already-successful simulation transaction.\n    float engineVibrationIntensity = 0.0F;\n    const HapticFeedbackSystem hapticFeedback;\n""",
)

print("W1-D production runtime integration patch: PASS")
