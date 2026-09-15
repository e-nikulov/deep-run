from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"pattern not found in {path}: {old[:120]!r}")
    if text.count(old) != 1:
        raise RuntimeError(f"pattern not unique in {path}: {text.count(old)} matches")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


header = ROOT / "Game" / "PhysicalPlayground.h"
replace_once(
    header,
    '#include "Game/Environment/VerticalOceanGameplayContract.h"\n',
    '#include "Game/Environment/VerticalOceanGameplayContract.h"\n#include "Game/Environment/WeatherSensorCoupling.h"\n',
)
replace_once(
    header,
    '#include "Simulation/Marine/BuoyancyComponent.h"\n',
    '#include "Simulation/Environment/WeatherSeaState.h"\n#include "Simulation/Marine/BuoyancyComponent.h"\n',
)
replace_once(
    header,
    '        if (physics_ == nullptr || !physicsBody_.IsValid() || !water_.has_value())\n        {\n            return std::unexpected("physical playground live acoustic authorities are unavailable");\n        }',
    '        if (physics_ == nullptr || !physicsBody_.IsValid() || !water_.has_value() || !weather_.has_value())\n        {\n            return std::unexpected("physical playground live acoustic/weather authorities are unavailable");\n        }',
)
replace_once(
    header,
    '        const auto snapshot = Submarine::BuildAnteyAcousticSnapshot(*runtimeState, ambientNoiseLevelDb);\n        if (!snapshot)\n        {\n            return std::unexpected("physical playground live acoustic snapshot failed: " + snapshot.error());\n        }',
    '        const auto weatherAmbient = EvaluateWeatherPassiveAmbientNoise(\n            *weather_, waterSample->signedDepthMeters, ambientNoiseLevelDb);\n        if (!weatherAmbient)\n        {\n            return std::unexpected("physical playground W1-E weather acoustic coupling failed: " +\n                                   weatherAmbient.error());\n        }\n        const auto snapshot = Submarine::BuildAnteyAcousticSnapshot(\n            *runtimeState, weatherAmbient->ambientNoiseLevelDb);\n        if (!snapshot)\n        {\n            return std::unexpected("physical playground live acoustic snapshot failed: " + snapshot.error());\n        }',
)
replace_once(
    header,
    '    std::optional<Marine::WaterBody> water_;\n',
    '    std::optional<Marine::WaterBody> water_;\n    // W1-E retains the exact environment authority that generated the production spectrum. Sensor adapters\n    // consume this value directly; they never reconstruct weather from render state or wave geometry.\n    std::optional<Environment::WeatherState> weather_;\n',
)

source = ROOT / "Game" / "PhysicalPlayground.cpp"
replace_once(
    source,
    '    water_ = *water;\n    buoyancy_ = std::move(buoyancy);',
    '    water_ = *water;\n    weather_ = *weather;\n    buoyancy_ = std::move(buoyancy);',
)

tests = ROOT / "Tests" / "M4EnvironmentChecks.h"
replace_once(
    tests,
    '#include "Tests/W1WeatherSeaStateChecks.h"\n',
    '#include "Tests/W1WeatherSeaStateChecks.h"\n#include "Tests/W1WeatherSensorCouplingChecks.h"\n',
)
replace_once(
    tests,
    '           RunW1SurfaceImpactChecks();',
    '           RunW1SurfaceImpactChecks() && RunW1WeatherSensorCouplingChecks();',
)

print("W1-E passive weather/runtime patch: PASS")
