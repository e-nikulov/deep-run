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


# PhysicalPlayground consumes the pure ballast-state controller instead of mapping every Depth input to MBT flow.
replace_once(
    "Game/PhysicalPlayground.cpp",
    '#include "Game/Submarine/ProductionAnteyAsset.h"\n',
    '#include "Game/Submarine/AnteyBallastControl.h"\n#include "Game/Submarine/ProductionAnteyAsset.h"\n',
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """// Exact 949A flood/blow timing is not asserted from public data. Full empty<->full is a 40 s GAME POLICY.\nconstexpr float M5MainBallastFillRateFractionPerSecond = 0.025F;\nconstexpr float M5MainBallastBlowRateFractionPerSecond = 0.025F;\nconstexpr float M5MaximumTrimMassFractionOfSubmergedMass = 0.015F;\n""",
    """// Exact tank timing remains explicit GAME POLICY in the pure ballast-state controller. Main ballast is not\n// used for ordinary submerged depth changes; the controller only arms normal blowing in the final surface band.\nconstexpr Submarine::AnteyBallastControlConfig M5AnteyBallastControl{};\n""",
)

sub_once(
    "Game/PhysicalPlayground.cpp",
    r"    const float mainBallastRate = command\.depthCommandFraction >= 0\.0F\n.*?    const float vesselWeightNewtons = nextDynamicMassKg \* \*gravityMagnitude;\n",
    """    const auto bodyWaterSample = water_->Sample(state->position);\n    if (!bodyWaterSample || !std::isfinite(bodyWaterSample->signedDepthMeters))\n        return std::unexpected(\"physical playground body depth is unavailable for ballast control\");\n\n    const float currentBaseMassKg = M5AnteySurfaceMassKg +\n        M5AnteyMainBallastCapacityKg * ballastState_.mainBallastFillFraction;\n    const float currentWeightNewtons = currentBaseMassKg * *gravityMagnitude;\n    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(\n        M5LowSpeedBallastDepthControl, command.depthCommandFraction,\n        sternControl->bodyForwardSpeedMetersPerSecond, state->linearVelocity.y, currentWeightNewtons);\n    if (!variableBallast)\n        return std::unexpected(\"physical playground variable-ballast evaluation failed: \" + variableBallast.error());\n\n    // Convert the controller request to a desired equivalent trim-water mass. The pure state controller then\n    // applies finite actuator slew and decides whether main-ballast flooding/blowing is operationally allowed.\n    const float requestedTrimMassDeltaKg = -variableBallast->forceNewtons.y / *gravityMagnitude;\n    const auto nextBallastState = Submarine::AdvanceAnteyBallastState(\n        M5AnteyBallastControl, ballastState_, command.depthCommandFraction,\n        bodyWaterSample->signedDepthMeters, requestedTrimMassDeltaKg, fixedDeltaSeconds);\n    if (!nextBallastState)\n        return std::unexpected(\"physical playground ballast-state advance failed: \" + nextBallastState.error());\n    const float nextDynamicMassKg = Submarine::AnteyPhysicalMassKg(M5AnteyBallastControl, *nextBallastState);\n    const float vesselWeightNewtons = nextDynamicMassKg * *gravityMagnitude;\n""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    mainBallastFillFraction_ = nextMainBallastFillFraction;\n    committedDynamicMassKg_ = nextDynamicMassKg;\n    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;\n""",
    """    ballastState_ = *nextBallastState;\n    committedDynamicMassKg_ = nextDynamicMassKg;\n    committedForwardSpeedMetersPerSecond_ = sternControl->bodyForwardSpeedMetersPerSecond;\n    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    mainBallastFillFraction_ = 1.0F;\n    committedDynamicMassKg_ = M5AnteySubmergedMassKg;\n""",
    """    ballastState_ = {};\n    committedDynamicMassKg_ = M5AnteySubmergedMassKg;\n    committedForwardSpeedMetersPerSecond_ = 0.0F;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """                \" m/s, main ballast fill \" + std::to_string(nextMainBallastFillFraction) +\n                \", trim mass delta \" + std::to_string(trimMassDeltaKg) +\n""",
    """                \" m/s, main ballast fill \" + std::to_string(nextBallastState->mainBallastFillFraction) +\n                \", trim mass delta \" + std::to_string(nextBallastState->trimMassDeltaKg) +\n""",
)

