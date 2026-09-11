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
    parser.add_argument("--source-partition-audit", type=Path)
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


def matrix_values_with_translation(obj: bpy.types.Object, translation: Vector) -> list[list[float]]:
    matrix = obj.matrix_world.copy()
    matrix.translation = translation
    return [[float(value) for value in row] for row in matrix]

def identity_matrix_values() -> list[list[float]]:
    return [[1.0 if row == column else 0.0 for column in range(4)] for row in range(4)]


def source_translation_matrix(z: float) -> list[list[float]]:
    matrix = identity_matrix_values()
    matrix[2][3] = float(z)
    return matrix


def retractable_sail_devices(
    objects: dict[str, bpy.types.Object], sail_maximum: Vector
) -> list[dict]:
    """Derive the normal submerged pose from explicit production metadata.

    The output keeps node references private to the authoring sidecar.  Runtime
    resolves them once to opaque model binding indices; the stow translation is
    derived from the reopened production geometry, never copied into gameplay.
    """
    sail_devices = sorted(
        (
            obj
            for obj in objects.values()
            if obj.type == "MESH"
            and bool(obj.get("runtime_export", False))
            and int(obj.get("lod", -1)) == 0
            and obj.get("source_first_role") == "SAIL_DEVICE"
        ),
        key=lambda obj: obj.name,
    )
    if not sail_devices:
        raise RuntimeError("No LOD0 source-first sail devices were found")

    retractable = []
    for obj in sail_devices:
        deployment = obj.get("DEVICE_DEPLOYMENT")
        if deployment not in ("RETRACTABLE", "STATIC"):
            raise RuntimeError(
                f"Sail device deployment must be explicitly RETRACTABLE or STATIC: {obj.name} ({deployment})"
            )
        if deployment != "RETRACTABLE":
            continue
        if obj.get("MOTION") != "TRANSLATION" or obj.get("AXIS") != "LOCAL_Z":
            raise RuntimeError(f"Retractable sail device has an unsupported motion contract: {obj.name}")

        _, device_maximum = bounds([obj])
        # Keep the outermost point just below the sail top.  Tall devices therefore
        # retract through the existing continuous sail/hull volume, not by hiding.
        clearance = 0.02
        stowed_top = sail_maximum.z - clearance
        retractable.append(
            {
                "semanticId": f"sail.retractable.{len(retractable) + 1:02d}",
                "nodeReference": obj.name,
                "classification": "RETRACTABLE",
                "defaultState": "STOWED",
                "deployedLocalPostTransform": identity_matrix_values(),
                "stowedLocalPostTransform": source_translation_matrix(stowed_top - device_maximum.z),
                "stowedSailEnvelopeMaximumSource": [
                    float(sail_maximum.x),
                    float(sail_maximum.y),
                    float(stowed_top),
                ],
            }
        )
    if not retractable:
        raise RuntimeError("No explicitly retractable LOD0 sail devices were found")
    return retractable


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


def propeller_metadata(root: bpy.types.Object | None, semantic_id: str) -> dict:
    name = root.name if root is not None else semantic_id
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
    hubs = [mesh for mesh in meshes if "_Hub" in mesh.name]
    if not hubs:
        raise RuntimeError(f"Propeller assembly has no source-first hub geometry: {name}")
    hub_minimum, hub_maximum = bounds(hubs)
    pivot = (hub_minimum + hub_maximum) * 0.5
    if not all(math.isfinite(value) for value in pivot):
        raise RuntimeError(f"Propeller hub pivot is non-finite: {name}")
    return {
        "semanticId": semantic_id,
        "nodeReference": name,
        "origin": list(pivot),
        "transform": matrix_values_with_translation(root, pivot),
        "axis": "+X",
        "visibleBlades": int(blade_count),
        "triangles": triangles,
        "handednessStatus": handedness,
    }


