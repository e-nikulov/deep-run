"""Clean source-guided Antey LOD0 reconstruction.

Only the two largest longitudinal exterior shells and the source sail/fairing
are sampled.  Their broken legacy topology is discarded by voxel retopology;
all production meshes are new datablocks with identity transforms.  The script
saves a temporary BLEND only and deliberately does not create LOD1-LOD3.
"""

from __future__ import annotations

import argparse
import math
import statistics
import sys
from collections import deque
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


SOURCE_LENGTH = 9.775832176208496
SOURCE_BEAM = 1.7063244581222534
SOURCE_CENTER_X = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SOURCE_CENTER_Y = (-4.875378131866455 + 4.900454044342041) * 0.5
SOURCE_HULL_CENTER_Z = (-0.4847494661808014 + 0.5867570638656616) * 0.5
LENGTH_SCALE = 154.0 / SOURCE_LENGTH
BEAM_SCALE = 18.2 / SOURCE_BEAM


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def canonical_matrix() -> Matrix:
    # source +Y is bow; source +X maps to canonical +Y port; +Z remains up.
    return Matrix(
        (
            (0.0, LENGTH_SCALE, 0.0, -SOURCE_CENTER_Y * LENGTH_SCALE),
            (BEAM_SCALE, 0.0, 0.0, -SOURCE_CENTER_X * BEAM_SCALE),
            (0.0, 0.0, BEAM_SCALE, -SOURCE_HULL_CENTER_Z * BEAM_SCALE),
            (0.0, 0.0, 0.0, 1.0),
        )
    )


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)


def new_collection(name: str) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def append_source(source: Path) -> dict[str, bpy.types.Object]:
    with bpy.data.libraries.load(str(source.resolve()), link=False) as (available, loaded):
        loaded.objects = [name for name in available.objects if name in {"Bridge", "Hull"}]
    result = {obj.name: obj for obj in loaded.objects if obj is not None}
    if set(result) != {"Bridge", "Hull"}:
        raise RuntimeError(f"Unexpected Antey source objects: {sorted(result)}")
    return result


def evaluated_mesh(obj: bpy.types.Object) -> bpy.types.Mesh:
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    return bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)


def components(mesh: bpy.types.Mesh) -> list[set[int]]:
    adjacency = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(mesh.vertices)))
    result = []
    while unseen:
        start = unseen.pop()
        current = {start}
        queue = deque([start])
        while queue:
            vertex = queue.popleft()
            for neighbour in adjacency[vertex]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    current.add(neighbour)
                    queue.append(neighbour)
        result.append(current)
    return result


def component_info(mesh: bpy.types.Mesh, obj: bpy.types.Object) -> list[dict]:
    result = []
    for index, vertices in enumerate(components(mesh)):
        points = [obj.matrix_world @ mesh.vertices[v].co for v in vertices]
        minimum = Vector(tuple(min(point[axis] for point in points) for axis in range(3)))
        maximum = Vector(tuple(max(point[axis] for point in points) for axis in range(3)))
        result.append({"index": index, "vertices": vertices, "minimum": minimum, "maximum": maximum, "dimensions": maximum - minimum})
    return result


def extract_geometry(
    obj: bpy.types.Object,
    selector,
    transform: Matrix,
    use_evaluated: bool = True,
) -> tuple[list[tuple[float, float, float]], list[list[int]]]:
    mesh = evaluated_mesh(obj) if use_evaluated else obj.data.copy()
    infos = component_info(mesh, obj)
    selected = set().union(*(item["vertices"] for item in infos if selector(item)))
    if not selected:
        summary = [tuple(round(value, 6) for value in item["dimensions"]) for item in infos]
        raise RuntimeError(f"No source components selected from {obj.name}; dimensions={summary}; matrix={obj.matrix_world}")
    used = sorted({vertex for polygon in mesh.polygons if all(v in selected for v in polygon.vertices) for vertex in polygon.vertices})
    remap = {old: new for new, old in enumerate(used)}
    vertices = [tuple(transform @ (obj.matrix_world @ mesh.vertices[index].co)) for index in used]
    faces = [[remap[index] for index in polygon.vertices] for polygon in mesh.polygons if all(v in selected for v in polygon.vertices)]
    bpy.data.meshes.remove(mesh)
    return vertices, faces


def mirror_geometry_y(geometry: tuple[list, list]) -> tuple[list, list]:
    vertices, faces = geometry
    mirrored_vertices = [(x, -y, z) for x, y, z in vertices]
    mirrored_faces = [list(reversed(face)) for face in faces]
    return mirrored_vertices, mirrored_faces


def combine(parts: list[tuple[list[tuple[float, float, float]], list[list[int]]]]) -> tuple[list, list]:
    vertices = []
    faces = []
    for part_vertices, part_faces in parts:
        offset = len(vertices)
        vertices.extend(part_vertices)
        faces.extend([[index + offset for index in face] for face in part_faces])
    return vertices, faces


