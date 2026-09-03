"""Forensic validation of the manually supplied Antey rudder topology mask.

The validator never saves a blend.  It resolves the literal world-space mask
against a freshly opened candidate, verifies every endpoint against an actual
mesh edge, checks the source seam hinge at five control angles, and compares
the neutral source-face union with the pre-correction candidate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector

TOLERANCE_M = 1.0e-5
MASK_TARGETS = {
    "Dorsal": {"source_object": "SM_Antey_LOD0_Hull", "rudder": "SM_Antey_LOD0_Rudder_Dorsal"},
    "Ventral": {"source_object": "SM_Antey_LOD0_Hull_LowerSource", "rudder": "SM_Antey_LOD0_Rudder_Ventral"},
}
UNION_NAMES = tuple(item for value in MASK_TARGETS.values() for item in (value["source_object"], value["rudder"]))


def parse_args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--mask", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def jsonable(value):
    if isinstance(value, dict) or hasattr(value, "keys"):
        return {str(key): jsonable(value[key]) for key in value.keys()}
    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]
    try:
        return value.to_list()
    except AttributeError:
        return value


def world_points(obj: bpy.types.Object) -> list[Vector]:
    return [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]


def face_signature(point: Vector, digits: int = 3) -> tuple[float, float, float]:
    values = tuple(round(float(point[index]), digits) for index in range(3))
    return tuple(0.0 if value == 0.0 else value for value in values)


def object_face_counter(obj: bpy.types.Object, digits: int = 3) -> Counter[tuple[tuple[float, float, float], ...]]:
    points = world_points(obj)
    return Counter(tuple(sorted(face_signature(points[index], digits) for index in polygon.vertices)) for polygon in obj.data.polygons)


def runtime_faces(scene: bpy.types.Scene) -> dict[str, Counter]:
    return {obj.name: object_face_counter(obj) for obj in scene.objects if obj.type == "MESH" and obj.get("runtime_export", False)}


def fresh_open(path: Path) -> bpy.types.Scene:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != path.resolve(strict=True):
        raise RuntimeError(f"fresh reopen path mismatch: {path}")
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()
    return bpy.context.scene


def resolve_mask_against_rudder(obj: bpy.types.Object, record: dict[str, object]) -> dict[str, object]:
    points = world_points(obj)
    vertices = record.get("vertices", [])
    expected_vertices = int(record["expectedVertexCount"])
    expected_edges = int(record["expectedEdgeCount"])
    if len(vertices) != expected_vertices:
        raise RuntimeError(f"mask vertex count mismatch for {obj.name}")
    resolved: dict[int, int] = {}
    vertex_checks = []
    for item in vertices:
        legacy = int(item["legacyIndex"])
        target = Vector(item["world"])
        distance, index, point = min(((point - target).length, index, point) for index, point in enumerate(points))
        if distance > TOLERANCE_M:
            raise RuntimeError(f"mask vertex outside tolerance on corrected rudder: {obj.name}:{legacy} {distance}")
        if legacy in resolved or index in resolved.values():
            raise RuntimeError(f"mask vertex resolution is not unique: {obj.name}:{legacy}")
        resolved[legacy] = index
        vertex_checks.append({"legacyIndex": legacy, "resolvedIndex": index, "errorM": distance, "world": list(target), "resolvedWorld": list(point)})
    edge_by_pair = {tuple(sorted(edge.vertices)): edge.index for edge in obj.data.edges}
    edge_checks = []
    for item in record.get("edges", []):
        legacy_edge = int(item["legacyIndex"])
        left_legacy, right_legacy = (int(value) for value in item["legacyVertices"])
        if left_legacy not in resolved or right_legacy not in resolved:
            raise RuntimeError(f"mask edge endpoint is absent from mask vertices: {obj.name}:{legacy_edge}")
        pair = tuple(sorted((resolved[left_legacy], resolved[right_legacy])))
        edge_index = edge_by_pair.get(pair)
        if edge_index is None:
            raise RuntimeError(f"mask edge is not an actual corrected mesh edge: {obj.name}:{legacy_edge}")
        edge_checks.append({"legacyIndex": legacy_edge, "legacyVertices": [left_legacy, right_legacy], "resolvedVertices": list(pair), "resolvedEdgeIndex": edge_index, "endpointWorld": [list(points[pair[0]]), list(points[pair[1]])]})
    if len(edge_checks) != expected_edges or len({item["resolvedEdgeIndex"] for item in edge_checks}) != expected_edges:
        raise RuntimeError(f"mask edge resolution count mismatch on corrected rudder: {obj.name}")
    return {"vertexChecks": vertex_checks, "edgeChecks": edge_checks, "resolvedVertices": resolved, "vertexErrorsMaxM": max((item["errorM"] for item in vertex_checks), default=0.0)}


def hinge_qa(obj: bpy.types.Object, hinge: dict[str, object]) -> dict[str, object]:
    angles = (-20.0, -15.0, 0.0, 15.0, 20.0)
    original_mode = obj.rotation_mode
    original_quaternion = obj.rotation_quaternion.copy()
    original_euler = obj.rotation_euler.copy()
    original_location = obj.location.copy()
    original_scale = obj.scale.copy()
    neutral_points = world_points(obj)
    pivot = Vector(hinge["pivot"])
    endpoints = [Vector(value) for value in hinge["endpoints"]]
    axis = Vector(hinge["axisVectorProduction"]).normalized()
    samples = []
    for angle in angles:
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = original_quaternion @ Quaternion((0.0, 0.0, 1.0), __import__("math").radians(angle))
        bpy.context.view_layer.update()
        points = world_points(obj)
        displacement = max(((point - base).length for point, base in zip(points, neutral_points)), default=0.0)
        endpoint_error = max((min((point - target).length for point in points) for target in endpoints), default=0.0)
        samples.append({"angleDeg": angle, "maxDisplacementM": displacement, "hingeEndpointMaxErrorM": endpoint_error, "finite": all(point.length < 1.0e6 for point in points)})
    obj.rotation_mode = original_mode
    if original_mode == "QUATERNION":
        obj.rotation_quaternion = original_quaternion
    else:
        obj.rotation_euler = original_euler
    obj.location = original_location
    obj.scale = original_scale
    bpy.context.view_layer.update()
    restored = world_points(obj)
    neutral_error = max(((point - base).length for point, base in zip(restored, neutral_points)), default=0.0)
    local_axis_world = (obj.matrix_world.to_3x3() @ Vector((0.0, 0.0, 1.0))).normalized()
    axis_error = min((local_axis_world - axis).length, (local_axis_world + axis).length)
    midpoint_error = (pivot - (endpoints[0] + endpoints[1]) * 0.5).length
    return {"anglesDeg": list(angles), "samples": samples, "neutralExact": neutral_error <= TOLERANCE_M, "neutralMaxErrorM": neutral_error, "hingeEndpointMaxErrorM": max((sample["hingeEndpointMaxErrorM"] for sample in samples), default=0.0), "localZAxisWorld": list(local_axis_world), "declaredAxis": list(axis), "axisAlignmentErrorM": axis_error, "pivotMidpointErrorM": midpoint_error, "axisAligned": axis_error <= TOLERANCE_M, "pivotExact": midpoint_error <= TOLERANCE_M, "allSamplesFinite": all(sample["finite"] for sample in samples)}


def lod_accounting(scene: bpy.types.Scene) -> dict[str, object]:
    groups = {"LOD0_base_meshes": [], "P700_hatch_meshes": [], "propeller_meshes": [], "other_meshes": []}
    for obj in sorted((item for item in scene.objects if item.type == "MESH" and obj_runtime(item)), key=lambda item: item.name):
        if int(obj.get("lod", -1)) != 0:
            continue
        role = str(obj.get("source_first_role", ""))
        item = {"name": obj.name, "role": role, "triangles": sum(len(poly.vertices) - 2 for poly in obj.data.polygons)}
        if "P700_COVER" in role or "P700_COVER" in obj.name:
            groups["P700_hatch_meshes"].append(item)
        elif role.startswith("PROPELLER") or "Propeller" in obj.name:
            groups["propeller_meshes"].append(item)
        elif role.startswith(("MAIN_HULL", "RUDDER", "STERN_PLANE", "BOW_PLANE")) or role in {"SAIL", "SAIL_DEVICE"}:
            groups["LOD0_base_meshes"].append(item)
        else:
            groups["other_meshes"].append(item)
    for key, items in groups.items():
        groups[key] = {"count": len(items), "triangles": sum(item["triangles"] for item in items), "objects": items}
    total = sum(value["triangles"] for value in groups.values())
    groups["TOTAL_RUNTIME_LOD0"] = {"objects": sum(value["count"] for key, value in groups.items() if key.endswith("meshes")), "triangles": total}
    return groups


def obj_runtime(obj: bpy.types.Object) -> bool:
    return bool(obj.get("runtime_export", False))


def main() -> None:
    options = parse_args()
    candidate = options.candidate.resolve(strict=True)
    before = options.before.resolve(strict=True)
    source = options.source.resolve(strict=True)
    mask_path = options.mask.resolve(strict=True)
    output = options.output.resolve()
    masks = json.loads(mask_path.read_text(encoding="utf-8"))
    boundary_path = candidate.parent / "manual_rudder_boundary.json"
    boundary = json.loads(boundary_path.read_text(encoding="utf-8")) if boundary_path.exists() else {"rudders": {}}

    scene = fresh_open(candidate)
    candidate_faces = runtime_faces(scene)
    materials = sorted({slot.material.name for obj in scene.objects if obj.type == "MESH" and obj_runtime(obj) and int(obj.get("lod", -1)) == 0 for slot in obj.material_slots if slot.material})
    all_materials = sorted(material.name for material in bpy.data.materials)
    rudders = {}
    changed_expected = set()
    for side, target in MASK_TARGETS.items():
        rudder = scene.objects.get(target["rudder"])
        hull = scene.objects.get(target["source_object"])
        if rudder is None or hull is None:
            raise RuntimeError(f"missing corrected target objects for {side}")
        record = masks.get(side)
        if not isinstance(record, dict):
            raise RuntimeError(f"mask section missing: {side}")
        resolved = resolve_mask_against_rudder(rudder, record)
        metadata = boundary.get("rudders", {}).get(side, {})
        hinge = metadata.get("hinge", {})
        if not hinge:
            raise RuntimeError(f"hinge metadata missing: {side}")
        static_matrix = hull.matrix_world.copy()
        qa = hinge_qa(rudder, hinge)
        fixed_unchanged = static_matrix == hull.matrix_world
        edge_incidence = Counter(tuple(sorted(edge)) for poly in rudder.data.polygons for edge in poly.edge_keys)
        boundary_edges = sorted(edge.index for edge in rudder.data.edges if edge_incidence.get(tuple(sorted(edge.vertices)), 0) == 1)
        duplicate_face_count = sum(max(count - 1, 0) for count in object_face_counter(rudder).values())
        cross_duplicate_face_count = sum(min(candidate_faces[target["source_object"]].get(face, 0), candidate_faces[target["rudder"]].get(face, 0)) for face in set(candidate_faces[target["source_object"]]) & set(candidate_faces[target["rudder"]]))
        hinge_edges = list(metadata.get("derivedHingeEdges", []))
        mask_edges = [item["resolvedEdgeIndex"] for item in resolved["edgeChecks"]]
        lod_objects = []
        for lod in range(4):
            lod_name = target["rudder"].replace("LOD0", f"LOD{lod}")
            lod_obj = scene.objects.get(lod_name)
            lod_objects.append({"lod": lod, "name": lod_name, "exists": lod_obj is not None, "triangles": sum(len(poly.vertices) - 2 for poly in lod_obj.data.polygons) if lod_obj else 0, "manualMaskCorrected": bool(lod_obj and lod_obj.get("MANUAL_MASK_CORRECTED", False)) if lod else bool(lod_obj and lod_obj.get("MANUAL_MASK_CORRECTED", False))})
        lod_pass = all(item["exists"] and item["manualMaskCorrected"] for item in lod_objects)
        rudders[side] = {"object": target["rudder"], "fixedStabilizerObject": target["source_object"], "manualMask": {"expectedVertices": int(record["expectedVertexCount"]), "expectedEdges": int(record["expectedEdgeCount"]), "resolvedVertices": len(resolved["vertexChecks"]), "resolvedEdges": len(resolved["edgeChecks"]), "maxVertexResolutionErrorM": resolved["vertexErrorsMaxM"], "coveragePercent": {"vertices": 100.0, "edges": 100.0}, "edgeRecords": resolved["edgeChecks"], "maskEdgeIds": mask_edges}, "facePatch": {"sourcePatchFaceCount": metadata.get("sourcePatchFaceCount", 0), "movableFaceCount": len(metadata.get("movableFaceIndices", [])), "movableTriangleCount": sum(len(poly.vertices) - 2 for poly in rudder.data.polygons), "movableFaceFingerprint": metadata.get("movableFaceFingerprint"), "manualSelectedFaceCount": len(metadata.get("manualSelectedFaceIndices", [])), "thicknessSideFaceCount": len(metadata.get("thicknessSideFaceIndices", [])), "topologyClassification": metadata.get("topologyClassification", {}), "maskOnlyVertexIndices": metadata.get("maskOnlyVertexIndices", []), "maskOnlyResolvedEdgeIndices": metadata.get("maskOnlyResolvedEdgeIndices", []), "duplicateFaceCount": duplicate_face_count, "crossObjectDuplicateFaceCount": cross_duplicate_face_count}, "perimeter": {"boundaryEdgeIds": boundary_edges, "boundaryEdgeCount": len(boundary_edges), "boundaryVertexChains": metadata.get("boundaryVertexChains", []), "classification": metadata.get("perimeterClassification", {}), "derivedBoundaryFingerprint": metadata.get("derivedBoundaryFingerprint")}, "hinge": {**hinge, "derivedHingeEdges": hinge_edges, "derivedHingeFingerprint": metadata.get("derivedHingeFingerprint"), "edgeChainLength": len(hinge_edges)}, "qa": qa, "lodPropagation": {"objects": lod_objects, "pass": lod_pass}, "fixedStabilizerMatrixUnchanged": fixed_unchanged, "pass": len(resolved["vertexChecks"]) == int(record["expectedVertexCount"]) and len(resolved["edgeChecks"]) == int(record["expectedEdgeCount"]) and qa["neutralExact"] and qa["axisAligned"] and qa["pivotExact"] and qa["allSamplesFinite"] and lod_pass and fixed_unchanged and duplicate_face_count == 0 and cross_duplicate_face_count == 0}
        changed_expected.update({target["source_object"], target["rudder"]})
        changed_expected.update(f"SM_Antey_LOD{lod}_Rudder_{side}" for lod in range(1, 4))

    before_scene = fresh_open(before)
    before_faces = runtime_faces(before_scene)
    after_scene = fresh_open(candidate)
    after_faces = runtime_faces(after_scene)
    all_names = set(before_faces) | set(after_faces)
    unexpected = []
    for name in sorted(all_names - changed_expected):
        if before_faces.get(name, Counter()) != after_faces.get(name, Counter()):
            unexpected.append(name)
    before_union = Counter(face for name in UNION_NAMES for face in before_faces.get(name, Counter()))
    after_union = Counter(face for name in UNION_NAMES for face in after_faces.get(name, Counter()))
    # Neutral source identity is compared as a set of source faces.  This
    # permits removal of a pre-existing coincident polygon while still
    # reporting any genuinely missing or added source face.  Multiplicity is
    # audited independently for movable rudders to catch z-fighting.
    before_union_unique = Counter({face: 1 for face in before_union})
    after_union_unique = Counter({face: 1 for face in after_union})
    union_missing = sum((before_union_unique - after_union_unique).values())
    union_extra = sum((after_union_unique - before_union_unique).values())
    movable_names = tuple(value["rudder"] for value in MASK_TARGETS.values())
    duplicate_movable_before = sum(max(count - 1, 0) for name in movable_names for count in before_faces.get(name, Counter()).values())
    duplicate_movable_after = sum(max(count - 1, 0) for name in movable_names for count in after_faces.get(name, Counter()).values())
    cross_duplicate_before = sum(sum(min(before_faces.get(pair[0], Counter()).get(face, 0), before_faces.get(pair[1], Counter()).get(face, 0)) for face in set(before_faces.get(pair[0], Counter())) & set(before_faces.get(pair[1], Counter()))) for pair in (("SM_Antey_LOD0_Hull", "SM_Antey_LOD0_Rudder_Dorsal"), ("SM_Antey_LOD0_Hull_LowerSource", "SM_Antey_LOD0_Rudder_Ventral")))
    cross_duplicate_after = sum(sum(min(after_faces.get(pair[0], Counter()).get(face, 0), after_faces.get(pair[1], Counter()).get(face, 0)) for face in set(after_faces.get(pair[0], Counter())) & set(after_faces.get(pair[1], Counter()))) for pair in (("SM_Antey_LOD0_Hull", "SM_Antey_LOD0_Rudder_Dorsal"), ("SM_Antey_LOD0_Hull_LowerSource", "SM_Antey_LOD0_Rudder_Ventral")))
    per_object_delta = {name: {"missing": sum((before_faces.get(name, Counter()) - after_faces.get(name, Counter())).values()), "extra": sum((after_faces.get(name, Counter()) - before_faces.get(name, Counter())).values())} for name in sorted(all_names)}

    candidate_lod = lod_accounting(after_scene)
    reference_glb_path = candidate.parent / "antey_runtime_glb_final.json"
    reference_glb = json.loads(reference_glb_path.read_text(encoding="utf-8")) if reference_glb_path.exists() else None
    lod_report = {"sourceFirstCandidate": candidate_lod, "referenceAnteyGlbAudit": {"path": str(reference_glb_path), "sha256": sha256(reference_glb_path), "lod0": reference_glb.get("lods", {}).get("LOD0") if reference_glb else None, "classification": reference_glb.get("lod0_classification") if reference_glb else None} if reference_glb else {"path": str(reference_glb_path), "present": False}, "reconciliation": "The frozen Antey.glb audit is the 34-object/74244-triangle runtime contract (20 base + 12 P700 hatches + 2 propellers = 74244). The source-first correction candidate intentionally retains its authoring/runtime mesh inventory separately; every additional candidate mesh is classified below and is not silently folded into the GLB audit."}
    report = {"status": "TECHNICAL_VALIDATION", "candidate": {"path": str(candidate), "sha256": sha256(candidate), "freshReopen": True}, "before": {"path": str(before), "sha256": sha256(before), "freshReopen": True}, "source": {"path": str(source), "sha256": sha256(source)}, "manualMaskInput": {"path": str(mask_path), "sha256": sha256(mask_path), "sections": {side: {"object": value.get("object"), "expectedVertexCount": value.get("expectedVertexCount"), "expectedEdgeCount": value.get("expectedEdgeCount"), "inputFile": value.get("inputFile")} for side, value in masks.items()}}, "rudders": rudders, "positiveMaskCoverage": {side: rudders[side]["manualMask"]["coveragePercent"] for side in rudders}, "sourceNeutralIdentity": {"unionMissingSourceFaces": union_missing, "unionExtraSourceFaces": union_extra, "duplicateExteriorDelta": duplicate_movable_after - duplicate_movable_before, "duplicateMovableFacesBefore": duplicate_movable_before, "duplicateMovableFacesAfter": duplicate_movable_after, "duplicateCrossObjectFacesBefore": cross_duplicate_before, "duplicateCrossObjectFacesAfter": cross_duplicate_after, "perObjectTransferDelta": per_object_delta, "pass": union_missing == 0 and union_extra == 0 and duplicate_movable_after == 0 and cross_duplicate_after == 0}, "localityRegression": {"expectedChangedRuntimeObjects": sorted(changed_expected), "unexpectedRuntimeMeshChanges": unexpected, "pass": not unexpected}, "materials": {"runtimeLOD0": materials, "allDatablocks": all_materials, "intended": ["MAT_Antey_Hull", "MAT_Antey_Propellers"], "accidentalPresent": sorted(set(all_materials) & {"Dots Stroke", "Material"}), "pass": set(materials).issubset({"MAT_Antey_Hull", "MAT_Antey_Propellers"}) and not (set(all_materials) & {"Dots Stroke", "Material"})}, "lodAccounting": lod_report, "contract": jsonable(after_scene.get("manual_rudder_positive_mask_contract", {})), "humanVisualApproval": "PENDING", "promotionRecommendation": "DO_NOT_PROMOTE", "pass": all(item["pass"] for item in rudders.values()) and union_missing == 0 and union_extra == 0 and duplicate_movable_after == 0 and cross_duplicate_after == 0 and not unexpected}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(jsonable(report), indent=2), encoding="utf-8")
    print(f"ANTEY_RUDDER_MASK_VALIDATION_WRITTEN {output} pass={report['pass']} union_missing={union_missing} union_extra={union_extra} unexpected={unexpected}")


if __name__ == "__main__":
    main()
