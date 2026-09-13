from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel: str, old: str, new: str) -> None:
    p = ROOT / rel
    s = p.read_text(encoding="utf-8")
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f"{rel}: expected one occurrence, found {n}: {old[:300]!r}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "Game/PhysicalPlayground.cpp",
    '''constexpr float M5MaximumReserveBuoyancyReleaseFractionOfWeight = 0.04F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
''',
    '''constexpr float M5MaximumReserveBuoyancyReleaseFractionOfWeight = 0.04F;
// Surface mode is armed only after a deliberate surfacing command reaches the near-surface band. This lets
// periscope-depth trim remain neutral around 10 m, while a continued surface command releases hydrostatic trim
// and lets reserve buoyancy find the natural ~3/4-submerged equilibrium.
constexpr float M5SurfaceHydrostaticModeArmDepthMeters = 7.0F;
constexpr float M2InitialBalanceRelativeTolerance = 1.0e-4F;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const float vesselWeightNewtons = M2GameAnteyMassTuningKg * *gravityMagnitude;
    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(
''',
    '''    const float vesselWeightNewtons = M2GameAnteyMassTuningKg * *gravityMagnitude;
    const auto bodyWaterSample = water_->Sample(state->position);
    if (!bodyWaterSample || !std::isfinite(bodyWaterSample->signedDepthMeters))
        return std::unexpected("physical playground body depth is unavailable for hydrostatic mode");
    bool nextSurfacedHydrostaticMode = surfacedHydrostaticMode_;
    if (command.depthCommandFraction > 0.05F)
        nextSurfacedHydrostaticMode = false;
    else if (command.depthCommandFraction < -0.05F &&
             bodyWaterSample->signedDepthMeters <= M5SurfaceHydrostaticModeArmDepthMeters)
        nextSurfacedHydrostaticMode = true;

    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
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
''',
    '''    // Submerged mode cancels reserve buoyancy above vessel weight, preserving neutral deep/periscope-depth
    // trim. A deliberate surface command first releases a bounded part of that compensation so the boat rises.
    // Once the near-surface threshold is crossed, surfaced mode latches and removes reserve compensation entirely;
    // the point-buoyancy model then loses displaced volume until natural hydrostatic equilibrium is reached.
    const float reserveExcessBuoyancyNewtons =
        (std::max)(0.0F, buoyancyResult->totalForceNewtons.y - vesselWeightNewtons);
    const float reserveTrimCompensationNewtons = nextSurfacedHydrostaticMode
        ? 0.0F
        : reserveExcessBuoyancyNewtons;
    const float reserveReleaseNewtons = nextSurfacedHydrostaticMode
        ? 0.0F
        : (std::max)(0.0F, -command.depthCommandFraction) *
              (std::min)(reserveExcessBuoyancyNewtons,
                         vesselWeightNewtons * M5MaximumReserveBuoyancyReleaseFractionOfWeight);
    Physics::PhysicsVector3 combinedBallastTrimForce = variableBallast->forceNewtons;
    combinedBallastTrimForce.y += -reserveTrimCompensationNewtons + reserveReleaseNewtons;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    facingState_ = facingAdvance->nextState;
    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;
''',
    '''    facingState_ = facingAdvance->nextState;
    surfacedHydrostaticMode_ = nextSurfacedHydrostaticMode;
    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''                ", reserve buoyancy excess " + std::to_string(reserveExcessBuoyancyNewtons) +
                " N, released reserve " + std::to_string(reserveReleaseNewtons) + " N, drag force " +
''',
    '''                ", reserve buoyancy excess " + std::to_string(reserveExcessBuoyancyNewtons) +
                " N, reserve trim " + std::to_string(reserveTrimCompensationNewtons) +
                " N, released reserve " + std::to_string(reserveReleaseNewtons) +
                " N, hydrostatic mode " + std::string(nextSurfacedHydrostaticMode ? "SURFACED" : "SUBMERGED_TRIM") +
                ", drag force " +
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    primaryPeriscopeRequestedRaised_ = false;
    primaryPeriscopeDeploymentProgress_ = 0.0F;
''',
    '''    primaryPeriscopeRequestedRaised_ = false;
    primaryPeriscopeDeploymentProgress_ = 0.0F;
    surfacedHydrostaticMode_ = false;
''')

replace_once(
    "Game/PhysicalPlayground.h",
    '''    bool primaryPeriscopeRequestedRaised_ = false;
    float primaryPeriscopeDeploymentProgress_ = 0.0F;

    // Bounded H2 diagnostics:
''',
    '''    bool primaryPeriscopeRequestedRaised_ = false;
    float primaryPeriscopeDeploymentProgress_ = 0.0F;
    bool surfacedHydrostaticMode_ = false;

    // Bounded H2 diagnostics:
''')

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''- releasing depth input does not instantly stop the boat; trim authority damps vertical rate through force over time;
''',
    '''- releasing depth input does not instantly stop the boat; trim authority damps vertical rate through force over time;
- deep and periscope-depth operation retains submerged trim compensation, but a deliberate continued surface command arms surfaced hydrostatic mode near 7 m centre depth; in that mode reserve compensation is released and the boat settles by displaced volume near the explicit ~75% submerged target;
- a dive command leaves surfaced mode immediately and restores submerged trim authority as displacement rises;
''')

print("surfaced hydrostatic mode correction applied")
