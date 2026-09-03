"""Derive the manually anchored P700 cover loops from the source topology.

This pass is source-only and produces forensic JSON.  It uses real source
vertices/edges and face flood-fill; it never creates a rectangle or moves a
vertex.  The same deterministic routine is consumed by the manual candidate
builder.
"""
from __future__ import annotations

import heapq
import json
import math
import sys
from collections import defaultdict, deque
from pathlib import Path

import bpy
from mathutils import Vector

SCX = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SCY = (-4.875378131866455 + 4.900454044342041) * 0.5
LS = 154.0 / 9.775832176208496
CS = 18.2 / 1.7063244581222534
TOL = 1e-5

MANUAL = {
    "FORWARD_INBOARD": (51.620544434, 3.250949383, 4.935768127),
    "AFT_INBOARD": (44.585681915, 3.225973606, 4.932476997),
    "FORWARD_OUTBOARD": (51.612987518, 6.745433331, 3.182579994),
    "AFT_OUTBOARD": (44.612236023, 6.793663025, 3.255691528),
    "BANK_AFT_INBOARD": (9.347927094, 3.355130434, 4.926637173),
    "BANK_AFT_OUTBOARD": (9.344326973, 6.887545586, 3.255691528),
}

STATIONS = [
    ("01_FORWARD", 51.620544434, "FORWARD_INBOARD", "FORWARD_OUTBOARD"),
    ("02_SEAM", 44.6, "AFT_INBOARD", "AFT_OUTBOARD"),
    ("03_SEAM", 37.5, None, None),
    ("04_SEAM", 30.5, None, None),
    ("05_SEAM", 23.4, None, None),
    ("06_SEAM", 16.4, None, None),
    ("07_AFT", 9.35, "BANK_AFT_INBOARD", "BANK_AFT_OUTBOARD"),
]


def arg(name: str, default: str | None = None) -> str | None:
    av = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    for i, value in enumerate(av):
        if value == name and i + 1 < len(av):
            return av[i + 1]
    return default


def production_point(point: Vector) -> Vector:
    return Vector(((point.y - SCY) * LS, -(point.x - SCX) * CS, point.z * CS))


def make_source_mesh() -> bpy.types.Mesh:
    obj = bpy.data.objects["Bridge"]
    depsgraph = bpy.context.evaluated_depsgraph_get()
    temporary = obj.evaluated_get(depsgraph).to_mesh()
    mesh = temporary.copy()
    for vertex in mesh.vertices:
        vertex.co = production_point(obj.matrix_world @ vertex.co)
    obj.evaluated_get(depsgraph).to_mesh_clear()
    mesh.update(calc_edges=True)
    return mesh


def nearest_vertex(mesh: bpy.types.Mesh, target: Vector) -> int:
    return min(range(len(mesh.vertices)), key=lambda i: (mesh.vertices[i].co - target).length)


def guide_vertex(mesh: bpy.types.Mesh, x: float, y: float, z: float) -> int:
    # Search only the actual launcher-bank shoulder surface.  The station
    # values are windows; the returned coordinate is always a real source
    # vertex and is reported below.
    candidates = []
    for vertex in mesh.vertices:
        c = vertex.co
        if abs(c.x - x) > 1.2 or abs(c.y - y) > 0.8 or abs(c.z - z) > 0.30:
            continue
        candidates.append((
            ((c.x - x) / 0.45) ** 2 + ((c.y - y) / 0.35) ** 2 + ((c.z - z) / 0.18) ** 2,
            vertex.index,
        ))
    if not candidates:
        raise RuntimeError(f"no source seam vertex near station x={x} y={y} z={z}")
    candidates.sort()
    return candidates[0][1]


def point_segment(point: Vector, start: Vector, end: Vector) -> tuple[float, float]:
    direction = end - start
    denominator = direction.length_squared
    t = max(0.0, min(1.0, (point - start).dot(direction) / denominator if denominator else 0.0))
    return (point - (start + t * direction)).length, t


