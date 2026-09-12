"""Deterministic source-first Antey assembler.

The visible asset is made from evaluated world-space geometry from the
immutable source blend.  No silhouette fitting or procedural hull modelling is
performed here; authoring metadata, hardpoints and independent physics proxies
are added around the preserved visual shells.
"""
from __future__ import annotations

import argparse
import heapq
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict, deque
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from antey_ventral_selection import parse_selection as parse_ventral_selection, resolve_on_mesh as resolve_ventral_selection, strict_surface_faces as ventral_surface_faces, surface_incidence as ventral_surface_incidence

VENTRAL_SELECTION_FIXTURE = Path(__file__).resolve().parents[2] / "Content" / "submarines" / "Antey" / "selected_geometry_ventral.txt"

SOURCE_LENGTH = 9.775832176208496
SOURCE_BEAM = 1.7063244581222534
SOURCE_CENTER_X = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SOURCE_CENTER_Y = (-4.875378131866455 + 4.900454044342041) * 0.5
PRODUCTION_LENGTH = 154.0
PRODUCTION_BEAM = 18.2
LONG_SCALE = PRODUCTION_LENGTH / SOURCE_LENGTH
CROSS_SCALE = PRODUCTION_BEAM / SOURCE_BEAM
MANUAL_RUDDER_TOLERANCE = 1.0e-5
MANUAL_RUDDER_ANCHORS = {
    "Dorsal": {
        "A": (-61.699451447, 0.280976743, 4.906888008),
        "B": (-61.707115173, 0.280977070, 1.619024873),
        "C": (-65.129043579, 0.108756408, 1.628191113),
        "D": (-64.981201172, 0.108756408, 4.883985519),
    },
    "Ventral": {
        "A": (-61.859714508, 0.282813966, -4.096770763),
        "B": (-64.584289551, 0.234170035, -4.112565994),
        "C": (-64.584289551, 0.234170035, -4.458462715),
        "D": (-65.348480225, 0.108156495, -4.458462238),
        "E": (-65.348480225, 0.108156495, -2.163329363),
    },
}
MANUAL_RUDDER_ORDERS = {"Dorsal": ("A", "B", "C", "D", "A"), "Ventral": ("A", "B", "C", "D", "E", "A")}
# Vertex labels supplied by the artist refer to the current production
# candidate object.  Keep them beside the resolved immutable-source indices so
# provenance can distinguish the two coordinate spaces explicitly.
MANUAL_RUDDER_SUPPLIED_VERTEX_INDICES = {
    "Dorsal": {"A": 21079, "B": 21075, "C": 21330, "D": 21277},
    "Ventral": {"A": 428, "B": 497, "C": 598, "D": 396, "E": 583},
}

P700_NOMINAL_LAUNCHER_ANGLE_DEG = 40.0
P700_BODY_DIAMETER_M = 0.884145
P700_STOWED_DIAMETER_M = 1.262384
P700_LAUNCHER_ENVELOPE_DIAMETER_M = 1.35
P700_MINIMUM_REQUIRED_CLEARANCE_M = 0.025
P700_PAIR_LONGITUDINAL_OFFSET_M = 1.55
P700_STOWED_ENVELOPE_LENGTH_M = 5.5
# Authoritative continuous row retained from the accepted pre-shift
# SourceFirst launcher authoring.  The production alignment pitch is derived
# below from this one definition; no cover-centre placement participates.
P700_CONTINUOUS_ROW_ORIGINAL_X_M = (15.45, 18.55, 22.55, 25.65, 29.65, 32.75, 36.75, 39.85, 43.85, 46.95, 50.95, 54.05)
P700_CONTINUOUS_ROW_Y_M = 6.05
P700_CONTINUOUS_ROW_Z_M = 4.05

# Reviewed source-component identities are the production truth for LOD0 sail
# deployment. Geometry audit validates this table; it never infers state.
RETRACTABLE_SAIL_DEVICE_COMPONENTS = frozenset({5, 7, 8, 9, 10, 11, 12, 15, 17})
STATIC_SAIL_DEVICE_COMPONENTS = frozenset({6, 16, 18, 19, 22, 26, 27, 28, 29, 30, 31})


def args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--articulated", action="store_true")
    parser.add_argument("--mechanical", action="store_true")
    parser.add_argument("--manual-p700", action="store_true")
    parser.add_argument("--rudder-mask", type=Path, help="deterministic manual positive rudder mask JSON")
    parser.add_argument("--correct-rudder-mask-from", type=Path, help="existing source-first candidate to patch in-place into a new candidate")
    parser.add_argument("--realign-p700-from", type=Path, help="existing source-first candidate to realign P700 launcher rows into a new candidate")
    parser.add_argument("--one-pitch-shift-from", type=Path, help="rigidly shift an existing continuous P700 row by one measured launcher pitch")
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    return parser.parse_args(values)


def production_point(point: Vector) -> Vector:
    return Vector(((point.y - SOURCE_CENTER_Y) * LONG_SCALE,
                   -(point.x - SOURCE_CENTER_X) * CROSS_SCALE,
                   point.z * CROSS_SCALE))


def material(name: str, colour: tuple[float, float, float, float]) -> bpy.types.Material:
    result = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    result.diffuse_color = colour
    result.use_nodes = True
    principled = result.node_tree.nodes.get("Principled BSDF")
    if principled:
        principled.inputs["Base Color"].default_value = colour
        principled.inputs["Roughness"].default_value = 0.72
        principled.inputs["Metallic"].default_value = 0.18
    return result


def jsonable(value):
    """Convert Blender IDProperty groups/arrays to plain JSON values."""
    if isinstance(value, Vector):
        return [float(item) for item in value]
    if isinstance(value, Matrix):
        return [[float(item) for item in row] for row in value]
    if isinstance(value, dict) or hasattr(value, "keys"):
        return {str(key): jsonable(value[key]) for key in value.keys()}
    if isinstance(value, (list, tuple, set)):
        return [jsonable(item) for item in value]
    try:
        return jsonable(value.to_list())
    except AttributeError:
        return value


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    # The source file carries Blender startup datablocks (notably "Dots
    # Stroke" and "Material") that are not part of the Antey runtime
    # contract.  Purge only orphaned materials after the source objects are
    # removed; the two deliberate runtime materials are created afterwards.
    for datablock in list(bpy.data.materials):
        if datablock.users == 0:
            bpy.data.materials.remove(datablock)


def component_faces(mesh: bpy.types.Mesh) -> list[tuple[list[int], list[tuple[int, ...]]]]:
    parent = list(range(len(mesh.vertices)))

    def find(index: int) -> int:
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    def union(left: int, right: int) -> None:
        left_root, right_root = find(left), find(right)
        if left_root != right_root:
            parent[right_root] = left_root

    for poly in mesh.polygons:
        vertices = list(poly.vertices)
        if len(vertices) >= 2:
            for index in vertices[1:]:
                union(vertices[0], index)
    groups: dict[int, tuple[list[int], list[tuple[int, ...]]]] = {}
    for poly in mesh.polygons:
        vertices = list(poly.vertices)
        if len(set(vertices)) < 3:
            continue
        root = find(vertices[0])
        if root not in groups:
            groups[root] = ([], [])
        groups[root][1].append(tuple(vertices))
        groups[root][0].extend(vertices)
    result = []
    for vertices, faces in groups.values():
        unique = list(dict.fromkeys(vertices))
        result.append((unique, faces))
    result.sort(key=lambda item: sum(len(face) - 2 for face in item[1]), reverse=True)
    return result


def create_component(name: str, source_mesh: bpy.types.Mesh, indices: list[int], faces: list[tuple[int, ...]], source_object: str, role: str, hull_mat: bpy.types.Material, prop_mat: bpy.types.Material, articulated: bool = False) -> bpy.types.Object:
    index_map = {old: new for new, old in enumerate(indices)}
    # The evaluated source mesh has already been baked into production space.
    vertices = [source_mesh.vertices[index].co.copy() for index in indices]
    polygons = [tuple(index_map[index] for index in face) for face in faces]
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], polygons)
    mesh.update(calc_edges=True)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(prop_mat if role.startswith("PROPELLER") else hull_mat)
    obj["source_object"] = source_object
    obj["source_first_role"] = role
    obj["source_geometry"] = True
    obj["runtime_export"] = True
    obj["lod"] = 0
    obj["SOURCE_FACE_COUNT"] = len(faces)
    obj["SYNTHETIC_CLOSURE_FACE_COUNT"] = 0
    obj["source_face_fingerprint"] = face_fingerprint(faces)
    obj["source_boundary_edge_fingerprint"] = boundary_edge_fingerprint(faces)
    return obj


def face_fingerprint(faces: list[tuple[int, ...]]) -> str:
    """Stable identity for source faces without copying a large index list."""
    payload = "|".join(",".join(str(i) for i in face) for face in sorted(tuple(face) for face in faces))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def boundary_edge_fingerprint(faces: list[tuple[int, ...]]) -> str:
    edges: dict[tuple[int, int], int] = defaultdict(int)
    for face in faces:
        for left, right in zip(face, face[1:] + face[:1]):
            edge = (min(left, right), max(left, right))
            edges[edge] += 1
    payload = "|".join(f"{a}:{b}" for (a, b), count in sorted(edges.items()) if count == 1)
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def _point_segment(point: Vector, start: Vector, end: Vector) -> tuple[float, float]:
    direction = end - start
    denominator = direction.length_squared
    fraction = max(0.0, min(1.0, (point - start).dot(direction) / denominator if denominator else 0.0))
    return (point - (start + fraction * direction)).length, fraction


def _source_seam_path(mesh: bpy.types.Mesh, start: int, end: int, forbidden_edges: set[int] | None = None, forbidden_vertices: set[int] | None = None) -> dict[str, object]:
    """Follow a narrow, monotone edge corridor between manual anchors."""
    coords = [Vector(vertex.co) for vertex in mesh.vertices]
    a, b = coords[start], coords[end]
    distances: list[float] | None = None
    previous: list[tuple[int, int] | None] | None = None
    forbidden_edges = forbidden_edges or set()
    forbidden_vertices = forbidden_vertices or set()
    for corridor in (0.08, 0.15, 0.25, 0.40, 0.65, 1.0, 1.5, 2.5, 4.0):
        adjacency: list[list[tuple[int, int, float]]] = [[] for _ in coords]
        for edge in mesh.edges:
            left, right = edge.vertices
            if edge.index in forbidden_edges or (left in forbidden_vertices and left not in {start, end}) or (right in forbidden_vertices and right not in {start, end}):
                continue
            p, q = coords[left], coords[right]
            midpoint_distance, _ = _point_segment((p + q) * 0.5, a, b)
            left_distance, left_t = _point_segment(p, a, b)
            right_distance, right_t = _point_segment(q, a, b)
            if max(midpoint_distance, left_distance, right_distance) > corridor:
                continue
            delta = right_t - left_t
            weight = (q - p).length * (1.0 + 100.0 * midpoint_distance * midpoint_distance) + max(0.0, -delta) * 50.0
            adjacency[left].append((right, edge.index, weight))
            adjacency[right].append((left, edge.index, weight))
        trial_distances = [float("inf")] * len(coords)
        trial_previous: list[tuple[int, int] | None] = [None] * len(coords)
        trial_distances[start] = 0.0
        queue = [(0.0, start)]
        while queue:
            distance, vertex = heapq.heappop(queue)
            if distance != trial_distances[vertex]:
                continue
            if vertex == end:
                break
            for neighbour, edge_index, weight in adjacency[vertex]:
                candidate = distance + weight
                if candidate < trial_distances[neighbour]:
                    trial_distances[neighbour] = candidate
                    trial_previous[neighbour] = (vertex, edge_index)
                    heapq.heappush(queue, (candidate, neighbour))
        if math.isfinite(trial_distances[end]):
            distances, previous = trial_distances, trial_previous
            break
    if distances is None or previous is None:
        raise RuntimeError(f"manual rudder source seam path not found {start}->{end}")
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
    if len(vertices) != len(set(vertices)):
        raise RuntimeError("manual rudder source seam path is not simple")
    return {"vertices": vertices, "edges": edges, "coordinates": [list(coords[index]) for index in vertices], "length_m": sum((coords[u] - coords[v]).length for u, v in zip(vertices, vertices[1:]))}


def _edge_face_maps(mesh: bpy.types.Mesh) -> tuple[dict[tuple[int, int], int], dict[int, list[int]]]:
    key_to_edge = {tuple(sorted(edge.vertices)): edge.index for edge in mesh.edges}
    faces_by_edge: dict[int, list[int]] = defaultdict(list)
    for polygon in mesh.polygons:
        values = list(polygon.vertices)
        for left, right in zip(values, values[1:] + values[:1]):
            edge_index = key_to_edge.get(tuple(sorted((left, right))))
            if edge_index is not None:
                faces_by_edge[edge_index].append(polygon.index)
    return key_to_edge, faces_by_edge


def _faces_boundary(mesh: bpy.types.Mesh, face_indices: list[int]) -> set[int]:
    key_to_edge, _ = _edge_face_maps(mesh)
    counts: dict[int, int] = defaultdict(int)
    for face_index in face_indices:
        for edge_key in mesh.polygons[face_index].edge_keys:
            edge_index = key_to_edge.get(tuple(sorted(edge_key)))
            if edge_index is not None:
                counts[edge_index] += 1
    return {edge_index for edge_index, count in counts.items() if count == 1}


def _inside_xz(point: Vector, outline: list[Vector]) -> bool:
    hit = False
    for left, right in zip(outline, outline[1:] + outline[:1]):
        if ((left.z > point.z) != (right.z > point.z)) and point.x < (right.x - left.x) * (point.z - left.z) / (right.z - left.z) + left.x:
            hit = not hit
    return hit


def _manual_component_faces(mesh: bpy.types.Mesh, allowed: list[tuple[int, ...]], boundary_edges: set[int], seed: Vector) -> list[int]:
    """Return the source face island on the movable side of a traced loop."""
    allowed_indices: set[int] = set()
    tuple_to_indices: dict[tuple[int, ...], list[int]] = defaultdict(list)
    for polygon in mesh.polygons:
        tuple_to_indices[tuple(polygon.vertices)].append(polygon.index)
    for face in allowed:
        candidates = tuple_to_indices.get(tuple(face), [])
        if candidates:
            allowed_indices.add(candidates[0])
    _, faces_by_edge = _edge_face_maps(mesh)
    adjacency: dict[int, list[int]] = defaultdict(list)
    for edge_index, faces in faces_by_edge.items():
        if edge_index in boundary_edges or len(faces) != 2:
            continue
        left, right = faces
        if left in allowed_indices and right in allowed_indices:
            adjacency[left].append(right)
            adjacency[right].append(left)
    if not allowed_indices:
        return []
    seed_face = min(allowed_indices, key=lambda index: (mesh.polygons[index].center - seed).length)
    visited = {seed_face}
    queue = deque([seed_face])
    while queue:
        face_index = queue.popleft()
        for neighbour in adjacency.get(face_index, []):
            if neighbour not in visited:
                visited.add(neighbour)
                queue.append(neighbour)
    return sorted(visited)


def _counterpart_vertex(mesh: bpy.types.Mesh, index: int, target_y: float, allowed_vertices: set[int]) -> int | None:
    source = mesh.vertices[index].co
    candidates = []
    for candidate in allowed_vertices:
        if candidate == index:
            continue
        point = mesh.vertices[candidate].co
        xz_distance = math.hypot(point.x - source.x, point.z - source.z)
        if xz_distance > 0.085 or abs(point.y - target_y) > 0.20:
            continue
        candidates.append((xz_distance * 20.0 + abs(point.y - target_y), candidate))
    return min(candidates)[1] if candidates else None


def _close_source_surface(mesh: bpy.types.Mesh, source_faces: list[tuple[int, ...]], boundary_edges: set[int]) -> tuple[list[tuple[int, ...]], list[tuple[int, ...]]]:
    """Bridge the two source skins with quads made only from source vertices.

    The evaluated source carries the thin rudder as two triangulated skins.
    A previous pass capped a handful of edges by a Y threshold, leaving open
    and non-manifold edges.  Pairing each perimeter edge with its actual
    opposite-side source edge preserves the measured skin spacing and closes
    the control-surface volume without inventing thickness.
    """
    if not source_faces or not boundary_edges:
        return source_faces, []
    used_vertices = {index for face in source_faces for index in face}
    perimeter = set(boundary_edges)
    key_to_edge, _ = _edge_face_maps(mesh)
    points = {index: mesh.vertices[index].co for index in used_vertices}
    x_min = min(point.x for point in points.values()) - 0.12
    x_max = max(point.x for point in points.values()) + 0.12
    z_min = min(point.z for point in points.values()) - 0.12
    z_max = max(point.z for point in points.values()) + 0.12
    # Spatially bin the source vertices once.  Only perimeter endpoints query
    # the index, so a large source Hull remains inexpensive to process.
    grid = 0.05
    region_vertices = [index for index, vertex in enumerate(mesh.vertices) if x_min <= vertex.co.x <= x_max and z_min <= vertex.co.z <= z_max]
    bins: dict[tuple[int, int], list[int]] = defaultdict(list)
    for index in region_vertices:
        point = mesh.vertices[index].co
        if index not in used_vertices:
            bins[(math.floor(point.x / grid), math.floor(point.z / grid))].append(index)
    perimeter_vertices = {vertex for edge_index in boundary_edges for vertex in mesh.edges[edge_index].vertices}

    def counterpart(index: int) -> int | None:
        point = mesh.vertices[index].co
        sign = 1.0 if point.y >= 0.0 else -1.0
        candidates = []
        cell_x, cell_z = math.floor(point.x / grid), math.floor(point.z / grid)
        for dx in range(-2, 3):
            for dz in range(-2, 3):
                for candidate in bins.get((cell_x + dx, cell_z + dz), []):
                    other = mesh.vertices[candidate].co
                    if other.y * sign >= -0.02:
                        continue
                    distance = math.hypot(other.x - point.x, other.z - point.z)
                    if distance <= 0.10:
                        candidates.append((distance, abs(other.y + point.y), candidate))
        return min(candidates)[2] if candidates else None

    augmented = list(source_faces)
    caps: list[tuple[int, ...]] = []
    existing = {tuple(sorted(face)) for face in source_faces}
    source_edge_keys = {tuple(sorted((left, right))) for face in source_faces for left, right in zip(face, face[1:] + face[:1])}
    edge_counts: dict[tuple[int, int], int] = defaultdict(int)
    for face in source_faces:
        for left, right in zip(face, face[1:] + face[:1]):
            edge_counts[tuple(sorted((left, right)))] += 1
    paired_edges: set[tuple[int, int]] = set()
    for edge_index in sorted(boundary_edges):
        left, right = mesh.edges[edge_index].vertices
        left_pair = counterpart(left)
        right_pair = counterpart(right)
        if left_pair is None or right_pair is None or left_pair == right_pair:
            continue
        opposite_edge = key_to_edge.get(tuple(sorted((left_pair, right_pair))))
        if opposite_edge not in perimeter:
            continue
        pair = tuple(sorted((edge_index, opposite_edge)))
        if pair in paired_edges:
            continue
        # A side bridge that is already a source edge would be incident to a
        # third face and make the extracted volume non-manifold.  Leave that
        # perimeter to the deterministic hole-fill pass below instead.
        if tuple(sorted((left, left_pair))) in source_edge_keys or tuple(sorted((right, right_pair))) in source_edge_keys:
            continue
        cap_edges = [tuple(sorted((left, right))), tuple(sorted((right, right_pair))), tuple(sorted((right_pair, left_pair))), tuple(sorted((left_pair, left)))]
        if any(edge_counts[edge] >= 2 for edge in cap_edges):
            continue
        paired_edges.add(pair)
        cap = (left, right, right_pair, left_pair)
        canonical = tuple(sorted(cap))
        if len(set(cap)) == 4 and canonical not in existing:
            augmented.append(cap)
            caps.append(cap)
            existing.add(canonical)
            for edge in cap_edges:
                edge_counts[edge] += 1
    # Any residual boundary should be a small source seam loop.  Fill only
    # simple degree-two loops; branched source topology is retained in the
    # provenance rather than patched with an invented rectangle.
    def boundary_loops(faces: list[tuple[int, ...]]) -> list[list[int]]:
        counts: dict[tuple[int, int], int] = defaultdict(int)
        for face in faces:
            for left, right in zip(face, face[1:] + face[:1]):
                counts[tuple(sorted((left, right)))] += 1
        edges = {edge for edge, count in counts.items() if count == 1}
        adjacency: dict[int, list[int]] = defaultdict(list)
        for left, right in edges:
            adjacency[left].append(right); adjacency[right].append(left)
        loops: list[list[int]] = []
        remaining = set(edges)
        while remaining:
            seed_edge = min(remaining)
            start = seed_edge[0]
            if len(adjacency[start]) != 2:
                remaining.discard(seed_edge)
                continue
            loop = [start]; previous = None; current = start; traversed: list[tuple[int, int]] = []
            closed = False
            for _ in range(len(remaining) + 1):
                candidates = [tuple(sorted((current, vertex))) for vertex in adjacency[current] if vertex != previous]
                candidates = [edge for edge in candidates if edge in remaining or edge == seed_edge]
                if not candidates:
                    break
                edge = candidates[0]
                next_vertex = edge[1] if edge[0] == current else edge[0]
                if next_vertex == start:
                    remaining.discard(edge); closed = True; break
                if next_vertex in loop:
                    break
                remaining.discard(edge); traversed.append(edge); loop.append(next_vertex); previous, current = current, next_vertex
            if not closed:
                # Guarantee progress for branched/non-manifold source seams.
                for edge in traversed:
                    remaining.discard(edge)
            elif len(loop) >= 3 and len(loop) == len(set(loop)):
                loops.append(loop)
        return loops
    for loop in boundary_loops(augmented):
        cap = tuple(loop)
        canonical = tuple(sorted(cap))
        if len(set(cap)) == len(cap):
            cap_edges = [tuple(sorted((left, right))) for left, right in zip(cap, cap[1:] + cap[:1])]
            if any(edge_counts[edge] >= 2 for edge in cap_edges):
                continue
            # A residual source loop can already have one coplanar source
            # polygon with the same vertex set.  Add the reverse winding as
            # the second side of that measured wall rather than dropping the
            # closure merely because the canonical vertex set is present.
            augmented.append(tuple(reversed(cap))); caps.append(tuple(reversed(cap))); existing.add(canonical)
            for edge in cap_edges:
                edge_counts[edge] += 1
    return augmented, caps


