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


# Physically bounded vertical-plane response: static hydrodynamic pitch stability balances stern-plane moment.
replace_once(
    "Game/PhysicalPlayground.cpp",
    """constexpr float M5SubmergedLongitudinalEffectiveAreaSquareMeters = 24.11986F;\nconstexpr float M5SurfacedLongitudinalEffectiveAreaSquareMeters = 109.77216F;\n""",
    """constexpr float M5SubmergedLongitudinalEffectiveAreaSquareMeters = 24.11986F;\nconstexpr float M5SurfacedLongitudinalEffectiveAreaSquareMeters = 109.77216F;\n// No reliable public 949A-specific maximum vertical rate was found. GAME POLICY therefore chooses a 25 degree\n// full-command steady trajectory. At the public 32 kn maximum submerged speed this is 6.96 m/s vertical,\n// consistent with the generic 6-9 m/s open-literature envelope for nuclear submarines without claiming it as TTX.\nconstexpr float M5MaximumHydrodynamicTrajectoryAngleRadians = 0.436332313F; // 25 degrees\nconstexpr float M5PitchStaticStabilityEffectiveMomentMeters3 =\n    40.0F * 0.5F * 32.0F / M5MaximumHydrodynamicTrajectoryAngleRadians;\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    if (!sternControl)\n    {\n        return std::unexpected(\n            \"physical playground stern control-surface calculation failed: \" + sternControl.error().message);\n    }\n    const float mainBallastRate = command.depthCommandFraction >= 0.0F\n""",
    """    if (!sternControl)\n    {\n        return std::unexpected(\n            \"physical playground stern control-surface calculation failed: \" + sternControl.error().message);\n    }\n    constexpr float TwoPi = 6.28318530717958647692F;\n    const float pitchRadians = std::remainder(\n        2.0F * std::atan2(state->orientation.z, state->orientation.w), TwoPi);\n    const float forwardSpeedSquared =\n        sternControl->bodyForwardSpeedMetersPerSecond * sternControl->bodyForwardSpeedMetersPerSecond;\n    const float pitchRestoringTorqueNewtonMeters =\n        -0.5F * water_->Config().densityKgPerCubicMeter * M5PitchStaticStabilityEffectiveMomentMeters3 *\n        forwardSpeedSquared * pitchRadians;\n    if (!std::isfinite(pitchRestoringTorqueNewtonMeters))\n        return std::unexpected(\"physical playground pitch restoring torque is non-finite\");\n\n    const float mainBallastRate = command.depthCommandFraction >= 0.0F\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """    Physics::PhysicsError propulsionForceError;\n    if (!physics_->AddForceAtWorldPosition(\n            physicsBody_, *propulsionForceWorld, *propulsorWorldPosition, &propulsionForceError))\n""",
    """    Physics::PhysicsError pitchStabilityError;\n    if (!physics_->AddTorque(\n            physicsBody_, {0.0F, 0.0F, pitchRestoringTorqueNewtonMeters}, &pitchStabilityError))\n    {\n        return std::unexpected(\n            \"physical playground pitch stability torque application failed: \" + pitchStabilityError.message);\n    }\n\n    Physics::PhysicsError propulsionForceError;\n    if (!physics_->AddForceAtWorldPosition(\n            physicsBody_, *propulsionForceWorld, *propulsorWorldPosition, &propulsionForceError))\n""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """                \", stern force \" + FormatVector(sternControl->forceNewtons) +\n                \", stern point \" + FormatVector(sternControl->worldPositionMeters) +\n                \", gravity magnitude \" +\n""",
    """                \", stern force \" + FormatVector(sternControl->forceNewtons) +\n                \", stern point \" + FormatVector(sternControl->worldPositionMeters) +\n                \", pitch restoring torque \" + std::to_string(pitchRestoringTorqueNewtonMeters) +\n                \" Nm, gravity magnitude \" +\n""",
)

# Regression checks for the two independently derived ~7 m/s vertical extrema.
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    """    if (std::abs(reserve - 0.32F) > 0.001F || std::abs(surfaceCenterDepth - 2.242268F) > 0.01F ||\n        std::abs(submergedTerminal - AnteyPublicMaximumSubmergedSpeedMetersPerSecond) > 0.02F ||\n        std::abs(surfacedTerminal - AnteyPublicMaximumSurfacedSpeedMetersPerSecond) > 0.02F)\n        return false;\n""",
    """    constexpr float MaximumTrajectoryAngleRadians = 0.436332313F;\n    const float maximumHydrodynamicVertical =\n        AnteyPublicMaximumSubmergedSpeedMetersPerSecond * std::sin(MaximumTrajectoryAngleRadians);\n    const float maximumPositiveBuoyancyNewtons =\n        AnteyMainBallastWaterCapacityKg * 9.81F;\n    const float maximumBallastOnlyVertical = std::sqrt(\n        2.0F * maximumPositiveBuoyancyNewtons / (SeaWaterDensity * 1800.0F));\n    if (std::abs(reserve - 0.32F) > 0.001F || std::abs(surfaceCenterDepth - 2.242268F) > 0.01F ||\n        std::abs(submergedTerminal - AnteyPublicMaximumSubmergedSpeedMetersPerSecond) > 0.02F ||\n        std::abs(surfacedTerminal - AnteyPublicMaximumSurfacedSpeedMetersPerSecond) > 0.02F ||\n        std::abs(maximumHydrodynamicVertical - 6.9572F) > 0.03F ||\n        std::abs(maximumBallastOnlyVertical - 7.0697F) > 0.03F)\n        return false;\n""",
)

# Replace stale force/surfaced-mode prose with the physical mass-domain contract.
sub_once(
    "docs/development/periscope-ballast-gameplay.md",
    r"At low forward speed a separate Game-owned variable-ballast/trim controller supplies bounded net vertical force at the vessel centre of mass:\n\n.*?- no presentation-only state may change depth\.\n",
    """At low forward speed a Game-owned trim controller requests a bounded **equivalent water-mass change** rather than applying a vertical force. At higher speed that trim authority fades and the stern horizontal planes carry the manoeuvre. Main-ballast fill is physical mass state: empty corresponds to the public surfaced displacement and full corresponds to the public submerged displacement.\n\n- surface intent -> reduce ballast/trim water and create positive buoyancy;\n- dive intent -> increase ballast/trim water and create negative buoyancy until neutral/submerged mass is reached;\n- neutral depth input -> trim target returns toward neutral and residual vertical motion is arrested by physical drag plus bounded trim-mass correction;\n- low-speed trim authority fades continuously as forward speed rises;\n- at hydrodynamic speed the stern planes and the hull's static pitch stability determine the vertical trajectory.\n\nThis is a gameplay-level physical ballast model. It intentionally **does not** claim Project 949A tank volumes, valve sequencing, pump/blow rates or emergency-blow timing that are not established by public data.\n\n### Required invariants\n\n- no teleporting or direct modification of world Y;\n- no fake lift from a control surface at zero water flow;\n- no direct ballast/reserve-compensation vertical force;\n- Jolt remains motion, mass and inertia authority;\n- WaterBody remains depth and displaced-water authority;\n- main-ballast/trim commands change physical rigid-body mass;\n- buoyancy is produced only by displaced water through `BuoyancySystem`;\n- with full main ballast the fully immersed 19,400 t state is neutrally buoyant;\n- with empty main ballast the 14,700 t state settles naturally at ~2.242 m body-reference depth in flat water;\n- stern-plane moment is opposed by speed-squared static pitch stability, so held input converges to a trajectory instead of allowing endless pitch rotation;\n- no presentation-only state may change depth.\n""",
)
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """Exact Project 949A flood/blow timing is not asserted from public data; the current 40 s full-range transition is explicit GAME POLICY. Small low-speed trim authority is represented as bounded equivalent water mass, not as a direct vertical force. Full-ahead propulsion and immersion-dependent quadratic drag are calibrated to the public 32 kn submerged / 15 kn surfaced canonical without hard velocity clamps. Public sources do not provide a trustworthy Project-949A-specific maximum vertical rate; generic open literature for nuclear submarines quotes roughly 6–9 m/s, so Deep Run does not label that range as an Antey-specific TTX.\n""",
    """Exact Project 949A flood/blow timing is not asserted from public data; the current 40 s full-range transition is explicit GAME POLICY. Small low-speed trim authority is represented as bounded equivalent water mass, not as a direct vertical force. Full-ahead propulsion and immersion-dependent quadratic drag are calibrated to the public 32 kn submerged / 15 kn surfaced canonical without hard velocity clamps. Public sources do not provide a trustworthy Project-949A-specific maximum vertical rate; generic open literature for nuclear submarines quotes roughly 6–9 m/s, so Deep Run does not label that range as an Antey-specific TTX. The current model independently produces ~7.07 m/s as the still-water terminal rise under maximum positive buoyancy and ~6.96 m/s as the 32 kn / 25-degree full-command hydrodynamic trajectory.\n""",
)

# Refresh nearby source comment so the retained controller cannot be mistaken for a force authority.
replace_once(
    "Game/PhysicalPlayground.cpp",
    """// Low/zero-speed vertical authority is intentionally separate from hydrodynamic plane lift. This bounded\n// Game-owned controller approximates variable ballast / trim effects without modelling classified tank hardware.\nconstexpr Submarine::VariableBallastDepthControlConfig M5LowSpeedBallastDepthControl{};\n""",
    """// Low/zero-speed depth authority is separate from hydrodynamic plane lift. This bounded controller computes\n// a trim request which Game converts to equivalent ballast-water mass; its force value is never applied to Jolt.\nconstexpr Submarine::VariableBallastDepthControlConfig M5LowSpeedBallastDepthControl{};\n""",
)

for path, tokens in {
    "docs/development/periscope-ballast-gameplay.md": ("ballast output is a real bounded force", "surfaced hydrostatic mode"),
    "Game/PhysicalPlayground.cpp": ("surfacedHydrostaticMode_", "combinedBallastTrimForce", "reserveTrimCompensationNewtons"),
}.items():
    text = Path(path).read_text(encoding="utf-8")
    for token in tokens:
        if token in text:
            raise RuntimeError(f"stale physical model token remains in {path}: {token}")