def seam_path(mesh: bpy.types.Mesh, start: int, end: int, corridor: float = 0.42, forbidden_edges: set[int] | None = None) -> dict[str, object]:
    coords = [Vector(vertex.co) for vertex in mesh.vertices]
    a, b = coords[start], coords[end]
    direction = b - a
    adjacency: list[list[tuple[int, int, float]]] = [[] for _ in coords]
    forbidden_edges = forbidden_edges or set()
    for edge in mesh.edges:
        if edge.index in forbidden_edges:
            continue
        u, v = edge.vertices
        pu, pv = coords[u], coords[v]
        distance_mid, t_mid = point_segment((pu + pv) * 0.5, a, b)
        distance_u, t_u = point_segment(pu, a, b)
        distance_v, t_v = point_segment(pv, a, b)
        if max(distance_mid, distance_u, distance_v) > corridor:
            continue
        delta_t = t_v - t_u
        # A boundary path must advance along its intended longitudinal or
        # transverse direction.  Rejecting a backwards edge prevents the
        # solver from sharing the first corner edge with the neighbouring
        # boundary chain (a tempting but invalid shortcut through a triangle).
        if delta_t < -0.03:
            continue
        reverse = 0.0
        weight = (pv - pu).length * (1.0 + 80.0 * distance_mid * distance_mid) + reverse
        adjacency[u].append((v, edge.index, weight))
        adjacency[v].append((u, edge.index, weight))
    distances = [float("inf")] * len(coords)
    previous: list[tuple[int, int] | None] = [None] * len(coords)
    distances[start] = 0.0
    queue = [(0.0, start)]
    while queue:
        distance, vertex = heapq.heappop(queue)
        if distance != distances[vertex]:
            continue
        if vertex == end:
            break
        for neighbour, edge_index, weight in adjacency[vertex]:
            candidate = distance + weight
            if candidate < distances[neighbour]:
                distances[neighbour] = candidate
                previous[neighbour] = (vertex, edge_index)
                heapq.heappush(queue, (candidate, neighbour))
    if not math.isfinite(distances[end]):
        raise RuntimeError(f"source seam path not found {start}->{end}")
    vertices: list[int] = []
    edges: list[int] = []
    current: int | None = end
    while current is not None:
        vertices.append(current)
        link = previous[current]
        if link is None:
            current = None
        else:
            current, edge_index = link
            edges.append(edge_index)
    vertices.reverse()
    edges.reverse()
    # A path may not revisit a vertex; that would be a cut across the panel.
    if len(vertices) != len(set(vertices)):
        raise RuntimeError(f"non-simple source seam path {start}->{end}")
    return {
        "vertices": vertices,
        "edges": edges,
        "coordinates": [list(coords[index]) for index in vertices],
        "length_m": sum((coords[u] - coords[v]).length for u, v in zip(vertices, vertices[1:])),
        "cost": distances[end],
    }


def edge_face_adjacency(mesh: bpy.types.Mesh) -> tuple[dict[tuple[int, int], int], dict[int, list[int]]]:
    key_to_edge = {tuple(sorted(edge.vertices)): edge.index for edge in mesh.edges}
    faces_by_edge: dict[int, list[int]] = defaultdict(list)
    for polygon in mesh.polygons:
        vertices = list(polygon.vertices)
        for left, right in zip(vertices, vertices[1:] + vertices[:1]):
            faces_by_edge[key_to_edge[tuple(sorted((left, right)))]] .append(polygon.index)
    return key_to_edge, faces_by_edge


def enclosed_faces(mesh: bpy.types.Mesh, boundary_edges: set[int], seed: Vector) -> list[int]:
    _, faces_by_edge = edge_face_adjacency(mesh)
    face_edges: dict[int, list[int]] = defaultdict(list)
    for edge_index, faces in faces_by_edge.items():
        for face_index in faces:
            face_edges[face_index].append(edge_index)
    face_adjacency: dict[int, list[int]] = defaultdict(list)
    for edge_index, faces in faces_by_edge.items():
        if edge_index in boundary_edges or len(faces) != 2:
            continue
        a, b = faces
        face_adjacency[a].append(b)
        face_adjacency[b].append(a)
    seed_face = min(mesh.polygons, key=lambda polygon: (polygon.center - seed).length).index
    visited = {seed_face}
    queue = deque([seed_face])
    while queue:
        face = queue.popleft()
        for neighbour in face_adjacency.get(face, []):
            if neighbour not in visited:
                visited.add(neighbour)
                queue.append(neighbour)
    return sorted(visited)