# Telemetry exposes the actual physics numbers the user needs to verify in live play.
replace_once(
    "Game/PhysicalPlayground.h",
    '#include "Game/Submarine/AnteyAcousticRuntimeBridge.h"\n',
    '#include "Game/Submarine/AnteyAcousticRuntimeBridge.h"\n#include "Game/Submarine/AnteyBallastControl.h"\n',
)
replace_once(
    "Game/PhysicalPlayground.h",
    """    float verticalSpeedMetersPerSecond = 0.0F;\n    float throttleFraction = 0.0F;\n    bool bowPlanesDeployed = true;\n""",
    """    float verticalSpeedMetersPerSecond = 0.0F;\n    float forwardSpeedMetersPerSecond = 0.0F;\n    float throttleFraction = 0.0F;\n    float mainBallastFillFraction = 1.0F;\n    float trimMassDeltaKg = 0.0F;\n    float dynamicMassKg = 0.0F;\n    bool bowPlanesDeployed = true;\n""",
)
replace_once(
    "Game/PhysicalPlayground.h",
    """    float mainBallastFillFraction_ = 1.0F;\n    float committedDynamicMassKg_ = 0.0F;\n""",
    """    Submarine::AnteyBallastState ballastState_{};\n    float committedDynamicMassKg_ = 0.0F;\n    float committedForwardSpeedMetersPerSecond_ = 0.0F;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """        .verticalSpeedMetersPerSecond = state->linearVelocity.y,\n        .throttleFraction = committedThrottleFraction_,\n        .bowPlanesDeployed = true,\n""",
    """        .verticalSpeedMetersPerSecond = state->linearVelocity.y,\n        .forwardSpeedMetersPerSecond = committedForwardSpeedMetersPerSecond_,\n        .throttleFraction = committedThrottleFraction_,\n        .mainBallastFillFraction = ballastState_.mainBallastFillFraction,\n        .trimMassDeltaKg = ballastState_.trimMassDeltaKg,\n        .dynamicMassKg = committedDynamicMassKg_,\n        .bowPlanesDeployed = true,\n""",
)

# NAV presentation bridge.
replace_once(
    "Game/Combat/CombatCommandUi.h",
    """    float verticalSpeedMetersPerSecond = 0.0F;\n    float throttleFraction = 0.0F;\n    bool bowPlanesDeployed = true;\n""",
    """    float verticalSpeedMetersPerSecond = 0.0F;\n    float forwardSpeedMetersPerSecond = 0.0F;\n    float throttleFraction = 0.0F;\n    float mainBallastFillFraction = 1.0F;\n    float trimMassDeltaKg = 0.0F;\n    float dynamicMassKg = 0.0F;\n    bool bowPlanesDeployed = true;\n""",
)
replace_once(
    "DeepRun/Main.cpp",
    """                            .signedDepthMeters = navigationTelemetry->signedDepthMeters,\n                            .verticalSpeedMetersPerSecond = navigationTelemetry->verticalSpeedMetersPerSecond,\n                            .throttleFraction = navigationTelemetry->throttleFraction,\n                            .bowPlanesDeployed = navigationTelemetry->bowPlanesDeployed,\n""",
    """                            .signedDepthMeters = navigationTelemetry->signedDepthMeters,\n                            .verticalSpeedMetersPerSecond = navigationTelemetry->verticalSpeedMetersPerSecond,\n                            .forwardSpeedMetersPerSecond = navigationTelemetry->forwardSpeedMetersPerSecond,\n                            .throttleFraction = navigationTelemetry->throttleFraction,\n                            .mainBallastFillFraction = navigationTelemetry->mainBallastFillFraction,\n                            .trimMassDeltaKg = navigationTelemetry->trimMassDeltaKg,\n                            .dynamicMassKg = navigationTelemetry->dynamicMassKg,\n                            .bowPlanesDeployed = navigationTelemetry->bowPlanesDeployed,\n""",
)
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    """    ImGui::Text(\"Current depth: %.1f m\", currentDepthMeters);\n    ImGui::Text(\"Vertical speed: %+0.2f m/s (UP+)\", snapshot.verticalSpeedMetersPerSecond);\n    ImGui::Text(\"Buoyancy / trim: %s\", BuoyancyTrimStateName(snapshot));\n    ImGui::Text(\"Throttle: %+0.0f%%\", snapshot.throttleFraction * 100.0F);\n""",
    """    ImGui::Text(\"Current depth: %.1f m\", currentDepthMeters);\n    ImGui::Text(\"Vertical speed: %+0.2f m/s (UP+)\", snapshot.verticalSpeedMetersPerSecond);\n    ImGui::Text(\"Axial speed: %+0.1f kn / %+0.2f m/s\",\n                snapshot.forwardSpeedMetersPerSecond * 1.94384449F,\n                snapshot.forwardSpeedMetersPerSecond);\n    ImGui::Text(\"Buoyancy / trim: %s\", BuoyancyTrimStateName(snapshot));\n    ImGui::Text(\"Main ballast: %.0f%% | Trim: %+0.1f t\",\n                snapshot.mainBallastFillFraction * 100.0F, snapshot.trimMassDeltaKg / 1000.0F);\n    ImGui::Text(\"Physical mass: %.0f t\", snapshot.dynamicMassKg / 1000.0F);\n    ImGui::Text(\"Throttle: %+0.0f%%\", snapshot.throttleFraction * 100.0F);\n""",
)

# Pure ballast-state regression checks: deep surfacing must not blow MBT, near-surface surfacing must,
# and a dive command must flood any partly empty main ballast. Trim mass must slew rather than jump.
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    '#include "Game/Submarine/AnteyHandlingModel.h"\n',
    '#include "Game/Submarine/AnteyBallastControl.h"\n#include "Game/Submarine/AnteyHandlingModel.h"\n',
)
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    """    const VariableBallastDepthControlConfig ballast{};\n""",
    """    const AnteyBallastControlConfig ballastStateConfig{};\n    const AnteyBallastState fullMainBallast{};\n    const auto deepSurfaceBallast = AdvanceAnteyBallastState(\n        ballastStateConfig, fullMainBallast, -1.0F, 100.0F, -100'000.0F, 1.0F);\n    const auto nearSurfaceBallast = AdvanceAnteyBallastState(\n        ballastStateConfig, fullMainBallast, -1.0F, 2.5F, -100'000.0F, 1.0F);\n    const AnteyBallastState partlyBlown{.mainBallastFillFraction = 0.5F, .trimMassDeltaKg = 0.0F};\n    const auto diveFromSurfaceBallast = AdvanceAnteyBallastState(\n        ballastStateConfig, partlyBlown, 1.0F, 2.0F, 100'000.0F, 1.0F);\n    if (!deepSurfaceBallast || !nearSurfaceBallast || !diveFromSurfaceBallast ||\n        std::abs(deepSurfaceBallast->mainBallastFillFraction - 1.0F) > 1.0e-6F ||\n        !(nearSurfaceBallast->mainBallastFillFraction < 1.0F) ||\n        !(diveFromSurfaceBallast->mainBallastFillFraction > partlyBlown.mainBallastFillFraction) ||\n        !(deepSurfaceBallast->trimMassDeltaKg < 0.0F) ||\n        std::abs(deepSurfaceBallast->trimMassDeltaKg) >= 100'000.0F)\n        return false;\n\n    const VariableBallastDepthControlConfig ballast{};\n""",
)

