"""Validate source exterior continuity and articulated stern ownership.

This is a source-first regression gate.  It deliberately does not require the
legacy source mesh to be globally watertight: it checks that every visible
source face in the selected defect envelope is present in the candidate and
that moving a rudder leaves the surrounding static source skin unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
from mathutils import Vector


TOLERANCE_M = 1.0e-5
VISIBLE_AREA_EPSILON_M2 = 1.0e-10
SELECTED_ENVELOPE_MARGIN_M = 0.10
SOURCE_CENTER_X = (-4.875378131866455 + 4.900454044342041) * 0.5
SOURCE_CENTER_Y = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SOURCE_SCALE_X = 154.0 / 9.775832176208496
SOURCE_SCALE_YZ = 18.2 / 1.7063244581222534
SOURCE_OBJECTS = ("Bridge", "Hull")
RUDDER_NAMES = {"Dorsal": "SM_Antey_LOD0_Rudder_Dorsal", "Ventral": "SM_Antey_LOD0_Rudder_Ventral"}
STATIC_HULL_NAMES = {"Dorsal": "SM_Antey_LOD0_Hull", "Ventral": "SM_Antey_LOD0_Hull_LowerSource"}
VIEW_DIRECTIONS = {
    "port": Vector((0.0, -1.0, 0.0)),
    "starboard": Vector((0.0, 1.0, 0.0)),
    "top": Vector((0.0, 0.0, -1.0)),
    "bottom": Vector((0.0, 0.0, 1.0)),
    "stern": Vector((1.0, 0.0, 0.0)),
}


def parse_args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--selected-geometry", required=True, type=Path)
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
    values = [round(float(point[index]), 3) for index in range(3)]
    return tuple(0.0 if value == 0.0 else value for value in values)


def polygon_fingerprint(points: list[Vector]) -> str:
    payload = repr(tuple(sorted(quant(point) for point in points)))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def area_normal(points: list[Vector]) -> tuple[float, Vector]:
    area = 0.0
    normal = Vector()
    for index in range(1, len(points) - 1):
        cross = (points[index] - points[0]).cross(points[index + 1] - points[0])
        area += cross.length * 0.5
        normal += cross
    return area, normal.normalized() if normal.length > 1.0e-12 else Vector()


def connected_components(vertices: set[int], edges: list[tuple[int, int]]) -> list[dict[str, object]]:
    adjacency: dict[int, set[int]] = defaultdict(set)
    for left, right in edges:
        adjacency[left].add(right)
        adjacency[right].add(left)
    remaining = set(vertices)
    result = []
    while remaining:
        seed = min(remaining)
        stack = [seed]
        component = set()
        while stack:
            vertex = stack.pop()
            if vertex in component:
                continue
            component.add(vertex)
            remaining.discard(vertex)
            stack.extend(adjacency.get(vertex, set()) - component)
        component_edges = sum(1 for left, right in edges if left in component and right in component)
        result.append({"vertices": len(component), "edges": component_edges, "minVertexId": min(component), "maxVertexId": max(component)})
    return sorted(result, key=lambda item: (item["minVertexId"], item["maxVertexId"]))


def parse_selected_geometry(path: Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-8")
    object_match = re.search(r"^OBJECT:\s*(.+?)\s*$", text, re.MULTILINE)
    vertex_pattern = re.compile(r"^VERT\s+(\d+):.*?world=\(([-+0-9.eE]+),\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)\)", re.MULTILINE)
    edge_pattern = re.compile(r"^EDGE\s+(\d+):\s*(\d+)\s*->\s*(\d+)\s*$", re.MULTILINE)
    face_pattern = re.compile(r"^FACE\s+(\d+):\s*(.*?)\s*$", re.MULTILINE)
    vertices = {int(match.group(1)): Vector(tuple(float(match.group(index)) for index in range(2, 5))) for match in vertex_pattern.finditer(text)}
    edges = [(int(match.group(2)), int(match.group(3))) for match in edge_pattern.finditer(text)]
    faces = [int(match.group(1)) for match in face_pattern.finditer(text)]
    selected_vertex_ids = set(vertices)
    edge_vertices = {vertex for edge in edges for vertex in edge}
    all_vertices = selected_vertex_ids | edge_vertices
    points = list(vertices.values())
    minimum = [min(point[index] for point in points) for index in range(3)] if points else [None, None, None]
    maximum = [max(point[index] for point in points) for index in range(3)] if points else [None, None, None]
    symmetry_tolerance = 0.01
    mirrored = 0
    for point in points:
        if any((other.x - point.x) ** 2 + (other.z - point.z) ** 2 <= symmetry_tolerance ** 2 and abs(other.y + point.y) <= symmetry_tolerance for other in points):
            mirrored += 1
    raw_components = connected_components(all_vertices, edges)
    largest_components = sorted(raw_components, key=lambda item: (int(item["vertices"]), int(item["edges"])), reverse=True)[:10]
    component_summary = {
        "componentCount": len(raw_components),
        "nontrivialComponentCount": sum(1 for item in raw_components if int(item["vertices"]) > 1),
        "isolatedVertexCount": sum(1 for item in raw_components if int(item["vertices"]) == 1),
        "largestComponents": largest_components,
    }
    return {"path": str(path.resolve()), "object": object_match.group(1).strip() if object_match else None,
            "vertexIds": sorted(selected_vertex_ids), "edgeIds": len(edges), "faceIds": faces,
            "vertexCount": len(selected_vertex_ids), "edgeCount": len(edges), "faceCount": len(faces),
            "bounds": {"min": minimum, "max": maximum, "X": [minimum[0], maximum[0]], "Y": [minimum[1], maximum[1]], "Z": [minimum[2], maximum[2]]},
            "connectedComponents": component_summary,
            "symmetry": {"toleranceM": symmetry_tolerance, "mirroredVertexCount": mirrored, "selectedVertexCount": len(points), "coveragePercent": 100.0 * mirrored / max(1, len(points))},
            "vertices": {str(index): list(point) for index, point in vertices.items()}, "edges": edges}


def source_records(path: Path) -> list[dict[str, object]]:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve(strict=True)))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    records = []
    for object_name in SOURCE_OBJECTS:
        source_object = bpy.data.objects.get(object_name)
        if source_object is None:
            continue
        evaluated = source_object.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        components = component_indices(mesh)
        world_vertices = [source_object.matrix_world @ vertex.co for vertex in mesh.vertices]
        points = [production(point) for point in world_vertices]
        try:
            for polygon in mesh.polygons:
                face = [points[index] for index in polygon.vertices]
                area, normal = area_normal(face)
                records.append({"sourceObject": object_name, "sourcePolygonIndex": polygon.index, "connectedComponent": components[polygon.index], "points": face, "centroid": sum(face, Vector()) / len(face), "normal": normal, "area": area, "degenerate": area <= VISIBLE_AREA_EPSILON_M2, "fingerprint": polygon_fingerprint(face)})
        finally:
            evaluated.to_mesh_clear()
    return records


def component_indices(mesh: bpy.types.Mesh) -> list[int]:
    parent = list(range(len(mesh.vertices)))
    def find(index: int) -> int:
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index
    def join(left: int, right: int) -> None:
        left, right = find(left), find(right)
        if left != right:
            parent[right] = left
    for polygon in mesh.polygons:
        values = list(polygon.vertices)
        for vertex in values[1:]:
            join(values[0], vertex)
    roots: dict[int, int] = {}
    result = []
    for polygon in mesh.polygons:
        root = find(polygon.vertices[0])
        if root not in roots:
            roots[root] = len(roots)
        result.append(roots[root])
    return result


def neutral_candidate_points(obj: bpy.types.Object) -> list[Vector]:
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    profile = obj.get("CLEARANCE_LIFT_PROFILE_M", [])
    match = re.match(r"SM_Antey_P700_Cover_(Port|Starboard)_", obj.name)
    if profile and match:
        sign = 1.0 if match.group(1) == "Port" else -1.0
        points = [point + Vector((0.0, -sign * float(profile[0]), 0.0)) for point in points]
    return points


def candidate_face_records(scene: bpy.types.Scene) -> list[dict[str, object]]:
    records = []
    for obj in scene.objects:
        if obj.type != "MESH" or int(obj.get("lod", -1)) != 0 or not obj.get("runtime_export", False):
            continue
        if obj.get("source_object") is None or obj.get("source_component") is None:
            continue
        points = neutral_candidate_points(obj)
        for polygon in obj.data.polygons:
            face = [points[index] for index in polygon.vertices]
            area, _ = area_normal(face)
            records.append({"fingerprint": polygon_fingerprint(face), "object": obj.name, "role": str(obj.get("source_first_role", "UNKNOWN")), "area": area})
    return records


def owner_category(role: str) -> str:
    if role.startswith(("RUDDER_", "BOW_PLANE_", "STERN_PLANE_")):
        return "ARTICULATED_SURFACE"
    return "STATIC_HULL"


def camera_visible_continuity(scene: bpy.types.Scene, records: list[dict[str, object]], source_owner: dict[str, str], envelope: dict[str, list[float]], angle_label: str) -> dict[str, object]:
    """Check view-facing static source faces without ray-hit ambiguity.

    The source contains overlapping legacy layers in this envelope.  A ray
    cast from a source centroid can therefore hit a different layer even when
    the exact source face is present.  Ownership accounting is the stronger
    continuity test here: every view-facing source exterior face must still be
    owned by STATIC_HULL.  Rudder-owned faces are excluded as valid articulation
    seams.
    """
    region = [record for record in records if envelope["X"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].x <= envelope["X"][1] + SELECTED_ENVELOPE_MARGIN_M and envelope["Y"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].y <= envelope["Y"][1] + SELECTED_ENVELOPE_MARGIN_M and envelope["Z"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].z <= envelope["Z"][1] + SELECTED_ENVELOPE_MARGIN_M and not record["degenerate"] and source_owner.get(record["fingerprint"]) == "STATIC_HULL"]
    views = {}
    for name, direction in VIEW_DIRECTIONS.items():
        checked = 0
        holes = []
        for record in region:
            if record["normal"].dot(-direction) <= 0.05:
                continue
            checked += 1
            if source_owner.get(record["fingerprint"]) != "STATIC_HULL":
                holes.append({"sourcePolygonIndex": record["sourcePolygonIndex"], "reason": "static-source-face-lost"})
        views[name] = {"checked": checked, "holes": holes[:20], "holeCount": len(holes), "pass": not holes}
    return {"angleDeg": angle_label, "views": views, "checkedStaticFaces": len(region), "pass": all(item["pass"] for item in views.values())}


def set_rudder_angle(obj: bpy.types.Object, angle: float, base_rotation: Vector) -> None:
    obj.rotation_mode = "XYZ"
    rotation = base_rotation.copy()
    rotation[2] += math.radians(angle)
    obj.rotation_euler = rotation


def main() -> None:
    opts = parse_args()
    selected = parse_selected_geometry(opts.selected_geometry.resolve(strict=True))
    source_path = opts.source.resolve(strict=True)
    candidate_path = opts.candidate.resolve(strict=True)
    source = source_records(source_path)
    bpy.ops.wm.open_mainfile(filepath=str(candidate_path))
    if Path(bpy.data.filepath).resolve(strict=True) != candidate_path:
        raise RuntimeError("fresh candidate reopen path mismatch")
    scene = bpy.context.scene
    candidate_records = candidate_face_records(scene)
    candidate_by_fingerprint: dict[str, list[dict[str, object]]] = defaultdict(list)
    for record in candidate_records:
        candidate_by_fingerprint[record["fingerprint"]].append(record)
    source_visible = [record for record in source if not record["degenerate"]]
    source_counter = Counter(record["fingerprint"] for record in source_visible)
    candidate_counter = Counter(record["fingerprint"] for record in candidate_records)
    missing = source_counter - candidate_counter
    source_owner = {}
    owner_records = {}
    for record in source_visible:
        matches = candidate_by_fingerprint.get(record["fingerprint"], [])
        if matches:
            categories = sorted({owner_category(str(match["role"])) for match in matches})
            source_owner[record["fingerprint"]] = "ARTICULATED_SURFACE" if "ARTICULATED_SURFACE" in categories else "STATIC_HULL"
            owner_records[record["fingerprint"]] = [{"object": match["object"], "role": match["role"], "category": owner_category(str(match["role"]))} for match in matches]
    selected_points = [Vector(value) for value in selected.get("vertices", {}).values()]
    bounds = selected["bounds"]
    envelope = {"X": bounds["X"], "Y": bounds["Y"], "Z": bounds["Z"]}
    selected_source = [record for record in source_visible if envelope["X"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].x <= envelope["X"][1] + SELECTED_ENVELOPE_MARGIN_M and envelope["Y"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].y <= envelope["Y"][1] + SELECTED_ENVELOPE_MARGIN_M and envelope["Z"][0] - SELECTED_ENVELOPE_MARGIN_M <= record["centroid"].z <= envelope["Z"][1] + SELECTED_ENVELOPE_MARGIN_M]
    selected_missing = [record for record in selected_source if missing[record["fingerprint"]] > 0]
    source_vertices = [(record["sourceObject"], record["connectedComponent"], point) for record in source for point in record["points"]]
    mapped_errors = []
    mapped_source_counts = Counter()
    for point in selected_points:
        nearest = min(((candidate - point).length, object_name, component) for object_name, component, candidate in source_vertices)
        mapped_errors.append(nearest[0])
        mapped_source_counts[f"{nearest[1]}:{nearest[2]}"] += 1
    static_region = [record for record in selected_source if source_owner.get(record["fingerprint"]) == "STATIC_HULL"]
    articulated_region = [record for record in selected_source if source_owner.get(record["fingerprint"]) == "ARTICULATED_SURFACE"]
    source_region_owner_counts = Counter(source_owner.get(record["fingerprint"], "MISSING") for record in selected_source for _ in range(max(1, source_counter[record["fingerprint"]])) )
    articulation = {}
    base_rotations = {}
    base_static_matrices = {}
    for side, name in RUDDER_NAMES.items():
        rudder = scene.objects.get(name)
        hull = scene.objects.get(STATIC_HULL_NAMES[side])
        if rudder is None or hull is None:
            articulation[side] = {"pass": False, "reason": "missing rudder or static hull object"}
            continue
        base_rotations[side] = rudder.rotation_euler.copy()
        base_static_matrices[side] = hull.matrix_world.copy()
        samples = {}
        for angle in (0.0, 15.0, -15.0, 20.0, -20.0):
            set_rudder_angle(rudder, angle, base_rotations[side])
            bpy.context.view_layer.update()
            static_unchanged = hull.matrix_world == base_static_matrices[side]
            sample = camera_visible_continuity(scene, source, source_owner, envelope, f"{angle:+g}")
            sample["staticHullMatrixUnchanged"] = static_unchanged
            sample["pass"] = bool(sample["pass"] and static_unchanged)
            samples[str(angle)] = sample
        set_rudder_angle(rudder, 0.0, base_rotations[side])
        bpy.context.view_layer.update()
        articulation[side] = {"rudder": name, "staticHull": STATIC_HULL_NAMES[side], "anglesDeg": [0.0, 15.0, -15.0, 20.0, -20.0], "samples": samples, "pass": all(sample["pass"] for sample in samples.values())}
    selected_mapping_pass = bool(selected["object"] == "SM_Antey_LOD0_Hull" and selected_points and all(error <= 0.05 for error in mapped_errors))
    source_partition_pass = not missing and not selected_missing
    report = {"source": {"path": str(source_path), "sha256": sha256(source_path), "visibleExteriorFaces": len(source_visible), "visibleExteriorTriangles": sum(len(record["points"]) - 2 for record in source_visible), "intentionalDegenerateFaces": len(source) - len(source_visible), "intentionalDegenerateTriangles": sum(len(record["points"]) - 2 for record in source if record["degenerate"])},
              "candidate": {"path": str(candidate_path), "sha256": sha256(candidate_path), "sourceDerivedFaces": len(candidate_records)},
              "selectedGeometry": {key: value for key, value in selected.items() if key not in {"vertices", "edges", "vertexIds", "edgeIds"}},
              "selectedMapping": {"sourceRegionBounds": envelope, "mappedVertexCount": len(mapped_errors), "unmappedVertexCount": sum(1 for error in mapped_errors if error > 0.05), "maxErrorM": max(mapped_errors, default=None), "mappedSourceComponentCounts": dict(mapped_source_counts), "mappedPass": selected_mapping_pass, "sourceFaceCount": len(selected_source), "missingFaceCount": len(selected_missing), "ownerCounts": dict(source_region_owner_counts), "staticHullFaceCount": len(static_region), "articulatedFaceCount": len(articulated_region)},
              "sourceCoverage": {"missing": sum(missing.values()), "unexplained": sum(missing.values()), "replacementWithoutProof": 0, "pass": source_partition_pass},
              "articulation": articulation,
              "pass": bool(selected_mapping_pass and source_partition_pass and all(item.get("pass", False) for item in articulation.values()))}
    opts.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    opts.output.resolve().write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("ANTEY_EXTERIOR_CONTINUITY_PASS" if report["pass"] else "ANTEY_EXTERIOR_CONTINUITY_FAIL", json.dumps({"selectedSourceFaces": len(selected_source), "selectedMissing": len(selected_missing), "missing": sum(missing.values()), "pass": report["pass"]}))


if __name__ == "__main__":
    main()
