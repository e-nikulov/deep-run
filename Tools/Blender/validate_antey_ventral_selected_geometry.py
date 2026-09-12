"""Hard regression gate for the artist-selected Antey Ventral rudder surface."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from antey_ventral_selection import parse_selection, surface_incidence

RUDDER = "SM_Antey_LOD0_Rudder_Ventral"


def options() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--selection", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def main() -> None:
    opts = options()
    candidate = opts.candidate.resolve(strict=True)
    selection_path = opts.selection.resolve(strict=True)
    bpy.ops.wm.open_mainfile(filepath=str(candidate))
    if Path(bpy.data.filepath).resolve(strict=True) != candidate:
        raise RuntimeError("Fresh candidate reopen mismatch")
    selection = parse_selection(selection_path)
    rudder = bpy.data.objects.get(RUDDER)
    if rudder is None or rudder.type != "MESH":
        raise RuntimeError(f"Missing {RUDDER}")
    incidence = surface_incidence(rudder, selection)
    report = {
        "candidate": str(candidate),
        "candidateSha256": sha256(candidate),
        "selection": str(selection_path),
        "targetObject": RUDDER,
        "mesh": {
            "vertices": len(rudder.data.vertices),
            "edges": len(rudder.data.edges),
            "faces": len(rudder.data.polygons),
            "triangles": sum(len(poly.vertices) - 2 for poly in rudder.data.polygons),
        },
        "fixture": {"vertices": len(selection["vertices"]), "edges": len(selection["edges"])},
        "surface": incidence,
        "status": "PASS" if incidence["pass"] else "FAIL",
    }
    opts.output.parent.mkdir(parents=True, exist_ok=True)
    opts.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "fixtureVertices": report["fixture"]["vertices"],
        "fixtureEdges": report["fixture"]["edges"],
        "surfaceOwnedVertices": incidence["surfaceOwnedVertices"],
        "surfaceOwnedEdges": incidence["surfaceOwnedEdges"],
        "looseVertices": len(incidence["looseVertices"]),
        "looseEdges": len(incidence["looseEdges"]),
    }))
    print(f"ANTEY_VENTRAL_SELECTED_GEOMETRY_{report['status']} {opts.output.resolve()}")
    if not incidence["pass"]:
        raise RuntimeError("Artist-selected Ventral topology is not fully polygon-surface-owned")


if __name__ == "__main__":
    main()