def face_fingerprint(faces: list[tuple[int, ...]]) -> str:
    import hashlib
    payload = "|".join(",".join(str(i) for i in face) for face in sorted(faces))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def boundary_fingerprint(edges: set[int]) -> str:
    import hashlib
    payload = "|".join(str(index) for index in sorted(edges))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def ordered_boundary(mesh: bpy.types.Mesh, edges: set[int], start_vertex: int) -> tuple[list[int], list[int]]:
    """Return one simple ordered loop from a degree-two source boundary."""
    adjacency: dict[int, list[tuple[int, int]]] = defaultdict(list)
    for edge_index in edges:
        left, right = mesh.edges[edge_index].vertices
        adjacency[left].append((right, edge_index))
        adjacency[right].append((left, edge_index))
    if not adjacency or start_vertex not in adjacency:
        raise RuntimeError("manual boundary does not contain its semantic corner")
    if any(len(neighbours) != 2 for neighbours in adjacency.values()):
        raise RuntimeError("manual boundary is not a simple degree-two source loop")
    first = sorted(adjacency[start_vertex])[0]
    vertices = [start_vertex]
    edge_loop: list[int] = []
    previous = start_vertex
    current, edge_index = first
    edge_loop.append(edge_index)
    vertices.append(current)
    while current != start_vertex:
        options = [(neighbour, ei) for neighbour, ei in adjacency[current] if neighbour != previous]
        if len(options) != 1:
            raise RuntimeError("ambiguous manual boundary traversal")
        previous, current = current, options[0][0]
        edge_loop.append(options[0][1])
        vertices.append(current)
        if len(edge_loop) > len(edges):
            raise RuntimeError("manual boundary traversal exceeded source loop")
    if len(edge_loop) != len(edges) or len(vertices) != len(edges) + 1:
        raise RuntimeError("manual boundary has disconnected or repeated components")
    return vertices, edge_loop


def boundary_for_faces(mesh: bpy.types.Mesh, face_indices: list[int]) -> set[int]:
    key_to_edge, _ = edge_face_adjacency(mesh)
    counts: dict[int, int] = defaultdict(int)
    for face_index in face_indices:
        for edge_key in mesh.polygons[face_index].edge_keys:
            counts[key_to_edge[tuple(sorted(edge_key))]] += 1
    return {edge_index for edge_index, count in counts.items() if count == 1}


