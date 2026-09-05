"""Write geometry-derived asset and authoring sidecars from a reopened BLEND."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from artifact_provenance import require_path_suffix


MAIN_BOW_SONAR_SEMANTIC_ID = "MGK540_BOW_ARRAY"


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


def propeller_meshes(root: bpy.types.Object | None, name: str) -> list[bpy.types.Object]:
    """Resolve geometry below a logical propeller articulation root.

    Promoted Antey scenes use EMPTY roots with child meshes for each LOD;
    older assets may use a single MESH root.  The root itself remains the
    authoritative transform/pivot in both cases.
    """
    if root is None:
        raise RuntimeError(f"Missing logical propeller root: {name}")
    if root.type == "MESH":
        if root.data is None:
            raise RuntimeError(f"Propeller mesh root has no mesh data: {name}")
        return [root]
    if root.type != "EMPTY":
        raise RuntimeError(f"Propeller root must be EMPTY or MESH: {name} ({root.type})")
    children = [obj for obj in root.children_recursive if obj.type == "MESH" and obj.data is not None]
    if not children:
        raise RuntimeError(f"EMPTY propeller root has no child mesh geometry: {name}")
    lod0 = [obj for obj in children if int(obj.get("lod", -1)) == 0]
    return lod0 or children


def propeller_metadata(root: bpy.types.Object | None, name: str) -> dict:
    meshes = propeller_meshes(root, name)
    bpy.context.view_layer.update()
    triangles = 0
    for mesh_object in meshes:
        mesh_object.data.calc_loop_triangles()
        triangles += len(mesh_object.data.loop_triangles)
    blade_count = root.get("visible_blades") if root is not None else None
    if blade_count is None and root is not None:
        blade_count = root.get("SOURCE_BLADE_COUNT")
    if blade_count is None:
        blade_count = len({re.sub(r"\.\d+$", "", mesh.name) for mesh in meshes if "_Blade_" in mesh.name})
    handedness = root.get("handedness_status") if root is not None else None
    if handedness is None:
        handedness = next((mesh.get("handedness_status") for mesh in meshes if mesh.get("handedness_status")), "UNKNOWN")
    return {
        "name": name,
        "origin": list(root.matrix_world.translation),
        "transform": matrix_values(root),
        "axis": "+X",
        "visibleBlades": int(blade_count),
        "triangles": triangles,
        "handednessStatus": handedness,
    }


def main_bow_sonar_region() -> dict:
    return {
        "semanticId": MAIN_BOW_SONAR_SEMANTIC_ID,
        "anchorMarker": None,
        "anchorStatus": "NO_GEOMETRIC_ANCHOR_AUTHORED",
        "semanticClass": "RESERVED_CONTENT_REGION",
        "system": "MGK-540 Skat-3 main bow sonar/acoustic array",
        "bowAllocation": "CENTRAL_PREDOMINANTLY_LOWER_FORWARD",
        "geometryContract": "NO_INTERNAL_ARRAY_GEOMETRY_REQUIRED",
        "authoringConstraint": (
            "NO_TORPEDO_INTERNAL_WEAPON_OR_LARGE_BOW_ELEMENT_INTERSECTION_"
            "WITHOUT_EXPLICIT_CONTENT_CONTRACT_REVIEW"
        ),
        "runtimeBoundary": (
            "Content semantic marker only; not a physical collider, authoritative sonar "
            "simulation state, or MGK-540 runtime implementation"
        ),
        "spatialValidation": "DOCUMENTED_ONLY_NO_BOUNDED_GEOMETRY",
    }


def source_first_authoring_objects(objects: dict[str, bpy.types.Object], role: str) -> list[bpy.types.Object]:
    return sorted(
        (obj for obj in objects.values() if obj.type == "EMPTY" and obj.get("authoring_role") == role),
        key=lambda obj: obj.name,
    )


def source_first_launcher_identity(name: str) -> tuple[str, int]:
    match = re.fullmatch(r"P700_(Port|Starboard)_(\d{2})", name)
    if match is None:
        raise RuntimeError(f"Invalid source-first P700 launcher name: {name}")
    return match.group(1).upper(), int(match.group(2))


def antey_data(antey_production_blend: Path, antey_source_blend: Path, antey_runtime_glb: Path) -> tuple[dict, dict]:
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    runtime0 = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    minimum, maximum = bounds(runtime0)
    hull_min, hull_max = bounds([objects["SM_Antey_LOD0_Hull"]])
    sail_min, sail_max = bounds([objects["SM_Antey_LOD0_Sail"]])
    legacy_launchers = sorted((obj for obj in objects.values() if obj.name.startswith("HP_P700_")), key=lambda item: item.name)
    source_first_launchers = source_first_authoring_objects(objects, "P700_LAUNCH_POSITION")
    launchers = []
    launcher_sides = {}
    if legacy_launchers:
        for obj in legacy_launchers:
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
            launcher_sides[obj.name] = "PORT" if "_PORT_" in obj.name else "STARBOARD"
    else:
        for obj in source_first_launchers:
            side, row_index = source_first_launcher_identity(obj.name)
            envelope = objects.get(f"P700_Envelope_{side.title()}_{row_index:02d}")
            forward = Vector(obj.get("launcher_axis", obj.rotation_quaternion @ Vector((1.0, 0.0, 0.0))))
            launchers.append({
                "name": obj.name,
                "position": list(obj.location),
                "orientationQuaternionWXYZ": list(obj.rotation_quaternion),
                "transform": matrix_values(obj),
                "launchForward": list(forward),
                "hatchGroup": f"{side}_HATCH_{(row_index + 1) // 2:02d}",
                "pairIndex": 1 + (row_index - 1) % 2,
                "bankRow": "LONGITUDINAL",
                "rowIndex": row_index,
                "launcherEnvelopeDiameter": float(envelope.get("diameterM")) if envelope and envelope.get("diameterM") is not None else None,
                "launcherEnvelopeLength": float(envelope.get("lengthM")) if envelope and envelope.get("lengthM") is not None else None,
            })
            launcher_sides[obj.name] = side

    legacy_torpedoes = sorted((obj for obj in objects.values() if obj.name.startswith("HP_TORPEDO_")), key=lambda item: item.name)
    source_first_torpedoes = source_first_authoring_objects(objects, "TORPEDO_TUBE")
    torpedoes = []
    if legacy_torpedoes:
        for obj in legacy_torpedoes:
            torpedoes.append({
                "name": obj.name,
                "tubeClass": obj.get("tube_class"),
                "position": list(obj.location),
                "orientationQuaternionWXYZ": list(obj.rotation_quaternion),
                "transform": matrix_values(obj),
                "launchForward": list(obj.rotation_quaternion @ Vector((1.0, 0.0, 0.0))),
            })
    else:
        for obj in source_first_torpedoes:
            diameter = float(obj.get("diameter_m"))
            tube_class = "533" if math.isclose(diameter, 0.533, abs_tol=1.0e-6) else "650" if math.isclose(diameter, 0.650, abs_tol=1.0e-6) else None
            if tube_class is None:
                raise RuntimeError(f"Unsupported source-first torpedo diameter: {obj.name} ({diameter})")
            torpedoes.append({
                "name": obj.name,
                "tubeClass": tube_class,
                "position": list(obj.location),
                "orientationQuaternionWXYZ": list(obj.rotation_quaternion),
                "transform": matrix_values(obj),
                "launchForward": list(obj.rotation_quaternion @ Vector((1.0, 0.0, 0.0))),
            })
    compartments = []
    compartment_prefix = "VOL_COMP_" if any(obj.name.startswith("VOL_COMP_") for obj in objects.values()) else "Antey_Compartment_"
    for obj in sorted((obj for obj in objects.values() if obj.name.startswith(compartment_prefix)), key=lambda item: item.name):
        volume_min, volume_max = bounds([obj])
        compartments.append({"name": obj.name, "center": list((volume_min + volume_max) * 0.5), "orientationQuaternionWXYZ": [1.0, 0.0, 0.0, 0.0], "halfExtents": list((volume_max - volume_min) * 0.5)})
    props = []
    for name in ("SM_Propeller_Port", "SM_Propeller_Starboard"):
        obj = objects.get(name)
        props.append(propeller_metadata(obj, name))
    rows = {}
    for side in ("PORT", "STARBOARD"):
        rows[side] = {}
        for row in sorted({item["bankRow"] for item in launchers if launcher_sides.get(item["name"]) == side}):
            members = sorted((item for item in launchers if launcher_sides.get(item["name"]) == side and item["bankRow"] == row), key=lambda item: item["rowIndex"])
            xs = [item["position"][0] for item in members]
            rows[side][row] = {"count": len(members), "positions": [item["name"] for item in members], "xPositions": xs, "adjacentSpacing": [b - a for a, b in zip(xs, xs[1:])]}
    asset = {
        "schemaVersion": 1,
        "assetId": "C0 Player Submarine",
        "name": "Antey",
        "identity": "neutral Project 949A-inspired production asset",
        "blend": str(antey_production_blend),
        "blendSha256": sha256(antey_production_blend),
        "glb": str(antey_runtime_glb),
        "glbSha256": sha256(antey_runtime_glb),
        "source": str(antey_source_blend),
        "sourceSha256": sha256(antey_source_blend),
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
        "collision": [obj.name for obj in objects.values() if obj.get("physics_proxy_role") == "COLLISION"] or [name for name in ("COL_Antey_Bow", "COL_Antey_Main", "COL_Antey_Aft", "COL_Antey_Sail") if name in objects],
        "buoyancyProxy": next((obj.name for obj in objects.values() if obj.get("physics_proxy_role") == "BUOYANCY"), "PHY_Antey_BuoyancyVolume" if "PHY_Antey_BuoyancyVolume" in objects else None),
        "semanticRegions": [main_bow_sonar_region()],
        "runtimeBoundary": "Spatial authoring only; Simulation owns loading, damage, flooding, fire, crew, hatch state, and propeller RPM",
    }
    return asset, authoring


def p700_data(p700_production_blend: Path, p700_source_blend: Path, p700_runtime_glb: Path) -> tuple[dict, dict]:
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
        "blend": str(p700_production_blend),
        "blendSha256": sha256(p700_production_blend),
        "glb": str(p700_runtime_glb),
        "glbSha256": sha256(p700_runtime_glb),
        "source": str(p700_source_blend),
        "sourceSha256": sha256(p700_source_blend),
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
    production_blend = Path(bpy.data.filepath).resolve()
    source_blend = options.source.resolve()
    runtime_glb = options.glb.resolve()
    if options.asset == "antey":
        require_path_suffix(production_blend, "Content/submarines/Antey/Antey_GameReady.blend", "ANTEY_PRODUCTION_BLEND")
        require_path_suffix(runtime_glb, "Content/submarines/Antey/Antey.glb", "ANTEY_RUNTIME_GLB")
        asset, authoring = antey_data(production_blend, source_blend, runtime_glb)
    else:
        require_path_suffix(production_blend, "Content/Weapons/P700/P700_Granit_GameReady.blend", "P700_PRODUCTION_BLEND")
        require_path_suffix(runtime_glb, "Content/Weapons/P700/P700_Granit.glb", "P700_RUNTIME_GLB")
        asset, authoring = p700_data(production_blend, source_blend, runtime_glb)
    for path, data in ((options.asset_json.resolve(), asset), (options.authoring_json.resolve(), authoring)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(f"PRODUCTION_SIDECARS_OK asset={options.asset}")


if __name__ == "__main__":
    main()