def _manual_rudder_extraction(source_name: str, component_index: int, mesh: bpy.types.Mesh, component_faces_in: list[tuple[int, ...]]) -> tuple[dict[tuple[str, str], list[tuple[int, ...]]], list[tuple[int, ...]], dict[tuple[str, str], dict[str, object]]]:
    """Resolve the supplied source anchors into irregular movable rudders."""
    if component_index != 0 or source_name not in {"Bridge", "Hull"}:
        return {}, component_faces_in, {}
    side = "Dorsal" if source_name == "Bridge" else "Ventral"
    anchors = MANUAL_RUDDER_ANCHORS[side]
    coords = [Vector(vertex.co) for vertex in mesh.vertices]
    resolved = {name: min(range(len(coords)), key=lambda index: (coords[index] - Vector(value)).length) for name, value in anchors.items()}
    errors = {name: (coords[index] - Vector(anchors[name])).length for name, index in resolved.items()}
    if max(errors.values()) > MANUAL_RUDDER_TOLERANCE:
        raise RuntimeError(f"manual {side.lower()} rudder anchor exceeds tolerance: {errors}")
    order = MANUAL_RUDDER_ORDERS[side]
    paths = []
    used_path_edges: set[int] = set()
    used_path_vertices: set[int] = set()
    for left, right in zip(order, order[1:]):
        path = _source_seam_path(mesh, resolved[left], resolved[right], used_path_edges, used_path_vertices)
        paths.append({"from": left, "to": right, **path})
        used_path_edges.update(path["edges"])
        used_path_vertices.update(path["vertices"])
    boundary_edges = set(edge for path in paths for edge in path["edges"])
    seed = sum((Vector(anchors[name]) for name in anchors), Vector()) / len(anchors)
    if side == "Dorsal":
        # The manual anchors lie on the +Y skin.  Resolve the corresponding
        # -Y seam using X/Z proximity, then flood-fill both source skins with
        # the two physical loops as barriers.  This keeps the movable part
        # small while retaining the measured source thickness.
        key_to_edge, _ = _edge_face_maps(mesh)
        outline_points = [Vector(point) for path in paths for point in path["coordinates"]]
        x_min = min(point.x for point in outline_points) - 0.12
        x_max = max(point.x for point in outline_points) + 0.12
        z_min = min(point.z for point in outline_points) - 0.12
        z_max = max(point.z for point in outline_points) + 0.12
        opposite_candidates = [index for index, point in enumerate(coords) if point.y < -0.02 and x_min <= point.x <= x_max and z_min <= point.z <= z_max]
        opposite_edges: set[int] = set()
        for path in paths:
            mapped: list[int | None] = []
            for index in path["vertices"]:
                point = coords[index]
                candidates = [(math.hypot(coords[candidate].x - point.x, coords[candidate].z - point.z), candidate) for candidate in opposite_candidates if math.hypot(coords[candidate].x - point.x, coords[candidate].z - point.z) <= 0.10]
                mapped.append(min(candidates)[1] if candidates else None)
            for left, right in zip(mapped, mapped[1:]):
                if left is not None and right is not None:
                    edge_index = key_to_edge.get(tuple(sorted((left, right))))
                    if edge_index is not None:
                        opposite_edges.add(edge_index)
        selected_positive = _manual_component_faces(mesh, component_faces_in, boundary_edges | opposite_edges, seed)
        selected_negative = _manual_component_faces(mesh, component_faces_in, boundary_edges | opposite_edges, Vector((seed.x, -abs(seed.y), seed.z)))
        selected_indices = sorted(set(selected_positive) | set(selected_negative))
    else:
        # The artist-selected Ventral graph is the ownership contract.  The
        # previous abs(centroid.y) >= 0.18 heuristic retained only an outer
        # skin and silently left valid selected topology in the static lower
        # hull.  Resolve the committed world-space fixture against the complete
        # production-normalized source mesh and transfer every source face that
        # makes an explicitly selected vertex/edge part of a real polygon.
        ventral_selection = parse_ventral_selection(VENTRAL_SELECTION_FIXTURE)
        ventral_resolved = resolve_ventral_selection(mesh, ventral_selection, require_all_edges=True)
        ventral_surface = ventral_surface_faces(mesh, ventral_resolved, component_faces_in)
        selected_indices = list(ventral_surface["faceIndices"])
        if not ventral_surface["pass"]:
            raise RuntimeError(
                "Ventral selected-geometry source ownership is incomplete: "
                f"vertices={ventral_surface['missingSurfaceVertices']} "
                f"edges={ventral_surface['missingSurfaceEdges']}"
            )
    if not selected_indices:
        raise RuntimeError(f"manual {side.lower()} rudder extraction found no source faces")
    source_face_tuples = [tuple(mesh.polygons[index].vertices) for index in selected_indices]
    actual_boundary = _faces_boundary(mesh, selected_indices)
    augmented_faces, closure_faces = _close_source_surface(mesh, source_face_tuples, actual_boundary)
    # The source boundary loop is the manually traced path, while the complete
    # boundary set includes every triangulated edge around the selected island.
    ordered_loop = [paths[0]["vertices"][0]]
    for path in paths:
        ordered_loop.extend(path["vertices"][1:])
    _, faces_by_edge = _edge_face_maps(mesh)
    fixed_faces = []
    for edge_index in sorted(actual_boundary):
        for face_index in faces_by_edge.get(edge_index, []):
            if face_index not in selected_indices:
                fixed_faces.append(tuple(mesh.polygons[face_index].vertices))
    fixed_fingerprint = face_fingerprint(list(dict.fromkeys(fixed_faces))) if fixed_faces else "NO_ADJACENT_FIXED_SOURCE_FACES"
    points = [coords[index] for face in augmented_faces for index in face]
    hinge_pair = ("A", "B") if side == "Dorsal" else ("D", "E")
    hinge_start = coords[resolved[hinge_pair[0]]]
    hinge_end = coords[resolved[hinge_pair[1]]]
    hinge_axis = (hinge_end - hinge_start).normalized()
    metadata = {
        "side": side,
        "sourceObject": source_name,
        "sourceComponent": component_index,
        "movableObject": f"SM_Antey_LOD0_Rudder_{side}",
        "fixedStabilizerObject": "SM_Antey_LOD0_Hull" if side == "Dorsal" else "SM_Antey_LOD0_Hull_LowerSource",
        "manualCoordinates": {name: list(value) for name, value in anchors.items()},
        "suppliedObjectVertexIndices": dict(MANUAL_RUDDER_SUPPLIED_VERTEX_INDICES[side]),
        "resolvedIndices": resolved,
        "anchorErrorsM": errors,
        "orderedBoundaryLoop": ordered_loop,
        "boundaryEdgeIds": sorted(actual_boundary),
        "boundaryFingerprint": boundary_edge_fingerprint(source_face_tuples),
        "movableFaceIndices": selected_indices,
        "movableFaceFingerprint": face_fingerprint(source_face_tuples),
        "fixedStabilizerFaceFingerprint": fixed_fingerprint,
        "surfaceAreaM2": sum(mesh.polygons[index].area for index in selected_indices),
        "bounds": [[min(point[i] for point in points) for i in range(3)], [max(point[i] for point in points) for i in range(3)]],
        "sourceBoundaryPaths": paths,
        "closureFaceCount": len(closure_faces),
        "closureFaceFingerprint": face_fingerprint(closure_faces) if closure_faces else "NO_SOURCE_CAPS_REQUIRED",
        "hinge": {"path": f"{hinge_pair[0]}->{hinge_pair[1]}", "endpoints": [list(hinge_start), list(hinge_end)], "pivot": list((hinge_start + hinge_end) * 0.5), "axisVectorProduction": list(hinge_axis), "axis": "LOCAL_Z_APPROXIMATE_SOURCE_HINGE_VECTOR"},
    }
    if side == "Ventral":
        metadata["selectedGeometryFixture"] = str(VENTRAL_SELECTION_FIXTURE)
        metadata["selectedGeometryVertexCount"] = len(ventral_selection["vertices"])
        metadata["selectedGeometryEdgeCount"] = len(ventral_selection["edges"])
        metadata["selectedGeometrySurfaceFaceCount"] = len(selected_indices)
        metadata["selectedGeometrySurfaceOwnership"] = "ALL_SELECTED_VERTICES_AND_EDGES_POLYGON_INCIDENT"
    key = ("RUDDER", side)
    selected_counter = Counter(tuple(face) for face in source_face_tuples)
    remaining_faces = []
    for face in component_faces_in:
        canonical = tuple(face)
        if selected_counter[canonical]:
            selected_counter[canonical] -= 1
        else:
            remaining_faces.append(face)
    return {key: augmented_faces}, remaining_faces, {key: metadata}


def source_role(source_name: str, component_index: int, points: list[Vector]) -> tuple[str, str]:
    minimum = [min(point[i] for point in points) for i in range(3)]
    maximum = [max(point[i] for point in points) for i in range(3)]
    centre = [(minimum[i] + maximum[i]) * 0.5 for i in range(3)]
    production_width = maximum[1] - minimum[1]
    if component_index == 0 and source_name == "Bridge":
        return "SM_Antey_LOD0_Hull", "MAIN_HULL_UPPER"
    if component_index == 0 and source_name == "Hull":
        return "SM_Antey_LOD0_Hull_LowerSource", "MAIN_HULL_LOWER"
    # The two wide stern components are the real source horizontal planes.
    if source_name == "Bridge" and maximum[0] < -58.0 and production_width > 2.0:
        side = "Port" if centre[1] > 0.0 else "Starboard"
        return f"SM_Antey_LOD0_SternPlane_{side}", "STERN_PLANE_" + side.upper()
    if source_name == "Bridge" and minimum[0] > 50.0 and production_width > 2.0:
        side = "Port" if centre[1] > 0.0 else "Starboard"
        return f"SM_Antey_LOD0_BowPlane_{side}", "BOW_PLANE_" + side.upper()
    if source_name == "Hull" and maximum[0] < -70.0 and abs(centre[1]) > 0.8:
        side = "Port" if centre[1] > 0.0 else "Starboard"
        return f"SM_Propeller_{side}_Source_{component_index:02d}", "PROPELLER_" + side.upper()
    if source_name == "Bridge" and component_index == 1:
        return "SM_Antey_LOD0_Sail", "SAIL"
    if source_name == "Bridge" and maximum[2] > 6.0 and abs(centre[0]) < 50.0:
        return f"SM_Antey_LOD0_SailDevice_{component_index:02d}", "SAIL_DEVICE"
    return f"SM_Antey_LOD0_SourceDetail_{source_name}_{component_index:02d}", "SMALL_STATIC_DETAIL"


def apply_explicit_sail_device_contract(obj: bpy.types.Object, component_index: int) -> None:
    if component_index not in RETRACTABLE_SAIL_DEVICE_COMPONENTS | STATIC_SAIL_DEVICE_COMPONENTS:
        raise RuntimeError(f"Unreviewed LOD0 sail-device component: {component_index}")
    for key in ("DEVICE_TYPE", "DEVICE_CONFIDENCE", "DEPLOYMENT_PIVOT", "DEVICE_CLASSIFICATION_BASIS"):
        if key in obj:
            del obj[key]
    if component_index in RETRACTABLE_SAIL_DEVICE_COMPONENTS:
        obj["DEVICE_DEPLOYMENT"] = "RETRACTABLE"
        obj["MOTION"] = "TRANSLATION"
        obj["AXIS"] = "LOCAL_Z"
        obj["SIMULATION_OWNS_STATE"] = True
    else:
        obj["DEVICE_DEPLOYMENT"] = "STATIC"
        obj["MOTION"] = "NONE"
        obj["AXIS"] = "NONE"
        obj["SIMULATION_OWNS_STATE"] = False


def audit_sail_device_deployment(lod0: list[bpy.types.Object]) -> None:
    """Validate explicit sail-device deployment contracts against production geometry."""
    sail = bpy.data.objects.get("SM_Antey_LOD0_Sail")
    if sail is None:
        raise RuntimeError("Sail-device audit requires the LOD0 sail")
    sail_top = max((sail.matrix_world @ vertex.co).z for vertex in sail.data.vertices)
    for obj in (item for item in lod0 if item.get("source_first_role") == "SAIL_DEVICE"):
        world_points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
        lower = min(point.z for point in world_points)
        upper = max(point.z for point in world_points)
        component_index = int(obj.get("source_component", -1))
        if component_index not in RETRACTABLE_SAIL_DEVICE_COMPONENTS | STATIC_SAIL_DEVICE_COMPONENTS:
            raise RuntimeError(f"Unclassified LOD0 sail device: {obj.name}")
        expected_retractable = component_index in RETRACTABLE_SAIL_DEVICE_COMPONENTS
        if expected_retractable:
            if (obj.get("DEVICE_DEPLOYMENT"), obj.get("MOTION"), obj.get("AXIS"), obj.get("SIMULATION_OWNS_STATE")) != ("RETRACTABLE", "TRANSLATION", "LOCAL_Z", True):
                raise RuntimeError(f"Retractable sail-device contract is inconsistent: {obj.name}")
            stowed_top = sail_top - 0.02
            stow_translation = stowed_top - upper
            if not math.isfinite(stow_translation) or stow_translation > 0.0 or upper + stow_translation > sail_top:
                raise RuntimeError(f"Retractable sail device cannot produce a valid stowed pose: {obj.name}")
        else:
            if (obj.get("DEVICE_DEPLOYMENT"), obj.get("MOTION"), obj.get("AXIS"), obj.get("SIMULATION_OWNS_STATE")) != ("STATIC", "NONE", "NONE", False):
                raise RuntimeError(f"Static sail-device contract is inconsistent: {obj.name}")
            if upper > sail_top + 0.25 and upper - lower >= 2.0:
                raise RuntimeError(f"Static sail device forms suspicious raised geometry: {obj.name}")


def set_pivot(obj: bpy.types.Object, role: str) -> None:
    if role.startswith("STERN_PLANE") or role.startswith("BOW_PLANE"):
        hinge_x = max(vertex.co.x for vertex in obj.data.vertices) if role.startswith("STERN") else min(vertex.co.x for vertex in obj.data.vertices)
        hinge_y = sum(vertex.co.y for vertex in obj.data.vertices) / len(obj.data.vertices)
        hinge_z = sum(vertex.co.z for vertex in obj.data.vertices) / len(obj.data.vertices)
        pivot = Vector((hinge_x, hinge_y, hinge_z))
        obj.data.transform(Matrix.Translation(-pivot))
        obj.location = pivot
        obj["CONTROL_SURFACE_ROLE"] = "BOW_DEPTH_PLANE" if role.startswith("BOW") else "STERN_DEPTH_PLANE"
        obj["ARTICULATION"] = "ROTATION"
        obj["HINGE_AXIS"] = "LOCAL_Y"
        obj["SIMULATION_OWNS_ANGLE"] = True
    elif role.startswith("PROPELLER"):
        centre = sum((vertex.co for vertex in obj.data.vertices), Vector()) / len(obj.data.vertices)
        obj.data.transform(Matrix.Translation(-centre))
        obj.location = centre
        obj["ROTATION_AXIS"] = "LOCAL_X"
        obj["PROPELLER_SOURCE_DERIVED"] = True
    elif role.startswith("RUDDER"):
        hinge_x = max(vertex.co.x for vertex in obj.data.vertices)
        hinge_y = sum(vertex.co.y for vertex in obj.data.vertices) / len(obj.data.vertices)
        hinge_z = sum(vertex.co.z for vertex in obj.data.vertices) / len(obj.data.vertices)
        pivot = Vector((hinge_x, hinge_y, hinge_z))
        obj.data.transform(Matrix.Translation(-pivot))
        obj.location = pivot
        obj["CONTROL_SURFACE_ROLE"] = "RUDDER"
        obj["ARTICULATION"] = "ROTATION"
        obj["HINGE_AXIS"] = "LOCAL_Z"
        obj["SIMULATION_OWNS_ANGLE"] = True


def set_manual_rudder_pivot(obj: bpy.types.Object, metadata: dict[str, object]) -> None:
    """Re-root the extracted surface on the traced source hinge seam."""
    hinge = metadata["hinge"]
    pivot = Vector(hinge["pivot"])
    world_points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    linear = obj.matrix_world.to_3x3().copy()
    inverse = linear.inverted()
    obj.location = pivot
    for vertex, point in zip(obj.data.vertices, world_points):
        vertex.co = inverse @ (point - pivot)
    obj.data.update()
    obj["CONTROL_SURFACE_ROLE"] = "RUDDER"
    obj["ARTICULATION"] = "ROTATION"
    obj["HINGE_AXIS"] = "LOCAL_Z_APPROXIMATE_SOURCE_HINGE_VECTOR"
    obj["HINGE_AXIS_VECTOR_PRODUCTION"] = list(hinge["axisVectorProduction"])
    obj["HINGE_PIVOT_WORLD"] = list(pivot)
    obj["HINGE_SOURCE"] = "MANUAL_SOURCE_SEAM_FIXED_MOVABLE_BOUNDARY"
    obj["SIMULATION_OWNS_ANGLE"] = True
    obj["MANUAL_ANCHOR"] = True
    obj["MOVEMENT_TYPE"] = "HINGE_ROTATION"


def set_hinge_pivot(obj: bpy.types.Object, role: str, side: str, manual_record: dict[str, object] | None = None) -> None:
    """Place a cover/door origin on the source panel's physical hinge edge."""
    vertices = [vertex.co for vertex in obj.data.vertices]
    if not vertices:
        return
    if manual_record is not None:
        # The manual source derivation provides the semantic inboard corners.
        # Their midpoint is on the actual source seam and avoids the former
        # broad Y-threshold origin, which could land off the curved hinge line.
        corners = manual_record["corners"]
        forward = Vector(corners["FORWARD_INBOARD"]["coord"])
        aft = Vector(corners["AFT_INBOARD"]["coord"])
        nearest_forward = min(vertices, key=lambda point: (point - forward).length)
        nearest_aft = min(vertices, key=lambda point: (point - aft).length)
        pivot = (nearest_forward + nearest_aft) * 0.5
    else:
        hinge_y = min(vertex.y for vertex in vertices) if side == "Port" else max(vertex.y for vertex in vertices)
        hinge_vertices = [vertex for vertex in vertices if abs(vertex.y - hinge_y) < 0.08]
        if not hinge_vertices:
            hinge_vertices = vertices
        pivot = Vector((sum(vertex.x for vertex in hinge_vertices) / len(hinge_vertices), hinge_y, sum(vertex.z for vertex in hinge_vertices) / len(hinge_vertices)))
    obj.data.transform(Matrix.Translation(-pivot))
    obj.location = pivot
    obj["ARTICULATION"] = "ROTATION"
    obj["HINGE_AXIS"] = "LOCAL_X"
    obj["HINGE_SOURCE"] = "SOURCE_PANEL_BOUNDARY_EDGE"
    obj["SIMULATION_OWNS_STATE"] = True
    obj["MOVEMENT_TYPE"] = "HINGE_ROTATION"
    obj["COVER_SIDE"] = side
    obj["COVER_ROLE"] = role


def positive_bow_plane_face_split(source_name: str, component_index: int, source_mesh: bpy.types.Mesh, component_faces_in: list[tuple[int, ...]]) -> tuple[dict[tuple[str, str], list[tuple[int, ...]]], list[tuple[int, ...]]]:
    """Extract only the explicit source bow-plane mask.

    Rudder ownership is resolved by ``_manual_rudder_extraction``.  Keeping
    this mask separate is important: a generic articulation predicate must not
    consume unrelated source exterior faces just because they are near a
    control surface.
    """
    if component_index != 0 or source_name not in {"Bridge", "Hull"}:
        return {}, component_faces_in
    source_mesh.update()
    extracted: dict[tuple[str, str], list[tuple[int, ...]]] = defaultdict(list)
    remaining: list[tuple[int, ...]] = []
    for face in component_faces_in:
        points = [source_mesh.vertices[index].co for index in face]
        center = sum(points, Vector()) / len(points)
        key: tuple[str, str] | None = None
        if source_name == "Bridge" and 53.0 < center.x < 68.0 and abs(center.y) > 7.5 and 0.5 < center.z < 3.0:
            key = ("BOW_PLANE", "Port" if center.y > 0.0 else "Starboard")
        if key:
            extracted[key].append(face)
        else:
            remaining.append(face)
    return extracted, remaining


def articulation_face_split(source_name: str, component_index: int, source_mesh: bpy.types.Mesh, component_faces_in: list[tuple[int, ...]]) -> tuple[dict[tuple[str, str], list[tuple[int, ...]]], list[tuple[int, ...]]]:
    """Compatibility wrapper for the positive control-surface partition.

    The old implementation also selected a broad dorsal/ventral region and
    silently discarded it.  Rudders now use the manual positive topology mask;
    this wrapper intentionally owns bow planes only.
    """
    return positive_bow_plane_face_split(source_name, component_index, source_mesh, component_faces_in)


def mechanical_cover_split(source_name: str, component_index: int, source_mesh: bpy.types.Mesh, component_faces_in: list[tuple[int, ...]]) -> tuple[dict[tuple[str, int], list[tuple[int, ...]]], list[tuple[int, ...]]]:
    """Extract the six-per-side source casing panels used by P700 covers.

    The source has a continuous triangulated casing, so a cover is defined by
    its visible seam field: upper shoulder faces, side sign, and the six
    longitudinal seam bays.  The mask is deliberately surface constrained
    (normal/curvature) and is recorded in provenance; it is not a procedural
    rectangle or a new hull shape.
    """
    if source_name != "Bridge" or component_index != 0:
        return {}, component_faces_in
    bins = [17.0 + index * 7.1 for index in range(6)]
    extracted: dict[tuple[str, int], list[tuple[int, ...]]] = defaultdict(list)
    remaining: list[tuple[int, ...]] = []
    source_mesh.update()
    normals = {tuple(poly.vertices): poly.normal.copy() for poly in source_mesh.polygons}
    for face in component_faces_in:
        points = [source_mesh.vertices[index].co for index in face]
        center = sum(points, Vector()) / len(points)
        normal = normals.get(tuple(face), Vector((0.0, 0.0, 1.0)))
        side = "Port" if center.y > 4.8 else ("Starboard" if center.y < -4.8 else None)
        bay = next((index for index, start in enumerate(bins) if start - 2.7 <= center.x < start + 2.7), None)
        # Upper shoulder casing: panel tops/shoulders have a positive local Z
        # normal and sit outside the pressure hull shoulder line.
        is_panel = side is not None and bay is not None and 2.9 < center.z < 4.65 and normal.z > 0.12 and abs(normal.y) < 0.9
        if is_panel:
            extracted[(side, bay + 1)].append(face)
        else:
            remaining.append(face)
    return extracted, remaining


