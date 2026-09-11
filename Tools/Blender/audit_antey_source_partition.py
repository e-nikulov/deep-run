"""Read-only source-polygon accounting audit for the accepted Antey candidate.

The prior validator compares polygon signatures after object partitioning.  This
tool preserves original evaluated polygon ids and correlates any discrepancy
with the authoring provenance ledger; it never saves a BLEND file.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


TOLERANCE = 1.0e-5
SOURCE_CENTER_X = (-4.875378131866455 + 4.900454044342041) * 0.5
SOURCE_CENTER_Y = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SOURCE_SCALE_X = 154.0 / 9.775832176208496
SOURCE_SCALE_YZ = 18.2 / 1.7063244581222534


def options() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--provenance", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def production(point: Vector) -> Vector:
    return Vector(((point.y - SOURCE_CENTER_X) * SOURCE_SCALE_X,
                   -(point.x - SOURCE_CENTER_Y) * SOURCE_SCALE_YZ,
                   point.z * SOURCE_SCALE_YZ))


def quant(point: Vector) -> tuple[float, float, float]:
    # Exactly reproduce the pre-existing validator's comparison key first.
    # The finer-grained geometry is retained separately in ledger vertices.
    return tuple(0.0 if (value := round(float(point[index]), 3)) == 0.0 else value for index in range(3))


def polygon_fingerprint(points: list[Vector]) -> str:
    text = repr(tuple(sorted(quant(point) for point in points)))
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def triangulated_fingerprints(points: list[Vector]) -> list[str]:
    if len(points) < 3:
        return []
    return [polygon_fingerprint([points[0], points[index], points[index + 1]]) for index in range(1, len(points) - 1)]


def area_normal(points: list[Vector]) -> tuple[float, Vector]:
    area = 0.0
    normal = Vector()
    for index in range(1, len(points) - 1):
        cross = (points[index] - points[0]).cross(points[index + 1] - points[0])
        area += cross.length * 0.5
        normal += cross
    return area, normal.normalized() if normal.length > 1.0e-12 else Vector()


def connected_components(mesh: bpy.types.Mesh) -> list[int]:
    parent = list(range(len(mesh.polygons)))
    edge_faces: dict[tuple[int, int], list[int]] = defaultdict(list)
    for polygon in mesh.polygons:
        vertices = list(polygon.vertices)
        for index, vertex in enumerate(vertices):
            edge_faces[tuple(sorted((vertex, vertices[(index + 1) % len(vertices)])))].append(polygon.index)
    def find(index: int) -> int:
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index
    def join(left: int, right: int) -> None:
        left, right = find(left), find(right)
        if left != right:
            parent[right] = left
    for faces in edge_faces.values():
        for face in faces[1:]:
            join(faces[0], face)
    roots: dict[int, list[int]] = defaultdict(list)
    for index in range(len(mesh.polygons)):
        roots[find(index)].append(index)
    ordered = sorted(roots.values(), key=lambda faces: (-sum(len(mesh.polygons[index].vertices) - 2 for index in faces), min(faces)))
    result = [0] * len(mesh.polygons)
    for component, faces in enumerate(ordered):
        for face in faces:
            result[face] = component
    return result


def bounds(points: list[Vector]) -> list[list[float]]:
    return [[min(point[index] for point in points) for index in range(3)], [max(point[index] for point in points) for index in range(3)]]


def region(centroid: Vector) -> str:
    x, y, z = centroid
    if x > 55:
        return "BOW"
    if x < -68:
        return "PROPELLER"
    if x < -52:
        return "AFT_BODY"
    if abs(y) > 5.2 and 5 < x < 55:
        return "P700_COVER_BANK"
    if 5 < x < 40 and z > 4.0:
        return "SAIL"
    return "MAIN_HULL"


def source_ledger(path: Path) -> tuple[list[dict], Counter[str]]:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve()))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    records: list[dict] = []
    signatures: Counter[str] = Counter()
    for object_name in ("Bridge", "Hull"):
        obj = bpy.data.objects[object_name]
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        components = connected_components(mesh)
        world = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
        points = [production(point) for point in world]
        for polygon in mesh.polygons:
            face = [points[index] for index in polygon.vertices]
            area, normal = area_normal(face)
            fingerprint = polygon_fingerprint(face)
            material = mesh.materials[polygon.material_index].name if polygon.material_index < len(mesh.materials) and mesh.materials[polygon.material_index] else None
            record = {"sourceObject": object_name, "sourcePolygonIndex": polygon.index, "sourceMaterial": material,
                      "sourceVertexIndices": list(polygon.vertices), "vertexCount": len(polygon.vertices),
                      "worldVertices": [list(world[index]) for index in polygon.vertices], "productionVertices": [list(point) for point in face],
                      "area": area, "centroid": list(sum(face, Vector()) / len(face)), "normal": list(normal),
                      "connectedComponent": components[polygon.index], "degenerate": area <= TOLERANCE * TOLERANCE,
                      "geometryFingerprint": fingerprint, "triangulatedSubFingerprints": triangulated_fingerprints(face),
                      "bounds": bounds(face), "region": region(sum(face, Vector()) / len(face))}
            records.append(record); signatures[fingerprint] += 1
        evaluated.to_mesh_clear()
    return records, signatures


def neutral_candidate_points(obj: bpy.types.Object) -> list[Vector]:
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    if obj.name.startswith("SM_Antey_P700_Cover_"):
        profile = obj.get("CLEARANCE_LIFT_PROFILE_M", [])
        if profile:
            sign = 1.0 if "_Port_" in obj.name else -1.0
            points = [point + Vector((0.0, -sign * float(profile[0]), 0.0)) for point in points]
    return points


def candidate_ledger(path: Path) -> tuple[list[dict], Counter[str]]:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve()))
    scene = bpy.context.scene; scene.frame_set(1)
    records: list[dict] = []
    signatures: Counter[str] = Counter()
    for obj in scene.objects:
        if obj.type != "MESH" or int(obj.get("lod", -1)) != 0 or not obj.get("runtime_export", False):
            continue
        if obj.get("synthetic_closure", False):
            continue
        source_name, source_component = obj.get("source_object"), obj.get("source_component")
        if source_name is None or source_component is None:
            continue
        points = neutral_candidate_points(obj)
        for polygon in obj.data.polygons:
            face = [points[index] for index in polygon.vertices]
            area, normal = area_normal(face)
            fingerprint = polygon_fingerprint(face)
            record = {"candidateObject": obj.name, "candidatePolygonIndex": polygon.index, "productionVertices": [list(point) for point in face],
                      "area": area, "centroid": list(sum(face, Vector()) / len(face)), "normal": list(normal),
                      "geometryFingerprint": fingerprint, "triangulatedSubFingerprints": triangulated_fingerprints(face),
                      "provenance": {"sourceObject": source_name, "sourceComponent": source_component},
                      "runtime_export": bool(obj.get("runtime_export", False)), "lod": int(obj.get("lod", -1)), "bounds": bounds(face)}
            records.append(record); signatures[fingerprint] += 1
    return records, signatures


def provenance_map(path: Path) -> dict[tuple[str, int], list[str]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    result: dict[tuple[str, int], list[str]] = defaultdict(list)
    for record in payload.get("records", []):
        target = record.get("productionObject")
        source = record.get("sourceObject")
        if not target or not source:
            continue
        # P700 records expose faceIndices; the manual rudder records use the
        # semantically equivalent movableFaceIndices field.
        for index in [*record.get("faceIndices", []), *record.get("movableFaceIndices", [])]:
            result[(source, int(index))].append(target)
    return result


def candidate_names(path: Path) -> set[str]:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve()))
    return set(bpy.data.objects.keys())


def candidate_surface_bvh(path: Path) -> tuple[BVHTree, list[str]]:
    """Build a source-derived LOD0 triangle surface only; no proxy is used."""
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve()))
    vertices: list[Vector] = []
    triangles: list[tuple[int, int, int]] = []
    triangle_objects: list[str] = []
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH" or int(obj.get("lod", -1)) != 0 or not obj.get("runtime_export", False):
            continue
        if obj.get("synthetic_closure", False) or obj.get("source_object") is None:
            continue
        points = neutral_candidate_points(obj)
        for polygon in obj.data.polygons:
            indices = list(polygon.vertices)
            for index in range(1, len(indices) - 1):
                start = len(vertices)
                vertices.extend((points[indices[0]], points[indices[index]], points[indices[index + 1]]))
                triangles.append((start, start + 1, start + 2)); triangle_objects.append(obj.name)
    return BVHTree.FromPolygons(vertices, triangles, all_triangles=True), triangle_objects


def surface_match(record: dict, bvh: BVHTree, triangle_objects: list[str]) -> list[str]:
    points = [Vector(point) for point in record["productionVertices"]]
    centroid = sum(points, Vector()) / len(points)
    samples = [*points, centroid]
    samples.extend((points[index] + points[(index + 1) % len(points)]) * 0.5 for index in range(len(points)))
    normal = Vector(record["normal"])
    targets: set[str] = set()
    for sample in samples:
        nearest = bvh.find_nearest(sample)
        if nearest is None:
            return []
        _, hit_normal, triangle_index, distance = nearest
        if distance > TOLERANCE or (normal.length > 0.0 and abs(normal.dot(hit_normal.normalized())) < 0.98):
            return []
        targets.add(triangle_objects[triangle_index])
    return sorted(targets)


def audit_material(name: str, color: tuple[float, float, float, float]) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = color
    return material


def mesh_from_faces(name: str, faces: list[list[Vector]], material: bpy.types.Material) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(name + "_Mesh")
    vertices: list[Vector] = []; polygons: list[list[int]] = []
    for face in faces:
        offset = len(vertices); vertices.extend(face); polygons.append(list(range(offset, offset + len(face))))
    mesh.from_pydata(vertices, [], polygons); mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh); bpy.context.scene.collection.objects.link(obj)
    return obj


def render(path: Path, missing: list[dict], output: Path, candidate: bool = False) -> None:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve()))
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"; scene.render.resolution_x = 1600; scene.render.resolution_y = 720; scene.render.resolution_percentage = 100
    scene.display.shading.light = "STUDIO"; scene.display.shading.color_type = "MATERIAL"; scene.world.color = (0.1, 0.1, 0.1)
    gray = audit_material("AUDIT_GRAY", (0.32, 0.32, 0.32, 1.0))
    bright = audit_material("AUDIT_UNACCOUNTED", (1.0, 0.08, 0.02, 1.0))
    for obj in scene.objects:
        obj.hide_render = obj.type == "MESH"
    if candidate:
        for obj in scene.objects:
            if obj.type == "MESH" and int(obj.get("lod", -1)) == 0 and obj.get("runtime_export", False):
                obj.hide_render = False
                obj.data.materials.clear(); obj.data.materials.append(gray)
    else:
        depsgraph = bpy.context.evaluated_depsgraph_get()
        source_faces: list[list[Vector]] = []
        for name in ("Bridge", "Hull"):
            obj = bpy.data.objects[name]; evaluated = obj.evaluated_get(depsgraph); mesh = evaluated.to_mesh()
            points = [production(obj.matrix_world @ vertex.co) for vertex in mesh.vertices]
            source_faces.extend([[points[index] for index in polygon.vertices] for polygon in mesh.polygons])
            evaluated.to_mesh_clear()
        mesh_from_faces("AUDIT_SOURCE_BASE", source_faces, gray)
    # Offset toward the audit camera only, avoiding z-fighting with the gray
    # source/candidate shell while leaving the ledgers in exact coordinates.
    mesh_from_faces("AUDIT_ALLEGED_FACES", [[Vector(point) + Vector((0.0, -0.025, 0.0)) for point in item["productionVertices"]] for item in missing], bright)
    camera = bpy.data.cameras.new("AUDIT_CAMERA"); camera_obj = bpy.data.objects.new("AUDIT_CAMERA", camera); scene.collection.objects.link(camera_obj)
    camera.type = "ORTHO"; camera.ortho_scale = 175.0; camera_obj.location = (0.0, -180.0, 8.0)
    camera_obj.rotation_euler = (Vector((0.0, 0.0, 2.5)) - camera_obj.location).to_track_quat("-Z", "Y").to_euler(); scene.camera = camera_obj
    scene.render.filepath = str(output); bpy.ops.render.render(write_still=True)


def main() -> None:
    opts = options(); opts.output = opts.output.resolve(); opts.output.mkdir(parents=True, exist_ok=True)
    candidate_hash_before, source_hash_before = sha256(opts.candidate), sha256(opts.source)
    source_records, source_signatures = source_ledger(opts.source)
    candidate_records, candidate_signatures = candidate_ledger(opts.candidate)
    missing_counts = source_signatures - candidate_signatures
    mapping = provenance_map(opts.provenance); names = candidate_names(opts.candidate)
    bvh, triangle_objects = candidate_surface_bvh(opts.candidate)
    missing: list[dict] = []
    remaining = Counter(missing_counts)
    for record in source_records:
        fingerprint = record["geometryFingerprint"]
        if remaining[fingerprint] <= 0:
            continue
        remaining[fingerprint] -= 1
        targets = [name for name in mapping.get((record["sourceObject"], record["sourcePolygonIndex"]), []) if name in names]
        geometric_targets = surface_match(record, bvh, triangle_objects)
        if targets:
            category = "PRESENT_IN_ARTICULATED_RUNTIME_MESH" if any("P700_Cover" in name or "Rudder" in name or "BowPlane" in name or "SternPlane" in name for name in targets) else "PRESENT_IN_STATIC_RUNTIME_MESH"
            evidence = "provenance.faceIndices exact source polygon id -> " + ", ".join(sorted(set(targets)))
            replacement = []; approval_lineage = None
        elif geometric_targets:
            targets = geometric_targets
            category = "PRESENT_IN_ARTICULATED_RUNTIME_MESH" if any("P700_Cover" in name or "Rudder" in name or "BowPlane" in name or "SternPlane" in name or "SailDevice" in name for name in targets) else "PRESENT_IN_STATIC_RUNTIME_MESH"
            evidence = "all source vertices, edge midpoints, and centroid lie on source-derived candidate triangles within 1e-5 m -> " + ", ".join(targets)
            replacement = []; approval_lineage = None
        else:
            category = "UNEXPLAINED_SOURCE_FACE"; evidence = "no exact provenance face-index mapping"; replacement = []; approval_lineage = None
        missing.append({**record, "category": category, "evidence": evidence, "provenanceTargets": sorted(set(targets)), "replacementGeometry": replacement, "approvalLineage": approval_lineage})
    missing_ids = {(item["sourceObject"], item["sourcePolygonIndex"]) for item in missing}
    candidate_by_fingerprint: dict[str, list[dict]] = defaultdict(list)
    for record in candidate_records:
        candidate_by_fingerprint[record["geometryFingerprint"]].append(record)
    complete_categories: Counter[str] = Counter()
    for record in source_records:
        key = (record["sourceObject"], record["sourcePolygonIndex"])
        if key in missing_ids:
            complete_categories["UNEXPLAINED_SOURCE_FACE"] += 1
            continue
        targets = candidate_by_fingerprint.get(record["geometryFingerprint"], [])
        if any(any(token in target["candidateObject"] for token in ("Rudder", "BowPlane", "SternPlane", "P700_Cover", "SailDevice")) for target in targets):
            complete_categories["PRESENT_IN_ARTICULATED_RUNTIME_MESH"] += 1
        elif targets:
            complete_categories["PRESENT_IN_STATIC_RUNTIME_MESH"] += 1
        else:
            complete_categories["UNEXPLAINED_SOURCE_FACE"] += 1
    groups: dict[tuple[str, int, str], dict] = {}
    classes: dict[str, dict] = {}
    for record in missing:
        key = (record["sourceObject"], record["connectedComponent"], record["region"])
        bucket = groups.setdefault(key, {"sourceObject": key[0], "component": key[1], "region": key[2], "count": 0, "area": 0.0})
        bucket["count"] += 1; bucket["area"] += record["area"]
        category = classes.setdefault(record["category"], {"category": record["category"], "count": 0, "sourceArea": 0.0, "matchedCandidateArea": 0.0, "evidence": record["evidence"]})
        category["count"] += 1; category["sourceArea"] += record["area"]
        if record["category"].startswith("PRESENT_"):
            category["matchedCandidateArea"] += record["area"]
    json.dump({"source": str(opts.source.resolve()), "candidate": str(opts.candidate.resolve()), "records": source_records}, (opts.output / "source_face_ledger.json").open("w", encoding="utf-8"), indent=2)
    json.dump({"candidate": str(opts.candidate.resolve()), "records": candidate_records}, (opts.output / "candidate_source_derived_ledger.json").open("w", encoding="utf-8"), indent=2)
    json.dump({"reportedUnaccounted": len(missing), "records": missing, "groups": sorted(groups.values(), key=lambda item: (item["sourceObject"], item["component"], item["region"])), "classifications": sorted(classes.values(), key=lambda item: item["category"])}, (opts.output / "unaccounted_source_faces.json").open("w", encoding="utf-8"), indent=2)
    render(opts.source, missing, opts.output / "review_unaccounted_source_faces.png")
    render(opts.candidate, missing, opts.output / "review_unaccounted_candidate_overlay.png", candidate=True)
    candidate_hash_after, source_hash_after = sha256(opts.candidate), sha256(opts.source)
    synthetic_closure_faces = sum(int(record.get("closureFaceCount", 0)) for record in json.loads(opts.provenance.read_text(encoding="utf-8")).get("records", []))
    replacement_without_proof = sum(1 for item in missing if item.get("replacementGeometry"))
    unexplained = sum(1 for item in missing if item["category"] == "UNEXPLAINED_SOURCE_FACE")
    summary = {"candidateShaBefore": candidate_hash_before, "candidateShaAfter": candidate_hash_after, "sourceShaBefore": source_hash_before, "sourceShaAfter": source_hash_after,
               "sourcePolygonCount": len(source_records), "candidatePolygonCount": len(candidate_records), "oldSignatureUnaccounted": sum(missing_counts.values()), "resolvedRawRecords": len(missing),
               "missing": len(missing), "unexplained": unexplained, "replacementWithoutProof": replacement_without_proof, "classifications": sorted(classes.values(), key=lambda item: item["category"]),
               "completeSourceCategoryCounts": dict(sorted(complete_categories.items())), "syntheticClosureFaceCount": synthetic_closure_faces,
               "unintentionalCandidateDuplicateCount": max(0, sum((candidate_signatures - source_signatures).values()) - synthetic_closure_faces),
               "pass": len(missing) == 0 and unexplained == 0 and replacement_without_proof == 0 and source_hash_before == source_hash_after}
    (opts.output / "source_partition_forensic_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print("SOURCE_PARTITION_FORENSIC_AUDIT", json.dumps(summary))


if __name__ == "__main__":
    main()
