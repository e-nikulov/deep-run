from __future__ import annotations

from pathlib import Path
import re
import subprocess


PATH = Path("Game/PhysicalPlayground.cpp")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    current = PATH.read_text(encoding="utf-8")

    # The branch was created from this main and PhysicalPlayground's intended change in this slice is only the
    # variable-ballast integration. Refuse normalization unless every intended semantic marker is present first.
    required_current_markers = {
        "ballast include": '#include "Game/Submarine/VariableBallastDepthControl.h"',
        "ballast policy": "M5LowSpeedBallastDepthControl",
        "ballast evaluation": "Submarine::CalculateVariableBallastDepthControl(",
        "ballast force": "variableBallast->forceNewtons",
        "ballast authority telemetry": "variableBallast->lowSpeedAuthorityFraction",
        "ballast target speed telemetry": "variableBallast->targetVerticalSpeedMetersPerSecond",
    }
    for label, marker in required_current_markers.items():
        if marker not in current:
            raise RuntimeError(f"current PhysicalPlayground is missing intended {label}: {marker}")

    target = subprocess.check_output(
        ["git", "show", "origin/main:Game/PhysicalPlayground.cpp"],
        text=True,
        encoding="utf-8",
    )

    target = replace_once(
        target,
        '#include "Game/Submarine/ProductionAnteyLodPolicy.h"\n',
        '#include "Game/Submarine/ProductionAnteyLodPolicy.h"\n'
        '#include "Game/Submarine/VariableBallastDepthControl.h"\n',
        "ballast include",
    )
    target = replace_once(
        target,
        "constexpr float M2MaximumPlaneDeflection = 0.5F;\n",
        "constexpr float M2MaximumPlaneDeflection = 0.5F;\n"
        "// Low/zero-speed vertical authority is intentionally separate from hydrodynamic plane lift. This bounded\n"
        "// Game-owned controller approximates variable ballast / trim effects without modelling classified tank hardware.\n"
        "constexpr Submarine::VariableBallastDepthControlConfig M5LowSpeedBallastDepthControl{};\n",
        "ballast policy",
    )

    control_pattern = re.compile(
        r"(\n\s*controlResults\[index\]\s*=\s*\*control;\s*\n\s*\}\s*\n)"
    )
    control_matches = list(control_pattern.finditer(target))
    if len(control_matches) != 1:
        raise RuntimeError(
            f"control-surface anchor: expected one match, found {len(control_matches)}"
        )
    insert_at = control_matches[0].end()
    ballast_eval = """
    const float vesselWeightNewtons = M2GameAnteyMassTuningKg * *gravityMagnitude;
    const auto variableBallast = Submarine::CalculateVariableBallastDepthControl(
        M5LowSpeedBallastDepthControl,
        command.depthCommandFraction,
        controlResults[M2BowPlaneIndex].bodyForwardSpeedMetersPerSecond,
        state->linearVelocity.y,
        vesselWeightNewtons);
    if (!variableBallast)
    {
        return std::unexpected("physical playground variable-ballast evaluation failed: " + variableBallast.error());
    }
"""
    target = target[:insert_at] + ballast_eval + target[insert_at:]

    float_loop = (
        "    for (std::size_t index = 0; "
        "index < surfaceFloatBuoyancyResult_.points.size(); ++index)\n"
    )
    if target.count(float_loop) != 1:
        raise RuntimeError(
            f"surface-float anchor: expected one match, found {target.count(float_loop)}"
        )
    ballast_force = """    // Variable ballast/trim is a real bounded simulation force at COM. It intentionally produces translation
    // without inventing a pitch moment; bow/stern planes remain the separate pitch mechanism when flow exists.
    Physics::PhysicsError ballastForceError;
    if (!physics_->AddForceAtWorldPosition(
            physicsBody_, variableBallast->forceNewtons, state->position, &ballastForceError))
    {
        return std::unexpected(
            "physical playground variable-ballast force application failed: " + ballastForceError.message);
    }

"""
    target = target.replace(float_loop, ballast_force + float_loop, 1)

    log_pattern = re.compile(
        r'FormatVector\(buoyancyResult->totalForceNewtons\)\s*\+\s*", drag force "\s*\+'
    )
    if len(list(log_pattern.finditer(target))) != 1:
        raise RuntimeError("diagnostic anchor is not unique")
    log_replacement = """FormatVector(buoyancyResult->totalForceNewtons) + ", variable ballast authority " +
                std::to_string(variableBallast->lowSpeedAuthorityFraction) + ", ballast target V/S " +
                std::to_string(variableBallast->targetVerticalSpeedMetersPerSecond) +
                " m/s, ballast force " + FormatVector(variableBallast->forceNewtons) + ", drag force " +"""
    target = log_pattern.sub(log_replacement, target, count=1)

    for label, marker in required_current_markers.items():
        if marker not in target:
            raise RuntimeError(f"normalized PhysicalPlayground lost intended {label}: {marker}")

    PATH.write_text(target, encoding="utf-8", newline="\n")
    print("PhysicalPlayground.cpp rebuilt from main with the complete intended ballast semantic set")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
