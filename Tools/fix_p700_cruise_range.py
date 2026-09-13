from pathlib import Path

root = Path(__file__).resolve().parents[1]

p700_path = root / "Simulation/Weapons/P700Granit.h"
p700 = p700_path.read_text(encoding="utf-8")
old = """        const auto impact = moveSegment(*direction, speed, stepSeconds);\n        if (!impact) return std::unexpected(impact.error());\n        if (*impact) return *impact;\n        cursorTime += stepSeconds;\n"""
new = """        const auto impact = moveSegment(*direction, speed, stepSeconds);\n        if (!impact) return std::unexpected(impact.error());\n        if (*impact) return *impact;\n        if (state.phase == P700GranitPhase::Spent) return std::optional<P700GranitImpact>{};\n        cursorTime += stepSeconds;\n"""
if p700.count(old) != 1:
    raise RuntimeError(f"expected exactly one cruise movement block, found {p700.count(old)}")
p700_path.write_text(p700.replace(old, new, 1), encoding="utf-8", newline="\n")

test_path = root / "Tests/M5P700Checks.h"
test = test_path.read_text(encoding="utf-8")
old_budget = "shortRangeDefinition.maximumTravelDistanceMeters = 500.0F;"
new_budget = "shortRangeDefinition.maximumTravelDistanceMeters = 2'000.0F;"
old_expect = "shortRangeRuntime->travelledDistanceMeters - 500.0F"
new_expect = "shortRangeRuntime->travelledDistanceMeters - 2'000.0F"
if test.count(old_budget) != 1 or test.count(old_expect) != 1:
    raise RuntimeError("short-range P-700 regression fixture did not match expected source")
test = test.replace(old_budget, new_budget, 1).replace(old_expect, new_expect, 1)
test_path.write_text(test, encoding="utf-8", newline="\n")

print("P-700 cruise range edge patched")
