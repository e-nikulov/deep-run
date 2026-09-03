"""Small, non-mutating mesh intersection helpers for Blender validators.

This module intentionally contains only the reusable geometry primitives used
by production validation.  It never edits Blender data, saves a file, or
depends on candidate/forensic authoring scripts.
"""
from __future__ import annotations

from collections.abc import Sequence

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


Triangle = tuple[Vector, Vector, Vector]


def world_vertices(obj: bpy.types.Object) -> list[Vector]:
    """Return object vertices in world space without changing the object."""
    bpy.context.view_layer.update()
    return [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]


def polygon_triangles(obj: bpy.types.Object) -> list[tuple[int, int, int]]:
    """Triangulate polygon fans for BVH construction without mesh mutation."""
    triangles: list[tuple[int, int, int]] = []
    for polygon in obj.data.polygons:
        vertices = list(polygon.vertices)
        for index in range(1, len(vertices) - 1):
            triangles.append((vertices[0], vertices[index], vertices[index + 1]))
    return triangles


def object_bvh(obj: bpy.types.Object) -> tuple[BVHTree, list[Triangle]]:
    """Build a world-space BVH and matching triangle list for *obj*."""
    vertices = world_vertices(obj)
    triangles = polygon_triangles(obj)
    bvh = BVHTree.FromPolygons(vertices, triangles, all_triangles=True)
    return bvh, [(vertices[a], vertices[b], vertices[c]) for a, b, c in triangles]


def point_in_triangle(
    point: Vector,
    tri: Triangle,
    epsilon: float = 1.0e-7,
    strict: bool = False,
) -> bool:
    a, b, c = tri
    v0, v1, v2 = b - a, c - a, point - a
    d00, d01, d11 = v0.dot(v0), v0.dot(v1), v1.dot(v1)
    d20, d21 = v2.dot(v0), v2.dot(v1)
    denominator = d00 * d11 - d01 * d01
    if abs(denominator) < 1.0e-15:
        return False
    u = (d11 * d20 - d01 * d21) / denominator
    v = (d00 * d21 - d01 * d20) / denominator
    w = 1.0 - u - v
    return min(u, v, w) > epsilon if strict else min(u, v, w) >= -epsilon


def segment_plane_hit(start: Vector, end: Vector, tri: Triangle) -> Vector | None:
    a, b, c = tri
    normal = (b - a).cross(c - a)
    length = normal.length
    if length < 1.0e-12:
        return None
    normal.normalize()
    denominator = normal.dot(end - start)
    if abs(denominator) < 1.0e-10:
        return None
    t = normal.dot(a - start) / denominator
    if t <= 1.0e-7 or t >= 1.0 - 1.0e-7:
        return None
    point = start + t * (end - start)
    return point if point_in_triangle(point, tri, strict=True) else None


def projected(point: Vector, drop_axis: int) -> tuple[float, float]:
    return tuple(float(point[index]) for index in range(3) if index != drop_axis)  # type: ignore[return-value]


def orient2(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def point_in_triangle_2d(
    point: tuple[float, float], tri: Sequence[tuple[float, float]], epsilon: float = 1.0e-9
) -> bool:
    signs = [orient2(tri[0], tri[1], point), orient2(tri[1], tri[2], point), orient2(tri[2], tri[0], point)]
    return not (min(signs) < -epsilon and max(signs) > epsilon)


def segment_intersection_2d(a, b, c, d) -> tuple[float, float] | None:
    denominator = (b[0] - a[0]) * (d[1] - c[1]) - (b[1] - a[1]) * (d[0] - c[0])
    if abs(denominator) < 1.0e-12:
        return None
    t = ((c[0] - a[0]) * (d[1] - c[1]) - (c[1] - a[1]) * (d[0] - c[0])) / denominator
    u = ((c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0])) / denominator
    if -1.0e-8 <= t <= 1.0 + 1.0e-8 and -1.0e-8 <= u <= 1.0 + 1.0e-8:
        return (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
    return None


def convex_hull_2d(points: Sequence[tuple[float, float]]) -> list[tuple[float, float]]:
    unique = sorted(set((round(p[0], 10), round(p[1], 10)) for p in points))
    if len(unique) <= 1:
        return unique
    lower: list[tuple[float, float]] = []
    for point in unique:
        while len(lower) >= 2 and orient2(lower[-2], lower[-1], point) <= 0.0:
            lower.pop()
        lower.append(point)
    upper: list[tuple[float, float]] = []
    for point in reversed(unique):
        while len(upper) >= 2 and orient2(upper[-2], upper[-1], point) <= 0.0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def polygon_area_2d(points: Sequence[tuple[float, float]]) -> float:
    return abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, list(points[1:]) + list(points[:1])))) * 0.5 if len(points) >= 3 else 0.0


def positive_triangle_intersection(first: Triangle, second: Triangle) -> bool:
    """Return true only for a positive-area triangle intersection.

    Coplanar edge/vertex touches are intentionally excluded; this avoids
    treating coincident boundaries as physical penetration in validators.
    """
    n0 = (first[1] - first[0]).cross(first[2] - first[0])
    n1 = (second[1] - second[0]).cross(second[2] - second[0])
    if n0.length < 1.0e-12 or n1.length < 1.0e-12:
        return False
    cross = n0.cross(n1)
    if cross.length < 1.0e-9:
        drop = max(range(3), key=lambda axis: abs(n0[axis]))
        a = [projected(point, drop) for point in first]
        b = [projected(point, drop) for point in second]
        points = [point for point in a if point_in_triangle_2d(point, b)] + [point for point in b if point_in_triangle_2d(point, a)]
        for i in range(3):
            for j in range(3):
                hit = segment_intersection_2d(a[i], a[(i + 1) % 3], b[j], b[(j + 1) % 3])
                if hit is not None:
                    points.append(hit)
        return polygon_area_2d(convex_hull_2d(points)) > 1.0e-8
    for tri_a, tri_b in ((first, second), (second, first)):
        for i in range(3):
            point = segment_plane_hit(tri_a[i], tri_a[(i + 1) % 3], tri_b)
            if point is not None:
                return True
    return False