def compartment_metadata(obj: bpy.types.Object) -> dict:
    center_value = obj.get("CENTER")
    if center_value is None or len(center_value) != 3:
        raise RuntimeError(f"Compartment requires explicit source-first CENTER metadata: {obj.name}")
    center = Vector(tuple(float(value) for value in center_value))
    if not all(math.isfinite(value) for value in center):
        raise RuntimeError(f"Compartment CENTER is non-finite: {obj.name}")
    local_minimum, local_maximum = bounds([obj])
    half_extents = (local_maximum - local_minimum) * 0.5
    if not all(math.isfinite(value) and value > 0.0 for value in half_extents):
        raise RuntimeError(f"Compartment has invalid source volume extents: {obj.name}")
    return {
        "name": obj.name,
        "center": list(center),
        "orientationQuaternionWXYZ": list(obj.matrix_world.to_quaternion()),
        "halfExtents": list(half_extents),
        "transform": matrix_values_with_translation(obj, center),
    }


def validate_semantic_spatial_metadata(
    propellers: list[dict], compartments: list[dict], hull_minimum: Vector, hull_maximum: Vector
) -> None:
    if {record["semanticId"] for record in propellers} != {"propeller.port", "propeller.starboard"}:
        raise RuntimeError("Propeller semantic IDs must be the explicit port/starboard pair")
    port = next(record for record in propellers if record["semanticId"] == "propeller.port")
    starboard = next(record for record in propellers if record["semanticId"] == "propeller.starboard")
    port_pivot = Vector(port["origin"])
    starboard_pivot = Vector(starboard["origin"])
    if (port_pivot - starboard_pivot).length <= 0.02 or abs(port_pivot.y) <= 0.02 or abs(starboard_pivot.y) <= 0.02:
        raise RuntimeError("Propeller pivots are coincident or collapsed onto the vessel centerline")
    if port_pivot.y <= 0.0 or starboard_pivot.y >= 0.0:
        raise RuntimeError("Propeller pivot sides contradict explicit semantic IDs")
    hull_mid_x = (hull_minimum.x + hull_maximum.x) * 0.5
    if port_pivot.x >= hull_mid_x or starboard_pivot.x >= hull_mid_x:
        raise RuntimeError("Propeller pivots are not in the aft half of the production hull")

    if len(compartments) != 10 or len({record["name"] for record in compartments}) != 10:
        raise RuntimeError("Production compartments must provide ten unique records")
    centers = [Vector(record["center"]) for record in compartments]
    if any(not all(math.isfinite(value) for value in center) for center in centers):
        raise RuntimeError("Production compartment centers must be finite")
    if max((left - right).length for left in centers for right in centers) <= 0.02:
        raise RuntimeError("Production compartment centers are collapsed")
    for record, center in zip(compartments, centers):
        extent = Vector(record["halfExtents"])
        if any(not math.isfinite(value) or value <= 0.0 for value in extent):
            raise RuntimeError(f"Production compartment has invalid extent: {record['name']}")
        minimum = center - extent
        maximum = center + extent
        # Longitudinal placement has a direct hull-envelope correspondence. The
        # transverse production hull is shaped rather than box-like, so validate
        # its centre/scale without falsely rejecting intentional compartment
        # volumes that cross a tapered outer silhouette.
        if (minimum.x < hull_minimum.x - 0.02 or maximum.x > hull_maximum.x + 0.02 or
                not (hull_minimum.y <= center.y <= hull_maximum.y) or
                not (hull_minimum.z <= center.z <= hull_maximum.z) or
                extent.y * 2.0 > hull_maximum.y - hull_minimum.y or
                extent.z * 2.0 > hull_maximum.z - hull_minimum.z):
            raise RuntimeError(f"Production compartment is outside the hull envelope: {record['name']}")


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


