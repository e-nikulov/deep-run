from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "Tests/TestMain.cpp",
    'source.find("M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter") != std::string::npos &&',
    'source.find("M5AnteySubmergedMassKg / water.Config().densityKgPerCubicMeter") != std::string::npos &&',
)

replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    """    // Canonical Antey handling contract: 24,000 t submerged gameplay mass, weaker astern drive, physical
    // shaft braking through zero, and a non-instantaneous 180-degree 2.5D facing transition.
    using namespace Game::Submarine;
    if (AnteyCanonicalFullSubmergedMassKg != 24'000'000.0F ||
        AnteyGameplayPropulsion.maxReverseRpm >= AnteyGameplayPropulsion.maxForwardRpm ||
        AnteyGameplayPropulsion.maxReverseThrustNewtons >= AnteyGameplayPropulsion.maxForwardThrustNewtons)
""",
    """    // Canonical Antey handling contract: 14,700 t surfaced + 4,700 t main-ballast water = 19,400 t
    // fully submerged gameplay mass, weaker astern drive, physical shaft braking through zero, and a
    // non-instantaneous 180-degree 2.5D facing transition.
    using namespace Game::Submarine;
    if (AnteyPublicSurfaceDisplacementMassKg != 14'700'000.0F ||
        AnteyCanonicalFullSubmergedMassKg != 19'400'000.0F ||
        AnteyMainBallastWaterCapacityKg != 4'700'000.0F ||
        std::abs(AnteyPublicReserveBuoyancyFraction - 0.3197279F) > 1.0e-4F ||
        AnteyGameplayPropulsion.maxReverseRpm >= AnteyGameplayPropulsion.maxForwardRpm ||
        AnteyGameplayPropulsion.maxReverseThrustNewtons >= AnteyGameplayPropulsion.maxForwardThrustNewtons)
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    "    const float vesselWeightNewtons = nextDynamicMassKg * *gravityMagnitude;\n",
    "",
)