def derive_manual_side(mesh: bpy.types.Mesh, side: str, corner_reference: list[dict[str, object]] | None = None, station_reference: list[dict[str, object]] | None = None) -> dict[str, object]:
    """Derive six real cover regions for one side of the source casing."""
    sign = 1.0 if side == "Port" else -1.0
    coords = [Vector(vertex.co) for vertex in mesh.vertices]
    targets = {
        name: Vector((value[0], value[1] * sign, value[2]))
        for name, value in MANUAL.items()
    }
    manual_names = {"FORWARD_INBOARD", "AFT_INBOARD", "FORWARD_OUTBOARD", "AFT_OUTBOARD", "BANK_AFT_INBOARD", "BANK_AFT_OUTBOARD"}
    station_vertices: list[tuple[str, int, int]] = []
    for station_index, (label, x, in_name, out_name) in enumerate(STATIONS):
        if side == "Port" and in_name:
            in_index = nearest_vertex(mesh, targets[in_name])
            out_index = nearest_vertex(mesh, targets[out_name])
        elif side == "Starboard" and station_reference:
            # Locate actual source vertices at the measured PORT station
            # coordinates reflected across the centreline.  This preserves
            # topology correspondence without constructing mirrored geometry.
            ref = station_reference[station_index]
            in_target = Vector((ref["inboard"]["coord"][0], -ref["inboard"]["coord"][1], ref["inboard"]["coord"][2]))
            out_target = Vector((ref["outboard"]["coord"][0], -ref["outboard"]["coord"][1], ref["outboard"]["coord"][2]))
            in_index = nearest_vertex(mesh, in_target)
            out_index = nearest_vertex(mesh, out_target)
        elif side == "Starboard" and in_name and in_name in manual_names:
            in_index = guide_vertex(mesh, x, 3.30 * sign, 4.93)
            out_index = guide_vertex(mesh, x, 6.80 * sign, 3.255)
        else:
            in_index = guide_vertex(mesh, x, 3.30 * sign, 4.93)
            out_index = guide_vertex(mesh, x, 6.80 * sign, 3.255)
        station_vertices.append((label, in_index, out_index))

    cross_paths = []
    for label, in_index, out_index in station_vertices:
        path = seam_path(mesh, in_index, out_index)
        cross_paths.append({"station": label, "inboard": in_index, "outboard": out_index, **path})
    inboard_paths = []
    outboard_paths = []
    for (label_a, ia, oa), (label_b, ib, ob) in zip(station_vertices, station_vertices[1:]):
        inboard_paths.append({"from": label_a, "to": label_b, **seam_path(mesh, ia, ib)})
        outboard_paths.append({"from": label_a, "to": label_b, **seam_path(mesh, oa, ob)})

    covers: list[dict[str, object]] = []
    all_face_sets: list[set[int]] = []
    for cover_index in range(6):
        left = cross_paths[cover_index]
        right = cross_paths[cover_index + 1]
        in_long = inboard_paths[cover_index]
        out_long = outboard_paths[cover_index]
        trace_vertices = list(in_long["vertices"])
        trace_edges = list(in_long["edges"])
        trace_vertices.extend(right["vertices"][1:])
        trace_edges.extend(right["edges"])
        trace_vertices.extend(reversed(out_long["vertices"][:-1]))
        trace_edges.extend(reversed(out_long["edges"]))
        trace_vertices.extend(reversed(left["vertices"][1:-1]))
        trace_edges.extend(reversed(left["edges"]))
        if trace_vertices[0] != trace_vertices[-1]:
            trace_vertices.append(trace_vertices[0])
        if len(trace_edges) != len(trace_vertices) - 1:
            raise RuntimeError(f"{side} cover {cover_index + 1}: source trace count mismatch")
        initial_corners = {
            "FORWARD_INBOARD": station_vertices[cover_index][1],
            "AFT_INBOARD": station_vertices[cover_index + 1][1],
            "FORWARD_OUTBOARD": station_vertices[cover_index][2],
            "AFT_OUTBOARD": station_vertices[cover_index + 1][2],
        }
        seed = sum((coords[index] for index in initial_corners.values()), Vector()) / 4.0
        selected_faces = enclosed_faces(mesh, set(trace_edges), seed)
        face_set = set(selected_faces)
        if any(face_set & previous for previous in all_face_sets):
            raise RuntimeError(f"{side} manual cover face overlap at cover {cover_index + 1}")
        all_face_sets.append(face_set)
        actual_boundary = boundary_for_faces(mesh, selected_faces)
        boundary_vertex_set = set(v for edge_index in actual_boundary for v in mesh.edges[edge_index].vertices)
        # A source seam can contain a short triangulation shoulder at a station;
        # when the guide vertex belongs to the preceding cell, resolve the
        # semantic corner to the nearest vertex on this *actual* boundary.
        # Port Cover 01 and the terminal aft seam retain the exact human IDs.
        corners: dict[str, int] = {}
        reference_by_name = {
            item["corner"]: Vector(item["coord"])
            for item in (corner_reference or [])
            if item.get("cover") == cover_index + 1
        }
        for name, initial_index in initial_corners.items():
            if side == "Port" and initial_index in boundary_vertex_set:
                corners[name] = initial_index
                continue
            target = Vector((reference_by_name[name].x, -reference_by_name[name].y, reference_by_name[name].z)) if name in reference_by_name else coords[initial_index]
            is_inboard = "INBOARD" in name
            candidates = [
                index for index in boundary_vertex_set
                if (coords[index].y * sign > 2.5 if is_inboard else coords[index].y * sign > 5.8)
            ]
            if not candidates:
                raise RuntimeError(f"{side} cover {cover_index + 1}: no boundary vertex for {name}")
            corners[name] = min(candidates, key=lambda index: (coords[index] - target).length)
        boundary_vertices, boundary_edges = ordered_boundary(mesh, actual_boundary, corners["FORWARD_INBOARD"])
        if any(index not in set(boundary_vertices) for index in corners.values()):
            raise RuntimeError(f"{side} cover {cover_index + 1}: semantic corner resolution left loop")
        face_tuples = [tuple(mesh.polygons[index].vertices) for index in selected_faces]
        covers.append({
            "cover": cover_index + 1,
            "corners": {name: {"vertex": index, "coord": list(coords[index])} for name, index in corners.items()},
            "boundary_vertices": boundary_vertices,
            "boundary_edges": boundary_edges,
            "boundary_fingerprint": boundary_fingerprint(actual_boundary),
            "face_indices": selected_faces,
            "face_fingerprint": face_fingerprint(face_tuples),
            "surface_area_m2": sum(mesh.polygons[index].area for index in selected_faces),
            "triangle_count": sum(len(mesh.polygons[index].vertices) - 2 for index in selected_faces),
            "longitudinal_extent_m": abs(coords[corners["FORWARD_INBOARD"]].x - coords[corners["AFT_INBOARD"]].x),
            "seed": list(seed),
            "initial_corners": {name: {"vertex": index, "coord": list(coords[index])} for name, index in initial_corners.items()},
            "trace": {"vertices": trace_vertices, "edges": trace_edges},
        })
    return {
        "side": side,
        "stations": [{"label": label, "inboard": {"vertex": i, "coord": list(coords[i])}, "outboard": {"vertex": o, "coord": list(coords[o])}} for label, i, o in station_vertices],
        "cross_paths": cross_paths,
        "inboard_paths": inboard_paths,
        "outboard_paths": outboard_paths,
        "covers": covers,
        "face_union": sorted(set().union(*all_face_sets)),
    }


