#!/usr/bin/env python3
"""Validate the bounded IG1-D production Antey LOD family and selection policy.

This validator intentionally distinguishes source-family metadata from runtime availability.
LOD0 is the only currently staged render file; LOD1-LOD3 metadata must remain valid without
fabricating absent GLBs. Physics/gameplay are outside this validator and must remain LOD-independent.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


EXPECTED_LODS = ("LOD0", "LOD1", "LOD2", "LOD3")
RUNTIME_VARIANTS = {
    "LOD0": "Antey.glb",
    "LOD1": None,
    "LOD2": None,
    "LOD3": None,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=Path, default=Path("Content/submarines/Antey"))
    return parser.parse_args()


def read_glb_triangle_count(path: Path) -> int:
    raw = path.read_bytes()
    if len(raw) < 20:
        raise RuntimeError(f"GLB too small: {path}")
    magic, version, total_length = struct.unpack_from("<III", raw, 0)
    if magic != 0x46546C67 or version != 2 or total_length != len(raw):
        raise RuntimeError(f"Invalid GLB header: {path}")
    json_length, json_type = struct.unpack_from("<II", raw, 12)
    if json_type != 0x4E4F534A:
        raise RuntimeError(f"GLB first chunk is not JSON: {path}")
    document = json.loads(raw[20 : 20 + json_length].decode("utf-8"))
    accessors = document.get("accessors", [])
    triangles = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            mode = primitive.get("mode", 4)
            if mode != 4:
                raise RuntimeError("Runtime Antey GLB must use TRIANGLES primitives")
            if "indices" in primitive:
                count = int(accessors[primitive["indices"]]["count"])
            else:
                position_accessor = primitive.get("attributes", {}).get("POSITION")
                if position_accessor is None:
                    raise RuntimeError("Runtime Antey primitive has no indices or POSITION accessor")
                count = int(accessors[position_accessor]["count"])
            if count % 3 != 0:
                raise RuntimeError("Runtime Antey triangle primitive index/vertex count is not divisible by three")
            triangles += count // 3
    return triangles


def current_selection(family: list[dict]) -> dict[str, str]:
    """Mirror the accepted runtime rule for the currently declared package variants.

    LOD0 is mandatory. A missing requested LOD may only fall back toward a more detailed
    staged variant; a coarser-than-requested substitution is never allowed.
    """
    if not family or not family[0]["runtimeAvailable"]:
        raise RuntimeError("LOD0 must be available before applying the selection policy")

    selected: dict[str, str] = {}
    for requested_index, requested in enumerate(family):
        resolved: str | None = None
        for candidate_index in range(requested_index, -1, -1):
            if family[candidate_index]["runtimeAvailable"]:
                resolved = str(family[candidate_index]["lod"])
                break
        if resolved is None:
            raise RuntimeError(f"No selectable staged render variant for {requested['lod']}")
        selected[str(requested["lod"])] = resolved
    return selected


def validate(package: Path) -> dict:
    metadata_path = package / "Antey.asset.json"
    if not metadata_path.is_file():
        raise RuntimeError(f"Missing production metadata: {metadata_path}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    lods = metadata.get("lods")
    if not isinstance(lods, dict) or tuple(lods.keys()) != EXPECTED_LODS:
        raise RuntimeError("Antey metadata must contain ordered LOD0..LOD3 records")

    previous_vertices: int | None = None
    previous_triangles: int | None = None
    family = []
    for lod_name in EXPECTED_LODS:
        record = lods[lod_name]
        objects = int(record["objects"])
        vertices = int(record["vertices"])
        triangles = int(record["triangles"])
        if objects <= 0 or vertices <= 0 or triangles <= 0:
            raise RuntimeError(f"{lod_name} topology counts must be positive")
        if previous_vertices is not None and vertices > previous_vertices:
            raise RuntimeError(f"{lod_name} has more vertices than the preceding finer LOD")
        if previous_triangles is not None and triangles > previous_triangles:
            raise RuntimeError(f"{lod_name} has more triangles than the preceding finer LOD")
        runtime_file = RUNTIME_VARIANTS[lod_name]
        available = runtime_file is not None and (package / runtime_file).is_file()
        family.append(
            {
                "lod": lod_name,
                "objects": objects,
                "vertices": vertices,
                "triangles": triangles,
                "runtimeAvailable": available,
                "runtimeFile": runtime_file,
            }
        )
        previous_vertices = vertices
        previous_triangles = triangles

    if not family[0]["runtimeAvailable"]:
        raise RuntimeError("LOD0 must be available in the IG1 runtime package")
    if any(item["runtimeAvailable"] for item in family[1:]):
        raise RuntimeError("IG1-D package unexpectedly contains an undeclared additional runtime LOD variant")

    runtime_triangles = read_glb_triangle_count(package / "Antey.glb")
    metadata_triangles = family[0]["triangles"]
    tolerance = max(16, int(metadata_triangles * 0.001))
    if abs(runtime_triangles - metadata_triangles) > tolerance:
        raise RuntimeError(
            f"LOD0 runtime/source topology divergence is too large: runtime={runtime_triangles}, "
            f"metadata={metadata_triangles}, tolerance={tolerance}"
        )

    selection = current_selection(family)
    expected_current_selection = {lod_name: "LOD0" for lod_name in EXPECTED_LODS}
    if selection != expected_current_selection:
        raise RuntimeError(
            f"Current IG1-D package selection changed unexpectedly: {selection!r}"
        )

    return {
        "status": "PASS",
        "family": family,
        "runtimeLod0Triangles": runtime_triangles,
        "metadataLod0Triangles": metadata_triangles,
        "selectionPolicy": selection,
        "fallbackDirection": "MORE_DETAILED_ONLY",
        "mandatoryRuntimeLod": "LOD0",
        "dynamicScreenSpaceSwitching": "DEFERRED",
        "physicsDependsOnRenderLod": False,
    }


def main() -> int:
    args = parse_args()
    try:
        result = validate(args.package)
    except Exception as exc:  # bounded command-line validator
        print(f"IG1-D LOD VALIDATION: FAIL: {exc}")
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
