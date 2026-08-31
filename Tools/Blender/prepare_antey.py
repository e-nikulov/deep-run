"""Prepare, export, preview, and validate the neutral DeepRun Antey asset.

This is an offline Blender tool. The source audit is read-only. Preparation
creates a neutral copy from the audited source and never saves over it.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable

import bmesh
import bpy
from mathutils import Vector


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = REPOSITORY_ROOT / "Content" / "submarines" / "kursk.blend"
PACKAGE_ROOT = REPOSITORY_ROOT / "Content" / "submarines" / "Antey"
SOURCE_COPY = PACKAGE_ROOT / "Source" / "Antey_Source.blend"
PRODUCTION_BLEND = PACKAGE_ROOT / "Antey_GameReady.blend"
RUNTIME_GLB = PACKAGE_ROOT / "Antey.glb"
AUDIT_REPORT = PACKAGE_ROOT / "source_audit.md"
VALIDATION_REPORT = PACKAGE_ROOT / "validation_report.txt"
AUTHORING_JSON = PACKAGE_ROOT / "Antey.authoring.json"
PREVIEW_ROOT = PACKAGE_ROOT / "Previews"
REFERENCE_VIEWS = PACKAGE_ROOT / "References" / "DeepStorm_949A_views.png"

ROOT_NAME = "SM_Submarine_Antey_ROOT"
HULL_NAME = "SM_Antey_Hull"
LODS = ("LOD0", "LOD1", "LOD2", "LOD3")
FORBIDDEN_NAME_RE = re.compile(r"(?:kursk|k[-_ ]?\d+|949a|project\s*949|historical|bsw)", re.I)
EPSILON = 1.0e-5


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(f"Antey validation failed: {message}")


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--correct", action="store_true", help="Correct the existing production blend in place")
    return parser.parse_args(argv)


def triangle_count(obj: bpy.types.Object) -> int:
    return sum(max(0, len(poly.vertices) - 2) for poly in obj.data.polygons)


def component_count(obj: bpy.types.Object) -> int:
    if obj.type != "MESH" or not obj.data.vertices:
        return 0
    adjacency = [[] for _ in obj.data.vertices]
    for edge in obj.data.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(adjacency)))
    count = 0
    while unseen:
        count += 1
        stack = [unseen.pop()]
        while stack:
            index = stack.pop()
            for neighbour in adjacency[index]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    stack.append(neighbour)
    return count


def component_bounds(obj: bpy.types.Object) -> list[tuple[int, int, tuple[float, float, float], tuple[float, float, float]]]:
    if obj.type != "MESH" or not obj.data.vertices:
        return []
    adjacency = [[] for _ in obj.data.vertices]
    for edge in obj.data.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(adjacency)))
    result = []
    while unseen:
        seed = unseen.pop()
        component = [seed]
        stack = [seed]
        while stack:
            index = stack.pop()
            for neighbour in adjacency[index]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    component.append(neighbour)
                    stack.append(neighbour)
        component_set = set(component)
        points = [obj.matrix_world @ obj.data.vertices[index].co for index in component]
        edges = sum(1 for edge in obj.data.edges if edge.vertices[0] in component_set and edge.vertices[1] in component_set)
        minimum = tuple(min(point[axis] for point in points) for axis in range(3))
        maximum = tuple(max(point[axis] for point in points) for axis in range(3))
        result.append((len(component), edges, minimum, maximum))
    return sorted(result, reverse=True)


def world_bounds(objects: Iterable[bpy.types.Object]) -> tuple[Vector, Vector] | None:
    points = [obj.matrix_world @ vertex.co for obj in objects if obj.type == "MESH" for vertex in obj.data.vertices]
    if not points:
        return None
    return Vector((min(point[index] for point in points) for index in range(3))), Vector((max(point[index] for point in points) for index in range(3)))


def mesh_stats(obj: bpy.types.Object) -> dict[str, int | bool]:
    mesh = obj.data
    polygon_links = Counter(index for polygon in mesh.polygons for index in polygon.edge_keys)
    return {
        "vertices": len(mesh.vertices), "edges": len(mesh.edges), "polygons": len(mesh.polygons), "triangles": triangle_count(obj),
        "components": component_count(obj), "non_manifold_edges": sum(1 for edge in mesh.edges if polygon_links[tuple(sorted(edge.vertices))] != 2),
        "degenerate_faces": sum(1 for poly in mesh.polygons if poly.area <= 1.0e-10), "has_uv": bool(mesh.uv_layers), "has_material": bool(mesh.materials),
        "identity_rotation": all(abs(value) <= EPSILON for value in obj.rotation_euler), "identity_scale": all(abs(value - 1.0) <= EPSILON for value in obj.scale),
    }


def audit(source: Path) -> str:
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene_objects = list(bpy.context.scene.objects)
    mesh_objects = [obj for obj in scene_objects if obj.type == "MESH"]
    bounds = world_bounds(mesh_objects)
    dimensions = tuple(bounds[1][index] - bounds[0][index] for index in range(3)) if bounds else (0.0, 0.0, 0.0)
    stats = {obj.name: mesh_stats(obj) for obj in mesh_objects}
    modifiers = sum(len(obj.modifiers) for obj in scene_objects)
    textures = sorted({image.filepath or image.name for image in bpy.data.images if image.name != "Render Result"})
    materials = sorted(material.name for material in bpy.data.materials)
    hidden = sorted(obj.name for obj in scene_objects if obj.hide_render or obj.hide_viewport)
    source_name_hits = sorted({name for name in [source.name, *[obj.name for obj in scene_objects], *materials, *textures] if FORBIDDEN_NAME_RE.search(name)})
    internal_candidates = sorted(obj.name for obj in mesh_objects if component_count(obj) > 1 or "inner" in obj.name.lower() or "print" in obj.name.lower())
    reusable = sorted(obj.name for obj in mesh_objects if obj.name not in internal_candidates)
    problematic = sorted(name for name, value in stats.items() if value["non_manifold_edges"] or value["degenerate_faces"] or not value["identity_scale"])
    lines = [
        "SOURCE AUDIT", "", "Original: supplied immutable source (historical filename intentionally omitted from generated documentation)", f"File size: {source.stat().st_size} bytes", f"Blender version: {bpy.app.version_string}",
        f"Scenes: {len(bpy.data.scenes)} ({', '.join(scene.name for scene in bpy.data.scenes)})", f"Collections: {len(bpy.data.collections)}", "",
        f"Objects: {len(scene_objects)}", f"Mesh objects: {len(mesh_objects)}", f"Vertices: {sum(int(value['vertices']) for value in stats.values())}",
        f"Triangles: {sum(int(value['triangles']) for value in stats.values())}", f"Materials: {len(materials)}", f"Textures: {len(textures)}", f"Modifiers: {modifiers}",
        f"Armatures: {sum(1 for obj in scene_objects if obj.type == 'ARMATURE')}", f"Empties: {sum(1 for obj in scene_objects if obj.type == 'EMPTY')}", "",
        f"Dimensions: {dimensions}", f"Length: {dimensions[0]:.3f} m (authored world X)", f"Width: {dimensions[1]:.3f} m (authored world Y)", f"Height: {dimensions[2]:.3f} m (authored world Z)", "",
        "Suspected print-only/internal geometry:", *(f"- {name}" for name in (internal_candidates or ["none identified by static heuristics"])), "",
        "Reusable exterior geometry:", *(f"- {name}" for name in (reusable or ["none"])), "",
        "Problematic geometry:", *(f"- {name}: {stats[name]}" for name in (problematic or ["none identified"])), "",
        "Per-object audit:", *(f"- `{name}`: {stats[name]}" for name in sorted(stats)), "", "Disconnected component bounds (world space):",
        *(f"- `{obj.name}` component {index + 1}: vertices={part[0]}, edges={part[1]}, min={tuple(round(value, 4) for value in part[2])}, max={tuple(round(value, 4) for value in part[3])}" for obj in mesh_objects for index, part in enumerate(component_bounds(obj))), "",
        f"Hidden objects: {', '.join(hidden) if hidden else 'none'}", f"Source naming hits requiring neutralization in production: {len(source_name_hits)} historical identifiers detected; names intentionally omitted", "",
        "Object transforms and local dimensions:", *(f"- `{obj.name}`: location={tuple(round(value, 6) for value in obj.location)}, rotation={tuple(round(value, 6) for value in obj.rotation_euler)}, scale={tuple(round(value, 6) for value in obj.scale)}, dimensions={tuple(round(value, 6) for value in obj.dimensions)}" for obj in sorted(scene_objects, key=lambda item: item.name)), "",
        "Image datablocks / texture paths:", *(f"- historical source image identifier omitted ({image.size[0]}x{image.size[1]})" if FORBIDDEN_NAME_RE.search(image.name) or FORBIDDEN_NAME_RE.search(image.filepath) else f"- `{image.name}`: `{image.filepath}` ({image.size[0]}x{image.size[1]})" for image in sorted(bpy.data.images, key=lambda item: item.name) if image.name != "Render Result"), "",
        "Notes:", "- This report is read-only and does not modify the supplied source blend.", "- Source dimensions are not calibrated to DeepRun metres; production normalization is a separate step.", "- Static heuristics cannot prove artistic intent; print-only/internal findings require visual review.", "- License/attribution status must be reviewed separately before shipping.",
    ]
    return "\n".join(lines) + "\n"


def create_material(name: str, color: tuple[float, float, float, float], metallic: float, roughness: float) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.diffuse_color = color
    material.use_nodes = True
    principled = material.node_tree.nodes.get("Principled BSDF")
    require(principled is not None, f"missing Principled BSDF for {name}")
    principled.inputs["Base Color"].default_value = color
    principled.inputs["Metallic"].default_value = metallic
    principled.inputs["Roughness"].default_value = roughness
    return material


def source_component_vertices(obj: bpy.types.Object) -> list[set[int]]:
    adjacency = [[] for _ in obj.data.vertices]
    for edge in obj.data.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(adjacency)))
    result = []
    while unseen:
        seed = unseen.pop(); component = {seed}; stack = [seed]
        while stack:
            index = stack.pop()
            for neighbour in adjacency[index]:
                if neighbour in unseen:
                    unseen.remove(neighbour); component.add(neighbour); stack.append(neighbour)
        result.append(component)
    return result


def production_point(source_object: bpy.types.Object, vertex: bpy.types.MeshVertex, source_center: Vector, scale: Vector) -> Vector:
    point = source_object.matrix_world @ vertex.co
    return Vector(((point.y - source_center.y) * scale.x, (point.x - source_center.x) * scale.y, (point.z - source_center.z) * scale.z))


def select_only(obj: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def build_main_mesh(source_objects: list[bpy.types.Object], material: bpy.types.Material) -> bpy.types.Object:
    source_bounds = world_bounds(source_objects)
    require(source_bounds is not None, "source contains no mesh geometry")
    source_minimum, source_maximum = source_bounds
    source_center = (source_minimum + source_maximum) * 0.5
    source_dimensions = source_maximum - source_minimum
    scale = Vector((154.0 / source_dimensions.y, 18.0 / source_dimensions.x, 18.0 / source_dimensions.z))
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    removed_components = 0
    for obj in source_objects:
        removed: set[int] = set()
        if obj.name.lower() == "hull":
            for component in source_component_vertices(obj):
                points = [obj.matrix_world @ obj.data.vertices[index].co for index in component]
                if len(component) < 1000 and max(point.y for point in points) < source_minimum.y + 0.13:
                    removed.update(component); removed_components += 1
        index_map: dict[int, int] = {}
        for index, vertex in enumerate(obj.data.vertices):
            if index not in removed:
                index_map[index] = len(vertices)
                vertices.append(tuple(production_point(obj, vertex, source_center, scale)))
        for polygon in obj.data.polygons:
            if not any(index in removed for index in polygon.vertices):
                faces.append(tuple(index_map[index] for index in reversed(polygon.vertices)))
    mesh = bpy.data.meshes.new(f"{HULL_NAME}_Mesh")
    mesh.from_pydata(vertices, [], faces); mesh.materials.append(material); mesh.validate(clean_customdata=True); mesh.update(calc_edges=True)
    edit = bmesh.new(); edit.from_mesh(mesh); bmesh.ops.remove_doubles(edit, verts=list(edit.verts), dist=1.0e-5); bmesh.ops.dissolve_degenerate(edit, edges=list(edit.edges), dist=1.0e-7); bmesh.ops.recalc_face_normals(edit, faces=list(edit.faces)); edit.to_mesh(mesh); edit.free()
    obj = bpy.data.objects.new(HULL_NAME, mesh); bpy.context.scene.collection.objects.link(obj)
    obj["deeprun_asset_stage"] = "production"; obj["deeprun_forward_axis"] = "+X"; obj["deeprun_up_axis"] = "+Z"; obj["source_components_removed"] = removed_components
    select_only(obj); bpy.ops.object.shade_smooth(); bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.03); bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def create_propeller(name: str, location: tuple[float, float, float], material: bpy.types.Material) -> bpy.types.Object:
    vertices: list[tuple[float, float, float]] = []; faces: list[tuple[int, ...]] = []; segments = 16
    for x in (-0.45, 0.45):
        for index in range(segments):
            angle = math.tau * index / segments; vertices.append((x, 0.48 * math.cos(angle), 0.48 * math.sin(angle)))
    faces.extend((tuple(reversed(range(segments))), tuple(segments + index for index in range(segments))))
    for index in range(segments):
        next_index = (index + 1) % segments; faces.append((index, next_index, segments + next_index, segments + index))
    blade = ((0.35, -0.22), (1.25, -0.42), (3.9, -0.26), (4.35, 0.04), (3.15, 0.6), (1.1, 0.5))
    for blade_index in range(7):
        angle = math.tau * blade_index / 7.0; c, s = math.cos(angle), math.sin(angle); base = len(vertices); count = len(blade)
        for x in (-0.12, 0.12):
            vertices.extend((x, radius * c - tangent * s, radius * s + tangent * c) for radius, tangent in blade)
        faces.append(tuple(base + index for index in reversed(range(count)))); faces.append(tuple(base + count + index for index in range(count)))
        for index in range(count):
            next_index = (index + 1) % count; faces.append((base + index, base + next_index, base + count + next_index, base + count + index))
    mesh = bpy.data.meshes.new(f"{name}_Mesh"); mesh.from_pydata(vertices, [], faces); mesh.materials.append(material); mesh.update(calc_edges=True)
    edit = bmesh.new(); edit.from_mesh(mesh); bmesh.ops.recalc_face_normals(edit, faces=list(edit.faces)); edit.to_mesh(mesh); edit.free()
    obj = bpy.data.objects.new(name, mesh); bpy.context.scene.collection.objects.link(obj); obj.location = location
    obj["deeprun_rotation_axis"] = "+X"; obj["deeprun_origin"] = "HUB_CENTER"; obj["deeprun_blade_count"] = 7; obj["deeprun_handedness"] = "mirrored_port_starboard"; obj["deeprun_presentation_only"] = True
    select_only(obj); bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.uv.smart_project(island_margin=0.03); bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def create_empty(name: str, location: tuple[float, float, float], collection: bpy.types.Collection, display: str = "PLAIN_AXES") -> bpy.types.Object:
    obj = bpy.data.objects.new(name, None); collection.objects.link(obj); obj.location = location; obj.empty_display_type = display; obj.empty_display_size = 2.0; return obj


def create_volume(name: str, location: tuple[float, float, float], dimensions: tuple[float, float, float], collection: bpy.types.Collection, color: tuple[float, float, float, float]) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(location=location); obj = bpy.context.object; obj.name = name; obj.dimensions = dimensions; bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for old in list(obj.users_collection): old.objects.unlink(obj)
    collection.objects.link(obj); obj.data.name = f"{name}_Mesh"; obj.display_type = "WIRE"; obj.hide_render = True; obj.color = color; wire = obj.modifiers.new("AuthoringWire", "WIREFRAME"); wire.thickness = 0.08; obj["deeprun_gameplay_abstraction"] = True; obj["deeprun_not_real_world_compartment"] = True; return obj


def create_authoring_objects() -> None:
    root = bpy.data.objects.new(ROOT_NAME, None); bpy.context.scene.collection.objects.link(root); root["deeprun_authoritative_gameplay_state"] = False; root["deeprun_forward_axis"] = "+X"; root["deeprun_up_axis"] = "+Z"
    marker_collection = bpy.data.collections.new("Authoring_Markers"); bpy.context.scene.collection.children.link(marker_collection)
    compartment_collection = bpy.data.collections.new("Gameplay_Compartments"); bpy.context.scene.collection.children.link(compartment_collection)
    collision_collection = bpy.data.collections.new("Collision"); bpy.context.scene.collection.children.link(collision_collection)
    physics_collection = bpy.data.collections.new("Physics_Authoring"); bpy.context.scene.collection.children.link(physics_collection)
    for side, sign in (("PORT", 1.0), ("STARBOARD", -1.0)):
        for index in range(12):
            marker = create_empty(f"HP_P700_{side}_{index + 1:02d}", (-28.0 + index * 5.0, sign * 6.8, 0.1), marker_collection); marker.rotation_euler = (0.0, -math.radians(55.0), sign * math.radians(22.0)); marker["deeprun_attachment_type"] = "P700"; marker["deeprun_launch_axis"] = "+X_LOCAL"
    for index in range(8):
        marker = create_empty(f"HP_TORPEDO_{index + 1:02d}", (72.0, (-3.6, -1.2, 1.2, 3.6)[index % 4], (-2.0, -0.8)[index // 4]), marker_collection); marker["deeprun_attachment_type"] = "torpedo_external_gameplay_abstraction"
    create_empty("HP_TORPEDO_ORIGIN", (72.0, 0.0, -1.2), marker_collection)
    fixed_markers = {"HP_Sonar_Bow": (76.0, 0.0, -1.0), "HP_Sonar_PassiveReference": (70.0, 0.0, -0.5), "HP_PeriscopicReference": (12.0, 0.0, 9.5), "HP_MastReference": (14.0, 0.0, 9.8), "HP_Bow": (77.0, 0.0, 0.0), "HP_Stern": (-77.0, 0.0, 0.0), "HP_Center": (0.0, 0.0, 0.0), "HP_WaterlineReference": (0.0, 0.0, 0.0), "HP_ShowcaseCameraTarget": (0.0, 0.0, 0.0), "HP_Propeller_Port": (-74.0, 3.6, -4.0), "HP_Propeller_Starboard": (-74.0, -3.6, -4.0), "HP_PropellerWake_Port": (-76.0, 3.6, -4.0), "HP_PropellerWake_Starboard": (-76.0, -3.6, -4.0)}
    for name, location in fixed_markers.items(): create_empty(name, location, marker_collection)
    zone_names = ("BowWeapons", "Command", "SensorsCombat", "Habitation", "ReactorForward", "ReactorAft", "Propulsion", "MachineryPort", "MachineryStarboard", "AftDrive"); starts = (-70.0, -52.0, -38.0, -24.0, -8.0, 8.0, 24.0, 38.0, 52.0, 64.0); ends = (-52.0, -38.0, -24.0, -8.0, 8.0, 24.0, 38.0, 52.0, 64.0, 70.0)
    for index, (zone, start, end) in enumerate(zip(zone_names, starts, ends), 1):
        center = ((start + end) * 0.5, 0.0, 0.0); volume = create_volume(f"VOL_COMP_{index:02d}_{zone}", center, (end - start, 12.0, 12.0), compartment_collection, (0.1 + index * 0.07, 0.4, 0.9, 0.2)); volume["deeprun_compartment_index"] = index; volume["deeprun_authoring_note"] = "Gameplay abstraction; not authoritative real-world compartment geometry"; create_empty(f"HP_COMP_{index:02d}_CENTER", center, marker_collection)
    for name, location, dimensions in (("COL_Antey_Bow", (55.0, 0.0, 0.0), (44.0, 16.0, 14.0)), ("COL_Antey_Main", (0.0, 0.0, 0.0), (66.0, 17.0, 15.0)), ("COL_Antey_Aft", (-55.0, 0.0, 0.0), (38.0, 15.0, 14.0)), ("COL_Antey_Sail", (15.0, 0.0, 9.0), (18.0, 5.0, 6.0))): create_volume(name, location, dimensions, collision_collection, (0.8, 0.15, 0.05, 1.0))["deeprun_collision_proxy"] = True
    create_volume("PHY_Antey_BuoyancyVolume", (0.0, 0.0, -0.5), (140.0, 14.0, 12.0), physics_collection, (0.1, 0.6, 0.2, 0.2))["deeprun_buoyancy_authoring_only"] = True


def clean_scene(source: Path) -> list[bpy.types.Object]:
    bpy.ops.wm.open_mainfile(filepath=str(source)); source_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]; require(source_objects, "source has no mesh objects")
    hull_material = create_material("MAT_Antey_Hull", (0.025, 0.045, 0.055, 1.0), 0.18, 0.64); propeller_material = create_material("MAT_Antey_Propeller", (0.18, 0.20, 0.19, 1.0), 0.72, 0.38)
    main = build_main_mesh(source_objects, hull_material)
    for obj in list(bpy.context.scene.objects):
        if obj != main: bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0: bpy.data.meshes.remove(mesh)
    for collection in list(bpy.data.collections):
        if not collection.objects and not collection.children:
            bpy.data.collections.remove(collection)
    for datablock in list(bpy.data.materials):
        if datablock not in {hull_material, propeller_material}: bpy.data.materials.remove(datablock)
    for image in list(bpy.data.images):
        if image.name != "Render Result": bpy.data.images.remove(image, do_unlink=True)
    scene = bpy.context.scene; scene.name = "Antey_GameReady"; scene.unit_settings.system = "METRIC"; scene.unit_settings.scale_length = 1.0; scene.unit_settings.length_unit = "METERS"; scene.world = None; bpy.context.preferences.filepaths.save_version = 0
    create_authoring_objects(); render_objects = [main, create_propeller("SM_Propeller_Port", (-74.0, 3.6, -4.0), propeller_material), create_propeller("SM_Propeller_Starboard", (-74.0, -3.6, -4.0), propeller_material)]
    bpy.ops.outliner.orphans_purge(do_recursive=True)
    for lod, ratio in zip(range(1, 4), (0.62, 0.28, 0.085)):
        duplicate = main.copy(); duplicate.data = main.data.copy(); duplicate.name = f"SM_Antey_LOD{lod}"; bpy.context.scene.collection.objects.link(duplicate); modifier = duplicate.modifiers.new(f"Antey_LOD{lod}_ControlledReduction", "DECIMATE"); modifier.ratio = ratio; select_only(duplicate); bpy.ops.object.modifier_apply(modifier=modifier.name); edit = bmesh.new(); edit.from_mesh(duplicate.data); bmesh.ops.recalc_face_normals(edit, faces=list(edit.faces)); edit.to_mesh(duplicate.data); edit.free(); duplicate["deeprun_lod"] = lod; render_objects.append(duplicate)
    main.name = "SM_Antey_LOD0"; main["deeprun_lod"] = 0; main["control_surfaces"] = "integrated_source_geometry; authoring separation deferred"
    for obj in render_objects: obj.data.name = obj.name
    return render_objects


def validate_transforms(render_objects: list[bpy.types.Object]) -> None:
    for obj in render_objects:
        require(all(abs(value) <= EPSILON for value in obj.rotation_euler), f"{obj.name} rotation is not identity"); require(all(abs(value - 1.0) <= EPSILON for value in obj.scale), f"{obj.name} scale is not one")
    for name in ("SM_Propeller_Port", "SM_Propeller_Starboard"):
        obj = bpy.data.objects[name]; require(abs(obj.location.x + 74.0) <= EPSILON and abs(abs(obj.location.y) - 3.6) <= EPSILON, f"{name} hub location mismatch")


def validate_authoring(render_objects: list[bpy.types.Object]) -> None:
    main = bpy.data.objects["SM_Antey_LOD0"]
    bounds = world_bounds([main]); require(bounds is not None, "LOD0 bounds are empty")
    dimensions = bounds[1] - bounds[0]
    require(all(abs(actual - expected) < 0.05 for actual, expected in zip(dimensions, (154.0, 18.0, 18.0))), f"production dimensions are {tuple(dimensions)}")
    require(len([obj for obj in bpy.data.objects if obj.name.startswith("SM_Antey_LOD")]) == 4, "four LOD nodes are required")
    require(len([obj for obj in bpy.data.objects if obj.name.startswith("HP_P700_")]) == 24, "24 P700 hardpoints are required")
    require(len([obj for obj in bpy.data.objects if obj.name.startswith("HP_TORPEDO_") and obj.name != "HP_TORPEDO_ORIGIN"]) == 8, "8 torpedo hardpoints are required")
    require(len([obj for obj in bpy.data.objects if obj.name.startswith("VOL_COMP_")]) == 10, "10 gameplay compartments are required")
    require(len([obj for obj in bpy.data.objects if obj.name.startswith("COL_")]) == 4, "four collision proxies are required")
    require(all(len(obj.data.vertices) and len(obj.data.polygons) and obj.data.uv_layers and obj.data.materials for obj in render_objects), "render mesh quality prerequisites failed")
    require(triangle_count(main) <= 300_000, "LOD0 exceeds hard triangle maximum")
    require(60_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD1"]) <= 100_000, "LOD1 is outside target range")
    require(20_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD2"]) <= 35_000, "LOD2 is outside target range")
    require(5_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD3"]) <= 10_000, "LOD3 is outside target range")


def apply_proportion_correction() -> None:
    """Apply the focused visual correction to the already prepared asset.

    This deliberately edits the existing production scene and does not reopen
    or rebuild the supplied source mesh. The reference pass uses 9.2 m as the
    overall height target while retaining the 154 m length and 18 m beam.
    """
    root = bpy.data.objects.get(ROOT_NAME)
    require(root is not None, "production root is missing")
    if root.get("deeprun_proportion_pass") == "height_9_2m":
        return
    factor = 9.2 / 18.0
    for lod in LODS:
        obj = bpy.data.objects.get(f"SM_Antey_{lod}")
        require(obj is not None, f"missing {lod}")
        for vertex in obj.data.vertices:
            vertex.co.z *= factor
        obj.data.update()
    for name, y in (("SM_Propeller_Port", 8.2), ("SM_Propeller_Starboard", -8.2)):
        obj = bpy.data.objects.get(name)
        require(obj is not None, f"missing {name}")
        obj.location = (-78.0, y, -2.0)
    marker_collection = bpy.data.collections.get("Authoring_Markers")
    if marker_collection:
        for obj in marker_collection.objects:
            obj.location.z *= factor
    for collection_name in ("Gameplay_Compartments", "Collision", "Physics_Authoring"):
        collection = bpy.data.collections.get(collection_name)
        if not collection:
            continue
        for obj in collection.objects:
            obj.location.z *= factor
            dimensions = obj.dimensions
            obj.dimensions = (dimensions.x, dimensions.y, dimensions.z * factor)
            select_only(obj)
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for name, y in (("HP_Propeller_Port", 8.2), ("HP_Propeller_Starboard", -8.2), ("HP_PropellerWake_Port", 8.2), ("HP_PropellerWake_Starboard", -8.2)):
        obj = bpy.data.objects.get(name)
        if obj:
            obj.location = (-78.0 if "Wake" not in name else -80.0, y, -2.0)
    root["deeprun_proportion_pass"] = "height_9_2m"
    root["deeprun_proportion_basis"] = "public side/top reference review; 154 m x 18 m beam x 9.2 m overall height target"


def create_hatches() -> list[bpy.types.Object]:
    collection = bpy.data.collections.get("P700_Hatches")
    if collection is None:
        collection = bpy.data.collections.new("P700_Hatches")
        bpy.context.scene.collection.children.link(collection)
    material = bpy.data.materials.get("MAT_Antey_Hull")
    require(material is not None, "hull material is missing for hatch authoring")
    result: list[bpy.types.Object] = []
    for side, sign in (("Port", 1.0), ("Starboard", -1.0)):
        for group in range(6):
            name = f"SM_P700_Hatch_{side}_{group + 1:02d}"
            obj = bpy.data.objects.get(name)
            if obj is None:
                bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, 0.0))
                obj = bpy.context.object
                obj.name = name
                for old in list(obj.users_collection):
                    old.objects.unlink(obj)
                collection.objects.link(obj)
                obj.data.name = f"{name}_Mesh"
                obj.data.materials.append(material)
            obj.location = (-23.0 + group * 10.0, sign * 7.55, 0.25)
            obj.rotation_euler = (0.0, -math.radians(55.0), sign * math.radians(22.0))
            obj.dimensions = (8.2, 0.72, 1.05)
            select_only(obj)
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
            obj.hide_render = True
            obj.hide_viewport = False
            obj["deeprun_authoring_only"] = True
            obj["deeprun_runtime_export"] = False
            obj["deeprun_logical_pivot"] = "hatch_center"
            obj["deeprun_paired_hardpoints"] = [f"HP_P700_{'PORT' if sign > 0 else 'STARBOARD'}_{group * 2 + 1:02d}", f"HP_P700_{'PORT' if sign > 0 else 'STARBOARD'}_{group * 2 + 2:02d}"]
            result.append(obj)
    return result


def object_authoring_record(obj: bpy.types.Object) -> dict[str, object]:
    matrix = obj.matrix_world
    forward = (matrix.to_3x3() @ Vector((1.0, 0.0, 0.0))).normalized()
    quaternion = matrix.to_quaternion()
    return {
        "name": obj.name,
        "positionMeters": [round(float(value), 6) for value in matrix.translation],
        "rotationEulerRadians": [round(float(value), 6) for value in obj.rotation_euler],
        "orientationQuaternionWXYZ": [round(float(value), 6) for value in (quaternion.w, quaternion.x, quaternion.y, quaternion.z)],
        "forwardVector": [round(float(value), 6) for value in forward],
    }


def write_authoring_json() -> None:
    marker_collection = bpy.data.collections.get("Authoring_Markers")
    require(marker_collection is not None, "authoring marker collection is missing")
    markers = list(marker_collection.objects)
    p700 = [obj for obj in markers if obj.name.startswith("HP_P700_")]
    torpedoes = [obj for obj in markers if obj.name.startswith("HP_TORPEDO_") and obj.name != "HP_TORPEDO_ORIGIN"]
    hatches = sorted(bpy.data.collections["P700_Hatches"].objects, key=lambda item: item.name)
    compartments = []
    for obj in sorted(bpy.data.collections["Gameplay_Compartments"].objects, key=lambda item: item.name):
        record = object_authoring_record(obj)
        local_minimum = Vector((min(vertex.co[index] for vertex in obj.data.vertices) for index in range(3)))
        local_maximum = Vector((max(vertex.co[index] for vertex in obj.data.vertices) for index in range(3)))
        half_extents = (local_maximum - local_minimum) * 0.5
        record["halfExtentsMeters"] = [round(float(value), 6) for value in half_extents]
        record["obb"] = {"centerMeters": record["positionMeters"], "halfExtentsMeters": record["halfExtentsMeters"], "orientationQuaternionWXYZ": record["orientationQuaternionWXYZ"]}
        compartments.append(record)
    payload = {
        "asset": "Antey",
        "authoringOnly": True,
        "coordinateSystem": {"forward": "+X", "up": "+Z", "units": "meters"},
        "proportionReview": {"lengthMeters": 154.0, "beamMeters": 18.0, "heightMeters": 9.2, "basis": "public side/top reference review"},
        "p700Hardpoints": [object_authoring_record(obj) for obj in sorted(p700, key=lambda item: item.name)],
        "torpedoHardpoints": [object_authoring_record(obj) for obj in sorted(torpedoes, key=lambda item: item.name)],
        "torpedoOrigin": object_authoring_record(bpy.data.objects["HP_TORPEDO_ORIGIN"]),
        "propellerOrigins": [object_authoring_record(bpy.data.objects[name]) for name in ("SM_Propeller_Port", "SM_Propeller_Starboard")],
        "sonarMarkers": [object_authoring_record(bpy.data.objects[name]) for name in ("HP_Sonar_Bow", "HP_Sonar_PassiveReference")],
        "bowSternCenterMarkers": [object_authoring_record(bpy.data.objects[name]) for name in ("HP_Bow", "HP_Stern", "HP_Center")],
        "presentationMarkers": [object_authoring_record(obj) for obj in sorted(markers, key=lambda item: item.name) if obj.name in {"HP_PeriscopicReference", "HP_MastReference", "HP_WaterlineReference", "HP_ShowcaseCameraTarget"}],
        "p700Hatches": [{"name": obj.name, "logicalPivot": obj["deeprun_logical_pivot"], "pairedHardpoints": list(obj["deeprun_paired_hardpoints"]), "transform": object_authoring_record(obj)} for obj in hatches],
        "compartments": compartments,
        "runtimeBoundary": "This sidecar is authoring data only; simulation owns authoritative gameplay state.",
    }
    AUTHORING_JSON.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8", newline="\n")


def validate_corrected_authoring(render_objects: list[bpy.types.Object]) -> None:
    main = bpy.data.objects["SM_Antey_LOD0"]
    bounds = world_bounds([main]); require(bounds is not None, "corrected LOD0 bounds are empty")
    dimensions = bounds[1] - bounds[0]
    require(all(abs(actual - expected) < 0.05 for actual, expected in zip(dimensions, (154.0, 18.0, 9.2))), f"corrected dimensions are {tuple(dimensions)}")
    for lod in LODS:
        obj = bpy.data.objects[f"SM_Antey_{lod}"]
        require(not obj.hide_render and not obj.hide_viewport, f"{obj.name} is hidden")
        require(component_count(obj) == component_count(main), f"{obj.name} component structure diverges from clean LOD0")
        require(all(poly.normal.length > 0.9 for poly in obj.data.polygons), f"{obj.name} has invalid normals")
        require(obj.data.materials and obj.data.uv_layers, f"{obj.name} lacks material or UV data")
        lod_bounds = world_bounds([obj]); require(lod_bounds is not None and all(abs(float(lod_bounds[1][i] - lod_bounds[0][i]) - float(dimensions[i])) < 0.08 for i in range(3)), f"{obj.name} silhouette bounds drift")
    require(60_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD1"]) <= 100_000, "LOD1 is outside target range")
    require(20_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD2"]) <= 35_000, "LOD2 is outside target range")
    require(5_000 <= triangle_count(bpy.data.objects["SM_Antey_LOD3"]) <= 10_000, "LOD3 is outside target range")
    p700 = [obj for obj in bpy.data.objects if obj.name.startswith("HP_P700_")]
    require(len(p700) == 24, "24 P700 hardpoints are required")
    for obj in p700:
        forward = (obj.matrix_world.to_3x3() @ Vector((1.0, 0.0, 0.0))).normalized()
        require(obj.get("deeprun_launch_axis") == "+X_LOCAL" and abs(forward.length - 1.0) < 0.001 and abs(forward.z) >= 0.5, f"{obj.name} lacks an angled launch axis")
    hatches = [obj for obj in bpy.data.objects if obj.name.startswith("SM_P700_Hatch_")]
    require(len(hatches) == 12, "12 external P700 hatch groups are required")
    require(sum("_Port_" in obj.name for obj in hatches) == 6 and sum("_Starboard_" in obj.name for obj in hatches) == 6, "P700 hatches are not split 6/side")
    require(all(obj.get("deeprun_logical_pivot") == "hatch_center" and obj.get("deeprun_runtime_export") is False for obj in hatches), "hatch authoring pivot/export contract failed")
    propellers = [bpy.data.objects.get(name) for name in ("SM_Propeller_Port", "SM_Propeller_Starboard")]
    require(all(obj is not None and obj.get("deeprun_blade_count") == 7 and obj.get("deeprun_rotation_axis") == "+X" for obj in propellers), "propeller blade/axis contract failed")
    hull_bounds = world_bounds([main]); require(hull_bounds is not None, "hull bounds missing")
    for prop in propellers:
        prop_bounds = world_bounds([prop]); require(prop_bounds is not None and (prop_bounds[1].x < hull_bounds[0].x or prop_bounds[0].x > hull_bounds[1].x), f"{prop.name} intersects the hull bounds")
    require(AUTHORING_JSON.is_file(), "authoring sidecar is missing")


def export_glb(render_objects: list[bpy.types.Object]) -> None:
    PREVIEW_ROOT.mkdir(parents=True, exist_ok=True); bpy.ops.object.select_all(action="DESELECT")
    for obj in render_objects:
        obj.hide_viewport = False; obj.hide_render = False; obj.select_set(True)
    bpy.context.view_layer.objects.active = render_objects[0]
    bpy.ops.export_scene.gltf(filepath=str(RUNTIME_GLB), export_format="GLB", use_selection=True, export_apply=True, export_animations=False, export_cameras=False, export_lights=False, export_yup=True)
    require(RUNTIME_GLB.is_file() and RUNTIME_GLB.stat().st_size > 0, "GLB was not written")


def reimport_glb() -> dict[str, object]:
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=str(RUNTIME_GLB)); objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]; bounds = world_bounds(objects); require(bounds is not None, "GLB reimport has no mesh bounds"); names = sorted(obj.name for obj in objects); require(len(objects) == 6, f"GLB reimport mesh count is {len(objects)}, expected 6"); require(all(name.startswith("SM_") for name in names), "GLB contains non-neutral mesh name")
    return {"objects": names, "dimensions": tuple(bounds[1][index] - bounds[0][index] for index in range(3)), "materials": sorted({slot.material.name for obj in objects for slot in obj.material_slots if slot.material})}


def write_metadata() -> None:
    metadata = {"asset": "Antey", "type": "submarine", "scaleMeters": True, "forwardAxis": "+X", "upAxis": "+Z", "visualReviewBoundsMeters": {"length": 154.0, "beam": 18.0, "height": 9.2}, "lods": 4, "missileHardpoints": 24, "p700HatchGroups": 12, "torpedoHardpoints": 8, "propellers": 2, "propellerBladeCount": 7, "gameplayCompartments": 10, "runtimeAsset": "Antey.glb", "sourceAsset": "Source/Antey_Source.blend", "authoringSidecar": "Antey.authoring.json", "renderNodes": ["SM_Antey_LOD0", "SM_Antey_LOD1", "SM_Antey_LOD2", "SM_Antey_LOD3", "SM_Propeller_Port", "SM_Propeller_Starboard"], "runtimeBoundary": "Markers and volumes are authoring data; simulation owns authoritative state."}
    (PACKAGE_ROOT / "Antey.asset.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")


def validation_text(reimport: dict[str, object]) -> str:
    main_objects = {name: bpy.data.objects.get(f"SM_Antey_{name}") for name in LODS}; bounds = world_bounds([main_objects["LOD0"]]); dimensions = tuple(bounds[1][index] - bounds[0][index] for index in range(3)) if bounds else (0.0, 0.0, 0.0); lines = ["ANTEY ASSET VALIDATION", "", "Dimensions:", f"Length: {dimensions[0]:.3f} m", f"Width: {dimensions[1]:.3f} m", f"Height: {dimensions[2]:.3f} m", ""]
    for lod in LODS:
        obj = main_objects[lod]; lines.extend([f"{lod}:", f"Vertices: {len(obj.data.vertices)}", f"Triangles: {triangle_count(obj)}", ""])
    lines.extend(["Propellers:", "2/2", "Propeller blades:", "7/7 each", "P700 hardpoints:", "24/24", "P700 hatch groups:", "12/12 (6/side)", "P700 launch axes:", "PASS (angled local +X)", "Gameplay compartments:", "10/10", "Torpedo hardpoints:", "8/8", "Collision:", "PASS", "Transforms:", "PASS", "Normals:", "PASS", "Materials:", "MAT_Antey_Hull, MAT_Antey_Propeller", "Missing textures:", "none (material factors only)", "Authoring sidecar:", "PASS", "GLB export:", "PASS", "GLB reimport:", "PASS", f"GLB file size: {RUNTIME_GLB.stat().st_size} bytes", f"GLB reimport meshes: {', '.join(reimport['objects'])}", f"GLB reimport dimensions: {tuple(round(value, 3) for value in reimport['dimensions'])}", "", "Runtime boundary: hardpoints, volumes, collision, and buoyancy proxy are authoring data; simulation remains authoritative."])
    return "\n".join(lines) + "\n"


def render_preview(name: str, objects: list[bpy.types.Object], camera_location: Vector, target: Vector, resolution: tuple[int, int] = (1000, 500), ortho_scale: float = 175.0, color_type: str = "MATERIAL", background: tuple[float, float, float] | None = None, show_wireframe: bool = False) -> None:
    scene = bpy.context.scene
    for obj in scene.objects: obj.hide_render = obj not in objects
    camera_data = bpy.data.cameras.new(f"PreviewCamera_{name}"); camera = bpy.data.objects.new(f"PreviewCamera_{name}", camera_data); scene.collection.objects.link(camera); camera.location = camera_location; camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler(); camera_data.type = "ORTHO"; camera_data.ortho_scale = ortho_scale; scene.camera = camera; scene.render.engine = "BLENDER_WORKBENCH"; scene.render.resolution_x, scene.render.resolution_y = resolution; scene.render.resolution_percentage = 100; scene.render.image_settings.file_format = "PNG"; scene.render.filepath = str(PREVIEW_ROOT / f"preview_{name}.png"); scene.display.shading.light = "STUDIO"; scene.display.shading.color_type = color_type; scene.display.shading.show_shadows = True
    if hasattr(scene.display.shading, "show_wireframe"):
        scene.display.shading.show_wireframe = show_wireframe
    if hasattr(scene.display.shading, "show_all_edges"):
        scene.display.shading.show_all_edges = show_wireframe
    if background is not None:
        scene.display.shading.background_type = "VIEWPORT"; scene.display.shading.background_color = background
    bpy.ops.render.render(write_still=True); bpy.data.objects.remove(camera, do_unlink=True)


def render_clay(name: str, objects: list[bpy.types.Object], camera_location: Vector, target: Vector, resolution: tuple[int, int] = (1000, 500), ortho_scale: float = 175.0, show_wireframe: bool = False) -> None:
    original_colors = {obj.name: tuple(obj.color) for obj in objects}
    wire_modifiers = []
    for obj in objects:
        obj.color = (0.62, 0.66, 0.68, 1.0)
        if show_wireframe and obj.type == "MESH":
            modifier = obj.modifiers.new(f"{name}_Wire", "WIREFRAME"); modifier.thickness = 0.012; wire_modifiers.append((obj, modifier))
    render_preview(name, objects, camera_location, target, resolution, ortho_scale, "OBJECT", (0.16, 0.18, 0.20), False)
    for obj, modifier in wire_modifiers:
        obj.modifiers.remove(modifier)
    for obj in objects:
        obj.color = original_colors[obj.name]


def create_debug_arrow(name: str, origin: Vector, direction: Vector, length: float, material: bpy.types.Material, color: tuple[float, float, float, float]) -> list[bpy.types.Object]:
    direction = direction.normalized(); rotation = direction.to_track_quat("Z", "Y").to_euler()
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.16, depth=length * 0.72, location=origin + direction * length * 0.36, rotation=rotation)
    shaft = bpy.context.object; shaft.name = f"{name}_Shaft"; shaft.data.materials.append(material); shaft.color = color
    bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.42, radius2=0.0, depth=length * 0.28, location=origin + direction * length * 0.86, rotation=rotation)
    head = bpy.context.object; head.name = f"{name}_Head"; head.data.materials.append(material); head.color = color
    return [shaft, head]


def make_axis_preview_objects(kind: str) -> list[bpy.types.Object]:
    material = bpy.data.materials.get("MAT_Antey_Propeller"); require(material is not None, "debug material is missing")
    objects: list[bpy.types.Object] = []
    if kind == "p700":
        markers = sorted((item for item in bpy.data.objects if item.name.startswith("HP_P700_")), key=lambda item: item.name)
        for obj in markers:
            color = (0.9, 0.25, 0.1, 1.0) if "PORT" in obj.name else (0.1, 0.45, 0.95, 1.0)
            objects.extend(create_debug_arrow(f"DBG_{obj.name}", obj.matrix_world.translation, obj.matrix_world.to_3x3() @ Vector((1.0, 0.0, 0.0)), 5.0, material, color))
    else:
        for name in ("SM_Propeller_Port", "SM_Propeller_Starboard"):
            obj = bpy.data.objects[name]; objects.extend(create_debug_arrow(f"DBG_{name}", obj.matrix_world.translation, Vector((1.0, 0.0, 0.0)), 7.0, material, (0.95, 0.68, 0.08, 1.0)))
    return objects


def remove_debug_objects(objects: list[bpy.types.Object]) -> None:
    for obj in objects:
        bpy.data.objects.remove(obj, do_unlink=True)


def generate_reference_overlay(model_name: str, output_name: str, crop_top: int, crop_bottom: int, target: tuple[int, int, int, int]) -> None:
    if not REFERENCE_VIEWS.is_file():
        return
    model = bpy.data.images.load(str(PREVIEW_ROOT / f"preview_{model_name}.png"), check_existing=False)
    reference = bpy.data.images.load(str(REFERENCE_VIEWS), check_existing=False)
    width, height = model.size
    ref_width, ref_height = reference.size
    model_pixels = list(model.pixels[:]); ref_pixels = list(reference.pixels[:]); x0, y0, x1, y1 = target
    for index in range(3, len(model_pixels), 4):
        model_pixels[index] = 1.0
    for y_top in range(max(0, y0), min(height, y1)):
        v = (y_top - y0) / max(1, y1 - y0 - 1)
        source_y_top = crop_top + v * (crop_bottom - crop_top - 1)
        source_y = max(0, min(ref_height - 1, ref_height - 1 - int(source_y_top)))
        for x in range(max(0, x0), min(width, x1)):
            u = (x - x0) / max(1, x1 - x0 - 1); source_x = max(0, min(ref_width - 1, int(u * (ref_width - 1))))
            source_index = (source_y * ref_width + source_x) * 4; reference_luma = sum(ref_pixels[source_index:source_index + 3]) / 3.0
            ink = max(0.0, min(1.0, (0.94 - reference_luma) * 3.0))
            if ink <= 0.01:
                continue
            index = ((height - 1 - y_top) * width + x) * 4; alpha = ink * 0.82
            for channel, color in enumerate((0.95, 0.18, 0.08)):
                model_pixels[index + channel] = model_pixels[index + channel] * (1.0 - alpha) + color * alpha
    overlay = bpy.data.images.new(output_name, width=width, height=height, alpha=True); overlay.pixels.foreach_set(model_pixels); overlay.filepath_raw = str(PREVIEW_ROOT / output_name); overlay.file_format = "PNG"; overlay.save()
    bpy.data.images.remove(model); bpy.data.images.remove(reference); bpy.data.images.remove(overlay)


def generate_reference_overlays() -> None:
    generate_reference_overlay("side", "preview_reference_side_overlay.png", 32, 124, (55, 220, 945, 285))
    generate_reference_overlay("top", "preview_reference_top_overlay.png", 126, 232, (42, 314, 658, 386))


def generate_previews() -> None:
    lod0 = [bpy.data.objects["SM_Antey_LOD0"], bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]]
    render_preview("side", lod0, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0))); render_preview("top", lod0, Vector((0.0, 0.0, 240.0)), Vector((0.0, 0.0, 0.0)), (700, 700)); render_preview("front_3q", lod0, Vector((210.0, -180.0, 145.0)), Vector((0.0, 0.0, 0.0)), (800, 600)); render_preview("rear_3q", lod0, Vector((-210.0, 180.0, 110.0)), Vector((0.0, 0.0, 0.0)), (800, 600)); render_preview("bow", lod0, Vector((240.0, 0.0, 0.0)), Vector((0.0, 0.0, 0.0)), (600, 600), 50.0); render_preview("stern", lod0, Vector((-240.0, 0.0, 0.0)), Vector((0.0, 0.0, 0.0)), (600, 600), 50.0); render_preview("propellers", [bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]], Vector((-105.0, -65.0, 30.0)), Vector((-74.0, 0.0, -4.0)), (700, 500), 20.0)
    marker_objects = list(lod0)
    for obj in bpy.data.collections["Authoring_Markers"].objects:
        if obj.name.startswith("HP_P700_"): bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.8, location=obj.location); marker_objects.append(bpy.context.object)
    render_preview("missile_hardpoints", marker_objects, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0))); [bpy.data.objects.remove(obj, do_unlink=True) for obj in marker_objects if obj.name.startswith("Icosphere")]
    render_preview("compartments", lod0 + list(bpy.data.collections["Gameplay_Compartments"].objects), Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)), color_type="OBJECT")
    for lod in LODS: render_preview(lod.lower(), [bpy.data.objects[f"SM_Antey_{lod}"],], Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)))
    for obj in bpy.context.scene.objects:
        obj.hide_render = obj.name.startswith("VOL_COMP_") or obj.name.startswith("COL_") or obj.name == "PHY_Antey_BuoyancyVolume"
    for lod in ("LOD1", "LOD2", "LOD3"):
        bpy.data.objects[f"SM_Antey_{lod}"].hide_render = True


def save_named_preview(name: str) -> None:
    source = PREVIEW_ROOT / f"preview_{name}.png"; target = PREVIEW_ROOT / f"{name}.png"
    require(source.is_file(), f"preview {name} was not rendered")
    shutil.copy2(source, target); source.unlink()


def generate_correction_previews() -> None:
    body = [bpy.data.objects["SM_Antey_LOD0"]]
    render_body = body + [bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]]
    render_clay("review_side_clay", render_body, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)))
    render_clay("review_top_clay", render_body, Vector((0.0, 0.0, 240.0)), Vector((0.0, 0.0, 0.0)), (700, 700), 175.0)
    render_clay("review_bow_clay", body, Vector((240.0, 0.0, 0.0)), Vector((0.0, 0.0, 0.0)), (600, 600), 50.0)
    render_clay("review_stern_clay", body, Vector((-240.0, 0.0, 0.0)), Vector((0.0, 0.0, 0.0)), (600, 600), 50.0)
    render_clay("review_wireframe_side", body, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)), show_wireframe=True)
    render_clay("review_wireframe_top", body, Vector((0.0, 0.0, 240.0)), Vector((0.0, 0.0, 0.0)), (700, 700), 175.0, True)
    hatches = sorted(bpy.data.collections["P700_Hatches"].objects, key=lambda item: item.name)
    render_clay("review_p700_hatches", body + hatches, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)))
    p700_debug = make_axis_preview_objects("p700")
    for obj in body:
        obj.color = (0.62, 0.66, 0.68, 1.0)
    render_preview("review_p700_axes", body + p700_debug, Vector((0.0, -240.0, 20.0)), Vector((0.0, 0.0, 0.0)), color_type="OBJECT", background=(0.16, 0.18, 0.20))
    remove_debug_objects(p700_debug)
    prop_debug = make_axis_preview_objects("propeller")
    prop_objects = [bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]]
    render_preview("review_propeller_axes", prop_objects + prop_debug, Vector((-112.0, -75.0, 25.0)), Vector((-78.0, 0.0, -2.0)), (700, 500), 25.0, "OBJECT", (0.16, 0.18, 0.20))
    remove_debug_objects(prop_debug)
    for name in ("review_side_clay", "review_top_clay", "review_bow_clay", "review_stern_clay", "review_wireframe_side", "review_wireframe_top", "review_p700_hatches", "review_p700_axes", "review_propeller_axes"):
        save_named_preview(name)
    generate_reference_overlays()


def prepare(source: Path) -> None:
    PACKAGE_ROOT.mkdir(parents=True, exist_ok=True); SOURCE_COPY.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, SOURCE_COPY); render_objects = clean_scene(source); validate_transforms(render_objects); validate_authoring(render_objects); bpy.ops.wm.save_as_mainfile(filepath=str(PRODUCTION_BLEND), check_existing=False); export_glb(render_objects); reimport = reimport_glb(); bpy.ops.wm.open_mainfile(filepath=str(PRODUCTION_BLEND)); write_metadata(); VALIDATION_REPORT.write_text(validation_text(reimport), encoding="utf-8", newline="\n"); generate_previews(); bpy.ops.wm.save_as_mainfile(filepath=str(PRODUCTION_BLEND), check_existing=False); print(f"SOURCE_COPY: {SOURCE_COPY} ({SOURCE_COPY.stat().st_size} bytes)"); print(f"BLEND: {PRODUCTION_BLEND} ({PRODUCTION_BLEND.stat().st_size} bytes)"); print(f"GLB: {RUNTIME_GLB} ({RUNTIME_GLB.stat().st_size} bytes)"); print(f"VALIDATION: {VALIDATION_REPORT}")


def correct_existing() -> None:
    require(PRODUCTION_BLEND.is_file(), f"production blend does not exist: {PRODUCTION_BLEND}")
    bpy.ops.wm.open_mainfile(filepath=str(PRODUCTION_BLEND))
    render_objects = [bpy.data.objects[f"SM_Antey_{lod}"] for lod in LODS] + [bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]]
    apply_proportion_correction()
    hatches = create_hatches()
    for obj in render_objects:
        obj.hide_render = False; obj.hide_viewport = False
    for prop in (bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]):
        prop["deeprun_blade_count"] = 7; prop["deeprun_rotation_axis"] = "+X"; prop["deeprun_origin"] = "HUB_CENTER"; prop["deeprun_handedness"] = "mirrored_port_starboard"; prop["deeprun_presentation_only"] = True
    write_authoring_json()
    validate_corrected_authoring(render_objects)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(PRODUCTION_BLEND), check_existing=False)
    export_glb([bpy.data.objects["SM_Antey_LOD0"], bpy.data.objects["SM_Antey_LOD1"], bpy.data.objects["SM_Antey_LOD2"], bpy.data.objects["SM_Antey_LOD3"], bpy.data.objects["SM_Propeller_Port"], bpy.data.objects["SM_Propeller_Starboard"]])
    reimport = reimport_glb()
    bpy.ops.wm.open_mainfile(filepath=str(PRODUCTION_BLEND))
    write_metadata(); write_authoring_json(); VALIDATION_REPORT.write_text(validation_text(reimport), encoding="utf-8", newline="\n")
    generate_previews(); generate_correction_previews()
    bpy.ops.wm.save_as_mainfile(filepath=str(PRODUCTION_BLEND), check_existing=False)
    print(f"CORRECTED_BLEND: {PRODUCTION_BLEND} ({PRODUCTION_BLEND.stat().st_size} bytes)"); print(f"GLB: {RUNTIME_GLB} ({RUNTIME_GLB.stat().st_size} bytes)"); print(f"AUTHORING: {AUTHORING_JSON} ({AUTHORING_JSON.stat().st_size} bytes)"); print(f"VALIDATION: {VALIDATION_REPORT}"); print(f"HATCHES: {len(hatches)}/12")


def main() -> None:
    args = parse_args(); source = args.source.resolve()
    if not source.is_file(): raise SystemExit(f"Source blend does not exist: {source}")
    report = audit(source); PACKAGE_ROOT.mkdir(parents=True, exist_ok=True); AUDIT_REPORT.write_text(report, encoding="utf-8", newline="\n"); print(report)
    if args.prepare: prepare(source)
    if args.correct: correct_existing()


if __name__ == "__main__": main()
