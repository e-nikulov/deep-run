from pathlib import Path

root = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

cmake = root / "CMakeLists.txt"
replace_once(
    cmake,
    """    Simulation/Marine/HydroDragSystem.cpp\n    Simulation/Marine/PropulsionSystem.cpp\n    Simulation/Marine/WaterBody.cpp\n""",
    """    Simulation/Marine/HydroDragSystem.cpp\n    Simulation/Marine/PropulsionSystem.cpp\n    Simulation/Marine/SurfaceImpactSystem.cpp\n    Simulation/Marine/WaterBody.cpp\n""",
)

environment = root / "Tests" / "M4EnvironmentChecks.h"
replace_once(
    environment,
    '#include "Tests/W1SpectralOceanChecks.h"\n#include "Tests/W1SurfaceVesselDynamicsChecks.h"\n',
    '#include "Tests/W1SpectralOceanChecks.h"\n#include "Tests/W1SurfaceImpactChecks.h"\n#include "Tests/W1SurfaceVesselDynamicsChecks.h"\n',
)
replace_once(
    environment,
    """    return !invalidResult && invalidResult.error().code == AcousticErrorCode::InvalidPropagationModifiers &&\n           RunW1WeatherSeaStateChecks() && RunW1SpectralOceanChecks() && RunW1SurfaceVesselDynamicsChecks();\n""",
    """    return !invalidResult && invalidResult.error().code == AcousticErrorCode::InvalidPropagationModifiers &&\n           RunW1WeatherSeaStateChecks() && RunW1SpectralOceanChecks() && RunW1SurfaceVesselDynamicsChecks() &&\n           RunW1SurfaceImpactChecks();\n""",
)

print("W1-D core compile/test gate patch: PASS")