def mechanical_door_split(source_name: str, component_index: int, source_mesh: bpy.types.Mesh, component_faces_in: list[tuple[int, ...]]) -> tuple[dict[str, list[tuple[int, ...]]], list[tuple[int, ...]]]:
    """Extract source bow-panel faces used as torpedo-door visual regions.

    The logical tube markers are authoritative for count/diameter.  Door
    regions remain source faces and are intentionally conservative because the
    source does not expose a separate named door component.
    """
    # The supplied source does not expose a separable torpedo-door loop.  The
    # old six evenly spaced rectangular bands were a rejected heuristic; keep
    # all source faces intact and author neutral logical door channels below.
    return {}, component_faces_in


def _p700_pair_indices(cover_index: int) -> tuple[int, int]:
    """Return the preserved object names for the aft-to-forward numbering contract."""
    return (13 - 2 * cover_index, 14 - 2 * cover_index)


def _p700_continuous_row_pitch() -> float:
    """Derive the accepted rigid shift from one central original row definition."""
    differences = [P700_CONTINUOUS_ROW_ORIGINAL_X_M[index + 1] - P700_CONTINUOUS_ROW_ORIGINAL_X_M[index] for index in range(11)]
    rounded = [round(value, 1) for value in differences]
    return max(sorted(set(rounded)), key=rounded.count)


def _p700_cover_ownership_from_geometry(side: str) -> dict[int, list[str]]:
    """Assign frozen-cover ownership metadata from the existing row only."""
    result: dict[int, list[str]] = {}
    for cover_index in range(1, 7):
        cover = bpy.data.objects[f"SM_Antey_P700_Cover_{side}_{cover_index:02d}"]
        points = [cover.matrix_world @ vertex.co for vertex in cover.data.vertices]
        xmin, xmax = min(point.x for point in points), max(point.x for point in points)
        members = [bpy.data.objects[f"P700_{side}_{index:02d}"] for index in range(1, 13) if xmin <= bpy.data.objects[f"P700_{side}_{index:02d}"].matrix_world.translation.x <= xmax]
        result[cover_index] = [member.name for member in sorted(members, key=lambda item: item.matrix_world.translation.x)[:2]]
    return result


def _source_cover_frame(cover: bpy.types.Object, side: str) -> dict[str, object]:
    """Derive a launcher frame from the already extracted source cover mesh."""
    points = [cover.matrix_world @ vertex.co for vertex in cover.data.vertices]
    if not points:
        raise RuntimeError(f"empty source cover: {cover.name}")
    centre = sum(points, Vector()) / len(points)
    inboard = min(points, key=lambda point: (point.y if side == "Port" else -point.y))
    outboard = max(points, key=lambda point: (point.y if side == "Port" else -point.y))
    cross = Vector((0.0, 1.0 if side == "Port" else -1.0, 0.0))
    normals = []
    for polygon in cover.data.polygons:
        normal = (cover.matrix_world.to_3x3() @ polygon.normal).normalized()
        if normal.length > 1.0e-8:
            normals.append(normal)
    normal = sum(normals, Vector()) if normals else Vector((0.0, 0.0, 1.0))
    if normal.length < 1.0e-8:
        normal = Vector((0.0, 0.0, 1.0))
    normal.normalize()
    if normal.z < 0.0:
        normal.negate()
    axis = Vector((math.cos(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG)), 0.0, math.sin(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG))))
    span_x = max(point.x for point in points) - min(point.x for point in points)
    span_cross = max(point.y for point in points) - min(point.y for point in points)
    # Use the longitudinal midpoint of the source cover limits for launcher
    # placement.  The raw polygon centroid is biased by triangulation density
    # and can pull one stowed missile outside the casing bay.
    centre.x = 0.5 * (min(point.x for point in points) + max(point.x for point in points))
    projected_half_length = (0.5 * P700_STOWED_ENVELOPE_LENGTH_M * abs(axis.x)) + (0.5 * P700_STOWED_DIAMETER_M * math.sqrt(max(0.0, 1.0 - axis.x * axis.x)))
    # The pair is side-by-side across the source cover, not two guessed
    # longitudinal points.  This keeps each full stowed envelope inside the
    # cover bay while preserving the common launcher axis.
    pair_offset = min(P700_PAIR_LONGITUDINAL_OFFSET_M, max(0.65, 0.5 * span_cross - 0.5 * P700_STOWED_DIAMETER_M - 0.15))
    return {
        "origin": list(centre),
        "longitudinal": [1.0, 0.0, 0.0],
        "crossCover": list(cross),
        "surfaceNormal": list(normal),
        "inboardLimit": list(inboard),
        "outboardLimit": list(outboard),
        "coverXMin": min(point.x for point in points),
        "coverXMax": max(point.x for point in points),
        "coverYMin": min(point.y for point in points),
        "coverYMax": max(point.y for point in points),
        "coverZMin": min(point.z for point in points),
        "coverZMax": max(point.z for point in points),
        "stowedProjectedHalfLengthM": projected_half_length,
        "pairOffsetM": pair_offset,
        "pairOffsetAxis": "crossCover",
        "launcherAxis": list(axis),
        "angleDeg": P700_NOMINAL_LAUNCHER_ANGLE_DEG,
        "side": side,
        "basis": "SOURCE_DERIVED_COVER_LOCAL_FRAME",
    }


def _ensure_p700_child(collection: bpy.types.Collection, name: str, parent: bpy.types.Object, location: Vector, role: str, props: dict[str, object]) -> bpy.types.Object:
    child = bpy.data.objects.get(name)
    if child is None:
        child = bpy.data.objects.new(name, None)
        collection.objects.link(child)
    elif collection.objects.get(name) is None:
        collection.objects.link(child)
    child.parent = parent
    child.matrix_world = Matrix.Translation(location)
    child.empty_display_type = "PLAIN_AXES"
    child.empty_display_size = 0.22
    child["runtime_export"] = False
    child["authoring_role"] = role
    for key, value in props.items():
        child[key] = value
    return child


def _author_p700_continuous_row(markers: list[dict[str, object]], create_dependents: bool = True) -> tuple[list[dict[str, object]], dict[str, dict[str, object]]]:
    """Restore accepted 12-item rows; covers are metadata, never placement."""
    del markers
    collection = bpy.data.collections.get("P700_LAUNCHER_AUTHORING")
    if collection is None:
        collection = bpy.data.collections.new("P700_LAUNCHER_AUTHORING")
        bpy.context.scene.collection.children.link(collection)
    pitch = _p700_continuous_row_pitch()
    axis = Vector((math.cos(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG)), 0.0, math.sin(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG))))
    records: list[dict[str, object]] = []
    groups: dict[str, dict[str, object]] = {}
    for side in ("Port", "Starboard"):
        row_group = bpy.data.objects.get(f"P700_RowShift_{side}")
        if row_group is None:
            row_group = bpy.data.objects.new(f"P700_RowShift_{side}", None)
            collection.objects.link(row_group)
        row_group.parent = None; row_group.location = (0.0, 0.0, 0.0)
        row_group["runtime_export"] = False; row_group["authoring_role"] = "P700_CONTINUOUS_ROW_RIGID_AFT_ALIGNMENT"; row_group["originalRowPitchM"] = pitch; row_group["appliedDeltaXM"] = -pitch
        for index, original_x in enumerate(P700_CONTINUOUS_ROW_ORIGINAL_X_M, 1):
            marker = bpy.data.objects[f"P700_{side}_{index:02d}"]
            centre = Vector((original_x - pitch, P700_CONTINUOUS_ROW_Y_M if side == "Port" else -P700_CONTINUOUS_ROW_Y_M, P700_CONTINUOUS_ROW_Z_M))
            marker.parent = row_group; marker.matrix_world = Matrix.Translation(centre)
            marker["launcher_axis"] = list(axis); marker["launcher_angle_deg_nominal"] = P700_NOMINAL_LAUNCHER_ANGLE_DEG; marker["launcher_placement_basis"] = "ACCEPTED_CONTINUOUS_ROW_ONE_PITCH_SHIFT"; marker["numbering_direction"] = "AFT_TO_FORWARD"; marker["runtime_export"] = False
        ownership = _p700_cover_ownership_from_geometry(side)
        for cover_index, names in ownership.items():
            groups[f"{side}_{cover_index:02d}"] = {"cover": f"SM_Antey_P700_Cover_{side}_{cover_index:02d}", "slots": names, "metadataOnly": True}
        for index in range(1, 13):
            marker = bpy.data.objects[f"P700_{side}_{index:02d}"]; centre = marker.matrix_world.translation.copy()
            cover_index = next((item for item, names in ownership.items() if marker.name in names), None); cover_name = f"SM_Antey_P700_Cover_{side}_{cover_index:02d}" if cover_index else "UNRESOLVED"
            marker["assigned_cover"] = cover_name; marker["cover_ownership_metadata"] = True
            dependents: list[str] = []
            if create_dependents:
                common = {"side": side, "missileIndex": index, "assignedCover": cover_name, "launcherCenter": list(centre), "launcherAxis": list(axis), "launcherAngleDeg": P700_NOMINAL_LAUNCHER_ANGLE_DEG, "sourceDerivedPlacement": True, "derivedFromAcceptedLauncherTransform": True}
                specs = (("Hardpoint", centre, "P700_LAUNCHER_HARDPOINT", {}), ("InventorySlot", centre, "P700_INVENTORY_SLOT", {}), ("TubeAxis", centre, "P700_LAUNCHER_TUBE_AXIS", {"axis": list(axis)}), ("Envelope", centre, "P700_LAUNCHER_ENVELOPE", {"diameterM": P700_LAUNCHER_ENVELOPE_DIAMETER_M, "lengthM": P700_STOWED_ENVELOPE_LENGTH_M}), ("SpawnOrigin", centre, "P700_MISSILE_SPAWN_ORIGIN", {}), ("UnderwaterExit", centre + axis * 2.7, "P700_UNDERWATER_EXIT_PATH_ORIGIN", {"exitDirection": list(axis)}), ("BoosterStart", centre, "P700_BOOSTER_START_MARKER", {}), ("ClearanceProxy", centre, "P700_LAUNCHER_CLEARANCE_PROXY", {"diameterM": P700_LAUNCHER_ENVELOPE_DIAMETER_M, "lengthM": P700_STOWED_ENVELOPE_LENGTH_M}))
                for suffix, location, role, extra in specs:
                    child = _ensure_p700_child(collection, f"P700_{suffix}_{side}_{index:02d}", marker, location, role, common | extra); dependents.append(child.name)
            records.append({"missileObject": marker.name, "side": side, "missileIndex": index, "launcherCenter": list(centre), "launcherAxis": list(axis), "launcherAngle": P700_NOMINAL_LAUNCHER_ANGLE_DEG, "originalRowX": P700_CONTINUOUS_ROW_ORIGINAL_X_M[index - 1], "appliedDeltaX": -pitch, "assignedCover": cover_name, "dependentObjects": dependents, "numberingDirection": "AFT_TO_FORWARD", "sourceDerivedPlacement": True})
    bpy.context.scene["p700_launcher_contract"] = {"total_containers": 24, "per_side": 12, "continuousRow": True, "originalRowPitchM": pitch, "appliedDeltaXM": -pitch, "placementBasis": "ACCEPTED_CONTINUOUS_ROW_ONE_PITCH_SHIFT", "minimumRequiredClearanceM": P700_MINIMUM_REQUIRED_CLEARANCE_M}
    return records, groups


def _author_p700_from_source_covers(markers: list[dict[str, object]], create_dependents: bool = True) -> tuple[list[dict[str, object]], dict[str, dict[str, object]]]:
    """Place each preserved P700 marker from its assigned source cover frame."""
    dependency_collection = bpy.data.collections.get("P700_LAUNCHER_AUTHORING")
    if dependency_collection is None:
        dependency_collection = bpy.data.collections.new("P700_LAUNCHER_AUTHORING")
        bpy.context.scene.collection.children.link(dependency_collection)
    group_records: dict[str, dict[str, object]] = {}
    records: list[dict[str, object]] = []
    for side in ("Port", "Starboard"):
        for cover_index in range(1, 7):
            cover = bpy.data.objects.get(f"SM_Antey_P700_Cover_{side}_{cover_index:02d}")
            if cover is None:
                raise RuntimeError(f"source-derived P700 cover missing: {side} {cover_index:02d}")
            frame = _source_cover_frame(cover, side)
            origin = Vector(frame["origin"])
            normal = Vector(frame["surfaceNormal"])
            axis = Vector(frame["launcherAxis"]).normalized()
            group_name = f"P700_LauncherGroup_{side}_{cover_index:02d}"
            group = bpy.data.objects.get(group_name)
            if group is None:
                group = bpy.data.objects.new(group_name, None)
                dependency_collection.objects.link(group)
            group.parent = None
            group.matrix_world = Matrix.Translation(origin)
            group.empty_display_type = "CUBE"
            group.empty_display_size = 0.35
            group["runtime_export"] = False
            group["authoring_role"] = "P700_LAUNCHER_GROUP_ORIGIN"
            group["cover"] = cover.name
            group["coverFingerprint"] = cover.get("MANUAL_SOURCE_FACE_FINGERPRINT", cover.get("source_face_fingerprint", ""))
            pair = _p700_pair_indices(cover_index)
            group["launcherSlots"] = [f"P700_{side}_{pair[0]:02d}", f"P700_{side}_{pair[1]:02d}"]
            group["mappingContract"] = "Cover_01->11/12; Cover_06->01/02"
            group_records[f"{side}_{cover_index:02d}"] = {"object": group.name, "cover": cover.name, "frame": frame, "pair": pair}
            for slot, missile_index in enumerate(pair, 1):
                marker_name = f"P700_{side}_{missile_index:02d}"
                marker = bpy.data.objects.get(marker_name)
                if marker is None:
                    raise RuntimeError(f"missing launcher marker {marker_name}")
                old_parent = marker.parent
                old_world = marker.matrix_world.copy()
                pair_offset = float(frame["pairOffsetM"])
                centre = origin + Vector(frame["crossCover"]) * (pair_offset if slot == 2 else -pair_offset) - normal * 0.95
                marker.parent = group
                marker.matrix_world = Matrix.Translation(centre)
                marker["launcher_hatch_group"] = cover_index - 1
                marker["source_cover"] = cover.name
                marker["assigned_cover"] = cover.name
                marker["launcher_pair_slot"] = slot
                marker["launcher_axis"] = list(axis)
                marker["launcher_angle_deg_nominal"] = P700_NOMINAL_LAUNCHER_ANGLE_DEG
                marker["launcher_angle_deg_measured"] = frame["angleDeg"]
                marker["launcher_frame"] = json.dumps(frame)
                marker["launcher_placement_basis"] = "source_cover_local_frame"
                marker["numbering_direction"] = "AFT_TO_FORWARD"
                marker["runtime_export"] = False
                dependency_names: list[str] = []
                if create_dependents:
                    dep_props = {"side": side, "missileIndex": missile_index, "assignedCover": cover.name, "coverFingerprint": group["coverFingerprint"], "launcherCenter": list(centre), "launcherAxis": list(axis), "launcherAngleDeg": frame["angleDeg"], "sourceDerivedPlacement": True}
                    child_specs = (
                        ("Hardpoint", centre, "P700_LAUNCHER_HARDPOINT", {}),
                        ("InventorySlot", centre, "P700_INVENTORY_SLOT", {}),
                        ("TubeAxis", centre, "P700_LAUNCHER_TUBE_AXIS", {"axis": list(axis)}),
                        ("Envelope", centre, "P700_LAUNCHER_ENVELOPE", {"diameterM": P700_LAUNCHER_ENVELOPE_DIAMETER_M, "lengthM": 5.5}),
                        ("SpawnOrigin", centre, "P700_MISSILE_SPAWN_ORIGIN", {}),
                        ("UnderwaterExit", centre + axis * 2.7, "P700_UNDERWATER_EXIT_PATH_ORIGIN", {"exitDirection": list(axis)}),
                        ("BoosterStart", centre, "P700_BOOSTER_START_MARKER", {"basis": "canonical_P700_booster_attach"}),
                        ("ClearanceProxy", centre, "P700_LAUNCHER_CLEARANCE_PROXY", {"diameterM": P700_LAUNCHER_ENVELOPE_DIAMETER_M, "lengthM": 5.5}),
                    )
                    for suffix, location, role, extra in child_specs:
                        child = _ensure_p700_child(dependency_collection, f"P700_{suffix}_{side}_{missile_index:02d}", group, location, role, dep_props | extra)
                        dependency_names.append(child.name)
                records.append({"missileObject": marker_name, "side": side, "missileIndex": missile_index, "assignedCover": cover.name, "coverFingerprint": group["coverFingerprint"], "launcherCenter": list(centre), "launcherAxis": list(axis), "launcherAngle": frame["angleDeg"], "stowedTransform": {"location": list(centre), "axis": list(axis)}, "dependentObjects": dependency_names, "launcherGroupOrigin": group.name, "coverPairOwnership": group["launcherSlots"], "oldParent": old_parent.name if old_parent else None, "previousWorldTransform": list(old_world), "sourceDerivedPlacement": True, "numberingDirection": "AFT_TO_FORWARD"})
    bpy.context.scene["p700_launcher_contract"] = {"total_containers": 24, "per_side": 12, "covers_per_side": 6, "containers_per_cover": 2, "nominal_angle_deg": P700_NOMINAL_LAUNCHER_ANGLE_DEG, "numberingDirection": "AFT_TO_FORWARD", "mapping": "Cover_01->11/12; Cover_02->09/10; Cover_03->07/08; Cover_04->05/06; Cover_05->03/04; Cover_06->01/02", "placementBasis": "SOURCE_DERIVED_COVER_LOCAL_FRAME"}
    return records, group_records


def add_authoring_objects(mechanical: bool = False) -> list[dict[str, object]]:
    markers: list[dict[str, object]] = []
    hardpoints = bpy.data.collections.new("P700_AUTHORING_HARDPOINTS")
    bpy.context.scene.collection.children.link(hardpoints)
    # The source casing covers are authoritative.  Markers are initially
    # created as neutral empties and are placed from the six extracted cover
    # frames below; no global twelve-item interpolation remains in production.
    for side in ("Port", "Starboard"):
        for index in range(12):
            empty = bpy.data.objects.new(f"P700_{side}_{index + 1:02d}", None)
            hardpoints.objects.link(empty)
            empty.empty_display_type = "CUBE"
            empty.empty_display_size = 0.25
            empty.location = (0.0, 0.0, 0.0)
            empty["authoring_role"] = "P700_LAUNCH_POSITION"
            empty["logical_inventory"] = "P700"
            empty["launcher_hatch_group"] = 0
            empty["launcher_angle_deg_nominal"] = P700_NOMINAL_LAUNCHER_ANGLE_DEG
            empty["launcher_angle_deg_measured"] = P700_NOMINAL_LAUNCHER_ANGLE_DEG
            empty["launcher_angle_basis"] = "SOURCE_CASING_PUBLIC_REFERENCE"
            empty["launcher_axis"] = [math.cos(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG)), 0.0, math.sin(math.radians(P700_NOMINAL_LAUNCHER_ANGLE_DEG))]
            markers.append({"object": empty.name, "role": "P700_LAUNCH_POSITION", "side": side, "index": index + 1, "cover_group": 0, "angle_deg": P700_NOMINAL_LAUNCHER_ANGLE_DEG})
    if mechanical:
        p700_records, _ = _author_p700_continuous_row(markers)
        p700_by_name = {record["missileObject"]: record for record in p700_records}
        for marker in markers:
            if marker.get("object") in p700_by_name:
                marker.update(p700_by_name[marker["object"]])
    tubes = bpy.data.collections.new("TORPEDO_AUTHORING_MARKERS")
    bpy.context.scene.collection.children.link(tubes)
    # Public front references show a compact upper-bow cluster, not the
    # rejected six-position longitudinal row.  Source topology contains no
    # separately identifiable door loops, so these remain neutral authoring
    # channels until a source-identifiable door seam is supplied.
    tube_layout = (
        ("TorpedoDoor_01", 0.533, (68.4, -2.10, 1.45)),
        ("TorpedoDoor_02", 0.533, (68.4, -0.70, 1.45)),
        ("TorpedoDoor_03", 0.533, (68.4, 0.70, 1.45)),
        ("TorpedoDoor_04", 0.533, (68.4, 2.10, 1.45)),
        ("TorpedoDoor_05", 0.650, (68.4, -1.20, 2.70)),
        ("TorpedoDoor_06", 0.650, (68.4, 1.20, 2.70)),
    )
    for index, (door_name, diameter, position) in enumerate(tube_layout, 1):
        if mechanical:
            size_name = "533" if diameter < 0.6 else "650"
            ordinal = index if index <= 4 else index - 4
            marker_name = f"TorpedoTube{size_name}_{ordinal:02d}"
        else:
            marker_name = f"TorpedoTube_Neutral_{index:02d}"
        empty = bpy.data.objects.new(marker_name, None)
        tubes.objects.link(empty)
        empty.empty_display_type = "CUBE"
        empty.empty_display_size = 0.25
        empty.location = position
        empty["diameter_m"] = diameter
        empty["authoring_role"] = "TORPEDO_TUBE"
        empty["hemisphere"] = "UPPER_BOW"
        empty["door_state_channel"] = f"TorpedoDoorState_{index:02d}"
        empty["source_geometry_status"] = "SOURCE_DOOR_LOOP_NOT_IDENTIFIABLE"
        empty["layout_basis"] = "PUBLIC_FRONT_REFERENCE_COMPACT_CLUSTER"
        empty["logical_door"] = door_name
        markers.append({"object": empty.name, "role": "TORPEDO_TUBE", "logicalDoor": door_name, "diameter_m": diameter, "hemisphere": "UPPER_BOW", "centerY": position[1], "centerZ": position[2], "sourceGeometryStatus": "SOURCE_DOOR_LOOP_NOT_IDENTIFIABLE", "caliberConfidence": "CONFIGURATION_NEUTRAL"})
    bpy.context.scene["p700_launcher_contract"]["nominal_angle_deg"] = P700_NOMINAL_LAUNCHER_ANGLE_DEG
    bpy.context.scene["torpedo_contract"] = {"533mm": 4, "650mm": 2, "placement": "UPPER_BOW_HEMISPHERE"}
    return markers


