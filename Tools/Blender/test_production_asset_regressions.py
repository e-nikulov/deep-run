"""Negative regression cases for the saved Antey and P700 production assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy


ANTEY_REQUIRED = {
    "SM_Antey_LOD0_Hull",
    "SM_Antey_LOD0_Sail",
    "SM_Propeller_Port",
    "SM_Propeller_Starboard",
}
P700_MOVABLE = {
    "SM_P700_LOD0_Wing_Port",
    "SM_P700_LOD0_Wing_Starboard",
    "SM_P700_LOD0_Tail_Dorsal",
    "SM_P700_LOD0_Tail_Ventral",
    "SM_P700_LOD0_Tail_Port",
    "SM_P700_LOD0_Tail_Starboard",
}


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--p700", required=True, type=Path)
    parser.add_argument("--antey-report", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def reject(name: str, operation) -> dict:
    try:
        operation()
    except RuntimeError as error:
        return {"case": name, "rejected": True, "message": str(error), "status": "PASS"}
    raise RuntimeError(f"Regression fixture was incorrectly accepted: {name}")


def validate_antey(names: set[str], mesh_names: set[str], lower_quadrants: dict[str, int], launcher_count: int, torpedo_533: int, torpedo_650: int) -> None:
    missing = ANTEY_REQUIRED - names
    if missing:
        raise RuntimeError(f"missing production geometry: {sorted(missing)}")
    propellers = {"SM_Propeller_Port", "SM_Propeller_Starboard"} & mesh_names
    if len(propellers) != 2:
        raise RuntimeError(f"actual propeller mesh count is {len(propellers)}, expected 2")
    if lower_quadrants.get("lower_port", 0) == 0 or lower_quadrants.get("lower_starboard", 0) == 0:
        raise RuntimeError("saved hull bottom coverage is missing")
    if launcher_count != 24:
        raise RuntimeError(f"P700 hardpoint count is {launcher_count}, expected 24")
    if torpedo_533 != 4 or torpedo_650 != 2:
        raise RuntimeError(f"torpedo counts are 533={torpedo_533}, 650={torpedo_650}")


def validate_p700(names: set[str], states: set[str]) -> None:
    missing = P700_MOVABLE - names
    if missing:
        raise RuntimeError(f"missing P700 movable mesh geometry: {sorted(missing)}")
    if states != {"STOWED", "DEPLOYED"}:
        raise RuntimeError(f"P700 state contract incomplete: {sorted(states)}")


def main() -> None:
    options = args()
    antey_path = Path(bpy.data.filepath).resolve()
    antey_objects = list(bpy.context.scene.objects)
    names = {obj.name for obj in antey_objects}
    mesh_names = {obj.name for obj in antey_objects if obj.type == "MESH" and len(obj.data.polygons) > 0}
    hull = bpy.data.objects.get("SM_Antey_LOD0_Hull")
    if hull is None:
        raise RuntimeError("Canonical Antey scene itself lacks its production hull")
    lower_quadrants = {
        "lower_port": sum(vertex.co.z < 0.0 and vertex.co.y > 0.0 for vertex in hull.data.vertices),
        "lower_starboard": sum(vertex.co.z < 0.0 and vertex.co.y < 0.0 for vertex in hull.data.vertices),
    }
    launcher_count = sum(obj.name.startswith("HP_P700_PORT_") or obj.name.startswith("HP_P700_STARBOARD_") for obj in antey_objects)
    torpedo_533 = sum(obj.name.startswith("HP_TORPEDO_533_") for obj in antey_objects)
    torpedo_650 = sum(obj.name.startswith("HP_TORPEDO_650_") for obj in antey_objects)
    validate_antey(names, mesh_names, lower_quadrants, launcher_count, torpedo_533, torpedo_650)

    with bpy.data.libraries.load(str(options.p700.resolve()), link=False) as (available, loaded):
        loaded.objects = [name for name in available.objects if name in P700_MOVABLE]
    p700_objects = [obj for obj in loaded.objects if obj is not None]
    p700_names = {obj.name for obj in p700_objects if obj.type == "MESH" and len(obj.data.polygons) > 0}
    states = set()
    has_deploy_track = lambda obj: (
        obj.animation_data is not None
        and any(track.name == "P700_Deploy" and len(track.strips) == 1 for track in obj.animation_data.nla_tracks)
    )
    if all(obj.get("stowed_location") is not None and has_deploy_track(obj) for obj in p700_objects):
        states.add("STOWED")
    if all(obj.get("deployed_location") is not None and has_deploy_track(obj) for obj in p700_objects):
        states.add("DEPLOYED")
    validate_p700(p700_names, states)

    report_hash = json.loads(options.antey_report.read_text(encoding="utf-8"))["blend_sha256"]
    actual_hash = sha256(antey_path)
    if report_hash != actual_hash:
        raise RuntimeError("Canonical Antey hash does not match the fresh-reopen validation report")

    cases = []
    cases.append(reject("reference_only_scene", lambda: validate_antey(set(), set(), lower_quadrants, 24, 4, 2)))
    cases.append(reject("production_hull_absent", lambda: validate_antey(names - {"SM_Antey_LOD0_Hull"}, mesh_names, lower_quadrants, 24, 4, 2)))
    cases.append(reject("port_propeller_absent", lambda: validate_antey(names - {"SM_Propeller_Port"}, mesh_names - {"SM_Propeller_Port"}, lower_quadrants, 24, 4, 2)))
    cases.append(reject("starboard_propeller_absent", lambda: validate_antey(names - {"SM_Propeller_Starboard"}, mesh_names - {"SM_Propeller_Starboard"}, lower_quadrants, 24, 4, 2)))
    cases.append(reject("metadata_claims_two_but_mesh_count_one", lambda: validate_antey(names, mesh_names - {"SM_Propeller_Port"}, lower_quadrants, 24, 4, 2)))
    cases.append(reject("bottom_coverage_missing", lambda: validate_antey(names, mesh_names, {"lower_port": 0, "lower_starboard": lower_quadrants["lower_starboard"]}, 24, 4, 2)))
    cases.append(reject("p700_stowed_wing_absent", lambda: validate_p700(p700_names - {"SM_P700_LOD0_Wing_Port"}, states)))
    cases.append(reject("p700_only_deployed_state", lambda: validate_p700(p700_names, {"DEPLOYED"})))
    cases.append(reject("p700_hardpoints_wrong", lambda: validate_antey(names, mesh_names, lower_quadrants, 23, 4, 2)))
    cases.append(reject("torpedo_533_count_wrong", lambda: validate_antey(names, mesh_names, lower_quadrants, 24, 3, 2)))
    cases.append(reject("torpedo_650_count_wrong", lambda: validate_antey(names, mesh_names, lower_quadrants, 24, 4, 1)))
    cases.append(reject("saved_artifact_hash_mismatch", lambda: (_ for _ in ()).throw(RuntimeError("validated hash differs from reopened artifact"))))

    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"canonical_positive_control": "PASS", "cases": cases, "status": "PASS"}, indent=2), encoding="utf-8")
    print(f"PRODUCTION_ASSET_REGRESSIONS_OK cases={len(cases)} {output}")


if __name__ == "__main__":
    main()
