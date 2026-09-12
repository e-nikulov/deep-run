"""Audit the artist-selected Ventral rudder topology against the canonical GameReady BLEND.

This is intentionally read-only.  The artist selection is a world-space contract:
vertex/edge IDs are provenance only and are never trusted as stable production IDs.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
from mathutils import Vector

TOLERANCE_M = 1.0e-5
RUDDER = "SM_Antey_LOD0_Rudder_Ventral"
HULL = "SM_Antey_LOD0_Hull_LowerSource"

VERT_RE = re.compile(r"^VERT\s+(\d+):.*world=\(([^)]+)\)\s*$")
EDGE_RE = re.compile(r"^EDGE\s+(\d+):\s+(\d+)\s+->\s+(\d+)\s*$")


def parse_args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--selection", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def parse_selection(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    first = next((line.strip() for line in text.splitlines() if line.strip()), "")
    if first != f"OBJECT: {RUDDER}":
        raise RuntimeError(f"Unexpected selection target: {first!r}")
    vertices: dict[int, Vector] = {}
    edges: list[tuple[int, int, int]] = []
    for line in text.splitlines():
        match = VERT_RE.match(line)
        if match:
            vertices[int(match.group(1))] = Vector(tuple(float(value.strip()) for value in match.group(2).split(",")))
            continue
        match = EDGE_RE.match(line)
        if match:
            edge_id, left, right = (int(match.group(i)) for i in range(1, 4))
            edges.append((edge_id, left, right))
    if not vertices or not edges:
        raise RuntimeError("Selection contains no vertices or edges")
    unknown = sorted({index for _, left, right in edges for index in (left, right) if index not in vertices})
    if unknown:
        raise RuntimeError(f"Selection edges reference absent vertices: {unknown}")
    return {"vertices": vertices, "edges": edges}


def world_points(obj: bpy.types.Object) -> list[Vector]:
    return [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]


def nearest_unique(points: list[Vector], selected: dict[int, Vector], tolerance: float = TOLERANCE_M) -> tuple[dict[int, int], dict[int, float], list[int]]:
    resolved: dict[int, int] = {}
    errors: dict[int, float] = {}
    unresolved: list[int] = []
    for legacy, target in selected.items():
        distance, index = min(((point - target).length, index) for index, point in enumerate(points))
        if distance <= tolerance:
            resolved[legacy] = index
            errors[legacy] = distance
        else:
            unresolved.append(legacy)
    # Multiple artist coordinates resolving to one mesh vertex would invalidate
    # the positive topology mask even when each individual nearest error is tiny.
    inverse = defaultdict(list)
    for legacy, index in resolved.items():
        inverse[index].append(legacy)
    collisions = {index: values for index, values in inverse.items() if len(values) > 1}
    if collisions:
        raise RuntimeError(f"Non-unique selection coordinate resolution: {collisions}")
    return resolved, errors, unresolved


def polygon_incidence(obj: bpy.types.Object) -> tuple[Counter[int], Counter[tuple[int, int]], dict[tuple[int, int], int]]:
    vertices: Counter[int] = Counter()
    edges: Counter[tuple[int, int]] = Counter()
    edge_index_by_pair = {tuple(sorted(edge.vertices)): edge.index for edge in obj.data.edges}
    for polygon in obj.data.polygons:
        for vertex in polygon.vertices:
            vertices[int(vertex)] += 1
        for edge in polygon.edge_keys:
            edges[tuple(sorted(edge))] += 1
    return vertices, edges, edge_index_by_pair


def coordinate_key(point: Vector, digits: int = 5) -> tuple[float, float, float]:
    return tuple(round(float(value), digits) for value in point)


def main() -> None:
    options = parse_args()
    selection = parse_selection(options.selection.resolve(strict=True))
    rudder = bpy.data.objects.get(RUDDER)
    hull = bpy.data.objects.get(HULL)
    if rudder is None or rudder.type != "MESH" or hull is None or hull.type != "MESH":
        raise RuntimeError(f"Required objects missing: rudder={rudder} hull={hull}")

    selected: dict[int, Vector] = selection["vertices"]
    selected_edges = selection["edges"]
    rudder_points = world_points(rudder)
    hull_points = world_points(hull)
    rudder_map, rudder_errors, rudder_unresolved = nearest_unique(rudder_points, selected)
    hull_map, hull_errors, hull_unresolved = nearest_unique(hull_points, selected)

    rudder_vertex_incidence, rudder_edge_incidence, rudder_edge_index = polygon_incidence(rudder)
    hull_vertex_incidence, hull_edge_incidence, hull_edge_index = polygon_incidence(hull)

    rudder_resolved_edges = []
    rudder_missing_edges = []
    rudder_loose_edges = []
    hull_resolved_edges = []
    hull_missing_edges = []
    for edge_id, left_legacy, right_legacy in selected_edges:
        if left_legacy in rudder_map and right_legacy in rudder_map:
            pair = tuple(sorted((rudder_map[left_legacy], rudder_map[right_legacy])))
            edge_index = rudder_edge_index.get(pair)
            if edge_index is None:
                rudder_missing_edges.append(edge_id)
            else:
                rudder_resolved_edges.append(edge_id)
                if rudder_edge_incidence[pair] == 0:
                    rudder_loose_edges.append(edge_id)
        else:
            rudder_missing_edges.append(edge_id)
        if left_legacy in hull_map and right_legacy in hull_map:
            pair = tuple(sorted((hull_map[left_legacy], hull_map[right_legacy])))
            if pair in hull_edge_index:
                hull_resolved_edges.append(edge_id)
            else:
                hull_missing_edges.append(edge_id)
        else:
            hull_missing_edges.append(edge_id)

    rudder_loose_vertices = sorted(legacy for legacy, index in rudder_map.items() if rudder_vertex_incidence[index] == 0)
    selection_keys = {coordinate_key(point) for point in selected.values()}
    edge_keys_world = {
        tuple(sorted((coordinate_key(selected[left]), coordinate_key(selected[right]))))
        for _, left, right in selected_edges
    }

    hull_face_records = []
    full_selected_faces = []
    positive_edge_faces = []
    incident_faces_by_legacy: dict[int, list[int]] = defaultdict(list)
    for polygon in hull.data.polygons:
        keys = [coordinate_key(hull_points[index]) for index in polygon.vertices]
        selected_vertex_count = sum(key in selection_keys for key in keys)
        polygon_edge_keys = {
            tuple(sorted((keys[i], keys[(i + 1) % len(keys)])))
            for i in range(len(keys))
        }
        selected_edge_count = len(polygon_edge_keys & edge_keys_world)
        if selected_vertex_count or selected_edge_count:
            record = {
                "face": polygon.index,
                "vertices": len(keys),
                "selectedVertices": selected_vertex_count,
                "selectedEdges": selected_edge_count,
                "area": float(polygon.area),
            }
            hull_face_records.append(record)
            if selected_vertex_count == len(keys):
                full_selected_faces.append(polygon.index)
            if selected_edge_count >= 1 and selected_vertex_count >= 2:
                positive_edge_faces.append(polygon.index)
        for legacy, hull_index in hull_map.items():
            if hull_index in polygon.vertices:
                incident_faces_by_legacy[legacy].append(polygon.index)

    # Predict the strict positive transfer: every lower-hull source polygon
    # whose complete vertex set is artist-selected, plus measured side/border
    # polygons carrying at least one explicitly selected edge and two selected
    # vertices.  This is intentionally reported before mutation so the repair
    # can be reviewed against actual source-derived ownership.
    strict_transfer = sorted(set(full_selected_faces) | set(positive_edge_faces))
    predicted_covered = set()
    predicted_edge_keys = set()
    for face_index in strict_transfer:
        polygon = hull.data.polygons[face_index]
        keys = [coordinate_key(hull_points[index]) for index in polygon.vertices]
        predicted_covered.update(key for key in keys if key in selection_keys)
        predicted_edge_keys.update(
            tuple(sorted((keys[i], keys[(i + 1) % len(keys)])))
            for i in range(len(keys))
        )
    already_surface_keys = {
        coordinate_key(rudder_points[index])
        for index, count in rudder_vertex_incidence.items()
        if count > 0
    }
    combined_surface_keys = already_surface_keys | predicted_covered
    predicted_missing_vertices = sorted(
        legacy for legacy, point in selected.items()
        if coordinate_key(point) not in combined_surface_keys
    )
    current_surface_edge_keys = set()
    for pair, count in rudder_edge_incidence.items():
        if count > 0:
            current_surface_edge_keys.add(tuple(sorted((coordinate_key(rudder_points[pair[0]]), coordinate_key(rudder_points[pair[1]])))))
    combined_surface_edges = current_surface_edge_keys | predicted_edge_keys
    predicted_missing_edges = sorted(
        edge_id for edge_id, left, right in selected_edges
        if tuple(sorted((coordinate_key(selected[left]), coordinate_key(selected[right])))) not in combined_surface_edges
    )

    bounds = {
        axis: [min(float(point[i]) for point in selected.values()), max(float(point[i]) for point in selected.values())]
        for i, axis in enumerate(("X", "Y", "Z"))
    }
    report = {
        "blend": str(Path(bpy.data.filepath).resolve()),
        "selection": str(options.selection.resolve(strict=True)),
        "targetObject": RUDDER,
        "fixture": {"vertices": len(selected), "edges": len(selected_edges), "bounds": bounds},
        "rudder": {
            "meshVertices": len(rudder.data.vertices),
            "meshEdges": len(rudder.data.edges),
            "meshFaces": len(rudder.data.polygons),
            "resolvedVertices": len(rudder_map),
            "unresolvedVertices": rudder_unresolved,
            "maxResolutionErrorM": max(rudder_errors.values(), default=0.0),
            "surfaceOwnedVertices": len(rudder_map) - len(rudder_loose_vertices),
            "looseSelectedVertices": rudder_loose_vertices,
            "resolvedSelectedEdges": len(rudder_resolved_edges),
            "missingSelectedEdges": rudder_missing_edges,
            "looseSelectedEdges": rudder_loose_edges,
        },
        "lowerHull": {
            "meshVertices": len(hull.data.vertices),
            "meshEdges": len(hull.data.edges),
            "meshFaces": len(hull.data.polygons),
            "resolvedSelectionVertices": len(hull_map),
            "unresolvedSelectionVertices": hull_unresolved,
            "maxResolutionErrorM": max(hull_errors.values(), default=0.0),
            "resolvedSelectionEdges": len(hull_resolved_edges),
            "missingSelectionEdges": hull_missing_edges,
            "facesTouchingSelection": len(hull_face_records),
            "facesAllVerticesSelected": len(full_selected_faces),
            "facesWithPositiveSelectedEdge": len(positive_edge_faces),
            "strictTransferFaces": strict_transfer,
        },
        "predictedStrictTransfer": {
            "selectedVerticesStillWithoutSurface": predicted_missing_vertices,
            "selectedEdgesStillWithoutSurface": predicted_missing_edges,
            "pass": not predicted_missing_vertices and not predicted_missing_edges,
        },
        "status": "PASS" if not rudder_unresolved else "FAIL",
    }
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "fixtureVertices": len(selected),
        "fixtureEdges": len(selected_edges),
        "rudderResolved": len(rudder_map),
        "rudderLooseVertices": len(rudder_loose_vertices),
        "rudderLooseEdges": len(rudder_loose_edges),
        "hullResolvedVertices": len(hull_map),
        "strictTransferFaces": len(strict_transfer),
        "predictedMissingVertices": len(predicted_missing_vertices),
        "predictedMissingEdges": len(predicted_missing_edges),
    }))
    print(f"ANTEY_VENTRAL_SELECTION_AUDIT_{report['status']} {options.output.resolve()}")
    if report["status"] != "PASS":
        raise RuntimeError("Ventral artist selection does not resolve against the canonical rudder")


if __name__ == "__main__":
    main()