def mesh_object(name: str, geometry: tuple[list, list], collection: bpy.types.Collection) -> bpy.types.Object:
    vertices, faces = geometry
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def angular_distance(a: float, b: float) -> float:
    return abs((a - b + math.pi) % (2.0 * math.pi) - math.pi)


def quantile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round((len(ordered) - 1) * fraction)))]


def source_derived_cage(
    name: str,
    points: list[tuple[float, float, float]],
    collection: bpy.types.Collection,
    station_count: int,
    angular_count: int,
    fixed_centre: tuple[float, float] | None,
) -> bpy.types.Object:
    """Create clean independent topology from source cross-section envelopes."""
    cloud = [Vector(point) for point in points]
    minimum_x = min(point.x for point in cloud)
    maximum_x = max(point.x for point in cloud)
    inset = min(0.45, (maximum_x - minimum_x) * 0.01)
    station_xs = [
        minimum_x + inset + (maximum_x - minimum_x - inset * 2.0) * index / (station_count - 1)
        for index in range(station_count)
    ]
    window = max((maximum_x - minimum_x) / station_count * 1.35, 0.40)
    rings: list[list[tuple[float, float, float]]] = []
    raw_radii: list[list[float]] = []
    centres: list[tuple[float, float]] = []
    for x in station_xs:
        local = [point for point in cloud if abs(point.x - x) <= window]
        if len(local) < angular_count:
            local = sorted(cloud, key=lambda point: abs(point.x - x))[: angular_count * 5]
        if fixed_centre is None:
            centre_y = statistics.median(point.y for point in local)
            centre_z = (quantile([point.z for point in local], 0.05) + quantile([point.z for point in local], 0.95)) * 0.5
        else:
            centre_y, centre_z = fixed_centre
        centres.append((centre_y, centre_z))
        samples = []
        for point in local:
            dy = point.y - centre_y
            dz = point.z - centre_z
            samples.append((math.atan2(dz, dy), math.hypot(dy, dz)))
        radii = []
        base_tolerance = 2.0 * math.pi / angular_count * 2.2
        for angular_index in range(angular_count):
            angle = 2.0 * math.pi * angular_index / angular_count
            candidates = [radius for source_angle, radius in samples if angular_distance(source_angle, angle) <= base_tolerance]
            if not candidates:
                candidates = [min(samples, key=lambda item: angular_distance(item[0], angle))[1]]
            # Outer-envelope quantile rejects internal legacy faces without
            # turning a single panel spike into the body radius.
            radii.append(quantile(candidates, 0.82))
        for _ in range(2):
            radii = [
                radii[(index - 1) % angular_count] * 0.20
                + radii[index] * 0.60
                + radii[(index + 1) % angular_count] * 0.20
                for index in range(angular_count)
            ]
        raw_radii.append(radii)

    # Longitudinal smoothing preserves station character while removing mesh noise.
    smoothed = [row[:] for row in raw_radii]
    for station in range(1, station_count - 1):
        for angular_index in range(angular_count):
            smoothed[station][angular_index] = (
                raw_radii[station - 1][angular_index] * 0.18
                + raw_radii[station][angular_index] * 0.64
                + raw_radii[station + 1][angular_index] * 0.18
            )
    for station, x in enumerate(station_xs):
        centre_y, centre_z = centres[station]
        ring = []
        for angular_index in range(angular_count):
            angle = 2.0 * math.pi * angular_index / angular_count
            radius = smoothed[station][angular_index]
            ring.append((x, centre_y + radius * math.cos(angle), centre_z + radius * math.sin(angle)))
        rings.append(ring)

    vertices = [vertex for ring in rings for vertex in ring]
    faces = []
    for station in range(station_count - 1):
        current = station * angular_count
        following = (station + 1) * angular_count
        for angular_index in range(angular_count):
            nxt = (angular_index + 1) % angular_count
            faces.append((current + angular_index, current + nxt, following + nxt, following + angular_index))
    stern_index = len(vertices)
    vertices.append((minimum_x, centres[0][0], centres[0][1]))
    bow_index = len(vertices)
    vertices.append((maximum_x, centres[-1][0], centres[-1][1]))
    for angular_index in range(angular_count):
        nxt = (angular_index + 1) % angular_count
        faces.append((stern_index, nxt, angular_index))
        last = (station_count - 1) * angular_count
        faces.append((bow_index, last + angular_index, last + nxt))
    obj = mesh_object(name, (vertices, faces), collection)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    obj.data.update()
    return obj


