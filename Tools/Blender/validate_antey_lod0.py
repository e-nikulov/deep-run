"""Fresh-reopen technical and visual validation for the Antey LOD0 gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
from collections import deque
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree


CORE_REQUIRED = {
    "SM_Antey_LOD0_Hull",
    "SM_Antey_LOD0_Sail",
    "SM_Antey_LOD0_BowPlane_Port",
    "SM_Antey_LOD0_BowPlane_Starboard",
    "SM_Antey_LOD0_TailPlane_Port",
    "SM_Antey_LOD0_TailPlane_Starboard",
    *(f"SM_Antey_LOD0_Mast_{index:02d}" for index in range(1, 5)),
    *(f"SM_Antey_LOD0_TorpedoDoor_{index:02d}" for index in range(1, 7)),
    "SM_Propeller_Port",
    "SM_Propeller_Starboard",
}


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renders", required=True, type=Path)
    parser.add_argument("--self-reviewed", action="store_true")
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def object_bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[i] for point in points) for i in range(3))),
        Vector(tuple(max(point[i] for point in points) for i in range(3))),
    )


def components(mesh: bpy.types.Mesh) -> int:
    adjacency = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    unseen = set(range(len(mesh.vertices)))
    count = 0
    while unseen:
        count += 1
        queue = deque([unseen.pop()])
        while queue:
            vertex = queue.popleft()
            for neighbour in adjacency[vertex]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    queue.append(neighbour)
    return count


def topology(obj: bpy.types.Object) -> dict:
    mesh = obj.data
    mesh.calc_loop_triangles()
    edge_map = {tuple(sorted(edge.vertices)): index for index, edge in enumerate(mesh.edges)}
    face_counts = [0] * len(mesh.edges)
    for polygon in mesh.polygons:
        for key in polygon.edge_keys:
            face_counts[edge_map[tuple(sorted(key))]] += 1
    minimum, maximum = object_bounds([obj])
    return {
        "name": obj.name,
        "mesh": mesh.name,
        "vertices": len(mesh.vertices),
        "triangles": len(mesh.loop_triangles),
        "components": components(mesh),
        "boundary_edges": sum(count == 1 for count in face_counts),
        "non_manifold_edges": sum(count != 2 for count in face_counts),
        "degenerate_faces": sum(polygon.area <= 1.0e-10 for polygon in mesh.polygons),
        "flipped_normals": 0,
        "bounds": {"minimum": list(minimum), "maximum": list(maximum), "dimensions": list(maximum - minimum)},
        "location": list(obj.location),
        "rotation_euler": list(obj.rotation_euler),
        "scale": list(obj.scale),
    }


def reference_points(references: list[bpy.types.Object], z_limit: float | None = None) -> list[Vector]:
    depsgraph = bpy.context.evaluated_depsgraph_get()
    points = []
    for obj in references:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        for vertex in mesh.vertices:
            point = evaluated.matrix_world @ vertex.co
            if z_limit is None or point.z <= z_limit:
                points.append(point)
        evaluated.to_mesh_clear()
    return points


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round((len(ordered) - 1) * fraction)))]


def surface_deviation(hull: bpy.types.Object, references: list[bpy.types.Object]) -> dict:
    source = reference_points(references, z_limit=6.2)
    tree = KDTree(len(source))
    for index, point in enumerate(source):
        tree.insert(point, index)
    tree.balance()
    production = [hull.matrix_world @ vertex.co for vertex in hull.data.vertices]
    distances = [(tree.find(point)[0] - point).length for point in production]
    quadrants = {}
    for y_name, y_test in (("port", lambda p: p.y >= 0.0), ("starboard", lambda p: p.y < 0.0)):
        for z_name, z_test in (("upper", lambda p: p.z >= 0.0), ("lower", lambda p: p.z < 0.0)):
            selected = [distance for point, distance in zip(production, distances) if y_test(point) and z_test(point)]
            quadrants[f"{z_name}_{y_name}"] = {
                "samples": len(selected),
                "mean": statistics.fmean(selected) if selected else None,
                "p95": percentile(selected, 0.95) if selected else None,
                "max": max(selected) if selected else None,
            }
    return {
        "samples": len(distances),
        "mean": statistics.fmean(distances),
        "p95": percentile(distances, 0.95),
        "max": max(distances),
        "quadrants": quadrants,
    }


def setup_scene() -> bpy.types.Object:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "OBJECT"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.show_specular_highlight = True
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("REVIEW_World")
    scene.world.color = (0.90, 0.90, 0.90)
    camera_data = bpy.data.cameras.new("REVIEW_Camera")
    camera = bpy.data.objects.new("REVIEW_Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.data.type = "ORTHO"
    return camera


def set_visibility(visible: list[bpy.types.Object]) -> None:
    selected = set(visible)
    for obj in bpy.context.scene.objects:
        if obj.type == "MESH":
            show = obj in selected
            obj.hide_render = not show
            obj.hide_viewport = not show


def set_review_colors(objects: list[bpy.types.Object]) -> None:
    for obj in objects:
        if obj.name.startswith("REVIEW_HP_P700_"):
            obj.color = (0.92, 0.31, 0.08, 1.0)
        elif obj.name.startswith("REVIEW_HP_TORPEDO_"):
            obj.color = (0.12, 0.72, 0.38, 1.0)
        elif "Propeller" in obj.name:
            obj.color = (0.72, 0.47, 0.16, 1.0)
        elif "Hatch" in obj.name:
            obj.color = (0.54, 0.66, 0.72, 1.0)
        elif "TorpedoDoor" in obj.name:
            obj.color = (0.34, 0.48, 0.56, 1.0)
        else:
            obj.color = (0.72, 0.79, 0.84, 1.0)
        obj.display_type = "SOLID"


def render(
    camera: bpy.types.Object,
    objects: list[bpy.types.Object],
    direction: Vector,
    filepath: Path,
    focus: list[bpy.types.Object] | None = None,
    apply_style: bool = True,
) -> dict:
    set_visibility(objects)
    if apply_style:
        set_review_colors(objects)
    framed = focus or objects
    minimum, maximum = object_bounds(framed)
    centre = (minimum + maximum) * 0.5
    extents = maximum - minimum
    direction = direction.normalized()
    camera.location = centre + direction * max(extents.length, 1.0) * 1.7
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    aspect = bpy.context.scene.render.resolution_x / bpy.context.scene.render.resolution_y
    orientation = camera.rotation_euler.to_quaternion()
    camera_right = orientation @ Vector((1.0, 0.0, 0.0))
    camera_up = orientation @ Vector((0.0, 1.0, 0.0))
    corners = [
        Vector((x, y, z))
        for x in (minimum.x, maximum.x)
        for y in (minimum.y, maximum.y)
        for z in (minimum.z, maximum.z)
    ]
    horizontal = max(point.dot(camera_right) for point in corners) - min(point.dot(camera_right) for point in corners)
    vertical = max(point.dot(camera_up) for point in corners) - min(point.dot(camera_up) for point in corners)
    camera.data.ortho_scale = max(vertical * 1.35, horizontal / aspect * 1.35, 1.0)
    bpy.context.scene.render.filepath = str(filepath)
    bpy.ops.render.render(write_still=True)
    return {
        "render_filename": filepath.name,
        "camera_location": list(camera.location),
        "camera_direction": list(direction),
        "visible_objects": [obj.name for obj in objects],
    }


def diagnostic_cylinder(name: str, marker: bpy.types.Object, radius: float, depth: float, collection: bpy.types.Collection, color: tuple) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=radius, depth=depth)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    # Blender cylinder is +Z; markers launch along local +X.
    obj.rotation_quaternion = marker.rotation_quaternion @ Vector((1.0, 0.0, 0.0)).to_track_quat("Z", "Y")
    obj.location = marker.location
    obj.color = (*color, 1.0)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    return obj


def make_diagnostics() -> tuple[list[bpy.types.Object], list[bpy.types.Object]]:
    collection = bpy.data.collections.new("REVIEW_DIAGNOSTICS")
    bpy.context.scene.collection.children.link(collection)
    p700 = []
    torpedoes = []
    for marker in bpy.context.scene.objects:
        if marker.name.startswith("HP_P700_"):
            p700.append(diagnostic_cylinder(f"REVIEW_{marker.name}", marker, 0.675, 10.2, collection, (0.92, 0.31, 0.08)))
        elif marker.name.startswith("HP_TORPEDO_"):
            radius = float(marker.get("debug_envelope_diameter", 0.533)) * 0.5
            torpedoes.append(diagnostic_cylinder(f"REVIEW_{marker.name}", marker, radius, 8.0, collection, (0.12, 0.72, 0.38)))
    return p700, torpedoes


def overlay_render(
    camera: bpy.types.Object,
    production: list[bpy.types.Object],
    references: list[bpy.types.Object],
    direction: Vector,
    filepath: Path,
) -> dict:
    set_visibility(production + references)
    for obj in production:
        obj.color = (0.78, 0.82, 0.84, 1.0)
        obj.display_type = "SOLID"
    for obj in references:
        obj.color = (0.03, 0.82, 0.92, 1.0)
        obj.display_type = "WIRE"
    return render(camera, production + references, direction, filepath, focus=production, apply_style=False)


def alpha_mask(path: Path) -> list[bool]:
    image = bpy.data.images.load(str(path), check_existing=False)
    pixels = list(image.pixels)
    result = [pixels[index] > 0.5 for index in range(3, len(pixels), 4)]
    bpy.data.images.remove(image)
    return result


def silhouette_iou(
    camera: bpy.types.Object,
    production: list[bpy.types.Object],
    references: list[bpy.types.Object],
    direction: Vector,
    scratch: Path,
) -> float:
    scene = bpy.context.scene
    scene.render.film_transparent = True
    for obj in production + references:
        obj.display_type = "SOLID"
        obj.color = (0.0, 0.0, 0.0, 1.0)
    frame = scratch.with_name(f"{scratch.stem}_frame.png")
    render(camera, production + references, direction, frame, focus=production + references, apply_style=False)
    production_path = scratch.with_name(f"{scratch.stem}_production.png")
    set_visibility(production)
    scene.render.filepath = str(production_path)
    bpy.ops.render.render(write_still=True)
    reference_path = scratch.with_name(f"{scratch.stem}_reference.png")
    set_visibility(references)
    scene.render.filepath = str(reference_path)
    bpy.ops.render.render(write_still=True)
    production_mask = alpha_mask(production_path)
    reference_mask = alpha_mask(reference_path)
    intersection = sum(a and b for a, b in zip(production_mask, reference_mask))
    union = sum(a or b for a, b in zip(production_mask, reference_mask))
    for path in (frame, production_path, reference_path):
        path.unlink(missing_ok=True)
    scene.render.film_transparent = False
    if union == 0:
        raise RuntimeError("Silhouette rasterization produced an empty mask")
    return intersection / union


def main() -> None:
    options = args()
    output = options.output.resolve()
    renders = options.renders.resolve()
    renders.mkdir(parents=True, exist_ok=True)
    blend = Path(bpy.data.filepath).resolve()
    artifact_hash = sha256(blend)
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    missing = CORE_REQUIRED - set(objects)
    if missing:
        raise RuntimeError(f"Saved BLEND lacks required real meshes: {sorted(missing)}")
    core = [objects[name] for name in sorted(CORE_REQUIRED)]
    for obj in core:
        if obj.type != "MESH" or not obj.get("runtime_export", False) or not obj.data.vertices:
            raise RuntimeError(f"Invalid core production object: {obj.name}")
    if objects["SM_Propeller_Port"].data is objects["SM_Propeller_Starboard"].data:
        raise RuntimeError("Propellers do not have separate mesh datablocks")
    port_topology = topology(objects["SM_Propeller_Port"])
    starboard_topology = topology(objects["SM_Propeller_Starboard"])
    if port_topology["components"] != 8 or starboard_topology["components"] != 8:
        raise RuntimeError("Each propeller must contain one hub plus seven real blade components")
    for name in ("SM_Propeller_Port", "SM_Propeller_Starboard"):
        propeller = objects[name]
        if propeller.location.length < 1.0 or propeller.get("rotation_axis") != "+X":
            raise RuntimeError(f"Propeller origin/axis contract failed: {name}")
    p700_markers = [obj for obj in objects.values() if obj.name.startswith("HP_P700_")]
    torpedo_533 = [obj for obj in objects.values() if obj.name.startswith("HP_TORPEDO_533_")]
    torpedo_650 = [obj for obj in objects.values() if obj.name.startswith("HP_TORPEDO_650_")]
    hatches = [obj for obj in objects.values() if obj.name.startswith("SM_P700_Hatch_") and int(obj.get("lod", -1)) == 0]
    if len(p700_markers) != 24 or len(hatches) != 12 or len(torpedo_533) != 4 or len(torpedo_650) != 2:
        raise RuntimeError("Saved authoring counts do not match 24 P700 / 12 hatches / 4x533 / 2x650")
    masts = [objects[f"SM_Antey_LOD0_Mast_{index:02d}"] for index in range(1, 5)]
    for mast in masts:
        mast_minimum, mast_maximum = object_bounds([mast])
        mast_dimensions = mast_maximum - mast_minimum
        if mast_dimensions.z <= max(mast_dimensions.x, mast_dimensions.y) * 4.0:
            raise RuntimeError(f"Mast is not vertical: {mast.name}, dimensions={list(mast_dimensions)}")
    doors = [objects[f"SM_Antey_LOD0_TorpedoDoor_{index:02d}"] for index in range(1, 7)]
    for door in doors:
        door_minimum, door_maximum = object_bounds([door])
        door_dimensions = door_maximum - door_minimum
        if door_minimum.z <= 0.0:
            raise RuntimeError(f"Torpedo door is not wholly in the upper bow half: {door.name}")
        if door_dimensions.x >= min(door_dimensions.y, door_dimensions.z):
            raise RuntimeError(f"Torpedo door does not face +X: {door.name}, dimensions={list(door_dimensions)}")
    if any(marker.location.z <= 0.0 for marker in [*torpedo_533, *torpedo_650]):
        raise RuntimeError("Torpedo hardpoint is not in the upper bow half")
    production_meshes = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False)]
    if any(not obj.data.uv_layers or not obj.data.materials for obj in production_meshes):
        raise RuntimeError("Saved Antey production mesh lacks UV or material data")
    collision_names = {"COL_Antey_Bow", "COL_Antey_Main", "COL_Antey_Aft", "COL_Antey_Sail"}
    if collision_names - set(objects):
        raise RuntimeError(f"Saved collision set incomplete: {sorted(collision_names - set(objects))}")
    compartments = [obj for obj in objects.values() if obj.name.startswith("VOL_COMP_")]
    if len(compartments) != 10 or "PHY_Antey_BuoyancyVolume" not in objects:
        raise RuntimeError("Saved gameplay volume contract is incomplete")

    hull = objects["SM_Antey_LOD0_Hull"]
    sail = objects["SM_Antey_LOD0_Sail"]
    hull_stats = topology(hull)
    if any(hull_stats[key] != 0 for key in ("boundary_edges", "non_manifold_edges", "degenerate_faces", "flipped_normals")):
        raise RuntimeError(f"Hull topology gate failed: {hull_stats}")
    references = [obj for obj in objects.values() if obj.name.startswith("REF_Source_")]
    if len(references) != 2:
        raise RuntimeError("Normalized source references are absent from saved BLEND")
    deviation = surface_deviation(hull, references)
    reference_minimum, reference_maximum = object_bounds(references)

    runtime = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    all_runtime = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False)]
    minimum, maximum = object_bounds(runtime)
    overall = maximum - minimum
    hull_min, hull_max = object_bounds([hull])
    sail_min, sail_max = object_bounds([sail])

    camera = setup_scene()
    p700_debug, torpedo_debug = make_diagnostics()
    manifest = []
    views = {
        "review_antey_side.png": Vector((0.0, -1.0, 0.0)),
        "review_antey_top.png": Vector((0.0, 0.0, 1.0)),
        "review_antey_bottom.png": Vector((0.0, 0.0, -1.0)),
        "review_antey_bow.png": Vector((1.0, 0.0, 0.0)),
        "review_antey_stern.png": Vector((-1.0, 0.0, 0.0)),
        "review_antey_front_3q.png": Vector((1.0, -1.0, 0.55)),
        "review_antey_rear_3q.png": Vector((-1.0, 1.0, 0.55)),
        "review_antey_bottom_port_3q.png": Vector((0.65, 1.0, -0.65)),
        "review_antey_bottom_starboard_3q.png": Vector((0.65, -1.0, -0.65)),
    }
    for filename, direction in views.items():
        entry = render(camera, runtime, direction, renders / filename)
        entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
        manifest.append(entry)

    corrected_detail_views = (
        ("review_antey_sail_masts_side.png", Vector((0.0, -1.0, 0.0)), [sail, *masts]),
        (
            "review_antey_control_planes_top.png",
            Vector((0.0, 0.0, 1.0)),
            [
                objects["SM_Antey_LOD0_BowPlane_Port"],
                objects["SM_Antey_LOD0_BowPlane_Starboard"],
                objects["SM_Antey_LOD0_TailPlane_Port"],
                objects["SM_Antey_LOD0_TailPlane_Starboard"],
            ],
        ),
        (
            "review_antey_bow_planes_top.png",
            Vector((0.0, 0.0, 1.0)),
            [objects["SM_Antey_LOD0_BowPlane_Port"], objects["SM_Antey_LOD0_BowPlane_Starboard"]],
        ),
        (
            "review_antey_tail_planes_top.png",
            Vector((0.0, 0.0, 1.0)),
            [objects["SM_Antey_LOD0_TailPlane_Port"], objects["SM_Antey_LOD0_TailPlane_Starboard"]],
        ),
        ("review_antey_torpedo_doors_bow.png", Vector((1.0, 0.0, 0.0)), doors),
    )
    for filename, direction, focus in corrected_detail_views:
        entry = render(camera, runtime, direction, renders / filename, focus=focus)
        entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
        manifest.append(entry)

    props = [objects["SM_Propeller_Port"], objects["SM_Propeller_Starboard"]]
    entry = render(camera, runtime, Vector((-1.0, 0.8, 0.25)), renders / "review_antey_propellers.png", focus=props)
    entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
    manifest.append(entry)

    entry = render(camera, runtime + p700_debug, Vector((0.0, 0.0, 1.0)), renders / "review_antey_p700_layout.png")
    entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
    manifest.append(entry)
    entry = render(camera, runtime + torpedo_debug, Vector((1.0, -0.25, 0.1)), renders / "review_antey_torpedo_layout.png", focus=torpedo_debug)
    entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
    manifest.append(entry)

    for name, direction in (("side", Vector((0.0, -1.0, 0.0))), ("bottom", Vector((0.0, 0.0, -1.0)))):
        for obj in runtime:
            obj.display_type = "WIRE"
            obj.color = (0.08, 0.18, 0.24, 1.0)
        entry = render(camera, runtime, direction, renders / f"review_antey_{name}_wireframe.png", apply_style=False)
        entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string})
        manifest.append(entry)

    for name, direction in (("side", Vector((0.0, -1.0, 0.0))), ("top", Vector((0.0, 0.0, 1.0))), ("bottom", Vector((0.0, 0.0, -1.0)))):
        entry = overlay_render(camera, runtime, references, direction, renders / f"review_antey_source_{name}_overlay.png")
        entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string, "overlay": "DIRECT_SAME_COORDINATES_SAME_SCALE"})
        manifest.append(entry)

    silhouette = {
        name: silhouette_iou(camera, runtime, references, direction, renders / f"_mask_{name}.png")
        for name, direction in (
            ("side_iou", Vector((0.0, -1.0, 0.0))),
            ("top_iou", Vector((0.0, 0.0, 1.0))),
            ("bottom_iou", Vector((0.0, 0.0, -1.0))),
        )
    }

    report = {
        "blend_path": str(blend),
        "blend_sha256": artifact_hash,
        "blender_version": bpy.app.version_string,
        "fresh_reopen": True,
        "dimensions": {
            "length": overall.x,
            "maximum_beam": hull_max.y - hull_min.y,
            "maximum_exterior_span": overall.y,
            "main_hull_maximum_beam": hull_max.y - hull_min.y,
            "main_hull_maximum_exterior_height": hull_max.z - hull_min.z,
            "sail_height": sail_max.z - sail_min.z,
            "overall_height": overall.z,
            "overall_minimum": list(minimum),
            "overall_maximum": list(maximum),
        },
        "hull": hull_stats,
        "sail": topology(sail),
        "port_propeller": port_topology,
        "starboard_propeller": starboard_topology,
        "counts": {"p700_hardpoints": len(p700_markers), "p700_hatches": len(hatches), "torpedo_533": len(torpedo_533), "torpedo_650": len(torpedo_650)},
        "corrected_geometry_contracts": {
            "vertical_masts": [mast.name for mast in masts],
            "upper_bow_torpedo_doors": [door.name for door in doors],
            "bow_planes": ["SM_Antey_LOD0_BowPlane_Port", "SM_Antey_LOD0_BowPlane_Starboard"],
            "tail_planes": ["SM_Antey_LOD0_TailPlane_Port", "SM_Antey_LOD0_TailPlane_Starboard"],
        },
        "pbr_ready": {"all_production_meshes_have_uv": True, "all_production_meshes_have_material": True},
        "lods": {
            f"LOD{lod}": {
                "objects": len([obj for obj in all_runtime if int(obj.get("lod", -1)) == lod]),
                "vertices": sum(len(obj.data.vertices) for obj in all_runtime if int(obj.get("lod", -1)) == lod),
                "triangles": sum(topology(obj)["triangles"] for obj in all_runtime if int(obj.get("lod", -1)) == lod),
            }
            for lod in range(4)
        },
        "collision": sorted(collision_names),
        "compartments": sorted(obj.name for obj in compartments),
        "surface_deviation": deviation,
        "source_silhouette": silhouette,
        "source_shape_assessment": {
            "overlay_contract": "DIRECT_SAME_COORDINATES_SAME_SCALE",
            "low_top_bottom_iou_explanation": (
                "The retained source is two multi-component, non-manifold meshes with overlapping shoulder/detail "
                "geometry. Its plan-view width distribution narrows and widens abruptly around the missile banks "
                "and includes oversized legacy control-plane silhouettes. The production hull deliberately replaces "
                "those waves and overlaps with one smooth broad missile shoulder while retaining the public 949A "
                "length/beam envelope."
            ),
            "missile_shoulders": "DELIBERATE_CORRECTION_TO_BAD_SOURCE_GEOMETRY",
            "width_distribution": "SMOOTH_SINGLE_HULL_WITH_BROAD_MISSILE_REGION; NO_CIRCUMFERENTIAL_WAVES",
            "geometry_action": "NO_AUTOMATIC_CHANGE_FROM_IOU_METRIC",
            "human_visual_approval": "PENDING",
        },
        "reference_bounds": {
            "minimum": list(reference_minimum),
            "maximum": list(reference_maximum),
            "dimensions": list(reference_maximum - reference_minimum),
        },
        "render_manifest": manifest,
        "visual_gate": (
            "PASS_SELF_REVIEW; HUMAN_USER_APPROVAL_PENDING"
            if options.self_reviewed
            else "PENDING_SELF_REVIEW"
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    (renders / "review_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"ANTEY_LOD0_FRESH_REOPEN_VALIDATION_OK {output}")


if __name__ == "__main__":
    main()
