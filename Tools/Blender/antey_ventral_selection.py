"""Shared artist-selection contract for the Antey ventral rudder.

The fixture is authoritative in world/production coordinates.  Numeric Blender
vertex/edge ids in the text file are provenance only: every consumer resolves
coordinates back to the mesh and then validates the exact topology.
"""
from __future__ import annotations

import re
from collections import Counter
from pathlib import Path
from typing import Iterable

from mathutils import Matrix, Vector

TARGET_OBJECT = "SM_Antey_LOD0_Rudder_Ventral"
TOLERANCE_M = 1.0e-5
VERT_RE = re.compile(r"^VERT\s+(\d+):.*world=\(([^)]+)\)\s*$")
EDGE_RE = re.compile(r"^EDGE\s+(\d+):\s+(\d+)\s+->\s+(\d+)\s*$")


def parse_selection(path: Path) -> dict:
    text = path.resolve(strict=True).read_text(encoding="utf-8")
    first = next((line.strip() for line in text.splitlines() if line.strip()), "")
    if first != f"OBJECT: {TARGET_OBJECT}":
        raise RuntimeError(f"Unexpected ventral selection target: {first!r}")
    vertices: dict[int, Vector] = {}
    edges: list[tuple[int, int, int]] = []
    for line in text.splitlines():
        match = VERT_RE.match(line)
        if match:
            index = int(match.group(1))
            if index in vertices:
                raise RuntimeError(f"Duplicate ventral fixture vertex id: {index}")
            vertices[index] = Vector(tuple(float(value.strip()) for value in match.group(2).split(",")))
            continue
        match = EDGE_RE.match(line)
        if match:
            edge_id, left, right = (int(match.group(i)) for i in range(1, 4))
            edges.append((edge_id, left, right))
    if not vertices or not edges:
        raise RuntimeError("Ventral selection fixture contains no vertices or edges")
    unknown = sorted({vertex for _, left, right in edges for vertex in (left, right) if vertex not in vertices})
    if unknown:
        raise RuntimeError(f"Ventral selection edges reference absent vertices: {unknown}")
    if len({edge_id for edge_id, _, _ in edges}) != len(edges):
        raise RuntimeError("Duplicate ventral fixture edge id")
    return {"object": TARGET_OBJECT, "vertices": vertices, "edges": edges}


def as_mask_record(path: Path) -> dict:
    selection = parse_selection(path)
    return {
        "object": TARGET_OBJECT,
        "expectedVertexCount": len(selection["vertices"]),
        "expectedEdgeCount": len(selection["edges"]),
        "surfaceOwnership": "TRANSFER_SELECTED_SOURCE_FACES",
        "vertices": [
            {"legacyIndex": int(index), "world": [float(value) for value in point]}
            for index, point in sorted(selection["vertices"].items())
        ],
        "edges": [
            {"legacyIndex": int(edge_id), "legacyVertices": [int(left), int(right)]}
            for edge_id, left, right in selection["edges"]
        ],
    }


def _nearest_unique(points: list[Vector], selected: dict[int, Vector], tolerance: float) -> dict[int, int]:
    resolved: dict[int, int] = {}
    for legacy, target in selected.items():
        distance, index = min(((point - target).length, index) for index, point in enumerate(points))
        if distance > tolerance:
            raise RuntimeError(f"Ventral selected vertex {legacy} does not resolve within {tolerance} m: {distance}")
        resolved[legacy] = index
    if len(set(resolved.values())) != len(resolved):
        inverse: dict[int, list[int]] = {}
        for legacy, index in resolved.items():
            inverse.setdefault(index, []).append(legacy)
        collisions = {index: values for index, values in inverse.items() if len(values) > 1}
        raise RuntimeError(f"Ventral selection coordinate resolution is not unique: {collisions}")
    return resolved


def resolve_on_mesh(
    mesh,
    selection: dict,
    *,
    matrix: Matrix | None = None,
    tolerance: float = TOLERANCE_M,
    require_all_edges: bool = True,
) -> dict:
    transform = matrix if matrix is not None else Matrix.Identity(4)
    points = [transform @ vertex.co for vertex in mesh.vertices]
    resolved_vertices = _nearest_unique(points, selection["vertices"], tolerance)
    edge_by_pair = {tuple(sorted(edge.vertices)): edge.index for edge in mesh.edges}
    resolved_edges: dict[int, tuple[int, int]] = {}
    missing_edges = []
    for edge_id, left, right in selection["edges"]:
        pair = tuple(sorted((resolved_vertices[left], resolved_vertices[right])))
        if pair not in edge_by_pair:
            missing_edges.append({"edge": edge_id, "vertices": [left, right], "resolved": list(pair)})
        else:
            resolved_edges[edge_id] = pair
    if require_all_edges and missing_edges:
        raise RuntimeError(f"Ventral selection edges do not resolve on mesh: {missing_edges[:20]} total={len(missing_edges)}")
    return {
        "points": points,
        "vertices": resolved_vertices,
        "vertexSet": set(resolved_vertices.values()),
        "edges": resolved_edges,
        "edgeSet": set(resolved_edges.values()),
        "missingEdges": missing_edges,
        "edgeIndexByPair": edge_by_pair,
    }