# Docs: distinguish ordinary submerged trim/planes from main-ballast surface transition.
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """Deep Run uses one Archimedean model across surfaced and submerged operation. The public-source gameplay canonical is 14,700 t surfaced and 19,400 t submerged; the 4,700 t difference is main-ballast water (~31.97% reserve buoyancy relative to surfaced displacement). Jolt rigid-body mass and inertia change with ballast fill. With main ballast empty, the calibrated waterplane settles the body reference at ~2.242 m below mean sea level in flat water; with main ballast full, 19,400 t is neutrally buoyant when fully submerged. There is no surfaced-mode buoyancy switch and no reserve-compensation vertical force.\n""",
    """Deep Run uses one Archimedean model across surfaced and submerged operation. The public-source gameplay canonical is 14,700 t surfaced and 19,400 t submerged; the 4,700 t difference is main-ballast water (~31.97% reserve buoyancy relative to surfaced displacement). Jolt rigid-body mass and inertia change with ballast fill. With main ballast empty, the calibrated waterplane settles the body reference at ~2.242 m below mean sea level in flat water; with main ballast full, 19,400 t is neutrally buoyant when fully submerged. There is no surfaced-mode buoyancy switch and no reserve-compensation vertical force. In ordinary submerged manoeuvring the main ballast remains flooded: quiet depth changes use bounded trim-water mass at low speed and the stern planes at hydrodynamic speed. Normal main-ballast blowing is armed only in the final near-surface band, while a dive command floods any partly empty main ballast.\n""",
)
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """- surface intent -> reduce ballast/trim water and create positive buoyancy;\n- dive intent -> increase ballast/trim water and create negative buoyancy until neutral/submerged mass is reached;\n- neutral depth input -> trim target returns toward neutral and residual vertical motion is arrested by physical drag plus bounded trim-mass correction;\n""",
    """- submerged surface intent -> reduce bounded trim-water mass; sustained intent in the final surface band may then blow main ballast;\n- submerged dive intent -> increase bounded trim-water mass while the filled main ballast stays filled; from a surfaced/partly blown state the same intent floods main ballast;\n- neutral depth input -> trim target returns toward neutral and residual vertical motion is arrested by physical drag plus finite trim-mass correction;\n""",
)
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """- main-ballast/trim commands change physical rigid-body mass;\n""",
    """- main-ballast/trim commands change physical rigid-body mass with finite GAME-policy rates;\n- ordinary deep/periscope-depth commands do not blow the main ballast tanks;\n""",
)
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """The normal NAV HUD exposes the committed simulation state in player-readable terms:\n\n- `INCREASING BUOYANCY`;\n""",
    """The normal NAV HUD exposes the committed simulation state in player-readable terms, including actual axial speed, main-ballast fill percentage, signed trim-water mass and total physical mass, plus the semantic trim state:\n\n- `INCREASING BUOYANCY`;\n""",
)

# Guard the intended operational semantics.
text = Path("Game/PhysicalPlayground.cpp").read_text(encoding="utf-8")
for token in ("M5MainBallastFillRateFractionPerSecond", "M5MainBallastBlowRateFractionPerSecond", "mainBallastFillFraction_"):
    if token in text:
        raise RuntimeError(f"stale direct main-ballast implementation remains: {token}")