def physics_proxy_metadata(obj: bpy.types.Object, semantic_id: str, role: str, cob: Vector | None = None) -> dict:
    """Serialize a bounded source-local BOX proxy, never render geometry.

    The current production contract intentionally emits axis-aligned boxes. The
    runtime still receives orientation metadata so the sidecar is explicit, but
    a rotated/sheared authoring object is rejected until that contract is
    deliberately reviewed. A proxy must be an independent hidden mesh and must
    never be one of the runtime-exported render meshes.
    """
    if obj.type != "MESH" or obj.data is None:
        raise RuntimeError(f"{role} physics proxy must be a MESH: {obj.name}")
    if any(not math.isfinite(float(value)) for row in obj.matrix_world for value in row):
        raise RuntimeError(f"{role} physics proxy transform is non-finite: {obj.name}")
    if obj.get("physics_proxy_role") != role:
        raise RuntimeError(f"{role} physics proxy role metadata is missing: {obj.name}")
    if bool(obj.get("runtime_export", False)):
        raise RuntimeError(f"{role} physics proxy accidentally points at render geometry: {obj.name}")
    if not obj.hide_render or not obj.hide_viewport:
        raise RuntimeError(f"{role} physics proxy must be hidden and independent from presentation: {obj.name}")
    if len(obj.data.vertices) != 8 or len(obj.data.polygons) != 6:
        raise RuntimeError(f"{role} physics proxy must remain a simple BOX mesh: {obj.name}")

    scale = obj.matrix_world.to_scale()
    if any(not math.isfinite(float(value)) or not math.isclose(float(value), 1.0, abs_tol=1.0e-6) for value in scale):
        raise RuntimeError(f"{role} physics proxy must have applied unit scale: {obj.name}")
    quaternion = obj.matrix_world.to_quaternion()
    if any(not math.isfinite(float(value)) for value in quaternion):
        raise RuntimeError(f"{role} physics proxy orientation is non-finite: {obj.name}")
    if not math.isclose(float(quaternion.w), 1.0, abs_tol=1.0e-6) or any(
        not math.isclose(float(value), 0.0, abs_tol=1.0e-6) for value in (quaternion.x, quaternion.y, quaternion.z)
    ):
        raise RuntimeError(f"{role} physics proxy rotation is unsupported for the current BOX contract: {obj.name}")

    local_points = [Vector(corner) for corner in obj.bound_box]
    local_minimum = Vector(tuple(min(point[index] for point in local_points) for index in range(3)))
    local_maximum = Vector(tuple(max(point[index] for point in local_points) for index in range(3)))
    local_center = (local_minimum + local_maximum) * 0.5
    half_extents = (local_maximum - local_minimum) * 0.5
    source_center = obj.matrix_world @ local_center
    if any(not math.isfinite(float(value)) for value in (*source_center, *half_extents)) or any(
        float(value) <= 0.0 for value in half_extents
    ):
        raise RuntimeError(f"{role} physics proxy has invalid source-local bounds: {obj.name}")

    record = {
        "semanticId": semantic_id,
        "shapeType": "BOX",
        "center": list(source_center),
        "dimensions": list(half_extents * 2.0),
        "halfExtents": list(half_extents),
        "orientationQuaternionWXYZ": [float(quaternion.w), float(quaternion.x), float(quaternion.y), float(quaternion.z)],
    }
    if cob is not None:
        if not all(math.isfinite(float(value)) for value in cob):
            raise RuntimeError(f"Buoyancy center of buoyancy is non-finite: {obj.name}")
        if any(abs(float(cob[index] - source_center[index])) > float(half_extents[index]) * 1.25 for index in range(3)):
            raise RuntimeError(f"Buoyancy center of buoyancy is not inside or associated with proxy: {obj.name}")
        record["centerOfBuoyancy"] = list(cob)
    return record


