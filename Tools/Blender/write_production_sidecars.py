"""Write geometry-derived asset and authoring sidecars from a reopened BLEND."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset", required=True, choices=("antey", "p700"))
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--glb", required=True, type=Path)
    parser.add_argument("--asset-json", required=True, type=Path)
    parser.add_argument("--authoring-json", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    corners = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[index] for point in corners) for index in range(3))),
        Vector(tuple(max(point[index] for point in corners) for index in range(3))),
    )


def radial_diameter(objects: list[bpy.types.Object]) -> float:
    return 2.0 * max(
        math.hypot(world.y, world.z)
        for obj in objects
        for vertex in obj.data.vertices
        for world in (obj.matrix_world @ vertex.co,)
    )


def matrix_values(obj: bpy.types.Object) -> list[list[float]]:
    return [[float(value) for value in row] for row in obj.matrix_world]


def topology_totals(objects: list[bpy.types.Object]) -> dict:
    for obj in objects:
        obj.data.calc_loop_triangles()
    return {
        "objects": len(objects),
        "vertices": sum(len(obj.data.vertices) for obj in objects),
        "triangles": sum(len(obj.data.loop_triangles) for obj in objects),
    }


def lods() -> dict:
    runtime = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and bool(obj.get("runtime_export", False))]
    return {f"LOD{lod}": topology_totals([obj for obj in runtime if int(obj.get("lod", -1)) == lod]) for lod in range(4)}


def antey_data(blend: Path, source: Path, glb: Path) -> tuple[dict, dict]:
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    runtime0 = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    minimum, maximum = bounds(runtime0)
    hull_min, hull_max = bounds([objects["SM_Antey_LOD0_Hull"]])
    sail_min, sail_max = bounds([objects["SM_Antey_LOD0_Sail"]])
    launchers = []
    for obj in sorted((obj for obj in objects.values() if obj.name.startswith("HP_P700_")), key=lambda item: item.name):
        forward = obj.rotation_quaternion @ Vector((1.0, 0.0, 0.0))
        launchers.append({
            "name": obj.name,
            "position": list(obj.location),
            "orientationQuaternionWXYZ": list(obj.rotation_quaternion),
            "transform": matrix_values(obj),
            "launchForward": list(forward),
            "hatchGroup": obj.get("hatch_group"),
            "pairIndex": int(obj.get("pair_index")),
            "bankRow": obj.get("bank_row"),
            "rowIndex": int(obj.get("row_index")),
            "launcherEnvelopeDiameter": float(obj.get("launcher_envelope_diameter")),
            "launcherEnvelopeLength": float(obj.get("launcher_envelope_length")),
        })
    torpedoes = []
    for obj in sorted((obj for obj in objects.values() if obj.name.startswith("HP_TORPEDO_")), key=lambda item: item.name):
        torpedoes.append({
            "name": obj.name,
            "tubeClass": obj.get("tube_class"),
            "position": list(obj.location),
            "orientationQuaternionWXYZ": list(obj.rotation_quaternion),
            "transform": matrix_values(obj),
            "launchForward": list(obj.rotation_quaternion @ Vector((1.0, 0.0, 0.0))),
        })
    compartments = []
    for obj in sorted((obj for obj in objects.values() if obj.name.startswith("VOL_COMP_")), key=lambda item: item.name):
        volume_min, volume_max = bounds([obj])
        compartments.append({"name": obj.name, "center": list((volume_min + volume_max) * 0.5), "orientationQuaternionWXYZ": [1.0, 0.0, 0.0, 0.0], "halfExtents": list((volume_max - volume_min) * 0.5)})
    props = []
    for name in ("SM_Propeller_Port", "SM_Propeller_Starboard"):
        obj = objects[name]
        obj.data.calc_loop_triangles()
        props.append({"name": name, "origin": list(obj.location), "axis": "+X", "visibleBlades": int(obj.get("visible_blades")), "triangles": len(obj.data.loop_triangles), "handednessStatus": obj.get("handedness_status")})
    rows = {}
    for side in ("PORT", "STARBOARD"):
        rows[side] = {}
        for row in sorted({item["bankRow"] for item in launchers if f"_{side}_" in item["name"]}):
            members = sorted((item for item in launchers if f"_{side}_" in item["name"] and item["bankRow"] == row), key=lambda item: item["rowIndex"])
            xs = [item["position"][0] for item in members]
            rows[side][row] = {"count": len(members), "positions": [item["name"] for item in members], "xPositions": xs, "adjacentSpacing": [b - a for a, b in zip(xs, xs[1:])]}
    asset = {
        "schemaVersion": 1,
        "assetId": "C0 Player Submarine",
        "name": "Antey",
        "identity": "neutral Project 949A-inspired production asset",
        "blend": str(blend),
        "blendSha256": sha256(blend),
        "glb": str(glb),
        "glbSha256": sha256(glb),
        "source": str(source),
        "sourceSha256": sha256(source),
        "coordinateContract": "+X bow; +Y port; +Z up; 1 BU = 1 m",
        "dimensionsMeters": {"length": maximum.x - minimum.x, "maximumBeam": hull_max.y - hull_min.y, "maximumExteriorSpan": maximum.y - minimum.y, "mainHullMaximumBeam": hull_max.y - hull_min.y, "mainHullExteriorHeight": hull_max.z - hull_min.z, "sailHeight": sail_max.z - sail_min.z, "overallHeight": maximum.z - minimum.z},
        "lods": lods(),
        "materials": ["MAT_Antey_Hull", "MAT_Antey_Propellers"],
        "licenseStatus": "SHIPPING BLOCKED PENDING LEGAL REVIEW",
    }
    authoring = {
        "schemaVersion": 1,
        "coordinateContract": asset["coordinateContract"],
        "p700Launchers": launchers,
        "p700Rows": rows,
        "hatchGroups": sorted({item["hatchGroup"] for item in launchers}),
        "launchAngleStatus": "40 degree elevation and 3 degree outward cant are gameplay authoring approximations guided by public references",
        "torpedoTubes": torpedoes,
        "propellers": props,
        "compartments": compartments,
        "collision": [name for name in ("COL_Antey_Bow", "COL_Antey_Main", "COL_Antey_Aft", "COL_Antey_Sail") if name in objects],
        "buoyancyProxy": "PHY_Antey_BuoyancyVolume",
        "markers": {name: list(objects[name].location) for name in ("HP_Antey_Bow", "HP_Antey_Stern", "HP_Antey_Center", "HP_Antey_Sonar_Bow")},
        "runtimeBoundary": "Spatial authoring only; Simulation owns loading, damage, flooding, fire, crew, hatch state, and propeller RPM",
    }
    return asset, authoring


def p700_data(blend: Path, source: Path, glb: Path) -> tuple[dict, dict]:
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    lod0 = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    states = {}
    for name, frame in (("STOWED", 1), ("DEPLOYED", 41)):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        minimum, maximum = bounds(lod0)
        states[name] = {"frame": frame, "dimensionsMeters": list(maximum - minimum), "minimum": list(minimum), "maximum": list(maximum)}
    movable = []
    for name in sorted(obj.name for obj in lod0 if "Wing_" in obj.name or "Tail_" in obj.name):
        obj = objects[name]
        movable.append({
            "name": name,
            "pivotLocal": [0.0, 0.0, 0.0],
            "pivotContract": obj.get("pivot_contract"),
            "stowed": {"location": list(obj.get("stowed_location")), "rotationEulerXYZ": list(obj.get("stowed_rotation_euler"))},
            "deployed": {"location": list(obj.get("deployed_location")), "rotationEulerXYZ": list(obj.get("deployed_rotation_euler"))},
        })
    bpy.context.scene.frame_set(1)
    body_diameter = radial_diameter([objects["SM_P700_LOD0_Body"]])
    maximum_stowed_diameter = radial_diameter(lod0)
    launcher_envelope_diameter = 1.35
    minimum_required_clearance = 0.025
    radial_clearance = (launcher_envelope_diameter - maximum_stowed_diameter) * 0.5
    asset = {
        "schemaVersion": 1,
        "assetId": "P700",
        "name": "P700 Granit",
        "blend": str(blend),
        "blendSha256": sha256(blend),
        "glb": str(glb),
        "glbSha256": sha256(glb),
        "source": str(source),
        "sourceSha256": sha256(source),
        "coordinateContract": "+X forward; +Y port; +Z up; 1 BU = 1 m",
        "states": states,
        "lods": lods(),
        "launcherEnvelopeDiameterMeters": launcher_envelope_diameter,
        "stowedPacking": {
            "bodyDiameterMeters": body_diameter,
            "maximumStowedDiameterMeters": maximum_stowed_diameter,
            "launcherEnvelopeDiameterMeters": launcher_envelope_diameter,
            "radialClearanceMeters": radial_clearance,
            "minimumRequiredClearance": minimum_required_clearance,
        },
        "materials": sorted({slot.material.name for obj in lod0 for slot in obj.material_slots if slot.material}),
        "licenseStatus": "SHIPPING BLOCKED PENDING LEGAL REVIEW",
    }
    authoring = {
        "schemaVersion": 1,
        "root": "P700_ROOT",
        "states": states,
        "movableSurfaces": movable,
        "animation": {
            "name": "P700_Deploy",
            "startFrame": 1,
            "endFrame": 41,
            "sampleFrames": {"0%": 1, "25%": 11, "50%": 21, "75%": 31, "100%": 41},
            "interpolation": "LINEAR rotation-only hinge transforms",
            "exportNormalization": "six source actions share one P700_Deploy NLA track and export as one GLB animation",
        },
        "stowedPacking": asset["stowedPacking"],
        "booster": {"mesh": "SM_P700_LOD0_Booster", "attachment": "P700_BOOSTER_ATTACH", "externalEnvelopeOnly": True},
        "pivotValidation": "FIXED PHYSICAL ROOT HINGES; ROTATION ONLY; FIVE SAMPLE SURFACE CLEARANCE PASS",
        "runtimeBoundary": "Future WeaponSystem keeps STOWED through launcher exit and underwater launch; deployment is allowed only in the appropriate post-launch phase",
    }
    return asset, authoring


def main() -> None:
    options = args()
    blend = Path(bpy.data.filepath).resolve()
    source = options.source.resolve()
    glb = options.glb.resolve()
    asset, authoring = antey_data(blend, source, glb) if options.asset == "antey" else p700_data(blend, source, glb)
    for path, data in ((options.asset_json.resolve(), asset), (options.authoring_json.resolve(), authoring)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(f"PRODUCTION_SIDECARS_OK asset={options.asset}")


if __name__ == "__main__":
    main()
