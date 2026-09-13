from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel: str, old: str, new: str) -> None:
    p = ROOT / rel
    s = p.read_text(encoding="utf-8")
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f"{rel}: expected one occurrence, found {n}: {old[:400]!r}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "Game/PhysicalPlayground.cpp",
    '''// Production COB is spatial authority. This explicit Game-owned offset preserves the accepted M2 pitch
// stability behaviour; it is not a fabricated historical metacentric height.
constexpr float M2GameBuoyancyStabilityOffsetMeters = 2.0F;
constexpr float M2BuoyancySubmersionHalfHeightMeters = 6.0F;
// Public Project 949A references give roughly 32% reserve buoyancy. In this Game-owned point model that makes
// the untrimmed surface equilibrium about 1/1.32 = 75.8% submerged while submerged trim cancels the reserve.
constexpr float M5AnteyReserveBuoyancyFraction = 0.32F;
constexpr float M5MaximumReserveBuoyancyReleaseFractionOfWeight = 0.04F;
// Surface mode is armed only after a deliberate surfacing command reaches the near-surface band. This lets
// periscope-depth trim remain neutral around 10 m, while a continued surface command releases hydrostatic trim
// and lets reserve buoyancy find the natural ~3/4-submerged equilibrium.
constexpr float M5SurfaceHydrostaticModeArmDepthMeters = 7.0F;
''',
    '''// Surface-waterplane calibration is explicit Game policy. The buoyancy samples sit on the production COB
// vertical level instead of the old +2 m prototype offset: that old offset shifted the natural waterline down
// through the deployed bow planes even though it did not contribute useful pitch torque for vertical forces.
constexpr float M2GameBuoyancyStabilityOffsetMeters = 0.0F;
constexpr float M2BuoyancySubmersionHalfHeightMeters = 4.35F;
// Public Project 949A references give roughly 32% reserve buoyancy. In this bounded point model the untrimmed
// equilibrium submerged fraction is 1/1.32 = 75.8% of potential displacement. With the calibrated waterplane
// half-height this places the upright body origin about 2.24 m below mean sea level: roughly 3/4 of the main
// hull remains immersed while the inspected deployed bow-plane lower edge (~+2.35 m local Y) stays above it.
constexpr float M5AnteyReserveBuoyancyFraction = 0.32F;
constexpr float M5MaximumReserveBuoyancyReleaseFractionOfWeight = 0.04F;
constexpr float M5SurfaceEquilibriumSubmergedFraction = 1.0F / (1.0F + M5AnteyReserveBuoyancyFraction);
constexpr float M5SurfaceEquilibriumBodyCenterDepthMeters =
    M2GameBuoyancyStabilityOffsetMeters +
    M2BuoyancySubmersionHalfHeightMeters * (2.0F * M5SurfaceEquilibriumSubmergedFraction - 1.0F);
static_assert(M5SurfaceEquilibriumBodyCenterDepthMeters > 2.15F &&
              M5SurfaceEquilibriumBodyCenterDepthMeters < 2.30F);
// Surface mode arms only very near the natural flotation band. Periscope-depth operation therefore keeps
// submerged trim, while the final part of a deliberate surfacing manoeuvre hands authority to hydrostatics.
constexpr float M5SurfaceHydrostaticModeArmDepthMeters = 2.8F;
''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(
        M5LowSpeedBallastDepthControl,
        command.depthCommandFraction,
        controlResults[M2SternPlaneIndex].bodyForwardSpeedMetersPerSecond,
        state->linearVelocity.y,
        vesselWeightNewtons);
''',
    '''    // Once surfaced hydrostatics are latched, continuing to hold Surface must not add an artificial upward
    // ballast force on top of reserve buoyancy. Feed a neutral command so the controller may only damp residual
    // vertical rate; a Dive command unlatches the mode above and regains normal ballast authority immediately.
    const float ballastDepthCommandFraction =
        nextSurfacedHydrostaticMode && command.depthCommandFraction <= 0.0F
            ? 0.0F
            : command.depthCommandFraction;
    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(
        M5LowSpeedBallastDepthControl,
        ballastDepthCommandFraction,
        controlResults[M2SternPlaneIndex].bodyForwardSpeedMetersPerSecond,
        state->linearVelocity.y,
        vesselWeightNewtons);
''')

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''- deep and periscope-depth operation retains submerged trim compensation, but a deliberate continued surface command arms surfaced hydrostatic mode near 7 m centre depth; in that mode reserve compensation is released and the boat settles by displaced volume near the explicit ~75% submerged target;
''',
    '''- deep and periscope-depth operation retains submerged trim compensation, but a deliberate continued surface command arms surfaced hydrostatic mode only in the final near-surface band (~2.8 m body-centre depth); in that mode reserve compensation is released and the boat settles by displaced volume around 2.24 m body-centre depth, keeping roughly three quarters of the main hull immersed and the mean waterline below the deployed bow planes;
''')

print("surfaced waterline calibration applied")