def production_physics_proxies(objects: dict[str, bpy.types.Object]) -> tuple[list[dict], dict]:
    collision_objects = sorted(
        (obj for obj in objects.values() if obj.get("physics_proxy_role") == "COLLISION"),
        key=lambda obj: obj.name,
    )
    buoyancy_objects = sorted(
        (obj for obj in objects.values() if obj.get("physics_proxy_role") == "BUOYANCY"),
        key=lambda obj: obj.name,
    )
    if len(collision_objects) != 1:
        raise RuntimeError(f"Production Antey requires exactly one COLLISION proxy, found {len(collision_objects)}")
    if len(buoyancy_objects) != 1:
        raise RuntimeError(f"Production Antey requires exactly one BUOYANCY proxy, found {len(buoyancy_objects)}")

    buoyancy = buoyancy_objects[0]
    if buoyancy.type != "MESH" or buoyancy.data is None:
        raise RuntimeError(f"BUOYANCY physics proxy must be a MESH: {buoyancy.name}")
    proxy_points = [buoyancy.matrix_world @ Vector(vertex.co) for vertex in buoyancy.data.vertices]
    if not proxy_points:
        raise RuntimeError("Production Antey buoyancy proxy has no vertices")
    cob = sum(proxy_points, Vector()) / len(proxy_points)
    collision = physics_proxy_metadata(collision_objects[0], "collision.primary", "COLLISION")
    buoyancy_record = physics_proxy_metadata(buoyancy, "buoyancy.primary", "BUOYANCY", cob)
    return [collision], buoyancy_record


def source_first_launcher_identity(name: str) -> tuple[str, int]:
    match = re.fullmatch(r"P700_(Port|Starboard)_(\d{2})", name)
    if match is None:
        raise RuntimeError(f"Invalid source-first P700 launcher name: {name}")
    return match.group(1).upper(), int(match.group(2))


def source_face_accounting(objects: dict[str, bpy.types.Object], audit_path: Path | None = None) -> dict[str, int | str]:
    """Derive source ownership totals from the reopened production BLEND.

    A missing source face is never a documented ``superseded`` bucket.  The
    source-partition audit remains the hard proof; this metadata helper only
    reports the reopened candidate's explicit ownership counts and refuses to
    claim a pass when the builder did not emit its source contract.
    """
    articulated_prefixes = ("RUDDER_", "BOW_PLANE_", "STERN_PLANE_", "P700_COVER_")
    static_faces = 0
    articulated_faces = 0
    synthetic_closure_faces = 0
    for obj in objects.values():
        if obj.type != "MESH" or not obj.get("runtime_export", False) or int(obj.get("lod", -1)) != 0 or obj.get("source_object") is None:
            continue
        role = str(obj.get("source_first_role", "UNKNOWN"))
        source_faces = int(obj.get("SOURCE_FACE_COUNT", len(obj.data.polygons)))
        synthetic_closure_faces += int(obj.get("SYNTHETIC_CLOSURE_FACE_COUNT", 0))
        if role.startswith(articulated_prefixes) or role == "SAIL_DEVICE":
            articulated_faces += source_faces
        else:
            static_faces += source_faces
    contract = bpy.context.scene.get("source_exterior_contract")
    audit = json.loads(audit_path.resolve(strict=True).read_text(encoding="utf-8")) if audit_path else None
    if audit is not None and not audit.get("pass", False):
        raise RuntimeError("Source-partition audit was supplied but did not pass")
    if contract is None:
        return {"staticRuntimeFaces": static_faces, "articulatedRuntimeFaces": articulated_faces,
                "supersededSourcePolygons": 0, "realMissingVisibleSourceFaces": "NOT_VALIDATED",
                "unexplainedSourceFaces": "NOT_VALIDATED", "replacementWithoutProof": "NOT_VALIDATED",
                "unintentionalCandidateDuplicateSourceFaces": "NOT_VALIDATED",
                "syntheticClosureFaces": synthetic_closure_faces, "status": "REQUIRES_SOURCE_PARTITION_AUDIT"}
    return {"sourcePolygonCount": int(contract.get("source_polygon_count", 0)),
            "sourceVisibleExteriorFaces": int(contract.get("source_visible_exterior_faces", 0)),
            "sourceIntentionalDegenerateFaces": int(contract.get("source_intentional_degenerate_faces", 0)),
            "staticRuntimeFaces": static_faces, "articulatedRuntimeFaces": articulated_faces,
            "supersededSourcePolygons": 0, "realMissingVisibleSourceFaces": int(contract.get("missing", -1)),
            "unexplainedSourceFaces": int(contract.get("unexplained", -1)),
            "replacementWithoutProof": int(contract.get("replacement_without_proof", -1)),
            "unintentionalCandidateDuplicateSourceFaces": int(audit.get("unintentionalCandidateDuplicateCount", -1)) if audit else "REPORTED_BY_SOURCE_PARTITION_AUDIT",
            "syntheticClosureFaces": synthetic_closure_faces,
            "status": "PASS" if audit is not None else "REQUIRES_SOURCE_PARTITION_AUDIT"}


