from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys


PATH = Path("Game/PhysicalPlayground.cpp")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def canonical_cpp(source: str) -> str:
    """Drop comments/formatting while preserving literals and every executable token."""
    out: list[str] = []
    i = 0
    state = "normal"
    quote = ""
    while i < len(source):
        c = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "normal":
            if c == "/" and nxt == "/":
                state = "line_comment"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block_comment"
                i += 2
                continue
            if c in ('"', "'"):
                quote = c
                state = "literal"
                out.append(c)
                i += 1
                continue
            if c.isspace():
                i += 1
                continue
            out.append(c)
            i += 1
            continue
        if state == "line_comment":
            if c == "\n":
                state = "normal"
            i += 1
            continue
        if state == "block_comment":
            if c == "*" and nxt == "/":
                state = "normal"
                i += 2
            else:
                i += 1
            continue
        out.append(c)
        if c == "\\" and i + 1 < len(source):
            out.append(source[i + 1])
            i += 2
            continue
        if c == quote:
            state = "normal"
        i += 1
    return "".join(out)


def main() -> int:
    current = PATH.read_text(encoding="utf-8")
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

    current_semantic = canonical_cpp(current)
    target_semantic = canonical_cpp(target)
    if current_semantic != target_semantic:
        limit = min(len(current_semantic), len(target_semantic))
        mismatch = next(
            (index for index in range(limit) if current_semantic[index] != target_semantic[index]),
            limit,
        )
        lo = max(0, mismatch - 180)
        hi = mismatch + 260
        print(
            "Semantic mismatch: refusing to normalize PhysicalPlayground.cpp",
            file=sys.stderr,
        )
        print("CURRENT:", current_semantic[lo:hi], file=sys.stderr)
        print("TARGET :", target_semantic[lo:hi], file=sys.stderr)
        return 2

    PATH.write_text(target, encoding="utf-8", newline="\n")
    print("PhysicalPlayground.cpp semantic equivalence verified; normalized content written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