def derive_manual_covers(mesh: bpy.types.Mesh) -> dict[str, object]:
    """Return source topology for both actual banks, with no mirroring."""
    port = derive_manual_side(mesh, "Port")
    port_reference = [
        {"cover": cover["cover"], "corner": name, "coord": data["coord"]}
        for cover in port["covers"]
        for name, data in cover["corners"].items()
    ]
    result = {"port": port, "starboard": derive_manual_side(mesh, "Starboard", port_reference, port["stations"])}
    port = result["port"]
    starboard = result["starboard"]
    # Actual starboard vertices are measured independently; this is a
    # correspondence report, not a procedural mirror operation.
    mirror_errors = []
    for pcover, scover in zip(port["covers"], starboard["covers"]):
        for name in ("FORWARD_INBOARD", "AFT_INBOARD", "FORWARD_OUTBOARD", "AFT_OUTBOARD"):
            p = Vector(pcover["corners"][name]["coord"])
            s = Vector(scover["corners"][name]["coord"])
            mirror_errors.append({"cover": pcover["cover"], "corner": name, "error_m": (s - Vector((p.x, -p.y, p.z))).length})
    result["starboard_mirror_errors"] = mirror_errors
    result["starboard_max_mirror_error_m"] = max((x["error_m"] for x in mirror_errors), default=0.0)
    result["manual_anchor_indices_source_mesh"] = {name: nearest_vertex(mesh, Vector(value)) for name, value in MANUAL.items()}
    result["manual_anchor_coordinates"] = MANUAL
    return result


def main() -> int:
    output = Path(arg("--out", str(Path(bpy.data.filepath).with_name("manual_p700_source_derivation.json"))))
    mesh = make_source_mesh()
    result = {"source_object": "Bridge", "component": 0, "vertex_count": len(mesh.vertices), **derive_manual_covers(mesh)}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({side: [{"cover": cover["cover"], "boundary_vertices": len(cover["boundary_vertices"]), "boundary_edges": len(cover["boundary_edges"]), "faces": len(cover["face_indices"]), "triangles": cover["triangle_count"], "area": cover["surface_area_m2"]} for cover in result[side]["covers"]] for side in ("port", "starboard")}, indent=2))
    bpy.data.meshes.remove(mesh)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