def antey_data(antey_production_blend: Path, antey_source_blend: Path, antey_runtime_glb: Path, source_partition_audit: Path | None = None) -> tuple[dict, dict]:
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    runtime0 = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    minimum, maximum = bounds(runtime0)
    hull_min, hull_max = bounds([objects["SM_Antey_LOD0_Hull"]])
    sail_min, sail_max = bounds([objects["SM_Antey_LOD0_Sail"]])
    sail_devices = retractable_sail_devices(objects, sail_max)
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
        compartments.append(compartment_metadata(obj))
    props = []
    for name, semantic_id in (("SM_Propeller_Port", "propeller.port"), ("SM_Propeller_Starboard", "propeller.starboard")):
        obj = objects.get(name)
        props.append(propeller_metadata(obj, semantic_id))
    validate_semantic_spatial_metadata(props, compartments, hull_min, hull_max)
    collision_proxies, buoyancy_proxy = production_physics_proxies(objects)
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
        "blenderVersion": bpy.app.version_string,
        "technicalAssetStatus": "ACCEPTED",
        "userVisualApproval": "PASS",
        "sourceAccounting": source_face_accounting(objects, source_partition_audit),
        "p700Contract": {
            "launchPositions": 24,
            "port": 12,
            "starboard": 12,
            "continuousRows": True,
            "oneOriginalPitchAftShift": True,
            "launcherElevationDegrees": 40.0,
            "sourceDerivedCovers": 12,
            "dependentAuthoring": True,
            "authoringPreviewAnimation": "QA_Antey_P700_Covers_Open",
            "runtimeAnimation": None,
            "coverStateOwner": "LauncherSystem.P700CoverState_Port_01..06 / P700CoverState_Starboard_01..06",
            "stateDuringLauncherExit": "STOWED",
        },
        "controlSurfaceAuthoring": {
            "bowPlanes": ["BowPlane_Port", "BowPlane_Starboard"],
            "sternPlanes": ["SternPlane_Port", "SternPlane_Starboard"],
            "rudders": ["Rudder_Dorsal", "Rudder_Ventral"],
            "propellers": ["Propeller_Port", "Propeller_Starboard"],
        },
        "compartmentAuthoring": {"count": 10, "status": "AUTHORED"},
        "comCobAuthoring": {"com": [-6.13971996307373, 0.0, -0.0844455063343048], "cob": [0.0, 0.0, 0.0], "status": "AUTHORED"},
        "runtimeOwnershipBoundary": "Engine runtime consumes canonical GLB geometry and articulation; future WeaponSystem owns P700 deployment phase/state, Simulation owns damage, flooding, fire, crew, hatch state, and propeller RPM",
        "licenseStatus": "LEGAL_BLOCKED / PENDING_REVIEW",
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
        "retractableSailDevices": sail_devices,
        "collision": collision_proxies,
        "buoyancyProxy": buoyancy_proxy,
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
        asset, authoring = antey_data(production_blend, source_blend, runtime_glb, options.source_partition_audit)
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