def strict_surface_faces(mesh, resolved: dict, allowed_face_tuples: Iterable[tuple[int, ...]] | None = None) -> dict:
    """Select every real source face required to make the artist graph surface-owned.

    A face is owned by the ventral rudder when either all of its vertices are
    selected, or it carries at least one explicitly selected edge and at least
    two selected vertices.  This is intentionally broader than the legacy
    thickness heuristic (>=2 selected edges / >=3 vertices), which is what left
    many accepted points as loose topology.
    """
    selected_vertices: set[int] = resolved["vertexSet"]
    selected_edges: set[tuple[int, int]] = resolved["edgeSet"]
    allowed_counts = Counter(tuple(face) for face in allowed_face_tuples) if allowed_face_tuples is not None else None
    selected: list[int] = []
    full_faces: list[int] = []
    edge_faces: list[int] = []
    touching: list[int] = []
    for polygon in mesh.polygons:
        if allowed_counts is not None:
            key = tuple(polygon.vertices)
            if allowed_counts[key] <= 0:
                continue
            allowed_counts[key] -= 1
        vertices = set(polygon.vertices)
        edge_keys = {tuple(sorted(edge)) for edge in polygon.edge_keys}
        selected_vertex_count = len(vertices & selected_vertices)
        selected_edge_count = len(edge_keys & selected_edges)
        if selected_vertex_count or selected_edge_count:
            touching.append(polygon.index)
        include = False
        if vertices <= selected_vertices:
            full_faces.append(polygon.index)
            include = True
        elif selected_edge_count >= 1 and selected_vertex_count >= 2:
            edge_faces.append(polygon.index)
            include = True
        if include:
            selected.append(polygon.index)
    selected = sorted(set(selected))
    selected_vertices_from_faces = {vertex for index in selected for vertex in mesh.polygons[index].vertices}
    selected_edges_from_faces = {
        tuple(sorted(edge))
        for index in selected
        for edge in mesh.polygons[index].edge_keys
    }
    missing_vertices = sorted(
        legacy for legacy, index in resolved["vertices"].items()
        if index not in selected_vertices_from_faces
    )
    # Only edges that physically exist on the mesh being classified can be
    # required from this patch.  On the current GameReady lower hull, some
    # fixture edges already live exclusively on the articulated rudder; the
    # final combined rudder validator requires all 357 edges to be surface-owned.
    missing_edges = sorted(
        edge_id for edge_id, pair in resolved["edges"].items()
        if pair not in selected_edges_from_faces
    )
    return {
        "faceIndices": selected,
        "allVerticesSelectedFaceIndices": sorted(full_faces),
        "selectedEdgeFaceIndices": sorted(edge_faces),
        "touchingFaceIndices": sorted(touching),
        "missingSurfaceVertices": missing_vertices,
        "missingSurfaceEdges": missing_edges,
        "unresolvedMeshEdges": resolved.get("missingEdges", []),
        "pass": not missing_vertices and not missing_edges,
    }


def surface_incidence(obj, selection: dict, *, tolerance: float = TOLERANCE_M) -> dict:
    resolved = resolve_on_mesh(obj.data, selection, matrix=obj.matrix_world, tolerance=tolerance, require_all_edges=True)
    vertex_incidence = Counter()
    edge_incidence = Counter()
    for polygon in obj.data.polygons:
        for vertex in polygon.vertices:
            vertex_incidence[int(vertex)] += 1
        for edge in polygon.edge_keys:
            edge_incidence[tuple(sorted(edge))] += 1
    loose_vertices = sorted(
        legacy for legacy, index in resolved["vertices"].items()
        if vertex_incidence[index] == 0
    )
    loose_edges = sorted(
        edge_id for edge_id, pair in resolved["edges"].items()
        if edge_incidence[pair] == 0
    )
    return {
        "vertices": len(selection["vertices"]),
        "edges": len(selection["edges"]),
        "resolvedVertices": len(resolved["vertices"]),
        "resolvedEdges": len(resolved["edges"]),
        "surfaceOwnedVertices": len(selection["vertices"]) - len(loose_vertices),
        "surfaceOwnedEdges": len(selection["edges"]) - len(loose_edges),
        "looseVertices": loose_vertices,
        "looseEdges": loose_edges,
        "pass": not loose_vertices and not loose_edges,
    }
