"""Render focused source-first exterior QA views for the Antey stern repair.

The candidate is never saved by this script.  It renders neutral-clay candidate
views and same-camera source/candidate color overlays for human visual review.
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


SOURCE_CENTER_X = (-4.875378131866455 + 4.900454044342041) * 0.5
SOURCE_CENTER_Y = (-0.8649876117706299 + 0.8413368463516235) * 0.5
SOURCE_SCALE_X = 154.0 / 9.775832176208496
SOURCE_SCALE_YZ = 18.2 / 1.7063244581222534
RUDDER_NAMES = {
    "dorsal": "SM_Antey_LOD0_Rudder_Dorsal",
    "ventral": "SM_Antey_LOD0_Rudder_Ventral",
}


def parse_args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def production(point: Vector) -> Vector:
    return Vector(
        (
            (point.y - SOURCE_CENTER_X) * SOURCE_SCALE_X,
            -(point.x - SOURCE_CENTER_Y) * SOURCE_SCALE_YZ,
            point.z * SOURCE_SCALE_YZ,
        )
    )


def source_payload(path: Path) -> tuple[list[tuple[float, float, float]], list[tuple[int, ...]]]:
    bpy.ops.wm.open_mainfile(filepath=str(path.resolve(strict=True)))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    for object_name in ("Bridge", "Hull"):
        source_object = bpy.data.objects.get(object_name)
        if source_object is None:
            raise RuntimeError(f"authoritative source object is missing: {object_name}")
        evaluated = source_object.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        offset = len(vertices)
        try:
            vertices.extend(tuple(production(source_object.matrix_world @ vertex.co)) for vertex in mesh.vertices)
            faces.extend(tuple(offset + index for index in polygon.vertices) for polygon in mesh.polygons if len(polygon.vertices) >= 3)
        finally:
            evaluated.to_mesh_clear()
    return vertices, faces


def material(name: str, color: tuple[float, float, float, float], metallic: float = 0.0) -> bpy.types.Material:
    existing = bpy.data.materials.get(name)
    if existing is not None:
        bpy.data.materials.remove(existing)
    result = bpy.data.materials.new(name)
    result.diffuse_color = color
    result.use_nodes = True
    nodes = result.node_tree.nodes
    principled = nodes.get("Principled BSDF")
    if principled is not None:
        principled.inputs["Base Color"].default_value = color
        principled.inputs["Metallic"].default_value = metallic
        principled.inputs["Roughness"].default_value = 0.42
    return result


def assign_material(objects: list[bpy.types.Object], mat: bpy.types.Material) -> None:
    for obj in objects:
        if obj.type != "MESH":
            continue
        obj.data.materials.clear()
        obj.data.materials.append(mat)


def runtime_objects() -> list[bpy.types.Object]:
    objects = []
    for obj in bpy.context.scene.objects:
        is_runtime_lod0 = obj.type == "MESH" and int(obj.get("lod", -1)) == 0 and bool(obj.get("runtime_export", False))
        obj.hide_render = not is_runtime_lod0
        if is_runtime_lod0:
            objects.append(obj)
    return objects


def create_source_overlay(vertices: list[tuple[float, float, float]], faces: list[tuple[int, ...]], mat: bpy.types.Material) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("QA_SourceOverlayMesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("QA_SourceOverlay", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj


def create_camera(name: str, target: Vector, offset: Vector) -> bpy.types.Object:
    camera_data = bpy.data.cameras.new(name + "Data")
    camera = bpy.data.objects.new(name, camera_data)
    bpy.context.scene.collection.objects.link(camera)
    camera.location = target + offset
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.lens = 62.0
    camera_data.clip_start = 0.1
    camera_data.clip_end = 500.0
    return camera


def create_lights(target: Vector) -> list[bpy.types.Object]:
    lights = []
    for index, (offset, energy, size) in enumerate(
        (
            (Vector((-16.0, -14.0, 18.0)), 1400.0, 9.0),
            (Vector((-10.0, 16.0, 8.0)), 950.0, 7.0),
            (Vector((12.0, 0.0, -6.0)), 700.0, 6.0),
        )
    ):
        data = bpy.data.lights.new(f"QA_SternRepair_Area_{index}", "AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        light = bpy.data.objects.new(data.name, data)
        bpy.context.scene.collection.objects.link(light)
        light.location = target + offset
        light.rotation_euler = (target - light.location).to_track_quat("-Z", "Y").to_euler()
        lights.append(light)
    return lights


def configure_scene(scene: bpy.types.Scene) -> None:
    scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in {item.identifier for item in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items} else "BLENDER_EEVEE_NEXT"
    scene.render.resolution_x = 960
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.render.image_settings.color_mode = "RGBA"
    world = scene.world or bpy.data.worlds.new("QA_SternRepair_World")
    scene.world = world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    if background is not None:
        background.inputs["Color"].default_value = (0.008, 0.012, 0.02, 1.0)
        background.inputs["Strength"].default_value = 0.22


def render(scene: bpy.types.Scene, path: Path) -> None:
    scene.render.filepath = str(path.resolve())
    bpy.ops.render.render(write_still=True)


def main() -> None:
    opts = parse_args()
    output = opts.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_vertices, source_faces = source_payload(opts.source.resolve())
    bpy.ops.wm.open_mainfile(filepath=str(opts.candidate.resolve(strict=True)))
    candidate_path = opts.candidate.resolve()
    if Path(bpy.data.filepath).resolve(strict=True) != candidate_path:
        raise RuntimeError("fresh candidate reopen path mismatch")
    scene = bpy.context.scene
    configure_scene(scene)
    candidate = runtime_objects()
    neutral = material("QA_SternRepair_NeutralClay", (0.54, 0.60, 0.68, 1.0), 0.18)
    source_color = material("QA_SternRepair_SourceOrange", (0.95, 0.20, 0.025, 1.0), 0.05)
    candidate_color = material("QA_SternRepair_CandidateBlue", (0.025, 0.28, 0.95, 1.0), 0.12)
    assign_material(candidate, neutral)
    target = Vector((-61.5, 0.0, 3.8))
    create_lights(target)
    views = [
        ("stern_port_neutral", Vector((0.0, -22.0, 2.2)), target, None),
        ("stern_starboard_neutral", Vector((0.0, 22.0, 2.2)), target, None),
        ("stern_rear3q_port_neutral", Vector((-20.0, -20.0, 9.0)), target, None),
        ("stern_rear3q_starboard_neutral", Vector((-20.0, 20.0, 9.0)), target, None),
        ("stern_top_neutral", Vector((-2.0, 0.0, 28.0)), target, None),
        ("stern_bottom_neutral", Vector((-2.0, 0.0, -20.0)), target, None),
        ("dorsal_rudder_neutral", Vector((-2.0, -18.0, 10.0)), Vector((-61.5, 0.0, 5.1)), None),
        ("dorsal_rudder_plus15", Vector((-2.0, -18.0, 10.0)), Vector((-61.5, 0.0, 5.1)), ("dorsal", 15.0)),
        ("dorsal_rudder_minus15", Vector((-2.0, -18.0, 10.0)), Vector((-61.5, 0.0, 5.1)), ("dorsal", -15.0)),
        ("ventral_rudder_neutral", Vector((-2.0, -18.0, -8.0)), Vector((-61.5, 0.0, 2.3)), None),
        ("ventral_rudder_plus15", Vector((-2.0, -18.0, -8.0)), Vector((-61.5, 0.0, 2.3)), ("ventral", 15.0)),
        ("ventral_rudder_minus15", Vector((-2.0, -18.0, -8.0)), Vector((-61.5, 0.0, 2.3)), ("ventral", -15.0)),
    ]
    for label, offset, view_target, articulation in views:
        for rudder in RUDDER_NAMES.values():
            obj = scene.objects.get(rudder)
            if obj is not None:
                obj.rotation_mode = "XYZ"
                obj.rotation_euler[2] = 0.0
        if articulation is not None:
            obj = scene.objects.get(RUDDER_NAMES[articulation[0]])
            if obj is not None:
                obj.rotation_mode = "XYZ"
                obj.rotation_euler[2] = math.radians(articulation[1])
        bpy.context.view_layer.update()
        camera = create_camera(f"QA_SternRepair_Camera_{label}", view_target, offset)
        scene.camera = camera
        render(scene, output / f"{label}.png")
        source_obj = create_source_overlay(source_vertices, source_faces, source_color)
        assign_material(candidate, candidate_color)
        render(scene, output / f"{label}_source_vs_candidate_overlay.png")
        bpy.data.objects.remove(source_obj, do_unlink=True)
        assign_material(candidate, neutral)
        bpy.data.objects.remove(camera, do_unlink=True)
    print(f"ANTEY_STERN_REPAIR_QA_RENDERS_PASS count={len(views) * 2} output={output}")


if __name__ == "__main__":
    main()
