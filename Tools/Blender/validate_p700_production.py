"""Fresh-reopen validation and hash-bound art review for P700 LOD0."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


REQUIRED = {
    "SM_P700_LOD0_Body",
    "SM_P700_LOD0_Booster",
    "SM_P700_LOD0_Wing_Port",
    "SM_P700_LOD0_Wing_Starboard",
    "SM_P700_LOD0_Tail_Dorsal",
    "SM_P700_LOD0_Tail_Ventral",
    "SM_P700_LOD0_Tail_Port",
    "SM_P700_LOD0_Tail_Starboard",
}


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renders", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def production_objects() -> list[bpy.types.Object]:
    return [
        obj
        for obj in bpy.context.scene.objects
        if obj.type == "MESH" and obj.name in REQUIRED and bool(obj.get("runtime_export", False))
    ]


def all_runtime_objects() -> list[bpy.types.Object]:
    return [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and bool(obj.get("runtime_export", False))]


def bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    corners = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[i] for point in corners) for i in range(3))),
        Vector(tuple(max(point[i] for point in corners) for i in range(3))),
    )


def radial_diameter(objects: list[bpy.types.Object]) -> float:
    return 2.0 * max(
        math.hypot(world.y, world.z)
        for obj in objects
        for vertex in obj.data.vertices
        for world in (obj.matrix_world @ vertex.co,)
    )


def action_paths(action: bpy.types.Action) -> set[str]:
    return {
        curve.data_path
        for layer in action.layers
        for strip in layer.strips
        for channelbag in strip.channelbags
        for curve in channelbag.fcurves
    }


def topology(obj: bpy.types.Object) -> dict:
    mesh = obj.data
    mesh.calc_loop_triangles()
    edge_index = {tuple(sorted(edge.vertices)): index for index, edge in enumerate(mesh.edges)}
    counts = [0] * len(mesh.edges)
    for polygon in mesh.polygons:
        for edge in polygon.edge_keys:
            counts[edge_index[tuple(sorted(edge))]] += 1
    return {
        "name": obj.name,
        "mesh": mesh.name,
        "vertices": len(mesh.vertices),
        "triangles": len(mesh.loop_triangles),
        "boundary_edges": sum(count == 1 for count in counts),
        "non_manifold_edges": sum(count != 2 for count in counts),
        "degenerate_faces": sum(polygon.area <= 1.0e-10 for polygon in mesh.polygons),
        "location": list(obj.location),
        "rotation_euler": list(obj.rotation_euler),
        "scale": list(obj.scale),
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "materials": [material.name for material in mesh.materials if material is not None],
        "pivot_contract": obj.get("pivot_contract"),
    }


def triangle_count(obj: bpy.types.Object) -> int:
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def world_bvh(obj: bpy.types.Object) -> BVHTree:
    vertices = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    polygons = [tuple(polygon.vertices) for polygon in obj.data.polygons]
    return BVHTree.FromPolygons(vertices, polygons, all_triangles=False)


def movable_self_intersections(objects: list[bpy.types.Object]) -> dict:
    movable = [obj for obj in objects if "Wing_" in obj.name or "Tail_" in obj.name]
    trees = {obj.name: world_bvh(obj) for obj in movable}
    pairs = []
    for index, first in enumerate(movable):
        for second in movable[index + 1 :]:
            overlaps = trees[first.name].overlap(trees[second.name])
            if overlaps:
                pairs.append({"a": first.name, "b": second.name, "triangle_pairs": len(overlaps)})
    return {"checked_pairs": len(movable) * (len(movable) - 1) // 2, "intersections": pairs, "status": "PASS" if not pairs else "FAIL"}


def setup_review_scene() -> tuple[bpy.types.Object, bpy.types.Material]:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    if scene.world is None:
        scene.world = bpy.data.worlds.new("REVIEW_World")
    scene.world.color = (0.88, 0.88, 0.88)
    camera_data = bpy.data.cameras.new("REVIEW_Camera")
    camera = bpy.data.objects.new("REVIEW_Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.data.type = "ORTHO"
    clay = bpy.data.materials.new("REVIEW_BrightClay")
    clay.diffuse_color = (0.62, 0.72, 0.80, 1.0)
    return camera, clay


def render_view(
    camera: bpy.types.Object,
    objects: list[bpy.types.Object],
    direction: Vector,
    filepath: Path,
    clay: bpy.types.Material,
) -> dict:
    for obj in objects:
        obj.hide_render = False
        obj.hide_viewport = False
        if obj.data.materials:
            for material in obj.data.materials:
                if material is not None:
                    material.diffuse_color = clay.diffuse_color
        else:
            obj.data.materials.append(clay)
    minimum, maximum = bounds(objects)
    centre = (minimum + maximum) * 0.5
    extents = maximum - minimum
    camera.location = centre + direction.normalized() * max(extents.length, 1.0) * 1.6
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.ortho_scale = max(extents.y, extents.z, extents.x * 0.56) * 1.20
    bpy.context.scene.render.filepath = str(filepath)
    bpy.ops.render.render(write_still=True)
    return {
        "render_filename": filepath.name,
        "camera_location": list(camera.location),
        "camera_direction": list(direction.normalized()),
        "visible_objects": [obj.name for obj in objects],
    }


def main() -> None:
    options = args()
    output = options.output.resolve()
    renders = options.renders.resolve()
    renders.mkdir(parents=True, exist_ok=True)
    blend = Path(bpy.data.filepath).resolve()
    artifact_hash = sha256(blend)
    objects = production_objects()
    runtime = all_runtime_objects()
    actual = {obj.name for obj in objects}
    if actual != REQUIRED:
        raise RuntimeError(f"Saved BLEND production mesh mismatch: missing={REQUIRED-actual}, extra={actual-REQUIRED}")
    if any(not obj.data.uv_layers or not obj.data.materials for obj in objects):
        raise RuntimeError("Saved P700 production mesh lacks UV or material data")
    references = [obj for obj in bpy.context.scene.objects if obj.name.startswith("REF_Source_")]
    if not references or any(not obj.hide_render for obj in references):
        raise RuntimeError("REFERENCE_SOURCE objects are absent or render-enabled")

    scene = bpy.context.scene
    sample_frames = (("000", 1), ("025", 11), ("050", 21), ("075", 31), ("100", 41))
    state_data = {}
    fixed_pivot_checks = []
    for sample, frame in sample_frames:
        scene.frame_set(frame)
        minimum, maximum = bounds(objects)
        dimensions = maximum - minimum
        intersections = movable_self_intersections(objects)
        state_data[sample] = {
            "frame": frame,
            "deployment_percent": int(sample),
            "minimum": list(minimum),
            "maximum": list(maximum),
            "dimensions": list(dimensions),
            "objects": [topology(obj) for obj in objects],
            "movable_surface_self_intersections": intersections,
        }
        if intersections["status"] != "PASS":
            raise RuntimeError(f"P700 deployment sample {sample}% movable surfaces intersect each other")
        for obj in objects:
            if "Wing_" not in obj.name and "Tail_" not in obj.name:
                continue
            expected = Vector(obj["deployed_location"])
            actual_location = obj.location.copy()
            delta = (actual_location - expected).length
            fixed_pivot_checks.append({"sample": sample, "object": obj.name, "location_delta": delta})
            if delta > 1.0e-6:
                raise RuntimeError(f"{obj.name} pivot translated by {delta} m at {sample}%")
    if state_data["000"]["dimensions"][1] > 1.351 or state_data["000"]["dimensions"][2] > 1.351:
        raise RuntimeError("Saved STOWED state does not fit the 1.35 m launcher envelope")
    if max(state_data["100"]["dimensions"][1:]) < 2.5:
        raise RuntimeError("Saved DEPLOYED state lost its source span")

    scene.frame_set(1)
    body = next(obj for obj in objects if obj.name == "SM_P700_LOD0_Body")
    body_diameter = radial_diameter([body])
    maximum_stowed_diameter = radial_diameter(objects)
    launcher_envelope_diameter = 1.35
    radial_clearance = (launcher_envelope_diameter - maximum_stowed_diameter) * 0.5
    minimum_required_clearance = 0.025
    if radial_clearance + 1.0e-6 < minimum_required_clearance:
        raise RuntimeError(
            f"P700 stowed radial clearance {radial_clearance:.6f} m is below "
            f"minimumRequiredClearance {minimum_required_clearance:.6f} m"
        )

    movable = [obj for obj in objects if "Wing_" in obj.name or "Tail_" in obj.name]
    source_actions = sorted({track.strips[0].action for obj in movable for track in obj.animation_data.nla_tracks}, key=lambda action: action.name)
    logical_tracks = sorted({track.name for obj in movable for track in obj.animation_data.nla_tracks})
    if logical_tracks != ["P700_Deploy"]:
        raise RuntimeError(f"Unexpected logical P700 animation tracks: {logical_tracks}")
    translation_actions = [action.name for action in source_actions if "location" in action_paths(action)]
    if translation_actions:
        raise RuntimeError(f"P700 hinge actions contain translations: {translation_actions}")

    camera, clay = setup_review_scene()
    render_manifest = []
    directions = {
        "side": Vector((0.0, -1.0, 0.0)),
        "front": Vector((1.0, 0.0, 0.0)),
        "3q": Vector((1.0, -1.0, 0.55)),
    }
    for sample, frame in sample_frames:
        scene.frame_set(frame)
        filename = renders / f"review_p700_deploy_{sample}.png"
        entry = render_view(camera, objects, Vector((1.0, -1.0, 0.55)), filename, clay)
        entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string, "deployment_percent": int(sample)})
        render_manifest.append(entry)

    for state, frame in (("stowed", 1), ("deployed", 41)):
        scene.frame_set(frame)
        for view, direction in directions.items():
            if state == "deployed" and view == "front":
                continue
            if state == "stowed" and view not in {"side", "front", "3q"}:
                continue
            filename = renders / f"review_p700_{state}_{view}.png"
            entry = render_view(camera, objects, direction, filename, clay)
            entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string, "state": state.upper()})
            render_manifest.append(entry)

    # Direct overlay: stowed bright clay and deployed cyan wire occupy the same coordinates.
    scene.frame_set(41)
    overlay_collection = bpy.data.collections.new("REVIEW_DEPLOYED_OVERLAY")
    scene.collection.children.link(overlay_collection)
    overlays = []
    for obj in objects:
        duplicate = obj.copy()
        duplicate.data = obj.data.copy()
        duplicate.animation_data_clear()
        duplicate.parent = None
        duplicate.matrix_world = obj.matrix_world.copy()
        duplicate.name = f"REVIEW_DEPLOYED_{obj.name}"
        duplicate.display_type = "WIRE"
        duplicate.color = (0.05, 0.85, 0.95, 1.0)
        overlay_collection.objects.link(duplicate)
        overlays.append(duplicate)
    scene.frame_set(1)
    overlay_file = renders / "review_p700_deploy_overlay.png"
    entry = render_view(camera, objects + overlays, Vector((0.0, -1.0, 0.0)), overlay_file, clay)
    entry.update({"blend_path": str(blend), "blend_sha256": artifact_hash, "blender_version": bpy.app.version_string, "state": "STOWED_CLAY_PLUS_DEPLOYED_WIRE"})
    render_manifest.append(entry)

    report = {
        "blend_path": str(blend),
        "blend_sha256": artifact_hash,
        "blender_version": bpy.app.version_string,
        "fresh_reopen": True,
        "reference_objects": [obj.name for obj in references],
        "states": state_data,
        "fold_kinematics": {
            "logical_animation": "P700_Deploy",
            "sample_frames": {sample: frame for sample, frame in sample_frames},
            "fixed_pivot_checks": fixed_pivot_checks,
            "self_intersection_status": "PASS_AT_ALL_FIVE_SAMPLES",
            "source_actions": [action.name for action in source_actions],
            "source_action_target_objects": [obj.name for obj in movable],
            "source_action_paths": {action.name: sorted(action_paths(action)) for action in source_actions},
            "logical_nla_tracks": logical_tracks,
        },
        "stowed_packing": {
            "body_diameter": body_diameter,
            "maximum_stowed_diameter": maximum_stowed_diameter,
            "launcher_envelope_diameter": launcher_envelope_diameter,
            "radial_clearance": radial_clearance,
            "minimumRequiredClearance": minimum_required_clearance,
            "status": "PASS",
        },
        "lods": {
            f"LOD{lod}": {
                "objects": len([obj for obj in runtime if int(obj.get("lod", -1)) == lod]),
                "vertices": sum(len(obj.data.vertices) for obj in runtime if int(obj.get("lod", -1)) == lod),
                "triangles": sum(triangle_count(obj) for obj in runtime if int(obj.get("lod", -1)) == lod),
            }
            for lod in range(4)
        },
        "launcher_envelope_diameter": launcher_envelope_diameter,
        "pivot_validation": "PASS_FIXED_PHYSICAL_ROOT_HINGES_ROTATION_ONLY",
        "deployment_phase_contract": "STOWED_DURING_LAUNCHER_EXIT_AND_UNDERWATER_LAUNCH; DEPLOY_ONLY_POST_LAUNCH_PHASE",
        "render_manifest": render_manifest,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    (renders / "review_manifest.json").write_text(json.dumps(render_manifest, indent=2), encoding="utf-8")
    print(f"P700_FRESH_REOPEN_VALIDATION_OK {output}")


if __name__ == "__main__":
    main()