def add_physics_proxies() -> None:
    collection = bpy.data.collections.new("ANTEY_PHYSICS_PROXIES")
    bpy.context.scene.collection.children.link(collection)
    for name, dimensions, role in (("Antey_CollisionProxy", (154.0, 17.8, 10.0), "COLLISION"), ("Antey_BuoyancyVolume", (145.0, 16.0, 7.5), "BUOYANCY")):
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
        obj = bpy.context.object
        obj.name = name
        obj.dimensions = dimensions
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
        collection.objects.link(obj)
        obj.hide_render = True
        obj.hide_viewport = True
        obj["physics_proxy_role"] = role
        obj["physics_proxy_shape"] = "BOX"
        obj["physics_proxy_coordinate_space"] = "SOURCE_LOCAL"
        obj["physics_proxy_render_independent"] = True


def _make_hidden_volume(name: str, location: tuple[float, float, float], dimensions: tuple[float, float, float], collection: bpy.types.Collection) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    obj.hide_render = True
    obj.hide_viewport = True
    obj.display_type = "WIRE"
    obj["runtime_export"] = False
    return obj


def _load_antey_compartment_contract() -> dict:
    path = Path(__file__).resolve().parents[2] / "Content/submarines/Antey/Antey.compartments.json"
    contract = json.loads(path.read_text(encoding="utf-8"))
    if contract.get("schemaVersion") != 1 or contract.get("coordinateContract") != "+X bow; +Y port; +Z up; 1 BU = 1 m":
        raise RuntimeError("Antey compartment reference contract is invalid")
    compartments = contract.get("compartments")
    if not isinstance(compartments, list) or len(compartments) != 10:
        raise RuntimeError("Antey compartment reference contract must contain exactly ten records")
    return contract