def interpolate_profile(profile: list[tuple[float, float]], x: float) -> float:
    if x <= profile[0][0]:
        return profile[0][1]
    if x >= profile[-1][0]:
        return profile[-1][1]

    # Monotone piecewise cubic Hermite interpolation.  The earlier per-segment
    # smoothstep forced the slope to zero at every profile key and produced
    # visible circumferential waves.  Shared PCHIP-style tangents keep the
    # longitudinal derivative continuous without overshooting the measured
    # half-beam/height envelope.
    xs = [point[0] for point in profile]
    values = [point[1] for point in profile]
    intervals = [xs[index + 1] - xs[index] for index in range(len(xs) - 1)]
    secants = [(values[index + 1] - values[index]) / intervals[index] for index in range(len(intervals))]
    tangents = [secants[0]]
    for index in range(1, len(values) - 1):
        left = secants[index - 1]
        right = secants[index]
        if left == 0.0 or right == 0.0 or left * right < 0.0:
            tangents.append(0.0)
        else:
            left_weight = 2.0 * intervals[index] + intervals[index - 1]
            right_weight = intervals[index] + 2.0 * intervals[index - 1]
            tangents.append((left_weight + right_weight) / (left_weight / left + right_weight / right))
    tangents.append(secants[-1])

    for index, ((x0, value0), (x1, value1)) in enumerate(zip(profile, profile[1:])):
        if x0 <= x <= x1:
            interval = x1 - x0
            t = (x - x0) / interval
            t2 = t * t
            t3 = t2 * t
            h00 = 2.0 * t3 - 3.0 * t2 + 1.0
            h10 = t3 - 2.0 * t2 + t
            h01 = -2.0 * t3 + 3.0 * t2
            h11 = t3 - t2
            return h00 * value0 + h10 * interval * tangents[index] + h01 * value1 + h11 * interval * tangents[index + 1]
    raise AssertionError("Profile interpolation fell through")


def antey_hull_cage(collection: bpy.types.Collection) -> bpy.types.Object:
    # Measurements are normalized from the canonical source silhouettes.  The
    # non-elliptic section and independent upper/lower profiles preserve the
    # broad 949A casing, shoulders, full belly, rounded bow and tapered stern.
    beam_profile = [
        (-77.0, 0.20), (-73.0, 1.55), (-69.0, 3.05), (-64.0, 4.60),
        (-58.0, 5.80), (-50.0, 6.65), (-43.0, 7.35), (-37.0, 8.45),
        (-31.0, 9.10), (32.0, 9.10), (39.0, 8.65), (47.0, 7.65),
        (54.0, 7.05), (58.0, 6.70),
        (66.0, 5.75), (70.0, 5.40), (73.0, 5.00), (75.0, 4.25),
        (76.2, 2.80), (76.8, 1.20), (77.0, 0.20),
    ]
    upper_profile = [
        (-77.0, 0.15), (-70.0, 1.75), (-62.0, 3.10), (-52.0, 4.15),
        (-42.0, 4.85), (-32.0, 5.35), (-20.0, 5.65), (32.0, 5.65),
        (48.0, 5.50), (60.0, 4.75), (69.0, 4.35), (73.0, 4.05),
        (75.2, 3.35), (76.4, 2.15), (77.0, 0.15),
    ]
    lower_profile = [
        (-77.0, 0.15), (-70.0, 1.65), (-62.0, 3.05), (-52.0, 4.20),
        (-42.0, 5.00), (-30.0, 5.55), (-18.0, 5.72), (40.0, 5.72),
        (54.0, 5.35), (64.0, 4.45), (70.0, 4.05), (73.0, 3.75),
        (75.2, 3.15), (76.4, 2.00), (77.0, 0.15),
    ]
    station_count = 171
    angular_count = 192
    # Carry the regular cage close to both tips so the tiny final caps do not
    # introduce a visible circumferential seam on the otherwise smooth skin.
    station_xs = [-76.90 + 153.80 * station / (station_count - 1) for station in range(station_count)]
    # A final low-pass over the uniformly sampled profiles removes residual
    # curvature kinks that can still read as rings under cavity lighting while
    # keeping the source-measured envelope and the cap endpoints fixed.
    beam_values = [interpolate_profile(beam_profile, x) for x in station_xs]
    upper_values = [interpolate_profile(upper_profile, x) for x in station_xs]
    lower_values = [interpolate_profile(lower_profile, x) for x in station_xs]
    for values in (beam_values, upper_values, lower_values):
        for _ in range(10):
            previous = values[:]
            for index in range(1, len(values) - 1):
                values[index] = previous[index - 1] * 0.20 + previous[index] * 0.60 + previous[index + 1] * 0.20

    rings = []
    exponent = 2.05
    for station, x in enumerate(station_xs):
        target_beam = beam_values[station]
        upper = upper_values[station]
        lower = lower_values[station]
        raw = []
        for angular_index in range(angular_count):
            angle = 2.0 * math.pi * angular_index / angular_count
            cosine = math.cos(angle)
            sine = math.sin(angle)
            y = target_beam * math.copysign(abs(cosine) ** (2.0 / exponent), cosine)
            height = upper if sine >= 0.0 else lower
            z = height * math.copysign(abs(sine) ** (2.0 / exponent), sine)
            raw.append((y, z))
        rings.append([(x, y, z) for y, z in raw])

    vertices = [vertex for ring in rings for vertex in ring]
    faces = []
    for station in range(station_count - 1):
        current = station * angular_count
        following = (station + 1) * angular_count
        for angular_index in range(angular_count):
            nxt = (angular_index + 1) % angular_count
            faces.append((current + angular_index, current + nxt, following + nxt, following + angular_index))
    stern = len(vertices)
    vertices.append((-77.0, 0.0, 0.0))
    bow = len(vertices)
    vertices.append((77.0, 0.0, 0.0))
    for angular_index in range(angular_count):
        nxt = (angular_index + 1) % angular_count
        faces.append((stern, nxt, angular_index))
        last = (station_count - 1) * angular_count
        faces.append((bow, last + angular_index, last + nxt))
    obj = mesh_object("SM_Antey_LOD0_Hull", (vertices, faces), collection)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def rounded_profile_cage(
    name: str,
    collection: bpy.types.Collection,
    x_profile: list[tuple[float, float, float, float]],
    angular_count: int,
    exponent: float = 2.0,
) -> bpy.types.Object:
    vertices = []
    for x, half_width, centre_z, half_height in x_profile:
        for angular_index in range(angular_count):
            angle = 2.0 * math.pi * angular_index / angular_count
            cosine = math.cos(angle)
            sine = math.sin(angle)
            y = half_width * math.copysign(abs(cosine) ** (2.0 / exponent), cosine)
            z = centre_z + half_height * math.copysign(abs(sine) ** (2.0 / exponent), sine)
            vertices.append((x, y, z))
    faces = []
    for station in range(len(x_profile) - 1):
        current = station * angular_count
        following = (station + 1) * angular_count
        for angular_index in range(angular_count):
            nxt = (angular_index + 1) % angular_count
            faces.append((current + angular_index, current + nxt, following + nxt, following + angular_index))
    faces.append(tuple(range(angular_count - 1, -1, -1)))
    last = (len(x_profile) - 1) * angular_count
    faces.append(tuple(last + index for index in range(angular_count)))
    obj = mesh_object(name, (vertices, faces), collection)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def make_fin_xz(name: str, outline: list[tuple[float, float]], half_thickness_y: float, collection: bpy.types.Collection) -> bpy.types.Object:
    vertices = [(x, -half_thickness_y, z) for x, z in outline] + [(x, half_thickness_y, z) for x, z in outline]
    count = len(outline)
    faces = [tuple(range(count - 1, -1, -1)), tuple(count + index for index in range(count))]
    for index in range(count):
        nxt = (index + 1) % count
        faces.append((index, nxt, count + nxt, count + index))
    obj = mesh_object(name, (vertices, faces), collection)
    bevel_object(obj, 0.10, 2)
    return obj


