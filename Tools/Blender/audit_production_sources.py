"""Read-only saved-artifact audit for the canonical Antey and P-700 sources.

Run by opening exactly one source .blend in a fresh Blender process.  The
script never saves the input file.  It writes an independent JSON inventory
and bright workbench renders of the scene that was actually opened.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import deque
from pathlib import Path

import bpy
from mathutils import Vector


def parse_args() -> argparse.Namespace:
    args = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renders", required=True, type=Path)
    parser.add_argument("--prefix", required=True)
    return parser.parse_args(args)


def component_vertex_sets(mesh: bpy.types.Mesh) -> list[set[int]]:
    adjacency = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(mesh.vertices)))
    components: list[set[int]] = []
    while unseen:
        start = unseen.pop()
        pending = deque([start])
        component = {start}
        while pending:
            current = pending.popleft()
            for neighbour in adjacency[current]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    component.add(neighbour)
                    pending.append(neighbour)
        components.append(component)
    return components


def component_audit(obj: bpy.types.Object, components: list[set[int]]) -> list[dict]:
    mesh = obj.data
    vertex_component = {}
    for component_index, vertices in enumerate(components):
        for vertex_index in vertices:
            vertex_component[vertex_index] = component_index
    face_counts = [0] * len(components)
    triangle_counts = [0] * len(components)
    for polygon in mesh.polygons:
        component_index = vertex_component[polygon.vertices[0]]
        face_counts[component_index] += 1
        triangle_counts[component_index] += max(1, len(polygon.vertices) - 2)
    summaries = []
    for component_index, vertices in enumerate(components):
        points = [obj.matrix_world @ mesh.vertices[index].co for index in vertices]
        minimum = [min(point[axis] for point in points) for axis in range(3)]
        maximum = [max(point[axis] for point in points) for axis in range(3)]
        summaries.append(
            {
                "component": component_index,
                "vertices": len(vertices),
                "faces": face_counts[component_index],
                "triangles_approx": triangle_counts[component_index],
                "world_bounds": {"minimum": minimum, "maximum": maximum},
                "world_dimensions": [maximum[i] - minimum[i] for i in range(3)],
            }
        )
    return sorted(summaries, key=lambda item: item["triangles_approx"], reverse=True)


def mesh_audit(obj: bpy.types.Object) -> dict:
    mesh = obj.data
    mesh.calc_loop_triangles()
    edge_face_counts = [0] * len(mesh.edges)
    edge_index = {tuple(sorted(edge.vertices)): index for index, edge in enumerate(mesh.edges)}
    for polygon in mesh.polygons:
        for key in polygon.edge_keys:
            edge_face_counts[edge_index[tuple(sorted(key))]] += 1
    world_corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    minimum = [min(corner[i] for corner in world_corners) for i in range(3)]
    maximum = [max(corner[i] for corner in world_corners) for i in range(3)]
    degenerate = sum(1 for polygon in mesh.polygons if polygon.area <= 1.0e-10)
    components = component_vertex_sets(mesh)
    return {
        "name": obj.name,
        "mesh": mesh.name,
        "vertices": len(mesh.vertices),
        "edges": len(mesh.edges),
        "polygons": len(mesh.polygons),
        "triangles": len(mesh.loop_triangles),
        "connected_components": len(components),
        "component_details": component_audit(obj, components),
        "boundary_edges": sum(count == 1 for count in edge_face_counts),
        "non_manifold_edges": sum(count != 2 for count in edge_face_counts),
        "degenerate_faces": degenerate,
        "location": list(obj.location),
        "rotation_euler": list(obj.rotation_euler),
        "scale": list(obj.scale),
        "world_bounds": {"minimum": minimum, "maximum": maximum},
        "dimensions": list(obj.dimensions),
        "hidden_viewport": obj.hide_get(),
        "hidden_render": obj.hide_render,
        "materials": [slot.material.name if slot.material else None for slot in obj.material_slots],
    }


def combined_bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[i] for point in points) for i in range(3))),
        Vector(tuple(max(point[i] for point in points) for i in range(3))),
    )


def render_review(output_dir: Path, prefix: str, mesh_objects: list[bpy.types.Object]) -> list[dict]:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (0.72, 0.78, 0.84)
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.show_specular_highlight = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("AUDIT_World")
    scene.world.color = (0.82, 0.82, 0.82)
    for obj in mesh_objects:
        obj.hide_render = False
        obj.hide_set(False)

    minimum, maximum = combined_bounds(mesh_objects)
    centre = (minimum + maximum) * 0.5
    extents = maximum - minimum
    diagonal = max(extents.length, 1.0)

    camera_data = bpy.data.cameras.new("AUDIT_Camera")
    camera = bpy.data.objects.new("AUDIT_Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.data.type = "ORTHO"

    views = {
        "axis_x": Vector((1.0, 0.0, 0.0)),
        "axis_y": Vector((0.0, 1.0, 0.0)),
        "axis_z": Vector((0.0, 0.0, 1.0)),
        "three_quarter": Vector((1.0, -1.0, 0.65)).normalized(),
        "opposite_three_quarter": Vector((-1.0, 1.0, -0.45)).normalized(),
    }
    results = []
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, direction in views.items():
        camera.location = centre + direction * diagonal * 1.5
        camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.ortho_scale = max(extents.length * 0.72, max(extents) * 1.15)
        filepath = output_dir / f"{prefix}_{name}.png"
        scene.render.filepath = str(filepath)
        bpy.ops.render.render(write_still=True)
        results.append(
            {
                "file": str(filepath),
                "camera_location": list(camera.location),
                "camera_direction": list(direction),
                "visible_objects": [obj.name for obj in mesh_objects],
            }
        )
    return results


def main() -> None:
    args = parse_args()
    args.output = args.output.resolve()
    args.renders = args.renders.resolve()
    mesh_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not mesh_objects:
        raise RuntimeError("The saved source contains no mesh objects")
    minimum, maximum = combined_bounds(mesh_objects)
    report = {
        "opened_blend": bpy.data.filepath,
        "blender_version": bpy.app.version_string,
        "object_count": len(bpy.context.scene.objects),
        "mesh_object_count": len(mesh_objects),
        "combined_world_bounds": {"minimum": list(minimum), "maximum": list(maximum)},
        "combined_world_dimensions": list(maximum - minimum),
        "objects": [mesh_audit(obj) for obj in mesh_objects],
    }
    report["renders"] = render_review(args.renders, args.prefix, mesh_objects)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"SOURCE_AUDIT_OK {args.output}")


if __name__ == "__main__":
    main()
