"""Validate the explicit animation ownership boundary for Antey and P700 GLBs."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path


def read_glb(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 20:
        raise RuntimeError(f"GLB is too small: {path}")
    magic, version, total_length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total_length != len(data):
        raise RuntimeError(f"Invalid GLB header: {path}")
    chunk_length, chunk_type = struct.unpack_from("<II", data, 12)
    if chunk_type != 0x4E4F534A:
        raise RuntimeError(f"GLB first chunk is not JSON: {path}")
    return json.loads(data[20 : 20 + chunk_length].decode("utf-8"))


def animation_rows(document: dict) -> list[dict[str, object]]:
    nodes = document.get("nodes", [])
    rows = []
    for animation in document.get("animations", []):
        for channel in animation.get("channels", []):
            target = channel.get("target", {})
            node_index = target.get("node")
            node_name = (
                nodes[node_index].get("name", f"node_{node_index}")
                if node_index is not None and node_index < len(nodes)
                else f"node_{node_index}"
            )
            rows.append(
                {
                    "animation": animation.get("name", ""),
                    "channel_count": len(animation.get("channels", [])),
                    "target_node": node_name,
                    "target_path": target.get("path", ""),
                }
            )
    return rows


def expected_p700_targets() -> set[str]:
    return {
        f"SM_P700_LOD{lod}_{surface}"
        for lod in range(4)
        for surface in (
            "Wing_Port",
            "Wing_Starboard",
            "Tail_Dorsal",
            "Tail_Ventral",
            "Tail_Port",
            "Tail_Starboard",
        )
    }


def validate(antey_path: Path, p700_path: Path) -> dict[str, object]:
    antey = read_glb(antey_path)
    p700 = read_glb(p700_path)
    antey_rows = animation_rows(antey)
    p700_rows = animation_rows(p700)
    expected = expected_p700_targets()
    p700_deploy = [a for a in p700.get("animations", []) if a.get("name") == "P700_Deploy"]
    p700_targets = {row["target_node"] for row in p700_rows if row["animation"] == "P700_Deploy"}
    p700_paths = {row["target_path"] for row in p700_rows if row["animation"] == "P700_Deploy"}
    antey_cover_targets = sorted(
        row["target_node"]
        for row in antey_rows
        if str(row["target_node"]).startswith("SM_Antey_P700_Cover_")
    )
    errors = []
    if antey.get("animations"):
        errors.append("Antey.glb must contain zero animations")
    if antey_cover_targets:
        errors.append("Antey cover nodes must not be animation targets")
    if len(p700_deploy) != 1:
        errors.append("P700.glb must contain exactly one P700_Deploy animation")
    elif len(p700_deploy[0].get("channels", [])) != 24:
        errors.append("P700_Deploy must contain exactly 24 channels")
    if p700_paths != {"rotation"}:
        errors.append(f"P700_Deploy paths must be rotation only: {sorted(p700_paths)}")
    if p700_targets != expected:
        errors.append("P700_Deploy target set does not match the 24 P700 movable surfaces")
    if any(target.startswith("SM_Antey_P700_Cover_") for target in p700_targets):
        errors.append("P700_Deploy must not target Antey covers")
    return {
        "antey": {"animations": antey.get("animations", []), "rows": antey_rows},
        "p700": {"animations": p700.get("animations", []), "rows": p700_rows},
        "antey_cover_targets": antey_cover_targets,
        "p700_target_count": len(p700_targets),
        "errors": errors,
        "pass": not errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--antey", required=True, type=Path)
    parser.add_argument("--p700", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    options = parser.parse_args(values)
    report = validate(options.antey.resolve(), options.p700.resolve())
    if options.output:
        options.output.resolve().parent.mkdir(parents=True, exist_ok=True)
        options.output.resolve().write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