def make_fin_xy(name: str, outline: list[tuple[float, float]], centre_z: float, half_thickness_z: float, collection: bpy.types.Collection) -> bpy.types.Object:
    vertices = [(x, y, centre_z - half_thickness_z) for x, y in outline] + [
        (x, y, centre_z + half_thickness_z) for x, y in outline
    ]
    count = len(outline)
    faces = [tuple(range(count - 1, -1, -1)), tuple(count + index for index in range(count))]
    for index in range(count):
        nxt = (index + 1) % count
        faces.append((index, nxt, count + nxt, count + index))
    obj = mesh_object(name, (vertices, faces), collection)
    bevel_object(obj, 0.10, 2)
    return obj


def voxel_retopologize(obj: bpy.types.Object, voxel_size: float, target_triangles: int) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    obj.data.remesh_voxel_size = voxel_size
    obj.data.remesh_voxel_adaptivity = 0.0
    bpy.ops.object.voxel_remesh()
    obj.data.calc_loop_triangles()
    current = len(obj.data.loop_triangles)
    if current > target_triangles:
        modifier = obj.modifiers.new("LOD0_ControlledDecimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = target_triangles / current
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    obj.data.update()


def apply_transform(obj: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def bevel_object(obj: bpy.types.Object, width: float, segments: int = 3) -> None:
    modifier = obj.modifiers.new("ProductionBevel", "BEVEL")
    modifier.width = width
    modifier.segments = segments
    bpy.ops.object.select_all(action="DESELECT")
    obj.hide_viewport = False
    obj.hide_select = False
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def make_box(name: str, location: tuple, dimensions: tuple, rotation_x: float, collection: bpy.types.Collection, bevel: float) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=(rotation_x, 0.0, 0.0))
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_Mesh"
    obj.dimensions = dimensions
    apply_transform(obj)
    bevel_object(obj, bevel)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    return obj


def make_cylinder(
    name: str,
    location: tuple,
    radius: float,
    depth: float,
    collection: bpy.types.Collection,
    vertices: int = 48,
    axis: str = "X",
) -> bpy.types.Object:
    if axis not in {"X", "Z"}:
        raise ValueError(f"Unsupported cylinder axis: {axis}")
    rotation = (0.0, math.pi * 0.5, 0.0) if axis == "X" else (0.0, 0.0, 0.0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_Mesh"
    apply_transform(obj)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    return obj


def make_propeller(name: str, location: tuple, handedness: float, collection: bpy.types.Collection) -> bpy.types.Object:
    vertices = []
    faces = []
    # Hub: X-aligned low-poly cylinder, independent component.
    rings = (-0.38, 0.38)
    segments = 24
    for x in rings:
        for index in range(segments):
            angle = 2.0 * math.pi * index / segments
            vertices.append((x, 0.62 * math.cos(angle), 0.62 * math.sin(angle)))
    for index in range(segments):
        nxt = (index + 1) % segments
        faces.append((index, nxt, segments + nxt, segments + index))
    faces.append(tuple(range(segments - 1, -1, -1)))
    faces.append(tuple(range(segments, segments * 2)))
    # Seven visibly swept blades.  Each is its own closed component.
    for blade in range(7):
        angle = 2.0 * math.pi * blade / 7.0
        radial = Vector((0.0, math.cos(angle), math.sin(angle)))
        tangent = Vector((0.0, -math.sin(angle), math.cos(angle))) * handedness
        outline = [
            radial * 0.52 - tangent * 0.15,
            radial * 1.18 - tangent * 0.26,
            radial * 2.35 + tangent * 0.38,
            radial * 1.90 + tangent * 0.72,
            radial * 0.70 + tangent * 0.25,
        ]
        base = len(vertices)
        for x in (-0.09, 0.09):
            for point in outline:
                vertices.append((x, point.y, point.z))
        count = len(outline)
        faces.append(tuple(base + index for index in range(count - 1, -1, -1)))
        faces.append(tuple(base + count + index for index in range(count)))
        for index in range(count):
            nxt = (index + 1) % count
            faces.append((base + index, base + nxt, base + count + nxt, base + count + index))
    obj = mesh_object(name, (vertices, faces), collection)
    obj.location = location
    obj.data.update()
    bevel_object(obj, 0.045, 2)
    obj["rotation_axis"] = "+X"
    obj["visible_blades"] = 7
    obj["handedness_status"] = "MIRRORED_PAIR; EXACT REAL-WORLD HANDEDNESS UNCONFIRMED"
    return obj


def make_empty(name: str, location: tuple, rotation: tuple, collection: bpy.types.Collection) -> bpy.types.Object:
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = 1.2
    obj.location = location
    obj.rotation_mode = "QUATERNION"
    direction = Vector(rotation).normalized()
    obj.rotation_quaternion = direction.to_track_quat("X", "Z")
    collection.objects.link(obj)
    return obj


def make_material(name: str, color: tuple, metallic: float, roughness: float) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*color, 1.0)
    material.use_nodes = True
    node = material.node_tree.nodes.get("Principled BSDF")
    if node:
        node.inputs["Base Color"].default_value = (*color, 1.0)
        metallic_input = node.inputs.get("Metallic IOR Level") or node.inputs.get("Metallic")
        if metallic_input:
            metallic_input.default_value = metallic
        node.inputs["Roughness"].default_value = roughness
    return material


def assign_material(obj: bpy.types.Object, material: bpy.types.Material) -> None:
    obj.data.materials.clear()
    obj.data.materials.append(material)


def build_authoring(collection: bpy.types.Collection, hatch_collection: bpy.types.Collection) -> None:
    angle = math.radians(40.0)
    cant = math.radians(3.0)
    # The side cutaway shows one dense longitudinal bank of twelve inclined
    # canisters per side. Adjacent canisters form six hatch groups. At 40
    # degrees elevation, 2.15 m longitudinal spacing yields ~1.38 m normal
    # axis separation around the 1.333 m stowed missile.
    # The supplied profile/plan sheet places the bank below and alongside the
    # sail rather than around the hull midpoint.
    missile_positions = [25.0 + (index - 5.5) * 2.15 for index in range(12)]
    for side, sign in (("PORT", 1.0), ("STARBOARD", -1.0)):
        for group in range(1, 7):
            first_index = (group - 1) * 2
            pair_positions = missile_positions[first_index : first_index + 2]
            hatch_x = sum(pair_positions) * 0.5
            hatch = make_box(
                f"SM_P700_Hatch_{'Port' if sign > 0 else 'Starboard'}_{group:02d}",
                (hatch_x, sign * 7.35, 3.65),
                (4.05, 2.10, 0.20),
                -sign * math.radians(34.0),
                hatch_collection,
                0.16,
            )
            hatch["hatch_group"] = f"{side}_HATCH_{group:02d}"
            hatch["pivot_status"] = "GAMEPLAY_AUTHORING_APPROXIMATION"
            for pair_index, x in enumerate(pair_positions, 1):
                missile_index = first_index + pair_index
                direction = (math.cos(angle) * math.cos(cant), sign * math.sin(cant), math.sin(angle))
                marker = make_empty(
                    f"HP_P700_{side}_{missile_index:02d}",
                    (x, sign * 7.05, 0.70),
                    direction,
                    collection,
                )
                marker["hatch_group"] = f"{side}_HATCH_{group:02d}"
                marker["pair_index"] = pair_index
                marker["bank_row"] = "LONGITUDINAL"
                marker["row_index"] = missile_index
                marker["launch_forward"] = direction
                marker["launcher_envelope_diameter"] = 1.35
                marker["launcher_envelope_length"] = 10.2

    # All six bow tube doors sit in the upper half of the bow.  The larger
    # 650 mm pair forms the upper row; the four 533 mm tubes form the denser
    # row below it, while remaining above the hull centreline.
    torpedo_layout = [
        ("HP_TORPEDO_533_01", "533", 76.48, -1.80, 1.15, 0.533),
        ("HP_TORPEDO_533_02", "533", 76.70, -0.60, 1.30, 0.533),
        ("HP_TORPEDO_533_03", "533", 76.70, 0.60, 1.30, 0.533),
        ("HP_TORPEDO_533_04", "533", 76.48, 1.80, 1.15, 0.533),
        ("HP_TORPEDO_650_01", "650", 76.20, -1.05, 2.25, 0.650),
        ("HP_TORPEDO_650_02", "650", 76.20, 1.05, 2.25, 0.650),
    ]
    for name, tube_class, x, y, z, diameter in torpedo_layout:
        marker = make_empty(name, (x, y, z), (1.0, 0.0, 0.0), collection)
        marker["tube_class"] = tube_class
        marker["launch_forward"] = (1.0, 0.0, 0.0)
        marker["debug_envelope_diameter"] = diameter


def ensure_uv_map(obj: bpy.types.Object) -> None:
    """Create an authoring UV set for generated production meshes."""
    if obj.type != "MESH" or obj.data.uv_layers:
        return
    bpy.ops.object.select_all(action="DESELECT")
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=1.15192, island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.uv_layers.active.name = "UVMap"
    obj.select_set(False)


def create_lods(lod0_objects: list[bpy.types.Object]) -> list[bpy.types.Object]:
    generated = []
    for lod, hull_ratio, detail_ratio in ((1, 0.78, 0.86), (2, 0.36, 0.58), (3, 0.11, 0.38)):
        collection = new_collection(f"ANTEY_PRODUCTION_LOD{lod}")
        for source in lod0_objects:
            if lod == 3 and ("Mast_" in source.name or "TorpedoDoor" in source.name or "Hatch_" in source.name):
                continue
            obj = source.copy()
            obj.data = source.data.copy()
            if "_LOD0_" in source.name:
                obj.name = source.name.replace("_LOD0_", f"_LOD{lod}_")
            else:
                obj.name = f"{source.name}_LOD{lod}"
            obj.data.name = f"{obj.name}_Mesh"
            collection.objects.link(obj)
            obj["runtime_export"] = True
            obj["lod"] = lod
            ratio = hull_ratio if source.name == "SM_Antey_LOD0_Hull" else detail_ratio
            if ratio < 0.999 and len(obj.data.polygons) > 24:
                modifier = obj.modifiers.new(f"LOD{lod}_ControlledDecimate", "DECIMATE")
                modifier.decimate_type = "COLLAPSE"
                modifier.ratio = ratio
                modifier.use_collapse_triangulate = True
                bpy.ops.object.select_all(action="DESELECT")
                obj.hide_viewport = False
                obj.select_set(True)
                bpy.context.view_layer.objects.active = obj
                bpy.ops.object.modifier_apply(modifier=modifier.name)
            generated.append(obj)
    return generated


def build_collision_and_volumes() -> None:
    collision = new_collection("ANTEY_COLLISION")
    collision_specs = [
        ("COL_Antey_Bow", (56.0, 0.0, -0.15), (38.0, 13.0, 8.0), 3.5),
        ("COL_Antey_Main", (5.0, 0.0, -0.10), (86.0, 16.5, 10.0), 4.0),
        ("COL_Antey_Aft", (-53.0, 0.0, -0.35), (32.0, 12.0, 7.5), 3.0),
        ("COL_Antey_Sail", (29.0, 0.0, 6.0), (28.0, 3.8, 3.4), 1.2),
    ]
    for name, location, dimensions, bevel in collision_specs:
        obj = make_box(name, location, dimensions, 0.0, collision, bevel)
        obj["collision_proxy"] = True
        obj["runtime_export"] = False

    authoring_volumes = new_collection("ANTEY_GAMEPLAY_VOLUMES")
    buoyancy = make_box("PHY_Antey_BuoyancyVolume", (0.0, 0.0, -0.25), (132.0, 13.5, 8.4), 0.0, authoring_volumes, 3.0)
    buoyancy["authoring_proxy"] = "BUOYANCY_ONLY; NOT_RENDER_COLLISION"
    buoyancy["runtime_export"] = False
    compartment_specs = [
        ("VOL_COMP_01_BowWeapons", 65.0, 12.0),
        ("VOL_COMP_02_ForwardSupport", 51.0, 12.0),
        ("VOL_COMP_03_Command", 36.0, 14.0),
        ("VOL_COMP_04_Habitability", 20.0, 14.0),
        ("VOL_COMP_05_Auxiliary", 4.0, 14.0),
        ("VOL_COMP_06_MachineryForward", -12.0, 14.0),
        ("VOL_COMP_07_EnergyPlantForward", -28.0, 14.0),
        ("VOL_COMP_08_EnergyPlantAft", -44.0, 14.0),
        ("VOL_COMP_09_AftMachinery", -59.0, 12.0),
        ("VOL_COMP_10_AftDrive", -71.0, 8.0),
    ]
    for name, x, length in compartment_specs:
        volume = make_box(name, (x, 0.0, -0.25), (length, 9.0, 6.4), 0.0, authoring_volumes, 0.35)
        volume["logical_zone_only"] = True
        volume["runtime_export"] = False
        volume["half_extents"] = (length * 0.5, 4.5, 3.2)

    markers = bpy.data.collections.get("ANTEY_AUTHORING")
    for name, location in (
        ("HP_Antey_Bow", (77.0, 0.0, 0.0)),
        ("HP_Antey_Stern", (-77.0, 0.0, 0.0)),
        ("HP_Antey_Center", (0.0, 0.0, 0.0)),
    ):
        marker = make_empty(name, location, (1.0, 0.0, 0.0), markers)
        marker["runtime_export"] = False


def main() -> None:
    options = args()
    source = options.source.resolve()
    output = options.output.resolve()
    if source == output:
        raise RuntimeError("Refusing to overwrite immutable Antey source")
    clear_scene()
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene["coordinate_contract"] = "+X bow; +Y port; +Z up; 1 BU = 1 m"
    source_objects = append_source(source)
    # Library-appended objects are not yet scene-evaluated, so matrix_world is
    # identity here; matrix_basis contains the authored source transform.
    original_matrices = {name: obj.matrix_basis.copy() for name, obj in source_objects.items()}
    reference_collection = new_collection("REFERENCE_SOURCE")
    production = new_collection("ANTEY_PRODUCTION_LOD0")
    hatches = new_collection("ANTEY_VISUAL_HATCHES")
    authoring = new_collection("ANTEY_AUTHORING")
    transform = canonical_matrix()

    for source_name, obj in source_objects.items():
        obj.name = f"REF_Source_{obj.name}"
        obj.data.name = f"REF_Source_{obj.data.name}"
        obj.matrix_world = transform @ original_matrices[source_name]
        obj.display_type = "WIRE"
        obj.hide_render = True
        obj.hide_viewport = True
        obj.hide_select = True
        obj["runtime_export"] = False
        reference_collection.objects.link(obj)

    # The failed source shells are used as read-only aligned visual references.
    # Production geometry comes from explicit, smooth station cages measured
    # against those silhouettes; no legacy topology survives in the runtime mesh.
    hull = antey_hull_cage(production)
    sail = rounded_profile_cage(
        "SM_Antey_LOD0_Sail",
        production,
        [
            (14.0, 1.30, 5.30, 0.75),
            (16.0, 1.80, 6.40, 1.80),
            (19.0, 2.05, 6.70, 2.20),
            (38.0, 2.05, 6.70, 2.20),
            (42.0, 1.80, 6.45, 1.85),
            (43.5, 1.10, 5.65, 1.00),
        ],
        angular_count=64,
        exponent=4.0,
    )

    shaft_fairings = []
    for side, y in (("Port", 5.25), ("Starboard", -5.25)):
        fairing = rounded_profile_cage(
            f"SM_Antey_LOD0_ShaftFairing_{side}",
            production,
            [
                (-72.0, 0.10, -0.90, 0.10),
                (-69.5, 0.65, -0.92, 0.62),
                (-65.5, 1.15, -0.82, 1.05),
                (-58.5, 1.65, -0.48, 1.45),
                (-52.0, 1.15, -0.05, 1.10),
                (-49.0, 0.12, 0.12, 0.12),
            ],
            angular_count=48,
        )
        fairing.data.transform(Matrix.Translation(Vector((0.0, y, 0.0))))
        shaft_fairings.append(fairing)

    vertical_fins = [
        make_fin_xz(
            "SM_Antey_LOD0_TailFin_Dorsal",
            [(-69.0, 0.75), (-64.0, 6.55), (-57.0, 6.05), (-51.0, 3.20), (-49.5, 1.75)],
            0.22,
            production,
        ),
        make_fin_xz(
            "SM_Antey_LOD0_TailFin_Ventral",
            [(-69.0, -0.70), (-64.0, -5.35), (-57.0, -4.95), (-51.0, -2.80), (-49.5, -1.55)],
            0.22,
            production,
        ),
    ]
    horizontal_fins = [
        make_fin_xy(
            "SM_Antey_LOD0_TailPlane_Port",
            [(-68.5, 3.00), (-65.0, 9.50), (-57.0, 9.50), (-51.5, 6.20)],
            0.15,
            0.18,
            production,
        ),
        make_fin_xy(
            "SM_Antey_LOD0_TailPlane_Starboard",
            [(-68.5, -3.00), (-65.0, -9.50), (-57.0, -9.50), (-51.5, -6.20)],
            0.15,
            0.18,
            production,
        ),
    ]
    bow_planes = [
        make_fin_xy(
            "SM_Antey_LOD0_BowPlane_Port",
            [(54.5, 6.50), (56.5, 9.50), (62.5, 9.50), (64.0, 6.15)],
            1.00,
            0.16,
            production,
        ),
        make_fin_xy(
            "SM_Antey_LOD0_BowPlane_Starboard",
            [(54.5, -6.50), (56.5, -9.50), (62.5, -9.50), (64.0, -6.15)],
            1.00,
            0.16,
            production,
        ),
    ]

    hull_material = make_material("MAT_Antey_Hull", (0.10, 0.16, 0.20), 0.25, 0.42)
    propeller_material = make_material("MAT_Antey_Propellers", (0.46, 0.29, 0.09), 0.68, 0.27)
    assign_material(hull, hull_material)
    assign_material(sail, hull_material)
    for obj in [*shaft_fairings, *vertical_fins, *horizontal_fins, *bow_planes]:
        assign_material(obj, hull_material)

    # Source-proportioned vertical mast silhouettes; neutral and without
    # historic markings.  Cylinders use an explicit Z axis because the same
    # helper defaults to X for the bow-facing torpedo doors.
    mast_specs = [(22.0, 0.22, 4.2), (25.0, 0.28, 5.0), (28.2, 0.24, 4.6), (31.0, 0.18, 3.8)]
    mast_objects = []
    for index, (x, radius, height) in enumerate(mast_specs, 1):
        obj = make_cylinder(
            f"SM_Antey_LOD0_Mast_{index:02d}",
            (x, 0.0, 8.90 + height * 0.5),
            radius,
            height,
            production,
            24,
            axis="Z",
        )
        assign_material(obj, hull_material)
        mast_objects.append(obj)

    port_propeller = make_propeller("SM_Propeller_Port", (-73.0, 5.25, -0.92), 1.0, production)
    starboard_propeller = make_propeller("SM_Propeller_Starboard", (-73.0, -5.25, -0.92), -1.0, production)
    assign_material(port_propeller, propeller_material)
    assign_material(starboard_propeller, propeller_material)

    build_authoring(authoring, hatches)
    for hatch in hatches.objects:
        assign_material(hatch, hull_material)
        hatch["runtime_export"] = True
        hatch["lod"] = 0

    doors = []
    door_layout = (
        (76.48, -1.80, 1.15, 0.285),
        (76.70, -0.60, 1.30, 0.285),
        (76.70, 0.60, 1.30, 0.285),
        (76.48, 1.80, 1.15, 0.285),
        (76.20, -1.05, 2.25, 0.345),
        (76.20, 1.05, 2.25, 0.345),
    )
    for index, (x, y, z, radius) in enumerate(door_layout, 1):
        door = make_cylinder(f"SM_Antey_LOD0_TorpedoDoor_{index:02d}", (x, y, z), radius, 0.14, production, 40)
        assign_material(door, hull_material)
        doors.append(door)

    runtime_objects = [
        hull,
        sail,
        *shaft_fairings,
        *vertical_fins,
        *horizontal_fins,
        *bow_planes,
        *mast_objects,
        port_propeller,
        starboard_propeller,
        *hatches.objects,
        *doors,
    ]
    for obj in runtime_objects:
        obj["runtime_export"] = True
        obj["lod"] = 0
        if any(abs(value) > 1.0e-6 for value in obj.rotation_euler) or any(abs(value - 1.0) > 1.0e-6 for value in obj.scale):
            raise RuntimeError(f"Production transform contract failed: {obj.name}")
        if len(obj.data.vertices) == 0 or len(obj.data.polygons) == 0:
            raise RuntimeError(f"Empty production mesh: {obj.name}")

    for obj in runtime_objects:
        ensure_uv_map(obj)

    lower_lods = create_lods(runtime_objects)
    if not lower_lods or any(len(obj.data.polygons) == 0 for obj in lower_lods):
        raise RuntimeError("Antey lower-LOD generation failed")
    build_collision_and_volumes()

    # Some Blender operators ignore hidden-object selection state. Re-assert
    # the immutable references after every production operation. Bake an
    # evaluated COPY into the normalized frame so later review operators can
    # never drop or reapply the source transforms.
    for source_name, obj in source_objects.items():
        evaluated_copy = evaluated_mesh(obj)
        evaluated_copy.transform(transform @ original_matrices[source_name])
        evaluated_copy.name = f"REF_Normalized_{source_name}_Mesh"
        obj.data = evaluated_copy
        obj.modifiers.clear()
        obj.matrix_basis = Matrix.Identity(4)
        obj.hide_render = True
        obj.hide_viewport = True
        obj.hide_select = True

    scene["asset_id"] = "C0 Player Submarine"
    scene["asset_name"] = "Antey"
    scene["source_length_scale"] = LENGTH_SCALE
    scene["source_beam_and_vertical_scale"] = BEAM_SCALE
    scene["lod0_gate"] = "PENDING_FRESH_REOPEN_VISUAL_REVIEW"
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    print(f"ANTEY_LOD0_TEMP_BUILD_OK {output}")


if __name__ == "__main__":
    main()