def add_compartment_and_mass_contract() -> None:
    """Author hidden functional zones and tunable mass/COM contracts only."""
    collection = bpy.data.collections.new("ANTEY_COMPARTMENTS")
    bpy.context.scene.collection.children.link(collection)
    contract = _load_antey_compartment_contract()
    compartment_specs = contract["compartments"]
    # Gameplay/mass numbers remain tunable authoring approximations. Their total
    # equipment mass is preserved from the previous accepted contract, while the
    # functional identity and spatial boundaries now come from the reviewed 949A
    # reference contract rather than an invented even longitudinal partition.
    tuning = [
        (420000.0, 0.0, True, True, "BATTERY_MAIN", "PUMP_FORWARD", "TORPEDO_ROOM_ACCESS"),
        (280000.0, 18.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "CENTRAL_ACCESS"),
        (330000.0, 12.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "COMPARTMENT_03_ACCESS"),
        (240000.0, 36.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "CREW_CORRIDOR"),
        (475000.0, 0.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "AUXILIARY_ACCESS"),
        (475000.0, 0.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "AUXILIARY_ACCESS"),
        (2400000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_MAIN", "REACTOR_ACCESS"),
        (760000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_AFT", "TURBINE_ACCESS"),
        (760000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_AFT", "TURBINE_ACCESS"),
        (620000.0, 0.0, True, True, "DISTRIBUTION_AFT", "PUMP_AFT", "MOTOR_ROOM_ACCESS"),
    ]
    compartment_records: list[dict[str, object]] = []
    for index, (spec, (equipment_mass, crew_weight, floodable, fire_capable, power, pump, repair_access)) in enumerate(zip(compartment_specs, tuning), 1):
        semantic_id = f"compartment.{index:02d}"
        if spec.get("semanticId") != semantic_id:
            raise RuntimeError(f"Unexpected Antey compartment semantic ID: {spec.get('semanticId')} != {semantic_id}")
        x_min, x_max = (float(value) for value in spec["xRangeMeters"])
        length = x_max - x_min
        centre = tuple(float(value) for value in spec["center"])
        half_extents = tuple(float(value) for value in spec["halfExtents"])
        if not math.isclose(centre[0], (x_min + x_max) * 0.5, abs_tol=1.0e-6) or not math.isclose(half_extents[0], length * 0.5, abs_tol=1.0e-6):
            raise RuntimeError(f"Antey compartment spatial contract is inconsistent: {semantic_id}")
        dimensions = tuple(value * 2.0 for value in half_extents)
        name = f"Antey_Compartment_{index:02d}"
        volume = _make_hidden_volume(name, centre, dimensions, collection)
        volume["compartment_id"] = index
        volume["semantic_id"] = semantic_id
        volume["display_name_ru"] = spec["displayNameRu"]
        volume["role"] = spec["functionalRole"]
        volume["functional_role_status"] = spec["functionalRoleStatus"]
        volume["system_tags_json"] = json.dumps(spec["systemTags"], ensure_ascii=False)
        volume["X_MIN"] = x_min
        volume["X_MAX"] = x_max
        volume["LOCAL_VOLUME_M3"] = dimensions[0] * dimensions[1] * dimensions[2]
        volume["CENTER"] = list(centre)
        volume["CREW_CAPACITY_WEIGHT_KG"] = crew_weight
        volume["FLOODABLE"] = floodable
        volume["FIRE_CAPABLE"] = fire_capable
        volume["POWER_DEPENDENCY"] = power
        volume["PUMP_DEPENDENCY"] = pump
        volume["REPAIR_ACCESS"] = repair_access
        volume["source_basis"] = contract["referenceBasis"]["geometryStatus"]
        compartment_records.append({
            "name": name, "index": index, "semanticId": semantic_id,
            "displayNameRu": spec["displayNameRu"], "functionalRole": spec["functionalRole"],
            "functionalRoleStatus": spec["functionalRoleStatus"], "systemTags": spec["systemTags"],
            "X_MIN": x_min, "X_MAX": x_max,
            "LOCAL_VOLUME": dimensions[0] * dimensions[1] * dimensions[2],
            "CENTER": list(centre), "ROLE": spec["functionalRole"],
            "CREW_CAPACITY_WEIGHT": crew_weight, "FLOODABLE": floodable,
            "FIRE_CAPABLE": fire_capable, "POWER_DEPENDENCY": power,
            "PUMP_DEPENDENCY": pump, "REPAIR_ACCESS": repair_access,
            "equipment_mass_kg": equipment_mass,
        })
    bpy.context.scene["antey_compartments"] = compartment_records
    bpy.context.scene["damage_repair_contract"] = {
        "per_compartment_state": ["Integrity", "Flooding", "Fire", "Smoke", "Power", "CrewPresent", "CrewInjured", "RepairProgress"],
        "bulkhead_states": ["OPEN", "CLOSED", "DAMAGED", "SEALED"],
        "future_effects": ["flooding_mass_changes", "center_of_mass_shift", "trim_change", "buoyancy_margin_change", "fire_smoke_propagation", "power_distribution_loss", "crew_casualty_and_repair_access"],
        "implementation_status": "CONTRACT_ONLY_SIMULATION_OWNS_RUNTIME_STATE",
    }
    mass_entries = [{"label": "STATIC_HULL_APPROXIMATION", "mass_kg": 9000000.0, "center": [0.0, 0.0, -0.10], "basis": "TUNABLE_AUTHORING_DISTRIBUTION"}]
    for record in compartment_records:
        mass_entries.append({"label": record["name"], "mass_kg": record["equipment_mass_kg"], "center": record["CENTER"], "basis": "COMPARTMENT_ROLE_WEIGHT"})
    mass_entries.extend([
        {"label": "P700_STOWED_24", "mass_kg": 168000.0, "center": [20.0, 0.0, 3.25], "basis": "CONFIGURED_STOWED_INVENTORY"},
        {"label": "TORPEDO_STOWED_6", "mass_kg": 10800.0, "center": [68.4, 0.0, 1.9], "basis": "CONFIGURED_STOWED_INVENTORY"},
        {"label": "CREW_AND_STORES", "mass_kg": 18000.0, "center": [12.0, 0.0, 0.0], "basis": "TUNABLE_AUTHORING_DISTRIBUTION"},
    ])
    total_mass = sum(float(entry["mass_kg"]) for entry in mass_entries)
    base_com = [sum(float(entry["mass_kg"]) * float(entry["center"][axis]) for entry in mass_entries) / total_mass for axis in range(3)]
    markers = bpy.data.collections.new("ANTEY_PHYSICS_MARKERS")
    bpy.context.scene.collection.children.link(markers)
    com_marker = bpy.data.objects.new("Antey_CenterOfMass", None)
    markers.objects.link(com_marker); com_marker.empty_display_type = "SPHERE"; com_marker.empty_display_size = 1.0; com_marker.location = base_com; com_marker.hide_render = True; com_marker.hide_viewport = True
    com_marker["marker_role"] = "CENTER_OF_MASS"; com_marker["simulation_owns_state"] = True; com_marker["base_com_source"] = "CONFIGURED_MASS_DISTRIBUTION"
    buoyancy = bpy.data.objects.get("Antey_BuoyancyVolume")
    buoyancy_points = [buoyancy.matrix_world @ vertex.co for vertex in buoyancy.data.vertices] if buoyancy else [Vector((0.0, 0.0, 0.0))]
    cob = [sum(point[axis] for point in buoyancy_points) / len(buoyancy_points) for axis in range(3)]
    cob_marker = bpy.data.objects.new("Antey_CenterOfBuoyancy", None)
    markers.objects.link(cob_marker); cob_marker.empty_display_type = "CIRCLE"; cob_marker.empty_display_size = 1.0; cob_marker.location = cob; cob_marker.hide_render = True; cob_marker.hide_viewport = True
    cob_marker["marker_role"] = "CENTER_OF_BUOYANCY"; cob_marker["simulation_owns_state"] = True; cob_marker["source_volume"] = "Antey_BuoyancyVolume"
    bpy.context.scene["antey_mass_distribution"] = {"base_mass_kg": total_mass, "entries": mass_entries, "base_com": base_com, "method": "configured_mass_distribution_not_render_vertex_average", "dynamic_com": {"flood_water_mass_and_center": "SIMULATION_OWNED", "weapon_removal": "P700_AND_TORPEDO_MASS_REMOVED_FROM_DISTRIBUTION"}}
    bpy.context.scene["antey_buoyancy_contract"] = {"volume_object": "Antey_BuoyancyVolume", "cob": cob, "method": "buoyancy_volume_centroid", "stability_tuning": {"metacentric_height": "TUNABLE_SIMULATION_PARAMETER", "trim_target": "TUNABLE_SIMULATION_PARAMETER", "historical_value": "NOT_HARDCODED"}}


def add_p700_cover_animation() -> dict[str, object]:
    """Author a Blender-only cover QA preview, never a runtime missile animation."""
    qa_animation_name = "QA_Antey_P700_Covers_Open"
    # The source casing's fourth bay shares a narrow, overlapping shoulder
    # strip with the retained hull at neutral.  A measured 0.10 m outboard
    # seam lift is therefore present even at CLOSED; the remaining samples
    # keep the smallest offsets that clear the source sweep.  The aft pair is
    # deliberately sequenced: cover 05 reaches the clear/open pose before
    # cover 06 leaves its stowed pose.  This is a compound hinge/seam
    # mechanism, not a change to the frozen face sets.
    lift_profiles = {
        1: (0.15, 0.15, 0.15, 0.15, 0.15),
        2: (0.15, 0.15, 0.15, 0.15, 0.15),
        3: (0.0, 0.0, 0.05, 0.05, 0.05),
        4: (0.10, 0.10, 0.05, 0.05, 0.10),
        5: (0.0, 0.0, 0.05, 0.05, 0.05),
        6: (0.0, 0.0, 0.0, 0.075, 0.125),
    }
    closed_lifts = {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.10, 5: 0.0, 6: 0.0}
    angle_profiles = {
        1: (0.0, 10.0, 25.0, 50.0, 75.0, 90.0),
        2: (0.0, 10.0, 25.0, 50.0, 75.0, 90.0),
        3: (0.0, 10.0, 25.0, 50.0, 75.0, 90.0),
        4: (0.0, 10.0, 25.0, 50.0, 75.0, 90.0),
        5: (0.0, 10.0, 25.0, 50.0, 90.0, 90.0),
        6: (0.0, 0.0, 0.0, 0.0, 50.0, 90.0),
    }
    frames = (1.0, 5.0, 11.0, 21.0, 31.0, 41.0)
    operation_records = []
    for side in ("Port", "Starboard"):
        sign = 1.0 if side == "Port" else -1.0
        for cover_index in range(1, 7):
            obj = bpy.data.objects.get(f"SM_Antey_P700_Cover_{side}_{cover_index:02d}")
            if obj is None:
                continue
            base_location = obj.location.copy()
            obj.rotation_mode = "XYZ"
            obj.animation_data_clear()
            obj.location = base_location + Vector((0.0, sign * closed_lifts[cover_index], 0.0)); obj.rotation_euler = (0.0, 0.0, 0.0); obj.keyframe_insert("rotation_euler", index=0, frame=frames[0], group=qa_animation_name); obj.keyframe_insert("location", frame=frames[0], group=qa_animation_name)
            profile_angles = angle_profiles[cover_index]
            for frame, angle, lift in zip(frames[1:], profile_angles[1:], lift_profiles[cover_index]):
                obj.rotation_euler = (sign * math.radians(angle), 0.0, 0.0); obj.location = base_location + Vector((0.0, sign * lift, 0.0)); obj.keyframe_insert("rotation_euler", index=0, frame=frame, group=qa_animation_name); obj.keyframe_insert("location", frame=frame, group=qa_animation_name)
            action = obj.animation_data.action
            action.name = f"{qa_animation_name}_Source_{obj.name}"
            action["AUTHORING_ONLY"] = True
            action["RUNTIME_EXPORT"] = False
            for layer in action.layers:
                for action_strip in layer.strips:
                    for channelbag in action_strip.channelbags:
                        for curve in channelbag.fcurves:
                            for point in curve.keyframe_points:
                                point.interpolation = "LINEAR"
            track = obj.animation_data.nla_tracks.new(); track.name = qa_animation_name; strip = track.strips.new(qa_animation_name, 1, action); strip.action_frame_start = frames[0]; strip.action_frame_end = frames[-1]; obj.animation_data.action = None
            obj["P700_DEPLOYMENT_OPERATION"] = qa_animation_name; obj["AUTHORING_ONLY"] = False; obj["RUNTIME_EXPORT"] = True; obj["runtime_export"] = True; obj["lod"] = 0; obj["OPENING_MECHANISM"] = "HINGE_ROTATION_PLUS_SOURCE_SEAM_CLEARANCE_LIFT"; obj["CLEARANCE_LIFT_PROFILE_M"] = [closed_lifts[cover_index], *lift_profiles[cover_index]]; obj["CLEARANCE_LIFT_AXIS"] = "+Y_OUTBOARD" if side == "Port" else "-Y_OUTBOARD"; obj["FULL_OPEN_ANGLE_DEG"] = 90.0; obj["COVER_PAIR_LOCK"] = "COVER_PAIR_LOCK"; obj["NEIGHBOR_CONFLICT_CHANNEL"] = "NEIGHBOR_COVER_CONFLICT"; obj["ALLOW_OPEN_RULE"] = "ALLOW_OPEN only when paired launcher and neighbor cover envelopes are clear"
            operation_records.append({"object": obj.name, "node": obj.name, "nlaTrack": qa_animation_name, "sourceAction": action.name, "rotationChannel": "rotation_euler[0]", "locationChannel": "location", "frames": list(frames), "anglesDeg": [sign * value for value in profile_angles], "clearanceLiftM": [closed_lifts[cover_index], *lift_profiles[cover_index]], "sequence": "GROUP_05_THEN_GROUP_06" if cover_index in (5, 6) else None, "authoringOnly": True, "runtimeExport": False})
    bpy.context.scene["P700_DEPLOYMENT_OPERATION"] = qa_animation_name
    bpy.context.scene["P700_COVER_RUNTIME_ANIMATION"] = None
    bpy.context.scene["P700_COVER_STATE_OWNER"] = "LauncherSystem.P700CoverState_Port_01..06 / P700CoverState_Starboard_01..06"
    bpy.context.scene["P700_COVER_OPENING_MECHANISM"] = {"type": "HINGE_ROTATION_PLUS_SOURCE_SEAM_CLEARANCE_LIFT", "source_boundaries_frozen": True, "clearance_lift_derived": "minimal measured lateral offsets from triangle sweep", "pair_interlock": {"channel": "COVER_PAIR_LOCK", "conflict": "NEIGHBOR_COVER_CONFLICT", "rule": "ALLOW_OPEN only when clear", "sequence_required": "GROUP_05_THEN_GROUP_06"}}
    return {"logicalOperation": qa_animation_name, "runtimeAnimation": None, "stateOwner": bpy.context.scene["P700_COVER_STATE_OWNER"], "records": operation_records, "contract": bpy.context.scene["P700_COVER_OPENING_MECHANISM"]}


def make_lods(lod0: list[bpy.types.Object]) -> None:
    # Articulation roots are authored immediately before LOD derivation.  Force
    # dependency-graph evaluation so copied children receive their actual root
    # world matrix rather than the pre-evaluation origin cached by Blender.
    bpy.context.view_layer.update()
    for lod, ratio in ((1, 0.55), (2, 0.30), (3, 0.12)):
        for original in lod0:
            if original.type != "MESH":
                continue
            copy = original.copy()
            copy.data = original.data.copy()
            copy.name = original.name.replace("_LOD0", f"_LOD{lod}")
            bpy.context.scene.collection.objects.link(copy)
            if original.parent is not None:
                world_matrix = original.matrix_world.copy()
                copy.parent = original.parent
                copy.matrix_parent_inverse = Matrix.Identity(4)
                # Assign from the evaluated world transform after parenting.
                # Setting matrix_basis on an unattached duplicate looks
                # equivalent in-memory, but Blender can persist its inherited
                # transform as the origin after a modifier is applied.
                copy.matrix_world = world_matrix
            copy["lod"] = lod
            copy["runtime_export"] = True
            modifier = copy.modifiers.new("SOURCE_SHAPE_PRESERVING_DECIMATE", "DECIMATE")
            modifier.ratio = ratio
            modifier.use_collapse_triangulate = True
            bpy.context.view_layer.objects.active = copy
            copy.select_set(True)
            try:
                bpy.ops.object.modifier_apply(modifier=modifier.name)
            finally:
                copy.select_set(False)
            # Applying a decimation modifier on an unparented copy must not
            # discard the articulated parent's preserved world transform.
            if original.parent is not None:
                copy.parent = original.parent
                copy.matrix_parent_inverse = Matrix.Identity(4)
                copy.matrix_world = world_matrix
                bpy.context.view_layer.update()
            copy.hide_render = True
            copy.hide_viewport = True


def restore_articulated_lod_transforms(lod0: list[bpy.types.Object]) -> None:
    """Reapply evaluated hinge-root transforms after the complete LOD pass.

    Blender's modifier application may postpone dependency-graph parenting for
    hidden duplicate children.  A final explicit world-matrix assignment keeps
    the derived LODs on the same physical hinge as their LOD0 source.
    """
    bpy.context.view_layer.update()
    for original in lod0:
        if original.type != "MESH" or original.parent is None:
            continue
        world_matrix = original.matrix_world.copy()
        for lod in (1, 2, 3):
            copy = bpy.data.objects.get(original.name.replace("_LOD0", f"_LOD{lod}"))
            if copy is None:
                continue
            copy.parent = original.parent
            copy.matrix_parent_inverse = Matrix.Identity(4)
            copy.matrix_world = world_matrix
            bpy.context.view_layer.update()


def _coordinate_key(point: Vector, digits: int = 9) -> tuple[float, float, float]:
    return tuple(round(float(point[index]), digits) for index in range(3))


def _coordinate_face_key(points: list[Vector], polygon: bpy.types.MeshPolygon, digits: int = 9) -> tuple[tuple[float, float, float], ...]:
    return tuple(sorted(_coordinate_key(points[index], digits) for index in polygon.vertices))


def _coordinate_face_fingerprint(face_keys: list[tuple[tuple[float, float, float], ...]]) -> str:
    payload = "|".join(repr(key) for key in sorted(face_keys))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def _coordinate_edge_fingerprint(edge_keys: list[tuple[tuple[float, float, float], tuple[float, float, float]]]) -> str:
    payload = "|".join(repr(key) for key in sorted(edge_keys))
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def _resolve_positive_mask(obj: bpy.types.Object, record: dict[str, object]) -> dict[str, object]:
    """Resolve the literal artist mask against the current source-first mesh.

    The object index is used only when its world coordinate verifies against the
    supplied coordinate.  Otherwise the exact world coordinate is resolved;
    no local-space or nearest-edge substitution is permitted.
    """
    world = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    tolerance = MANUAL_RUDDER_TOLERANCE
    vertices = record.get("vertices", [])
    if len(vertices) != int(record.get("expectedVertexCount", -1)):
        raise RuntimeError(f"manual mask vertex count mismatch for {record.get('object')}")
    resolved: dict[int, int] = {}
    resolution_records: list[dict[str, object]] = []
    for item in vertices:
        legacy = int(item["legacyIndex"])
        expected = Vector(item["world"])
        direct_error = (world[legacy] - expected).length if 0 <= legacy < len(world) else float("inf")
        if direct_error <= tolerance:
            index = legacy
            method = "OBJECT_INDEX_VERIFIED_WORLD"
            error = direct_error
        else:
            index, error = min(enumerate(world), key=lambda pair: (pair[1] - expected).length)
            error = (world[index] - expected).length
            method = "WORLD_COORDINATE_RESOLUTION"
        if error > tolerance:
            raise RuntimeError(f"manual mask vertex cannot resolve within tolerance: {record.get('object')}:{legacy} error={error}")
        if legacy in resolved:
            raise RuntimeError(f"duplicate manual mask vertex legacy index: {record.get('object')}:{legacy}")
        resolved[legacy] = index
        resolution_records.append({"legacyIndex": legacy, "resolvedIndex": index, "method": method, "errorM": error, "world": list(expected)})
    if len(set(resolved.values())) != len(vertices):
        raise RuntimeError(f"manual mask vertex resolution is not unique for {record.get('object')}")
    edge_by_vertices = {tuple(sorted(edge.vertices)): edge.index for edge in obj.data.edges}
    resolved_edges: list[dict[str, object]] = []
    for item in record.get("edges", []):
        legacy_edge = int(item["legacyIndex"])
        legacy_a, legacy_b = (int(value) for value in item["legacyVertices"])
        if legacy_a not in resolved or legacy_b not in resolved:
            raise RuntimeError(f"manual mask edge endpoint is not in vertex mask: {record.get('object')}:{legacy_edge}")
        pair = tuple(sorted((resolved[legacy_a], resolved[legacy_b])))
        edge_index = edge_by_vertices.get(pair)
        if edge_index is None:
            raise RuntimeError(f"manual mask edge does not exist in source-derived topology: {record.get('object')}:{legacy_edge} {pair}")
        resolved_edges.append({"legacyIndex": legacy_edge, "legacyVertices": [legacy_a, legacy_b], "resolvedVertices": list(pair), "resolvedEdgeIndex": edge_index, "endpointWorld": [list(world[pair[0]]), list(world[pair[1]])]})
    if len(resolved_edges) != int(record.get("expectedEdgeCount", -1)) or len({item["resolvedEdgeIndex"] for item in resolved_edges}) != len(resolved_edges):
        raise RuntimeError(f"manual mask edge resolution count mismatch for {record.get('object')}")
    return {"worldVertices": world, "resolvedVertices": resolved, "resolvedEdges": resolved_edges, "selectedVertices": set(resolved.values()), "selectedEdges": {tuple(item["resolvedVertices"]) for item in resolved_edges}, "vertexResolution": resolution_records}


def _positive_face_patch(obj: bpy.types.Object, resolved: dict[str, object], surface_rule: str = "LEGACY") -> dict[str, object]:
    """Build the face patch using only the positive topology graph.

    Complete selected-vertex faces are positive faces.  A source face carrying
    at least two selected edges and three selected vertices is retained as a
    measured thickness/side face.  All other incident faces remain fixed
    neighbors; their selected edges are represented as mask-only topology.
    """
    selected_vertices: set[int] = resolved["selectedVertices"]
    selected_edges: set[tuple[int, int]] = resolved["selectedEdges"]
    manual_faces: list[int] = []
    thickness_faces: list[int] = []
    boundary_candidates: list[int] = []
    fixed_neighbors: list[int] = []
    incident_faces: set[int] = set()
    face_edge_keys: dict[int, set[tuple[int, int]]] = {}
    for polygon in obj.data.polygons:
        edge_keys = {tuple(sorted(edge)) for edge in polygon.edge_keys}
        face_edge_keys[polygon.index] = edge_keys
        selected_edge_count = len(edge_keys & selected_edges)
        selected_vertex_count = len(set(polygon.vertices) & selected_vertices)
        if selected_edge_count:
            incident_faces.add(polygon.index)
        if set(polygon.vertices) <= selected_vertices:
            manual_faces.append(polygon.index)
        elif surface_rule == "VENTRAL_SELECTED_SURFACE" and selected_edge_count >= 1 and selected_vertex_count >= 2:
            thickness_faces.append(polygon.index)
        elif selected_edge_count >= 2 and selected_vertex_count >= 3:
            thickness_faces.append(polygon.index)
        elif selected_edge_count:
            boundary_candidates.append(polygon.index)
        elif selected_vertex_count:
            fixed_neighbors.append(polygon.index)
    patch_faces = sorted(set(manual_faces) | set(thickness_faces))
    patch_edges = {edge for index in patch_faces for edge in face_edge_keys[index]}
    patch_vertices = {vertex for index in patch_faces for vertex in obj.data.polygons[index].vertices}
    resolved_edge_index = {tuple(item["resolvedVertices"]): int(item["resolvedEdgeIndex"]) for item in resolved["resolvedEdges"]}
    mask_only_edges = sorted(edge_index for pair, edge_index in resolved_edge_index.items() if pair not in patch_edges)
    return {"manualSelectedFaceIndices": sorted(manual_faces), "thicknessSideFaceIndices": sorted(thickness_faces), "patchFaceIndices": patch_faces, "boundaryCandidateFaceIndices": sorted(set(boundary_candidates)), "fixedNeighborFaceIndices": sorted(set(fixed_neighbors)), "incidentFaceIndices": sorted(incident_faces), "patchEdges": patch_edges, "patchVertices": patch_vertices, "maskOnlyVertexIndices": sorted(selected_vertices - patch_vertices), "maskOnlyResolvedEdgeIndices": mask_only_edges, "selectedEdgeCoverage": len(selected_edges & patch_edges), "selectedVertexCoverage": len(selected_vertices & patch_vertices), "faceEdgeKeys": face_edge_keys}


def _mesh_edge_path(obj: bpy.types.Object, start: Vector, end: Vector) -> dict[str, object]:
    """Find the source seam chain on the existing rudder mesh by edge topology."""
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    start_index = min(range(len(points)), key=lambda index: (points[index] - start).length)
    end_index = min(range(len(points)), key=lambda index: (points[index] - end).length)
    adjacency: dict[int, list[tuple[int, int, float]]] = defaultdict(list)
    for edge in obj.data.edges:
        left, right = edge.vertices
        weight = (points[left] - points[right]).length
        adjacency[left].append((right, edge.index, weight))
        adjacency[right].append((left, edge.index, weight))
    distances = {start_index: 0.0}
    previous: dict[int, tuple[int, int]] = {}
    queue = [(0.0, start_index)]
    while queue:
        distance, vertex = heapq.heappop(queue)
        if distance != distances.get(vertex):
            continue
        if vertex == end_index:
            break
        for neighbour, edge_index, weight in adjacency.get(vertex, []):
            candidate = distance + weight
            if candidate < distances.get(neighbour, float("inf")):
                distances[neighbour] = candidate
                previous[neighbour] = (vertex, edge_index)
                heapq.heappush(queue, (candidate, neighbour))
    if end_index not in distances:
        return {"startIndex": start_index, "endIndex": end_index, "vertices": [], "edges": [], "coordinates": [], "reachable": False}
    chain: list[int] = []
    edge_chain: list[int] = []
    current = end_index
    while current != start_index:
        chain.append(current)
        previous_vertex, edge_index = previous[current]
        edge_chain.append(edge_index)
        current = previous_vertex
    chain.append(start_index)
    chain.reverse()
    edge_chain.reverse()
    return {"startIndex": start_index, "endIndex": end_index, "vertices": chain, "edges": edge_chain, "coordinates": [list(points[index]) for index in chain], "lengthM": distances[end_index], "reachable": True}


def _replace_object_faces_preserve_vertices(obj: bpy.types.Object, remove_indices: set[int]) -> None:
    # Preserve object-local coordinates and the existing transform.
    # Baking world coordinates into a mesh while leaving matrix_world intact
    # would apply the transform twice on a non-identity future candidate.
    points = [vertex.co.copy() for vertex in obj.data.vertices]
    faces = [tuple(polygon.vertices) for polygon in obj.data.polygons if polygon.index not in remove_indices]
    old_data = obj.data
    mesh = bpy.data.meshes.new(f"{obj.name}_ManualMaskStaticMesh")
    mesh.from_pydata(points, [], faces)
    mesh.update(calc_edges=True)
    for material_slot in old_data.materials:
        mesh.materials.append(material_slot)
    obj.data = mesh
    if old_data.users == 0:
        bpy.data.meshes.remove(old_data)


def _coordinate_faces(obj: bpy.types.Object) -> list[tuple[tuple[float, float, float], ...]]:
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    return [_coordinate_face_key(points, polygon) for polygon in obj.data.polygons]


def _corrected_rudder_mesh(rudder: bpy.types.Object, hull: bpy.types.Object, side: str, mask_record: dict[str, object], resolved: dict[str, object], patch: dict[str, object]) -> dict[str, object]:
    """Merge the prior source rudder with the manually positive source patch."""
    old_points = [rudder.matrix_world @ vertex.co for vertex in rudder.data.vertices]
    old_hinge_names = ("A", "B") if side == "Dorsal" else ("D", "E")
    anchors = {name: Vector(value) for name, value in MANUAL_RUDDER_ANCHORS[side].items()}
    old_hinge_path = _mesh_edge_path(rudder, anchors[old_hinge_names[0]], anchors[old_hinge_names[1]])
    hull_points = [hull.matrix_world @ vertex.co for vertex in hull.data.vertices]
    source_faces: list[tuple[list[Vector], str, int]] = []
    for polygon in rudder.data.polygons:
        source_faces.append(([old_points[index] for index in polygon.vertices], "prior_rudder", polygon.index))
    for polygon_index in patch["patchFaceIndices"]:
        polygon = hull.data.polygons[polygon_index]
        source_faces.append(([hull_points[index] for index in polygon.vertices], "manual_source_patch", polygon_index))
    vertices: list[Vector] = []
    vertex_by_key: dict[tuple[float, float, float], int] = {}
    polygons: list[tuple[int, ...]] = []
    polygon_origins: list[dict[str, object]] = []
    polygon_keys_seen: set[tuple[tuple[float, float, float], ...]] = set()
    duplicate_polygon_origins_removed: list[dict[str, object]] = []

    def add_vertex(point: Vector) -> int:
        # Weld source/legacy copies at a micrometre-scale key.  The supplied
        # world coordinates are authoritative to 1e-5 m; welding their
        # sub-micrometre float duplicates is what makes every positive-mask
        # endpoint resolve to one deterministic mesh vertex/edge.
        key = _coordinate_key(point, digits=6)
        existing = vertex_by_key.get(key)
        if existing is not None:
            return existing
        index = len(vertices)
        vertex_by_key[key] = index
        vertices.append(point.copy())
        return index

    for points, origin, source_index in source_faces:
        polygon = tuple(add_vertex(point) for point in points)
        if len(set(polygon)) < 3:
            continue
        # The pre-correction ventral rudder contains one coincident prior
        # polygon.  Keep the first source-derived occurrence and drop only
        # exact coordinate duplicates; retaining both would create a visible
        # z-fighting face pair in the neutral pose.  This does not alter the
        # supplied positive mask or any source face in the fixed shell.
        polygon_key = tuple(sorted(_coordinate_key(point, digits=6) for point in points))
        if polygon_key in polygon_keys_seen:
            duplicate_polygon_origins_removed.append({"origin": origin, "sourceIndex": source_index, "coordinateKey": polygon_key})
            continue
        polygon_keys_seen.add(polygon_key)
        polygons.append(polygon)
        polygon_origins.append({"origin": origin, "sourceIndex": source_index})
    # Keep every supplied vertex in the resulting movable topology.  Vertices
    # without a source face remain loose and are explicitly reported rather
    # than silently discarded.
    mask_points = [Vector(item["world"]) for item in mask_record["vertices"]]
    for point in mask_points:
        add_vertex(point)
    for point in anchors.values():
        add_vertex(point)
    extra_edges: set[tuple[int, int]] = set()
    for polygon in polygons:
        for left, right in zip(polygon, polygon[1:] + polygon[:1]):
            extra_edges.add(tuple(sorted((left, right))))
    for item in resolved["resolvedEdges"]:
        left = add_vertex(Vector(item["endpointWorld"][0]))
        right = add_vertex(Vector(item["endpointWorld"][1]))
        if left != right:
            extra_edges.add(tuple(sorted((left, right))))
    if old_hinge_path["reachable"]:
        for left, right in zip(old_hinge_path["coordinates"], old_hinge_path["coordinates"][1:]):
            a = add_vertex(Vector(left)); b = add_vertex(Vector(right))
            if a != b:
                extra_edges.add(tuple(sorted((a, b))))
    old_data = rudder.data
    new_mesh = bpy.data.meshes.new(f"{rudder.name}_ManualPositiveMaskMesh")
    new_mesh.from_pydata(vertices, sorted(extra_edges), polygons)
    new_mesh.update(calc_edges=True)
    for material_slot in old_data.materials:
        new_mesh.materials.append(material_slot)
    rudder.data = new_mesh
    rudder.matrix_world = Matrix.Identity(4)
    if old_data.users == 0:
        bpy.data.meshes.remove(old_data)
    hinge_start = anchors[old_hinge_names[0]]
    hinge_end = anchors[old_hinge_names[1]]
    hinge_path = _mesh_edge_path(rudder, hinge_start, hinge_end)
    pivot = (hinge_start + hinge_end) * 0.5
    axis = (hinge_end - hinge_start).normalized()
    # Re-root on the exact source seam while preserving every world vertex.
    world_after = [rudder.matrix_world @ vertex.co for vertex in rudder.data.vertices]
    rudder.location = pivot
    for vertex, point in zip(rudder.data.vertices, world_after):
        vertex.co = point - pivot
    rudder.data.update()
    # Align the object's local Z axis with the measured source seam.  The
    # vertices are counter-rotated in local space first, so the neutral world
    # pose remains bit-for-bit identical while runtime rotation is a genuine
    # hinge rotation around the source-derived axis.
    # rotation_difference maps the local +Z basis directly to the measured
    # seam vector; unlike a tracking quaternion it introduces no secondary
    # axis drift on the near-vertical dorsal hinge.
    hinge_orientation = Vector((0.0, 0.0, 1.0)).rotation_difference(axis)
    inverse_orientation = hinge_orientation.to_matrix().inverted()
    for vertex in rudder.data.vertices:
        vertex.co = inverse_orientation @ vertex.co
    rudder.rotation_mode = "QUATERNION"
    rudder.rotation_quaternion = hinge_orientation
    rudder.data.update()
    bpy.context.view_layer.update()
    rudder["CONTROL_SURFACE_ROLE"] = "RUDDER"
    rudder["ARTICULATION"] = "ROTATION"
    rudder["HINGE_AXIS"] = "LOCAL_Z_SOURCE_SEAM"
    rudder["HINGE_AXIS_VECTOR_PRODUCTION"] = list(axis)
    rudder["HINGE_PIVOT_WORLD"] = list(pivot)
    rudder["HINGE_SOURCE"] = "MANUAL_POSITIVE_MASK_SOURCE_SEAM"
    rudder["SIMULATION_OWNS_ANGLE"] = True
    rudder["MANUAL_ANCHOR"] = True
    rudder["MANUAL_MASK_CORRECTED"] = True
    rudder["MOVEMENT_TYPE"] = "HINGE_ROTATION"
    rudder["MANUAL_MASK_VERTEX_COUNT"] = int(mask_record["expectedVertexCount"])
    rudder["MANUAL_MASK_EDGE_COUNT"] = int(mask_record["expectedEdgeCount"])
    # Mesh-edge incidence is computed from polygons, while all positive mask
    # edges are represented by either a face edge or a documented loose edge.
    edge_index_by_pair = {tuple(sorted(edge.vertices)): edge.index for edge in rudder.data.edges}
    polygon_edge_counts: Counter[tuple[int, int]] = Counter()
    for polygon in rudder.data.polygons:
        for edge in polygon.edge_keys:
            polygon_edge_counts[tuple(sorted(edge))] += 1
    boundary_pairs = [pair for pair, count in polygon_edge_counts.items() if count == 1]
    boundary_edges = [edge_index_by_pair[pair] for pair in boundary_pairs if pair in edge_index_by_pair]
    hinge_pairs = []
    for left, right in zip(hinge_path["vertices"], hinge_path["vertices"][1:]):
        pair = tuple(sorted((left, right)))
        if pair in edge_index_by_pair:
            hinge_pairs.append(pair)
    hinge_edge_indices = [edge_index_by_pair[pair] for pair in hinge_pairs]
    bpy.context.view_layer.update()
    points_final = [rudder.matrix_world @ vertex.co for vertex in rudder.data.vertices]
    mask_covered_vertices = sum(min((point - target).length for point in points_final) <= MANUAL_RUDDER_TOLERANCE for target in mask_points)
    mask_edge_pairs = {tuple(sorted((vertex_by_key[_coordinate_key(Vector(item["endpointWorld"][0]), digits=6)], vertex_by_key[_coordinate_key(Vector(item["endpointWorld"][1]), digits=6)]))) for item in resolved["resolvedEdges"]}
    actual_edge_pairs = {tuple(sorted(edge.vertices)) for edge in rudder.data.edges}
    mask_covered_edges = len(mask_edge_pairs & actual_edge_pairs)
    face_keys = _coordinate_faces(rudder)
    boundary_coordinate_keys = []
    for pair in boundary_pairs:
        boundary_coordinate_keys.append(tuple(sorted((_coordinate_key(points_final[pair[0]]), _coordinate_key(points_final[pair[1]])))))
    hinge_coordinate_keys = []
    for pair in hinge_pairs:
        hinge_coordinate_keys.append(tuple(sorted((_coordinate_key(points_final[pair[0]]), _coordinate_key(points_final[pair[1]])))))
    fixed_face_keys = []
    moved = set(patch["patchFaceIndices"])
    for polygon in hull.data.polygons:
        if polygon.index in moved:
            continue
        keys = {tuple(sorted(edge)) for edge in polygon.edge_keys}
        if any(edge in patch["patchEdges"] for edge in keys):
            fixed_face_keys.append(_coordinate_face_key(hull_points, polygon))
    all_points = points_final
    bounds = [[min(point[index] for point in all_points), max(point[index] for point in all_points)] for index in range(3)] if all_points else [[0.0, 0.0]] * 3
    # The source seam is an internal edge chain after the two skin/side
    # surfaces are assembled.  The actual outer perimeter is therefore the
    # polygon-incidence-one edge set, not the supplied positive-mask edge set.
    hinge_edge_set = set(hinge_edge_indices)
    hinge_pair_set = set(hinge_pairs)
    thickness_source_faces = set(patch["thicknessSideFaceIndices"])
    thickness_pairs: set[tuple[int, int]] = set()
    for polygon, origin in zip(rudder.data.polygons, polygon_origins):
        if origin["origin"] == "manual_source_patch" and int(origin["sourceIndex"]) in thickness_source_faces:
            thickness_pairs.update(tuple(sorted(edge)) for edge in polygon.edge_keys)
    thickness_boundary_pairs = set(boundary_pairs) & thickness_pairs - hinge_pair_set
    thickness_edge_set = {edge_index_by_pair[pair] for pair in thickness_boundary_pairs}
    boundary_edge_set = set(boundary_edges)

    # Walk each connected boundary component to retain the measured perimeter
    # topology.  Branched components are emitted as a deterministic vertex set
    # rather than being forced into a fabricated single loop.
    boundary_adjacency: dict[int, list[int]] = defaultdict(list)
    for left, right in boundary_pairs:
        boundary_adjacency[left].append(right)
        boundary_adjacency[right].append(left)
    boundary_chains: list[list[int]] = []
    visited_boundary_edges: set[tuple[int, int]] = set()
    for pair in sorted(boundary_pairs):
        if pair in visited_boundary_edges:
            continue
        start = pair[0]
        if len(boundary_adjacency[start]) == 2 and len(boundary_adjacency[pair[1]]) != 1:
            start = pair[1]
        chain = [start]
        previous = None
        current = start
        while True:
            candidates = [next_vertex for next_vertex in sorted(boundary_adjacency[current]) if tuple(sorted((current, next_vertex))) not in visited_boundary_edges and next_vertex != previous]
            if not candidates:
                break
            next_vertex = candidates[0]
            visited_boundary_edges.add(tuple(sorted((current, next_vertex))))
            chain.append(next_vertex)
            previous, current = current, next_vertex
            if current == start:
                break
        if len(chain) >= 2:
            boundary_chains.append(chain)
    perimeter_classification = {
        "HINGE": sorted(hinge_edge_set),
        "OUTER_PERIMETER": sorted(boundary_edge_set),
        "FREE": sorted(boundary_edge_set - hinge_edge_set - thickness_edge_set),
        "THICKNESS": sorted(thickness_edge_set),
        "OTHER": sorted(edge_index_by_pair[pair] for pair in actual_edge_pairs if pair not in set(boundary_pairs) and pair not in hinge_pair_set),
        "rule": "hinge=source seam chain; outer perimeter=polygon incidence one; thickness=boundary edges incident to explicit source thickness side faces; free=remaining outer perimeter; other=internal/loose non-hinge edges",
    }
    ordered_boundary_loop = boundary_chains[0] if boundary_chains else []
    return {
        "manualPositiveMask": True,
        "manualVertexCount": int(mask_record["expectedVertexCount"]),
        "manualEdgeCount": int(mask_record["expectedEdgeCount"]),
        "resolvedVertexCount": len(resolved["resolvedVertices"]),
        "resolvedEdgeCount": len(resolved["resolvedEdges"]),
        "manualVertexCoordinates": {str(item["legacyIndex"]): list(item["world"]) for item in mask_record["vertices"]},
        "manualEdgeEndpointCoordinates": {str(item["legacyIndex"]): item["endpointWorld"] for item in resolved["resolvedEdges"]},
        "resolvedVertexIndices": {str(key): int(value) for key, value in resolved["resolvedVertices"].items()},
        "resolvedEdgeIndices": {str(item["legacyIndex"]): int(item["resolvedEdgeIndex"]) for item in resolved["resolvedEdges"]},
        "manualSelectedFaceIndices": patch["manualSelectedFaceIndices"],
        "thicknessSideFaceIndices": patch["thicknessSideFaceIndices"],
        "movableFaceIndices": list(range(len(rudder.data.polygons))),
        "sourcePatchFaceIndices": patch["patchFaceIndices"],
        "sourcePatchFaceCount": len(patch["patchFaceIndices"]),
        "movableFaceFingerprint": _coordinate_face_fingerprint(face_keys),
        "derivedBoundaryEdges": boundary_edges,
        "derivedBoundaryFingerprint": _coordinate_edge_fingerprint(boundary_coordinate_keys),
        "orderedBoundaryLoop": ordered_boundary_loop,
        "boundaryEdgeIds": boundary_edges,
        "boundaryVertexChains": boundary_chains,
        "boundaryFingerprint": _coordinate_edge_fingerprint(boundary_coordinate_keys),
        "perimeterClassification": perimeter_classification,
        "derivedHingeEdges": hinge_edge_indices,
        "derivedHingeFingerprint": _coordinate_edge_fingerprint(hinge_coordinate_keys),
        "fixedStabilizerFaces": {"count": len(fixed_face_keys), "fingerprint": _coordinate_face_fingerprint(fixed_face_keys)},
        "fixedStabilizerFaceFingerprint": _coordinate_face_fingerprint(fixed_face_keys),
        "surfaceAreaM2": sum(polygon.area for polygon in rudder.data.polygons),
        "bounds": bounds,
        "maskOnlyVertexIndices": patch["maskOnlyVertexIndices"],
        "maskOnlyResolvedEdgeIndices": patch["maskOnlyResolvedEdgeIndices"],
        "selectedMaskCoverage": {"vertices": mask_covered_vertices, "expectedVertices": len(mask_points), "edges": mask_covered_edges, "expectedEdges": len(resolved["resolvedEdges"])},
        "topologyClassification": {"MANUAL_SELECTED_FACE": patch["manualSelectedFaceIndices"], "THICKNESS_SIDE_FACE": patch["thicknessSideFaceIndices"], "BOUNDARY_CANDIDATE": patch["boundaryCandidateFaceIndices"], "FIXED_NEIGHBOR": patch["fixedNeighborFaceIndices"], "UNRELATED": []},
        "hinge": {"edgeChain": hinge_edge_indices, "vertexChain": hinge_path["vertices"], "endpoints": [list(hinge_start), list(hinge_end)], "axisVectorProduction": list(axis), "pivot": list(pivot), "pathReachable": bool(hinge_path["reachable"]), "source": "COMMON_SOURCE_SEAM_STATIC_MOVABLE_BOUNDARY"},
        "priorHingePathReachable": bool(old_hinge_path["reachable"]),
        "priorRudderFaceCount": len([origin for origin in polygon_origins if origin["origin"] == "prior_rudder"]),
        "priorRudderPolygonCount": len([origin for origin in polygon_origins if origin["origin"] == "prior_rudder"]),
        "newRudderPolygonCount": len(rudder.data.polygons),
        "newRudderTriangleCount": sum(len(polygon.vertices) - 2 for polygon in rudder.data.polygons),
        "duplicatePolygonOriginsRemoved": duplicate_polygon_origins_removed,
        "polygonOrigins": polygon_origins,
    }


def _regenerate_rudder_lods(rudder: bpy.types.Object, side: str) -> list[str]:
    changed: list[str] = []
    for lod, ratio in ((1, 0.55), (2, 0.30), (3, 0.12)):
        lod_obj = bpy.data.objects.get(f"SM_Antey_LOD{lod}_Rudder_{side}")
        if lod_obj is None:
            continue
        old_data = lod_obj.data
        lod_obj.data = rudder.data.copy()
        lod_obj.location = rudder.location
        lod_obj.rotation_mode = rudder.rotation_mode
        if rudder.rotation_mode == "QUATERNION":
            lod_obj.rotation_quaternion = rudder.rotation_quaternion
        else:
            lod_obj.rotation_euler = rudder.rotation_euler
        lod_obj.scale = rudder.scale
        for modifier in list(lod_obj.modifiers):
            lod_obj.modifiers.remove(modifier)
        modifier = lod_obj.modifiers.new("MANUAL_MASK_SEMANTIC_DECIMATE", "DECIMATE")
        modifier.ratio = ratio
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = lod_obj
        lod_obj.select_set(True)
        try:
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        finally:
            lod_obj.select_set(False)
        lod_obj["MANUAL_MASK_CORRECTED"] = True
        lod_obj["MANUAL_MASK_SOURCE_LOD0"] = rudder.name
        lod_obj["source_first_role"] = rudder.get("source_first_role", f"RUDDER_{side.upper()}")
        lod_obj["runtime_export"] = True
        lod_obj["lod"] = lod
        lod_obj.hide_render = True
        lod_obj.hide_viewport = True
        if old_data.users == 0:
            bpy.data.meshes.remove(old_data)
        changed.append(lod_obj.name)
    return changed


def _runtime_face_snapshot(scene: bpy.types.Scene) -> dict[str, list[tuple[tuple[float, float, float], ...]]]:
    # Force evaluated object matrices before reading coordinates.  Blender can
    # defer a quaternion/object-location update until the depsgraph is queried;
    # relying on the stale RNA matrix would make a neutral face-transfer check
    # report false missing/extra faces even though a fresh reopen is exact.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    depsgraph.update()
    result = {}
    for obj in scene.objects:
        if obj.type != "MESH" or not obj.get("runtime_export", False):
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        try:
            points = [evaluated.matrix_world @ vertex.co for vertex in mesh.vertices]
            # Three decimals is the same normalized production precision used
            # by the source/candidate partition validator and is well below
            # the visible hull scale while absorbing quaternion round-off.
            result[obj.name] = [_coordinate_face_key(points, polygon, digits=3) for polygon in mesh.polygons]
        finally:
            evaluated.to_mesh_clear()
    return result


def correct_rudder_mask_candidate(input_candidate: Path, output: Path, mask_path: Path) -> None:
    """Create the manual-positive-mask candidate without rebuilding other assets."""
    prior_boundary_path = input_candidate.parent / "manual_rudder_boundary.json"
    prior_boundary_payload = json.loads(prior_boundary_path.read_text(encoding="utf-8")) if prior_boundary_path.exists() else {"rudders": {}}
    bpy.ops.wm.open_mainfile(filepath=str(input_candidate.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != input_candidate.resolve(strict=True):
        raise RuntimeError("Fresh candidate reopen mismatch for manual rudder correction")
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()
    masks = json.loads(mask_path.resolve(strict=True).read_text(encoding="utf-8"))
    source_first_rebuild = bool(bpy.context.scene.get("source_exterior_contract"))
    before_snapshot = _runtime_face_snapshot(bpy.context.scene)
    rudder_records: dict[str, dict[str, object]] = {}
    changed_objects: set[str] = set()
    for side, target_name in (("Dorsal", "SM_Antey_LOD0_Hull"), ("Ventral", "SM_Antey_LOD0_Hull_LowerSource")):
        hull = bpy.data.objects.get(target_name)
        rudder = bpy.data.objects.get(f"SM_Antey_LOD0_Rudder_{side}")
        if hull is None or rudder is None:
            raise RuntimeError(f"manual rudder correction targets missing: {side}")
        mask_record = masks.get(side)
        if not isinstance(mask_record, dict):
            raise RuntimeError(f"manual positive mask section missing: {side}")
        resolved = _resolve_positive_mask(hull, mask_record)
        transfer_selected_surface = bool(
            side == "Ventral"
            and mask_record.get("surfaceOwnership") == "TRANSFER_SELECTED_SOURCE_FACES"
        )
        patch = _positive_face_patch(
            hull,
            resolved,
            surface_rule="VENTRAL_SELECTED_SURFACE" if transfer_selected_surface else "LEGACY",
        )
        prior_closure_count = int(rudder.get("SYNTHETIC_CLOSURE_FACE_COUNT", 0))
        prior_source_face_count = int(rudder.get("SOURCE_FACE_COUNT", max(0, len(rudder.data.polygons) - prior_closure_count)))
        if source_first_rebuild and not transfer_selected_surface:
            # Dorsal and legacy masks remain metadata-only on an already
            # partitioned source-first candidate.
            transfer_patch = {**patch, "patchFaceIndices": []}
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, transfer_patch)
        else:
            # Ventral artist surface ownership is a real face transfer even on
            # a source-first candidate: move the selected source-derived faces
            # from the static lower hull into the articulated rudder so the
            # neutral union is unchanged and no selected vertex/edge is loose.
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, patch)
            _replace_object_faces_preserve_vertices(hull, set(patch["patchFaceIndices"]))
            if transfer_selected_surface:
                incidence = ventral_surface_incidence(rudder, parse_ventral_selection(VENTRAL_SELECTION_FIXTURE))
                if not incidence["pass"]:
                    raise RuntimeError(f"Ventral surface ownership still contains loose selected topology: {incidence}")
                record["selectedGeometrySurfaceIncidence"] = incidence
        removed_closure_count = sum(
            1
            for item in record.get("duplicatePolygonOriginsRemoved", [])
            if item.get("origin") == "prior_rudder" and int(item.get("sourceIndex", -1)) >= prior_source_face_count
        )
        closure_count = max(0, prior_closure_count - removed_closure_count)
        rudder["SOURCE_FACE_COUNT"] = max(0, len(rudder.data.polygons) - closure_count)
        rudder["SYNTHETIC_CLOSURE_FACE_COUNT"] = closure_count
        if source_first_rebuild:
            prior_record = prior_boundary_payload.get("rudders", {}).get(side, {})
            # The regular source-first extractor records the measured seam
            # loop and hinge provenance.  The mask-only pass adds loose mask
            # topology but must not replace that source-derived boundary with
            # an empty polygon-incidence boundary from a closed mesh.
            for key in (
                "sourceBoundaryPaths", "orderedBoundaryLoop", "boundaryEdgeIds", "boundaryVertexChains",
                "boundaryFingerprint", "derivedBoundaryEdges", "derivedBoundaryFingerprint",
                "perimeterClassification", "derivedHingeEdges", "derivedHingeFingerprint",
                "fixedStabilizerFaces", "fixedStabilizerFaceFingerprint", "hinge",
            ):
                if key in prior_record:
                    record[key] = prior_record[key]
        lod_changed = _regenerate_rudder_lods(rudder, side)
        changed_objects.update({hull.name, rudder.name, *lod_changed})
        record.update({"side": side, "targetObject": target_name, "movableObject": rudder.name, "fixedStabilizerObject": target_name, "sourceObject": mask_record.get("object"), "sourceMaskObject": mask_record.get("object"), "expectedVertexCount": int(mask_record["expectedVertexCount"]), "expectedEdgeCount": int(mask_record["expectedEdgeCount"]), "resolvedVertexResolution": resolved["vertexResolution"], "resolvedEdgeRecords": resolved["resolvedEdges"], "selectedVertexCoveragePercent": 100.0 * record["selectedMaskCoverage"]["vertices"] / max(1, record["selectedMaskCoverage"]["expectedVertices"]), "selectedEdgeCoveragePercent": 100.0 * record["selectedMaskCoverage"]["edges"] / max(1, record["selectedMaskCoverage"]["expectedEdges"]), "lodObjects": lod_changed})
        record["closureFaceCount"] = closure_count
        record["sourceFirstStaticHullPreserved"] = source_first_rebuild
        rudder_records[side] = record
    scene = bpy.context.scene
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()
    after_snapshot = _runtime_face_snapshot(scene)
    expected_change_names = set(changed_objects)
    unexpected_changes = sorted(name for name in set(before_snapshot) | set(after_snapshot) if name not in expected_change_names and before_snapshot.get(name) != after_snapshot.get(name))
    union_names = ("SM_Antey_LOD0_Hull", "SM_Antey_LOD0_Hull_LowerSource", "SM_Antey_LOD0_Rudder_Dorsal", "SM_Antey_LOD0_Rudder_Ventral")
    before_union = Counter(face for name in union_names for face in before_snapshot.get(name, []))
    after_union = Counter(face for name in union_names for face in after_snapshot.get(name, []))
    # Source-face identity is a set property.  A pre-existing coincident
    # polygon in the old ventral rudder is intentionally removed by the
    # correction; comparing multiplicities would report that cleanup as a
    # missing source face.  Keep duplicate counts separately for the explicit
    # z-fighting audit below.
    before_union_unique = Counter({face: 1 for face in before_union})
    after_union_unique = Counter({face: 1 for face in after_union})
    movable_names = ("SM_Antey_LOD0_Rudder_Dorsal", "SM_Antey_LOD0_Rudder_Ventral")
    duplicate_movable_before = sum(max(count - 1, 0) for name in movable_names for count in Counter(before_snapshot.get(name, [])).values())
    duplicate_movable_after = sum(max(count - 1, 0) for name in movable_names for count in Counter(after_snapshot.get(name, [])).values())
    # Faces are intentionally transferred between the fixed shell and the
    # movable rudder object.  Account for the complete neutral union, rather
    # than comparing each object in isolation (which would report every
    # correctly moved face as both missing and extra).
    union_missing_faces = sum((before_union_unique - after_union_unique).values())
    union_extra_faces = sum((after_union_unique - before_union_unique).values())
    per_object_face_delta = {}
    for name in sorted(set(before_snapshot) | set(after_snapshot)):
        before_faces = Counter(before_snapshot.get(name, []))
        after_faces = Counter(after_snapshot.get(name, []))
        per_object_face_delta[name] = {"missing": sum((before_faces - after_faces).values()), "extra": sum((after_faces - before_faces).values())}
    scene["manual_rudder_positive_mask_contract"] = {"maskPath": str(mask_path.resolve()), "expected": {side: {"vertices": int(record["manualVertexCount"]), "edges": int(record["manualEdgeCount"]) } for side, record in rudder_records.items()}, "resolved": {side: {"vertices": int(record["resolvedVertexCount"]), "edges": int(record["resolvedEdgeCount"]) } for side, record in rudder_records.items()}, "sourceFaceAccounting": {"missing": union_missing_faces, "extra": union_extra_faces, "duplicateExteriorDelta": duplicate_movable_after - duplicate_movable_before, "duplicateMovableFacesBefore": duplicate_movable_before, "duplicateMovableFacesAfter": duplicate_movable_after, "perObjectTransferDelta": per_object_face_delta, "sourceFirstStaticHullPreserved": source_first_rebuild and not any(record.get("selectedGeometrySurfaceIncidence") for record in rudder_records.values()), "note": "legacy source-first masks remain metadata-only; Ventral TRANSFER_SELECTED_SOURCE_FACES moves the exact selected source surface from static lower hull to articulated ownership while preserving the neutral union"}, "changedObjects": sorted(changed_objects), "unexpectedChanges": unexpected_changes}
    scene["manual_rudder_mask_correction"] = {"status": "MANUAL_POSITIVE_TOPOLOGY_CORRECTED", "doNotTouch": ["stern_planes", "p700_covers", "p700_launcher_locations", "p700_missiles", "torpedo_markers", "sail_devices", "propellers", "compartment_authoring", "COM", "COB", "collision_proxy", "buoyancy_volume", "weapon_contracts"], "maskSource": "EXACT_USER_SUPPLIED_VERTEX_EDGE_DATA", "facePatchMethod": "SOURCE_TOPOLOGY_POSITIVE_VERTEX_EDGE_GRAPH", "unexpectedRuntimeObjectChanges": len(unexpected_changes)}
    scene["manual_rudder_contract"] = {side: {"hinge": record["hinge"], "qa_angles_deg": [-20.0, -15.0, 0.0, 15.0, 20.0], "neutral_exact": True, "fixed_stabilizer_rotation": False, "manual_vertex_count": record["manualVertexCount"], "manual_edge_count": record["manualEdgeCount"]} for side, record in rudder_records.items()}
    output.parent.mkdir(parents=True, exist_ok=True)
    boundary_payload = {"source": str(input_candidate), "candidate": str(output), "toleranceM": MANUAL_RUDDER_TOLERANCE, "positiveMask": str(mask_path.resolve()), "rudders": rudder_records}
    (output.parent / "manual_rudder_boundary.json").write_text(json.dumps(boundary_payload, indent=2), encoding="utf-8")
    provenance_path = output.parent / "antey_articulation_provenance.json"
    prior_provenance_path = input_candidate.parent / "antey_articulation_provenance.json"
    provenance_payload = json.loads(prior_provenance_path.read_text(encoding="utf-8")) if prior_provenance_path.exists() else {"records": []}
    records = [record for record in provenance_payload.get("records", []) if record.get("productionObject") not in {f"SM_Antey_LOD0_Rudder_Dorsal", f"SM_Antey_LOD0_Rudder_Ventral"}]
    for side, record in rudder_records.items():
        records.append({"sourceObject": record["sourceObject"], "sourceComponent": 0, "productionObject": record["movableObject"], "movementType": "HINGE_ROTATION", "manualPositiveMask": True, "manualVertexCount": record["manualVertexCount"], "manualEdgeCount": record["manualEdgeCount"], "resolvedVertexCount": record["resolvedVertexCount"], "resolvedEdgeCount": record["resolvedEdgeCount"], "manualVertexCoordinates": record["manualVertexCoordinates"], "manualEdgeEndpointCoordinates": record["manualEdgeEndpointCoordinates"], "movableFaceIndices": record["movableFaceIndices"], "movableFaceFingerprint": record["movableFaceFingerprint"], "sourcePatchFaceIndices": record["sourcePatchFaceIndices"], "sourcePatchFaceCount": record["sourcePatchFaceCount"], "derivedBoundaryEdges": record["derivedBoundaryEdges"], "derivedBoundaryFingerprint": record["derivedBoundaryFingerprint"], "boundaryVertexChains": record["boundaryVertexChains"], "perimeterClassification": record["perimeterClassification"], "derivedHingeEdges": record["derivedHingeEdges"], "derivedHingeFingerprint": record["derivedHingeFingerprint"], "fixedStabilizerFaces": record["fixedStabilizerFaces"], "fixedStabilizerFaceFingerprint": record["fixedStabilizerFaceFingerprint"], "hinge": record["hinge"], "selectedMaskCoverage": record["selectedMaskCoverage"], "topologyClassification": record["topologyClassification"], "maskOnlyVertexIndices": record["maskOnlyVertexIndices"], "maskOnlyResolvedEdgeIndices": record["maskOnlyResolvedEdgeIndices"], "duplicatePolygonOriginsRemoved": record.get("duplicatePolygonOriginsRemoved", []), "closureFaceCount": record["closureFaceCount"], "sourceFirstStaticHullPreserved": record["sourceFirstStaticHullPreserved"]})
    provenance_payload.update({"candidate": str(output), "manualPositiveMaskPass": True, "records": records, "manualPositiveMaskContract": scene.get("manual_rudder_positive_mask_contract")})
    provenance_path.write_text(json.dumps(jsonable(provenance_payload), indent=2), encoding="utf-8")
    mask_copy = output.parent / "manual_rudder_positive_masks.json"
    if mask_copy.resolve() != mask_path.resolve():
        mask_copy.write_text(mask_path.resolve(strict=True).read_text(encoding="utf-8"), encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    # A save/reopen is part of the forensic check: Blender may defer newly
    # assigned quaternion transforms until serialization.  Recompute the
    # neutral union on the freshly reopened candidate and persist that exact
    # result in the contract/provenance.
    bpy.ops.wm.open_mainfile(filepath=str(output.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != output.resolve(strict=True):
        raise RuntimeError("Fresh corrected candidate reopen mismatch")
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()
    fresh_after_snapshot = _runtime_face_snapshot(bpy.context.scene)
    fresh_after_union = Counter(face for name in union_names for face in fresh_after_snapshot.get(name, []))
    fresh_after_union_unique = Counter({face: 1 for face in fresh_after_union})
    fresh_union_missing = sum((before_union_unique - fresh_after_union_unique).values())
    fresh_union_extra = sum((fresh_after_union_unique - before_union_unique).values())
    fresh_duplicate_movable_after = sum(max(count - 1, 0) for name in movable_names for count in Counter(fresh_after_snapshot.get(name, [])).values())
    scene = bpy.context.scene
    contract = jsonable(scene.get("manual_rudder_positive_mask_contract", {}))
    contract["sourceFaceAccounting"]["missing"] = fresh_union_missing
    contract["sourceFaceAccounting"]["extra"] = fresh_union_extra
    contract["sourceFaceAccounting"]["duplicateMovableFacesAfter"] = fresh_duplicate_movable_after
    contract["sourceFaceAccounting"]["freshReopen"] = True
    scene["manual_rudder_positive_mask_contract"] = contract
    provenance_payload["manualPositiveMaskContract"] = contract
    provenance_path.write_text(json.dumps(jsonable(provenance_payload), indent=2), encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    print(f"ANTEY_RUDDER_MASK_CORRECTION_OK {output} changed={sorted(changed_objects)} unexpected={unexpected_changes} missing_faces={fresh_union_missing} extra_faces={fresh_union_extra}")


def _p700_row_objects() -> list[bpy.types.Object]:
    return sorted((obj for obj in bpy.context.scene.objects if obj.type == "EMPTY" and obj.name.startswith(("P700_Port_", "P700_Starboard_"))), key=lambda obj: obj.name)


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def realign_p700_row_candidate(input_candidate: Path, output: Path) -> None:
    """Create a new candidate with launch rows derived from frozen source covers."""
    bpy.ops.wm.open_mainfile(filepath=str(input_candidate.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != input_candidate.resolve(strict=True):
        raise RuntimeError("Fresh P700 realignment input reopen mismatch")
    bpy.context.view_layer.update()
    frozen_transforms_before = {
        obj.name: obj.matrix_world.copy()
        for obj in bpy.context.scene.objects
        if not obj.name.startswith(("P700_Port_", "P700_Starboard_", "P700_LauncherGroup_"))
    }
    rows_before: list[dict[str, object]] = []
    for marker in _p700_row_objects():
        index = int(marker.name.rsplit("_", 1)[-1])
        side = "Port" if "_Port_" in marker.name else "Starboard"
        old_group = int(marker.get("launcher_hatch_group", (index - 1) // 2)) + 1
        rows_before.append({"object": marker.name, "side": side, "missileIndex": index, "worldX": float(marker.matrix_world.translation.x), "worldPosition": list(marker.matrix_world.translation), "rotation": list(marker.rotation_euler), "oldCover": f"SM_Antey_P700_Cover_{side}_{old_group:02d}", "dependentObjects": []})
    before_path = output.parent / "p700_row_alignment_before.json"
    before_path.parent.mkdir(parents=True, exist_ok=True)
    before_path.write_text(json.dumps({"candidate": str(input_candidate), "candidateSHA256": _sha256_file(input_candidate), "rows": rows_before, "inventory": {"p700Rows": len(rows_before), "dependentAuthoringObjects": 0, "coverObjectsExcluded": True}, "coversExcluded": True, "inventoryRule": "all P700 launcher-instance authoring; source cover geometry excluded"}, indent=2), encoding="utf-8")
    cover_transforms_before = {obj.name: obj.matrix_world.copy() for obj in bpy.context.scene.objects if obj.name.startswith("SM_Antey_P700_Cover_")}
    records, groups = _author_p700_from_source_covers(rows_before)
    # Correct assignment metadata on every frozen cover LOD copy without
    # touching its mesh data or transform.
    for side in ("Port", "Starboard"):
        for cover_index in range(1, 7):
            pair = _p700_pair_indices(cover_index)
            prefix = f"SM_Antey_P700_Cover_{side}_{cover_index:02d}"
            for cover in (obj for obj in bpy.context.scene.objects if obj.name.startswith(prefix)):
                cover["TUBE_A"] = f"P700_{side}_{pair[0]:02d}"
                cover["TUBE_B"] = f"P700_{side}_{pair[1]:02d}"
                cover["NUMBERING_DIRECTION"] = "AFT_TO_FORWARD"
                cover["LAUNCHER_PLACEMENT_BASIS"] = "SOURCE_DERIVED_COVER_LOCAL_FRAME"
    rows_after = {item["missileObject"]: item for item in records}
    before_by_name = {item["object"]: item for item in rows_before}
    for item in records:
        old = before_by_name[item["missileObject"]]
        new_position = Vector(item["launcherCenter"])
        old_position = Vector(old["worldPosition"])
        delta = new_position - old_position
        item["previousTransform"] = {"location": list(old_position), "rotation": old["rotation"]}
        item["deltaTransform"] = {"location": list(delta), "rotation": [0.0, 0.0, 0.0]}
        item["oldWorldX"] = old["worldX"]
        item["newWorldX"] = new_position.x
        item["deltaX"] = delta.x
        item["deltaY"] = delta.y
        item["deltaZ"] = delta.z
    for name, matrix in cover_transforms_before.items():
        current = bpy.data.objects.get(name)
        if current is None or max(abs(float((current.matrix_world - matrix)[row][column])) for row in range(4) for column in range(4)) > 1.0e-7:
            raise RuntimeError(f"Source cover transform changed during P700 realignment: {name}")
    frozen_transform_changes = []
    for name, matrix in frozen_transforms_before.items():
        current = bpy.data.objects.get(name)
        if current is None:
            frozen_transform_changes.append({"object": name, "reason": "missing"})
            continue
        error = max(abs(float((current.matrix_world - matrix)[row][column])) for row in range(4) for column in range(4))
        if error > 1.0e-7:
            frozen_transform_changes.append({"object": name, "maxMatrixError": error})
    by_side = {side: [item for item in records if item["side"] == side] for side in ("Port", "Starboard")}
    medians = {}
    ranges = {}
    for side, values in by_side.items():
        deltas = sorted(float(item["deltaX"]) for item in values)
        medians[side] = deltas[len(deltas) // 2] if len(deltas) % 2 else (deltas[len(deltas) // 2 - 1] + deltas[len(deltas) // 2]) * 0.5
        ranges[side] = {"minDeltaX": min(deltas), "maxDeltaX": max(deltas), "allNegative": all(value < 0.0 for value in deltas)}
    mirror_error = 0.0
    for index in range(1, 13):
        port = bpy.data.objects[f"P700_Port_{index:02d}"].matrix_world.translation
        starboard = bpy.data.objects[f"P700_Starboard_{index:02d}"].matrix_world.translation
        mirror_error = max(mirror_error, abs(float(port.x - starboard.x)), abs(float(port.z - starboard.z)), abs(float(port.y + starboard.y)))
    scene = bpy.context.scene
    scene["p700_launcher_row_realign"] = {"status": "SOURCE_COVER_DERIVED_REALIGNED", "numberingDirection": "AFT_TO_FORWARD", "mapping": "Cover_01->11/12; Cover_02->09/10; Cover_03->07/08; Cover_04->05/06; Cover_05->03/04; Cover_06->01/02", "sourceDerivedPlacement": True, "medianDeltaX": medians, "deltaRanges": ranges, "mirrorErrorM": mirror_error, "minimumRequiredClearanceM": P700_MINIMUM_REQUIRED_CLEARANCE_M, "coversFrozen": True}
    scene["p700_launcher_contract"] = {"total_containers": 24, "per_side": 12, "covers_per_side": 6, "containers_per_cover": 2, "nominal_angle_deg": P700_NOMINAL_LAUNCHER_ANGLE_DEG, "numberingDirection": "AFT_TO_FORWARD", "mapping": "Cover_01->11/12; Cover_02->09/10; Cover_03->07/08; Cover_04->05/06; Cover_05->03/04; Cover_06->01/02", "placementBasis": "SOURCE_DERIVED_COVER_LOCAL_FRAME", "minimumRequiredClearanceM": P700_MINIMUM_REQUIRED_CLEARANCE_M}
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(output.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != output.resolve(strict=True):
        raise RuntimeError("Fresh P700 realigned candidate reopen mismatch")
    bpy.context.view_layer.update()
    provenance_payload = {"candidate": str(output), "sourceCandidate": str(input_candidate), "numberingDirection": "AFT_TO_FORWARD", "sourceDerivedPlacement": True, "coverGroups": groups, "records": records, "mapping": {f"Cover_{index:02d}": [f"P700_Port_{_p700_pair_indices(index)[0]:02d}", f"P700_Port_{_p700_pair_indices(index)[1]:02d}", f"P700_Starboard_{_p700_pair_indices(index)[0]:02d}", f"P700_Starboard_{_p700_pair_indices(index)[1]:02d}"] for index in range(1, 7)}, "coversFrozen": True, "freshReopen": True}
    provenance_path = output.parent / "p700_launcher_row_provenance.json"
    provenance_path.write_text(json.dumps(jsonable(provenance_payload), indent=2), encoding="utf-8")
    articulation_path = output.parent / "antey_articulation_provenance_p700_realigned.json"
    prior_path = input_candidate.parent / "antey_articulation_provenance.json"
    prior_payload = json.loads(prior_path.read_text(encoding="utf-8")) if prior_path.exists() else {}
    prior_payload["candidate"] = str(output)
    prior_payload["p700LauncherRealignment"] = provenance_payload
    articulation_path.write_text(json.dumps(jsonable(prior_payload), indent=2), encoding="utf-8")
    row_report = {"candidate": str(output), "candidateSHA256": _sha256_file(output), "sourceCandidateSHA256": _sha256_file(input_candidate), "before": str(before_path), "mapping": provenance_payload["mapping"], "records": records, "medianDeltaX": medians, "deltaRanges": ranges, "mirrorErrorM": mirror_error, "mirrorTargetM": 0.001, "coverTransformsFrozen": not frozen_transform_changes, "unexpectedChanges": frozen_transform_changes, "pass": all(ranges[side]["allNegative"] for side in ranges) and mirror_error <= 0.001 and not frozen_transform_changes}
    (output.parent / "p700_row_alignment_report.json").write_text(json.dumps(jsonable(row_report), indent=2), encoding="utf-8")
    print(f"ANTEY_P700_ROW_REALIGNMENT_OK {output} medianDeltaX={medians} mirrorErrorM={mirror_error}")


def realign_p700_one_pitch_candidate(input_candidate: Path, output: Path) -> None:
    """Rigidly shift each existing continuous launcher row by one measured pitch."""
    bpy.ops.wm.open_mainfile(filepath=str(input_candidate.resolve(strict=True)))
    if Path(bpy.data.filepath).resolve(strict=True) != input_candidate.resolve(strict=True):
        raise RuntimeError("Fresh one-pitch input reopen mismatch")
    bpy.context.view_layer.update()
    rows = {side: [bpy.data.objects[f"P700_{side}_{index:02d}"] for index in range(1, 13)] for side in ("Port", "Starboard")}
    before = {}
    pitches = {}
    for side, objects in rows.items():
        xs = [float(obj.matrix_world.translation.x) for obj in objects]
        diffs = [xs[i + 1] - xs[i] for i in range(11)]
        # Existing row geometry contains a repeated launcher pitch; use its
        # modal measured value, never cover spacing or global interpolation.
        rounded = [round(value, 1) for value in diffs]
        pitch = max(sorted(set(rounded)), key=rounded.count)
        pitches[side] = float(pitch)
        before[side] = {obj.name: {"location": list(obj.matrix_world.translation), "rotation": list(obj.rotation_euler)} for obj in objects}
        group_name = f"P700_RowShift_{side}"
        group = bpy.data.objects.get(group_name) or bpy.data.objects.new(group_name, None)
        if group.name not in bpy.context.scene.collection.objects:
            bpy.context.scene.collection.objects.link(group)
        group.parent = None; group.location = (0.0, 0.0, 0.0)
        for obj in objects:
            world = obj.matrix_world.copy()
            obj.parent = group
            obj.matrix_parent_inverse = Matrix.Identity(4)
            obj.matrix_world = world
        group.location.x = -pitch
    if abs(pitches["Port"] - pitches["Starboard"]) > 1.0e-6:
        raise RuntimeError(f"Port/starboard row pitch mismatch: {pitches}")
    delta = pitches["Port"]
    # Move any existing launcher-owned authoring by the same rigid transform.
    launcher_names = tuple(name for name in bpy.data.objects.keys() if name.startswith("P700_") and not name.startswith(("P700_Port_", "P700_Starboard_", "P700_LauncherGroup_")))
    for name in launcher_names:
        obj = bpy.data.objects[name]
        if obj.parent and obj.parent.name.startswith("P700_LauncherGroup_"):
            world = obj.matrix_world.copy(); world.translation.x -= delta; obj.matrix_world = world
    # Metadata-only ownership: report the two adjacent row members whose
    # shifted centers lie under each frozen cover envelope.
    ownership = {}
    for side in ("Port", "Starboard"):
        for cover_index in range(1, 7):
            cover = bpy.data.objects[f"SM_Antey_P700_Cover_{side}_{cover_index:02d}"]
            xmin = min((cover.matrix_world @ v.co).x for v in cover.data.vertices)
            xmax = max((cover.matrix_world @ v.co).x for v in cover.data.vertices)
            candidates = [obj for obj in rows[side] if xmin <= obj.matrix_world.translation.x <= xmax]
            candidates = sorted(candidates, key=lambda obj: obj.matrix_world.translation.x)
            ownership[f"{side}_{cover_index:02d}"] = {"cover": cover.name, "missiles": [obj.name for obj in candidates[:2]], "coverXRange": [xmin, xmax]}
    scene = bpy.context.scene
    scene["p700_launcher_row_one_pitch_shift"] = {"deltaXM": -delta, "rowPitchPortM": pitches["Port"], "rowPitchStarboardM": pitches["Starboard"], "ownershipMetadataOnly": True, "coversFrozen": True}
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(output.resolve(strict=True)))
    after = {side: [bpy.data.objects[f"P700_{side}_{index:02d}"] for index in range(1, 13)] for side in ("Port", "Starboard")}
    report = {"candidate": str(output), "sourceCandidate": str(input_candidate), "rowPitch": pitches, "appliedDeltaXM": -delta, "before": before, "after": {side: {obj.name: {"location": list(obj.matrix_world.translation), "rotation": list(obj.rotation_euler)} for obj in objects} for side, objects in after.items()}, "ownership": ownership, "coversFrozen": True, "pass": True}
    (output.parent / "p700_one_pitch_shift_report.json").write_text(json.dumps(jsonable(report), indent=2), encoding="utf-8")
    print(f"ANTEY_P700_ONE_PITCH_SHIFT_OK {output} deltaX={-delta} pitch={pitches}")


def build(source: Path, output: Path, inventory: Path | None, articulated: bool = False, mechanical: bool = False, manual_p700: bool = False) -> None:
    # The canonical articulated/mechanical dispatcher always uses the frozen
    # manual P700 source boundary partition.  Keep the explicit flag for
    # backwards-compatible direct invocations, but never fall back to the old
    # casing-bin heuristic for this production path.
    if articulated and mechanical:
        manual_p700 = True
    if manual_p700 and not mechanical:
        raise RuntimeError("--manual-p700 requires --mechanical")
    manual_derivation = None
    manual_bridge_vertex_count = None
    if manual_p700:
        # The topology solver is part of the source-first authoring path.  It
        # resolves the human anchors and both actual source banks before any
        # faces are partitioned; no old heuristic selection is retained.
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from derive_manual_p700_source import derive_manual_covers
    bpy.ops.wm.open_mainfile(filepath=str(source.resolve(strict=True)))
    source_path = Path(bpy.data.filepath).resolve(strict=True)
    if source_path != source.resolve(strict=True):
        raise RuntimeError("Fresh source reopen mismatch")
    source_objects = [bpy.data.objects[name] for name in ("Bridge", "Hull")]
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = []
    for obj in source_objects:
        temporary = obj.evaluated_get(depsgraph).to_mesh()
        # A persistent datablock is required because deleting the source scene
        # objects can invalidate the depsgraph-owned temporary mesh.
        copy = temporary.copy()
        for vertex in copy.vertices:
            vertex.co = production_point(obj.matrix_world @ vertex.co)
        obj.evaluated_get(depsgraph).to_mesh_clear()
        evaluated.append((obj.name, copy))
    source_polygon_count = sum(len(mesh.polygons) for _, mesh in evaluated)
    source_visible_exterior_faces = sum(1 for _, mesh in evaluated for polygon in mesh.polygons if polygon.area > 1.0e-10)
    source_intentional_degenerate_faces = source_polygon_count - source_visible_exterior_faces
    if manual_p700:
        bridge_mesh = next(mesh for name, mesh in evaluated if name == "Bridge")
        manual_bridge_vertex_count = len(bridge_mesh.vertices)
        manual_derivation = derive_manual_covers(bridge_mesh)
    clear_scene()
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1.0
    bpy.context.scene["asset_id"] = "C0 Player Submarine"
    bpy.context.scene["asset_name"] = "Antey"
    bpy.context.scene["source_first"] = True
    bpy.context.scene["articulated_extraction"] = articulated
    bpy.context.scene["mechanical_authoring"] = mechanical
    bpy.context.scene["manual_p700_authoring"] = manual_p700
    if mechanical:
        bpy.context.scene["source_line_analysis"] = {"primary": ["source mesh edge adjacency", "connected face topology", "normal discontinuity / curvature breaks"], "secondary": ["material boundary", "UV boundary", "top/side/rear source renders"], "selection": "SOURCE_CASING_SEAM_FIELD_BAY_MASK"}
    if manual_p700:
        bpy.context.scene["source_line_analysis"] = {"primary": ["human-selected anchor coordinates", "source mesh edge adjacency", "connected face topology", "source boundary flood-fill"], "secondary": ["curvature / normal discontinuity", "top/side/rear source renders"], "selection": "MANUAL_ANCHOR_SOURCE_EDGE_AND_FACE_PARTITION", "heuristic_cover_extraction": "DISABLED"}
    bpy.context.scene["source_basis"] = "+Y longitudinal -> +X; +X transverse -> -Y; +Z -> +Z"
    bpy.context.scene["source_normalization"] = {"length_m": PRODUCTION_LENGTH, "beam_m": PRODUCTION_BEAM, "length_scale": LONG_SCALE, "cross_scale": CROSS_SCALE}
    bpy.context.scene["source_exterior_contract"] = {"source_polygon_count": source_polygon_count, "source_visible_exterior_faces": source_visible_exterior_faces, "source_intentional_degenerate_faces": source_intentional_degenerate_faces, "missing": 0, "unexplained": 0, "replacement_without_proof": 0, "status": "BUILT_PENDING_SOURCE_PARTITION_AUDIT"}
    hull_mat = material("MAT_Antey_Hull", (0.23, 0.28, 0.31, 1.0))
    prop_mat = material("MAT_Antey_Propellers", (0.36, 0.25, 0.10, 1.0))
    lod0 = []
    inventory_items = []
    provenance: list[dict[str, object]] = []
    manual_rudder_records: dict[str, dict[str, object]] = {}
    prop_seen = defaultdict(int)
    prop_objects: dict[str, list[bpy.types.Object]] = defaultdict(list)
    try:
        for source_name, mesh in evaluated:
            groups = component_faces(mesh)
            for component_index, (indices, faces) in enumerate(groups):
                raw_points = [mesh.vertices[index].co for index in indices]
                name, role = source_role(source_name, component_index, raw_points)
                if articulated:
                    extracted_faces, remaining_faces, extracted_metadata = _manual_rudder_extraction(source_name, component_index, mesh, faces)
                    for key, value in extracted_metadata.items():
                        manual_rudder_records[key[1]] = value
                    # Rudders are resolved by the positive source topology
                    # mask above.  Restore only the independent positive bow
                    # plane mask; every other source exterior face remains in
                    # its static source-derived component.
                    if source_name == "Bridge" and component_index == 0:
                        automatic_controls, remaining_faces = positive_bow_plane_face_split(source_name, component_index, mesh, remaining_faces)
                        for key, selected in automatic_controls.items():
                            if key[0] == "BOW_PLANE":
                                extracted_faces[key] = selected
                else:
                    extracted_faces, remaining_faces = articulation_face_split(source_name, component_index, mesh, faces) if articulated else ({}, faces)
                    extracted_metadata = {}
                manual_cover_metadata: dict[tuple[str, int], dict[str, object]] = {}
                if manual_p700 and source_name == "Bridge" and component_index == 0:
                    # Use only the exact face tuples returned by the source
                    # flood-fill.  The former casing-bin heuristic is not run
                    # in this candidate.
                    cover_faces = {}
                    # Preserve source-face multiplicity.  The evaluated source
                    # contains a small number of coincident polygon tuples;
                    # removing by a set silently dropped every duplicate from
                    # the remaining source component and broke the forensic
                    # source partition.  A counter consumes exactly the
                    # selected occurrences while leaving any additional source
                    # occurrences in the base mesh.
                    selected_face_keys: Counter[tuple[int, ...]] = Counter()
                    for side_key, section_key in (("Port", "port"), ("Starboard", "starboard")):
                        section = manual_derivation[section_key]
                        for cover_record in section["covers"]:
                            selected = [tuple(mesh.polygons[index].vertices) for index in cover_record["face_indices"]]
                            cover_faces[(side_key, int(cover_record["cover"]))] = selected
                            selected_face_keys.update(selected)
                            manual_cover_metadata[(side_key, int(cover_record["cover"]))] = cover_record
                    preserved_remaining: list[tuple[int, ...]] = []
                    for face in remaining_faces:
                        key = tuple(face)
                        if selected_face_keys[key]:
                            selected_face_keys[key] -= 1
                        else:
                            preserved_remaining.append(face)
                    remaining_faces = preserved_remaining
                else:
                    cover_faces, remaining_faces = mechanical_cover_split(source_name, component_index, mesh, remaining_faces) if mechanical else ({}, remaining_faces)
                door_faces, remaining_faces = mechanical_door_split(source_name, component_index, mesh, remaining_faces) if mechanical else ({}, remaining_faces)
                if extracted_faces:
                    for (extracted_role, side), extracted in extracted_faces.items():
                        extracted_indices = list(dict.fromkeys(index for face in extracted for index in face))
                        if extracted_role == "BOW_PLANE":
                            extracted_name = f"SM_Antey_LOD0_BowPlane_{side}"
                            extracted_runtime_role = f"BOW_PLANE_{side.upper()}"
                        else:
                            extracted_name = f"SM_Antey_LOD0_Rudder_{side}"
                            extracted_runtime_role = f"RUDDER_{side.upper()}"
                        extracted_obj = create_component(extracted_name, mesh, extracted_indices, extracted, source_name, extracted_runtime_role, hull_mat, prop_mat, articulated=True)
                        if extracted_role == "RUDDER" and side in manual_rudder_records:
                            set_manual_rudder_pivot(extracted_obj, manual_rudder_records[side])
                        else:
                            set_pivot(extracted_obj, extracted_runtime_role)
                        extracted_obj["SOURCE_FACE_EXTRACTION"] = True
                        closure_count = int(extracted_metadata.get((extracted_role, side), {}).get("closureFaceCount", 0)) if extracted_role == "RUDDER" else 0
                        extracted_obj["SOURCE_FACE_COUNT"] = len(extracted) - closure_count
                        extracted_obj["SYNTHETIC_CLOSURE_FACE_COUNT"] = closure_count
                        extracted_obj["SOURCE_COMPONENT"] = component_index
                        extracted_obj["source_component"] = component_index
                        lod0.append(extracted_obj)
                        inventory_items.append({"object": extracted_obj.name, "source_object": source_name, "source_component": component_index, "role": extracted_runtime_role, "triangles": sum(len(face) - 2 for face in extracted), "source_face_extraction": True})
                        rudder_provenance = {"sourceObject": source_name, "sourceComponent": component_index, "sourceFaceFingerprint": face_fingerprint(extracted), "sourceBoundaryEdgeFingerprint": boundary_edge_fingerprint(extracted), "productionObject": extracted_obj.name, "movementType": "HINGE_ROTATION", "pivot": list(extracted_obj.location), "axis": extracted_obj.get("HINGE_AXIS", "UNKNOWN"), "neutralTransform": {"location": list(extracted_obj.location), "rotation_euler": list(extracted_obj.rotation_euler), "scale": list(extracted_obj.scale)}}
                        if extracted_role == "RUDDER" and side in manual_rudder_records:
                            rudder_provenance.update({"manualAnchor": True, "manualAnchorCoordinates": manual_rudder_records[side]["manualCoordinates"], "resolvedAnchorIndices": manual_rudder_records[side]["resolvedIndices"], "sourceBoundaryPaths": manual_rudder_records[side]["sourceBoundaryPaths"], "orderedBoundaryLoop": manual_rudder_records[side]["orderedBoundaryLoop"], "boundaryEdgeIds": manual_rudder_records[side]["boundaryEdgeIds"], "boundaryFingerprint": manual_rudder_records[side]["boundaryFingerprint"], "movableFaceIndices": manual_rudder_records[side]["movableFaceIndices"], "movableFaceFingerprint": manual_rudder_records[side]["movableFaceFingerprint"], "fixedStabilizerFaceFingerprint": manual_rudder_records[side]["fixedStabilizerFaceFingerprint"], "surfaceAreaM2": manual_rudder_records[side]["surfaceAreaM2"], "bounds": manual_rudder_records[side]["bounds"], "hinge": manual_rudder_records[side]["hinge"], "closureFaceCount": manual_rudder_records[side]["closureFaceCount"], "closureFaceFingerprint": manual_rudder_records[side]["closureFaceFingerprint"]})
                        provenance.append(rudder_provenance)
                    faces = remaining_faces
                    if not faces:
                        continue
                if cover_faces:
                    for (side, cover_index), selected in sorted(cover_faces.items(), key=lambda item: (item[0][0], item[0][1])):
                        selected_indices = list(dict.fromkeys(index for face in selected for index in face))
                        cover_name = f"SM_Antey_P700_Cover_{side}_{cover_index:02d}"
                        cover_role = f"P700_COVER_{side.upper()}_{cover_index:02d}"
                        cover = create_component(cover_name, mesh, selected_indices, selected, source_name, cover_role, hull_mat, prop_mat, articulated=True)
                        set_hinge_pivot(cover, cover_role, side, manual_cover_metadata.get((side, cover_index)) if manual_p700 else None)
                        cover["SOURCE_FACE_EXTRACTION"] = True
                        cover["SOURCE_COMPONENT"] = component_index
                        cover["source_component"] = component_index
                        cover["COVER_GROUP"] = cover_index
                        pair_indices = _p700_pair_indices(cover_index)
                        cover["TUBE_A"] = f"P700_{side}_{pair_indices[0]:02d}"
                        cover["TUBE_B"] = f"P700_{side}_{pair_indices[1]:02d}"
                        cover["NOMINAL_LAUNCHER_ANGLE_DEG"] = 40.0
                        cover["P700_COVER_STATE_CHANNEL"] = f"P700CoverState_{side}_{cover_index:02d}"
                        if manual_p700:
                            record = manual_cover_metadata.get((side, cover_index))
                            if record is None:
                                raise RuntimeError(f"Missing manual P700 provenance for {side} cover {cover_index:02d}")
                            cover["MANUAL_ANCHOR"] = bool(side == "Port" and cover_index == 1)
                            cover["MANUAL_SOURCE_BOUNDARY"] = True
                            cover["MANUAL_SOURCE_BOUNDARY_FINGERPRINT"] = record["boundary_fingerprint"]
                            cover["MANUAL_SOURCE_FACE_FINGERPRINT"] = record["face_fingerprint"]
                            cover["MANUAL_SOURCE_BOUNDARY_EDGE_IDS"] = json.dumps(record["boundary_edges"])
                            cover["MANUAL_SOURCE_BOUNDARY_VERTEX_LOOP"] = json.dumps(record["boundary_vertices"])
                            cover["MANUAL_SOURCE_FACE_INDICES"] = json.dumps(record["face_indices"])
                            cover["MANUAL_CORNER_COORDINATES"] = json.dumps({name: value["coord"] for name, value in record["corners"].items()})
                            cover["MANUAL_TRACE_EDGE_IDS"] = json.dumps(record["trace"]["edges"])
                            cover["OPENING_DIRECTION"] = "+LOCAL_X" if side == "Port" else "-LOCAL_X"
                            cover["CLOSED_ANGLE_DEG"] = 0.0
                            cover["FULL_OPEN_ANGLE_DEG"] = 0.0
                            cover["HISTORICAL_FULL_OPEN_ANGLE"] = "UNKNOWN_PUBLIC_DATA"
                        lod0.append(cover)
                        triangles = sum(len(face) - 2 for face in selected)
                        inventory_items.append({"object": cover.name, "source_object": source_name, "source_component": component_index, "role": cover_role, "triangles": triangles, "source_face_extraction": True})
                        cover_provenance = {"sourceObject": source_name, "sourceComponent": component_index, "sourceFaceFingerprint": face_fingerprint(selected), "sourceBoundaryEdgeFingerprint": boundary_edge_fingerprint(selected), "productionObject": cover.name, "movementType": "HINGE_ROTATION", "pivot": list(cover.location), "axis": "LOCAL_X", "neutralTransform": {"location": list(cover.location), "rotation_euler": list(cover.rotation_euler), "scale": list(cover.scale), "state": "CLOSED"}, "coverGroup": cover_index, "tubeA": cover["TUBE_A"], "tubeB": cover["TUBE_B"], "numberingDirection": "AFT_TO_FORWARD", "placementBasis": "SOURCE_DERIVED_COVER_LOCAL_FRAME"}
                        if manual_p700:
                            record = manual_cover_metadata[(side, cover_index)]
                            derived_manual_vertices = {name: int(value["vertex"]) for name, value in record["corners"].items()}
                            supplied_manual_vertices = {"FORWARD_INBOARD": 24738, "AFT_INBOARD": 24444, "FORWARD_OUTBOARD": 18889, "AFT_OUTBOARD": 24610}
                            cover_provenance.update({"manualAnchor": bool(side == "Port" and cover_index == 1), "manualSourceBoundary": True, "manualVertices": supplied_manual_vertices if side == "Port" and cover_index == 1 else derived_manual_vertices, "sourceDerivedManualVertices": derived_manual_vertices, "manualCoordinates": {name: value["coord"] for name, value in record["corners"].items()}, "boundaryVertexLoop": record["boundary_vertices"], "boundaryEdgeIndices": record["boundary_edges"], "boundaryFingerprint": record["boundary_fingerprint"], "faceIndices": record["face_indices"], "faceFingerprint": record["face_fingerprint"], "surfaceAreaM2": record["surface_area_m2"], "openingDirection": cover["OPENING_DIRECTION"], "safeFullOpenAngleDeg": 0.0})
                            if side == "Port" and cover_index == 1:
                                cover_provenance["suppliedManualVertices"] = {"FORWARD_INBOARD": 24738, "AFT_INBOARD": 24444, "FORWARD_OUTBOARD": 18889, "AFT_OUTBOARD": 24610}
                                cover_provenance["suppliedManualCoordinates"] = {"FORWARD_INBOARD": [51.620544434, 3.250949383, 4.935768127], "AFT_INBOARD": [44.585681915, 3.225973606, 4.932476997], "FORWARD_OUTBOARD": [51.612987518, 6.745433331, 3.182579994], "AFT_OUTBOARD": [44.612236023, 6.793663025, 3.255691528]}
                                cover_provenance["bankTerminalVertices"] = {"BANK_AFT_INBOARD": 24640, "BANK_AFT_OUTBOARD": 24772}
                        provenance.append(cover_provenance)
                if door_faces:
                    for door_name, selected in sorted(door_faces.items()):
                        selected_indices = list(dict.fromkeys(index for face in selected for index in face))
                        door = create_component(f"SM_Antey_{door_name}", mesh, selected_indices, selected, source_name, "TORPEDO_DOOR", hull_mat, prop_mat, articulated=True)
                        door["SOURCE_FACE_EXTRACTION"] = True
                        door["SOURCE_COMPONENT"] = component_index
                        door["source_component"] = component_index
                        door["MOVEMENT_TYPE"] = "HINGE_ROTATION"
                        door["TORPEDO_DOOR_STATE_CHANNEL"] = door_name.replace("TorpedoDoor", "TorpedoDoorState_")
                        lod0.append(door)
                        triangles = sum(len(face) - 2 for face in selected)
                        inventory_items.append({"object": door.name, "source_object": source_name, "source_component": component_index, "role": "TORPEDO_DOOR", "triangles": triangles, "source_face_extraction": True})
                        provenance.append({"sourceObject": source_name, "sourceComponent": component_index, "sourceFaceFingerprint": face_fingerprint(selected), "sourceBoundaryEdgeFingerprint": boundary_edge_fingerprint(selected), "productionObject": door.name, "movementType": "HINGE_ROTATION", "pivot": list(door.location), "axis": "LOCAL_X", "neutralTransform": {"location": list(door.location), "rotation_euler": list(door.rotation_euler), "scale": list(door.scale)}})
                faces = remaining_faces
                if role.startswith("PROPELLER_"):
                    side = role.rsplit("_", 1)[-1].title()
                    prop_seen[role] += 1
                    name = f"SM_Propeller_{side}" if prop_seen[role] == 1 else f"SM_Propeller_{side}_Blade_{prop_seen[role]:02d}"
                # Preserve duplicate propeller blades as separate source-derived
                # components; grouping is represented by the side/role metadata.
                obj = create_component(name, mesh, indices, faces, source_name, role, hull_mat, prop_mat, articulated=articulated)
                obj["source_component"] = component_index
                if role == "SAIL_DEVICE":
                    apply_explicit_sail_device_contract(obj, component_index)
                set_pivot(obj, role)
                if role.startswith("PROPELLER_"):
                    prop_objects[role].append(obj)
                lod0.append(obj)
                inventory_items.append({"object": obj.name, "source_object": source_name, "source_component": component_index, "role": role, "triangles": sum(len(face) - 2 for face in faces)})
                provenance.append({"sourceObject": source_name, "sourceComponent": component_index, "sourceFaceFingerprint": face_fingerprint(faces), "sourceBoundaryEdgeFingerprint": boundary_edge_fingerprint(faces), "productionObject": obj.name, "movementType": "STATIC" if role not in {"SAIL_DEVICE"} else obj.get("MOTION", "UNKNOWN"), "pivot": list(obj.location), "axis": obj.get("HINGE_AXIS", obj.get("AXIS", "NONE")), "neutralTransform": {"location": list(obj.location), "rotation_euler": list(obj.rotation_euler), "scale": list(obj.scale)}})
    finally:
        for _, mesh in evaluated:
            if mesh.users == 0:
                bpy.data.meshes.remove(mesh)
    # All seven source blade components on one shaft share one physical pivot.
    # Re-rooting preserves every world-space vertex while making future
    # LOCAL_X propeller rotation deterministic.
    for role, objects in prop_objects.items():
        world_points = [obj.matrix_world @ vertex.co for obj in objects for vertex in obj.data.vertices]
        shaft_centre = sum(world_points, Vector()) / len(world_points)
        for obj in objects:
            old_location = obj.location.copy()
            obj.data.transform(Matrix.Translation(old_location - shaft_centre))
            obj.location = shaft_centre
            obj["SHAFT_PIVOT_SHARED"] = True
        if articulated:
            side = role.rsplit("_", 1)[-1].title()
            for obj in objects:
                if obj.name == f"SM_Propeller_{side}":
                    obj.name = f"SM_Propeller_{side}_Hub"
            root = bpy.data.objects.new(f"SM_Propeller_{side}", None)
            bpy.context.scene.collection.objects.link(root)
            root.location = shaft_centre
            root.empty_display_type = "PLAIN_AXES"
            root.empty_display_size = 1.0
            root["runtime_export"] = True
            root["PROP_ASSEMBLY"] = True
            root["ROTATION_AXIS"] = "LOCAL_X"
            root["SHAFT_PIVOT_SHARED"] = True
            root["SOURCE_BLADE_COUNT"] = len(objects)
            for blade_index, obj in enumerate(objects, 1):
                world_matrix = obj.matrix_world.copy()
                obj.parent = root
                obj.matrix_world = world_matrix
    if mechanical:
        # Bow planes have independent deployment and control channels.  The
        # root stays at the source hinge; a future authoring clip may move it
        # through the source recess before applying angle about LOCAL_Y.
        for side in ("Port", "Starboard"):
            plane = bpy.data.objects.get(f"SM_Antey_LOD0_BowPlane_{side}")
            if plane is None:
                continue
            root = bpy.data.objects.new(f"SM_Antey_BowPlaneDeploymentRoot_{side}", None)
            bpy.context.scene.collection.objects.link(root)
            world_matrix = plane.matrix_world.copy()
            root.location = plane.location
            root.empty_display_type = "PLAIN_AXES"; root.empty_display_size = 0.6
            plane.parent = root; plane.matrix_world = world_matrix
            root["DEPLOYMENT_STATES"] = ["STOWED", "DEPLOYING", "DEPLOYED"]
            root["CONTROL_CHANNEL"] = "BowPlaneDeployment"
            root["SOURCE_RECESS_PATH"] = "SOURCE_BOUNDARY_REQUIRES_FUTURE_MECHANISM_AUTHORING"
            root["STOWED_TRANSLATION"] = [0.0, 0.0, 0.0]
            root["SIMULATION_OWNS_STATE"] = True
    marker_inventory = add_authoring_objects(mechanical=mechanical)
    if mechanical:
        for marker in marker_inventory:
            if marker.get("role") == "P700_LAUNCH_POSITION":
                provenance.append({"productionObject": marker.get("missileObject", marker.get("object")), **{key: value for key, value in marker.items() if key not in {"role", "object"}}})
    p700_animation_records: dict[str, object] = {}
    if mechanical:
        p700_animation_records = add_p700_cover_animation()
        # Keep a provenance record for every logical torpedo door channel even
        # when the source does not expose a separately named door component.
        for index, marker in enumerate([m for m in marker_inventory if m.get("role") == "TORPEDO_TUBE"], 1):
            provenance.append({"sourceObject": "Bridge", "sourceComponent": 0, "sourceFaceFingerprint": "SOURCE_DOOR_FACE_NOT_SEPARATELY_IDENTIFIABLE", "sourceBoundaryEdgeFingerprint": "SOURCE_DOOR_BOUNDARY_NOT_SEPARATELY_IDENTIFIABLE", "productionObject": marker.get("logicalDoor"), "logicalMarker": marker.get("object"), "movementType": "HINGE_ROTATION_TBD", "pivot": list(bpy.data.objects[marker["object"]].location), "axis": "LOCAL_X_TBD", "neutralTransform": {"state": "CLOSED", "logicalMarker": marker.get("object")}, "source_note": "logical authoring channel; visible door seam not separable in Source", "caliberConfidence": marker.get("caliberConfidence"), "frontMap": {"centerY": marker.get("centerY"), "centerZ": marker.get("centerZ"), "diameterM": marker.get("diameter_m")}})
    add_physics_proxies()
    if mechanical:
        add_compartment_and_mass_contract()
    audit_sail_device_deployment(lod0)
    make_lods(lod0)
    restore_articulated_lod_transforms(lod0)
    if mechanical:
        scene_contract = {
            "states": ["LOADED", "READY", "COVER_UNLOCKING", "COVER_OPENING", "COVER_OPEN", "LAUNCH_PERMISSION", "BOOSTER_IGNITION", "UNDERWATER_EXIT", "MISSILE_CLEAR_OF_TUBE", "MISSILE_CLEAR_OF_COVER", "COVER_CLOSING", "COVER_CLOSED", "LAUNCHER_EMPTY"],
            "missile_states": ["STOWED_IN_LAUNCHER", "UNDERWATER_BOOST", "WATER_BREACH", "POST_BREACH_TRANSITION", "SURFACES_DEPLOYING", "POWERED_FLIGHT"],
            "surface_interlock": "P700 surfaces remain STOWED_IN_LAUNCHER through UNDERWATER_EXIT and deploy only after MISSILE_CLEAR_OF_COVER",
            "pair_cover_interlock": "CoverGroup remains open until Tube_A/Tube_B envelopes and booster transition are clear"
        }
        scene_contract["p700_animation_contract"] = p700_animation_records
        scene_contract["manual_rudder_contract"] = {side: {"hinge": record["hinge"], "qa_angles_deg": [-20, -15, 0, 15, 20], "neutral_exact": True, "fixed_stabilizer_rotation": False} for side, record in manual_rudder_records.items()}
        scene_contract["stern_planes_contract"] = {"state": "FROZEN", "qa_angles_deg": [-15, 0, 15], "geometry_mutation": False}
        scene_contract["torpedo_door_contract"] = {"system": "Antey TorpedoLauncherSystem", "states": ["CLOSED", "OPENING", "OPEN", "CLOSING"], "layout": "COMPACT_UPPER_BOW_4x533_PLUS_2x650", "source_geometry": "NO_SEPARATE_DOOR_LOOP_IN_SOURCE", "hinge": "TBD_UNTIL_SOURCE_SEAM"}
        bpy.context.scene["p700_launch_state_contract"] = scene_contract
        bpy.context.scene["p700_missile_contract"] = {"canonical_asset": "Content/Weapons/P700/P700_Granit_GameReady.blend", "state": "STOWED_IN_LAUNCHER", "align": "missile longitudinal axis follows each launcher axis", "geometry_mutation": False, "launcher_exit": "underwater", "surface_deployment": "post MISSILE_CLEAR_OF_COVER"}
        bpy.context.scene["external_equipment_modes"] = {"DEEP_SUBMERGED": "all retractables STOWED; P700 covers CLOSED; torpedo doors CLOSED", "PERISCOPE_DEPTH": "selected periscope/sensor only", "COMMAND_DATA_WINDOW": "mission link mast deployment authoring contract", "SURFACED": "deploy only task-required devices", "EMERGENCY_DIVE": "retract all and wait for interlocks"}
        bpy.context.scene["control_surface_physics_contract"] = {"BowPlaneDeployment": "simulation-owned deployment state", "BowPlaneAngle": "simulation-owned angle; vertical force/pitch moment", "SternPlaneAngle": "simulation-owned angle; vertical force/pitch moment", "RudderAngle": "simulation-owned angle; lateral force/yaw moment", "visual_mesh": "presentation only"}
        bpy.context.scene["physics_ownership_contract"] = {"simulation_owns": ["mass", "DynamicCOM", "buoyancy", "COB", "control_surface_state", "propeller_RPM", "compartment_state", "flooding", "P700CoverState", "TorpedoDoorState", "SailDeviceState"], "blender_provides": ["geometry", "pivots", "source_volumes", "markers", "provenance", "QA"]}
        provenance_path = (inventory.parent if inventory else output.parent) / "antey_articulation_provenance.json"
        provenance_path.parent.mkdir(parents=True, exist_ok=True)
        provenance_path.write_text(json.dumps(jsonable({"source": str(source), "candidate": str(output), "records": provenance, "markerRecords": marker_inventory, "method": "manual source edge paths and connected face flood-fill" if manual_p700 else "source mesh face and boundary-edge fingerprints; connected topology and normal/curvature seam constrained masks; material/UV secondary", "manualAnchorPass": manual_p700}), indent=2), encoding="utf-8")
        rudder_boundary_payload = {"source": str(source), "candidate": str(output), "toleranceM": MANUAL_RUDDER_TOLERANCE, "rudders": manual_rudder_records}
        (provenance_path.parent / "manual_rudder_boundary.json").write_text(json.dumps(rudder_boundary_payload, indent=2), encoding="utf-8")
        (provenance_path.parent / "antey_physics_contract.json").write_text(json.dumps(jsonable({"compartments": bpy.context.scene.get("antey_compartments", []), "damageRepair": bpy.context.scene.get("damage_repair_contract", {}), "mass": bpy.context.scene.get("antey_mass_distribution", {}), "buoyancy": bpy.context.scene.get("antey_buoyancy_contract", {}), "ownership": bpy.context.scene.get("physics_ownership_contract", {})}), indent=2), encoding="utf-8")
        (provenance_path.parent / "p700_cover_mechanism.json").write_text(json.dumps(jsonable(p700_animation_records), indent=2), encoding="utf-8")
        if manual_p700 and manual_derivation is not None:
            derivation_path = provenance_path.parent / "manual_p700_source_derivation.json"
            derivation_payload = {"source_object": "Bridge", "component": 0, "vertex_count": manual_bridge_vertex_count, **manual_derivation}
            derivation_path.write_text(json.dumps(derivation_payload, indent=2), encoding="utf-8")
            (provenance_path.parent / "mechanical_antey_articulation_provenance.json").write_text(json.dumps({"note": "Preserved prior mechanical candidate provenance; manual pass is authoritative for the new candidate.", "candidate": "Antey_SourceFirst_Mechanical_candidate.blend"}, indent=2), encoding="utf-8")
        constraints_path = provenance_path.parent / "public_reference_constraints.json"
        constraints_path.write_text(json.dumps({"type": "PUBLIC_REFERENCE_CONSTRAINTS", "sha_scope": "authoring only", "references": ["https://de.wikipedia.org/wiki/Projekt_949", "https://www.globalsecurity.org/military/world/russia/949-specs.htm", "https://ru.wikipedia.org/wiki/%D0%9F%D0%BE%D0%B4%D0%B2%D0%BE%D0%B4%D0%BD%D1%8B%D0%B5_%D0%BB%D0%BE%D0%B4%D0%BA%D0%B8_%D0%BF%D1%80%D0%BE%D0%B5%D0%BA%D1%82%D0%B0_949%D0%90_%C2%AB%D0%90%D0%BD%D1%82%D0%B5%D0%B9%C2%BB"], "facts": {"shafts": 2, "fixed_pitch_propellers": 2, "blades_per_propeller": 7, "p700_containers": 24, "p700_per_side": 12, "nominal_launcher_angle_deg": 40.0, "covers_per_side": 6, "containers_per_cover": 2, "retractable_sail_devices": "multiple; system identity configuration-dependent", "historical_max_rpm": "UNKNOWN_PUBLIC_DATA"}, "limits": {"deployment_depth": "TBD", "deployment_speed": "TBD", "propeller_curves": "K_T/K_Q future calibration"}}, indent=2), encoding="utf-8")
    if inventory:
        inventory.parent.mkdir(parents=True, exist_ok=True)
        inventory.write_text(json.dumps({"source": str(source), "normalization": {"length_m": PRODUCTION_LENGTH, "beam_m": PRODUCTION_BEAM, "objects": inventory_items}, "total_lod0_objects": len(lod0), "total_lod0_triangles": sum(item["triangles"] for item in inventory_items)}, indent=2), encoding="utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    print(f"ANTEY_SOURCE_FIRST_BUILD_OK {output} articulated={articulated} mechanical={mechanical} manual_p700={manual_p700}")


if __name__ == "__main__":
    options = args()
    if options.one_pitch_shift_from:
        realign_p700_one_pitch_candidate(options.one_pitch_shift_from.resolve(), options.output.resolve())
    elif options.realign_p700_from:
        realign_p700_row_candidate(options.realign_p700_from.resolve(), options.output.resolve())
    elif options.correct_rudder_mask_from:
        if not options.rudder_mask:
            raise RuntimeError("--correct-rudder-mask-from requires --rudder-mask")
        correct_rudder_mask_candidate(
            options.correct_rudder_mask_from.resolve(),
            options.output.resolve(),
            options.rudder_mask.resolve(),
        )
    else:
        build(options.source.resolve(), options.output.resolve(), options.inventory.resolve() if options.inventory else None, articulated=options.articulated, mechanical=options.mechanical, manual_p700=options.manual_p700)
