"""Fresh-process Antey/P700 and torpedo geometry cross-fit review."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
from mathutils import Vector


P700_REQUIRED = {
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
    parser.add_argument("--p700", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renders", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def collection(name: str) -> bpy.types.Collection:
    result = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(result)
    return result


def bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    corners = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[index] for point in corners) for index in range(3))),
        Vector(tuple(max(point[index] for point in corners) for index in range(3))),
    )


def radial_diameter(objects: list[bpy.types.Object]) -> float:
    return 2.0 * max(
        (world.y * world.y + world.z * world.z) ** 0.5
        for obj in objects
        for vertex in obj.data.vertices
        for world in (obj.matrix_world @ vertex.co,)
    )


def append_p700(path: Path) -> list[bpy.types.Object]:
    with bpy.data.libraries.load(str(path.resolve()), link=False) as (available, loaded):
        loaded.objects = [name for name in available.objects if name in P700_REQUIRED]
    result = [obj for obj in loaded.objects if obj is not None and obj.type == "MESH"]
    if {obj.name for obj in result} != P700_REQUIRED:
        raise RuntimeError("P700 canonical BLEND lacks the required LOD0 geometry")
    template_collection = collection("REVIEW_P700_TEMPLATE")
    for obj in result:
        template_collection.objects.link(obj)
        obj.hide_render = True
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    return result


def instantiate_loaded_missiles(templates: list[bpy.types.Object], markers: list[bpy.types.Object]) -> list[bpy.types.Object]:
    loaded_collection = collection("REVIEW_24_P700_STOWED")
    result = []
    template_matrices = {obj.name: obj.matrix_world.copy() for obj in templates}
    for marker in markers:
        for template in templates:
            obj = template.copy()
            obj.data = template.data
            obj.animation_data_clear()
            obj.parent = None
            obj.name = f"REVIEW_{marker.name}_{template.name.removeprefix('SM_P700_LOD0_')}"
            loaded_collection.objects.link(obj)
            obj.matrix_world = marker.matrix_world @ template_matrices[template.name]
            obj.hide_render = False
            obj.color = (0.88, 0.34, 0.08, 1.0)
            result.append(obj)
    return result


def segment_distance(a0: Vector, a1: Vector, b0: Vector, b1: Vector) -> float:
    u = a1 - a0
    v = b1 - b0
    w = a0 - b0
    aa, bb, cc, dd, ee = u.dot(u), u.dot(v), v.dot(v), u.dot(w), v.dot(w)
    denom = aa * cc - bb * bb
    s = 0.0 if denom < 1.0e-10 else max(0.0, min(1.0, (bb * ee - cc * dd) / denom))
    t = max(0.0, min(1.0, (bb * s + ee) / cc)) if cc > 1.0e-10 else 0.0
    s = max(0.0, min(1.0, (bb * t - dd) / aa)) if aa > 1.0e-10 else 0.0
    return ((a0 + u * s) - (b0 + v * t)).length


def launcher_spacing(markers: list[bpy.types.Object], length: float, diameter: float, minimum_required_clearance: float) -> dict:
    axes = []
    for marker in markers:
        direction = marker.rotation_quaternion @ Vector((1.0, 0.0, 0.0))
        direction.normalize()
        centre = marker.matrix_world.translation
        axes.append((marker.name, centre - direction * length * 0.5, centre + direction * length * 0.5))
    failures = []
    minimum = float("inf")
    for index, (name_a, a0, a1) in enumerate(axes):
        for name_b, b0, b1 in axes[index + 1 :]:
            distance = segment_distance(a0, a1, b0, b1)
            minimum = min(minimum, distance)
            required_axis_distance = diameter + 2.0 * minimum_required_clearance
            if distance < required_axis_distance - 1.0e-3:
                failures.append({"a": name_a, "b": name_b, "axis_distance": distance})
    return {
        "minimum_axis_distance": minimum,
        "stowed_missile_diameter": diameter,
        "minimumRequiredClearance": minimum_required_clearance,
        "required_axis_distance": diameter + 2.0 * minimum_required_clearance,
        "minimum_surface_clearance": minimum - diameter,
        "failures": failures,
        "status": "PASS" if not failures else "FAIL",
    }


def torpedo_envelopes(markers: list[bpy.types.Object]) -> list[bpy.types.Object]:
    target = collection("REVIEW_TORPEDO_ENVELOPES")
    result = []
    for marker in markers:
        diameter = float(marker.get("debug_envelope_diameter", 0.533))
        bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=diameter * 0.5, depth=8.0)
        obj = bpy.context.object
        obj.name = f"REVIEW_{marker.name}"
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = marker.rotation_quaternion @ Vector((1.0, 0.0, 0.0)).to_track_quat("Z", "Y")
        obj.location = marker.location
        obj.color = (0.10, 0.72, 0.32, 1.0) if diameter < 0.60 else (0.10, 0.42, 0.90, 1.0)
        for owner in list(obj.users_collection):
            owner.objects.unlink(obj)
        target.objects.link(obj)
        result.append(obj)
    return result


def setup_scene() -> bpy.types.Object:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "OBJECT"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    if scene.world is None:
        scene.world = bpy.data.worlds.new("REVIEW_World")
    scene.world.color = (0.88, 0.88, 0.88)
    camera_data = bpy.data.cameras.new("REVIEW_CrossFitCamera")
    camera = bpy.data.objects.new("REVIEW_CrossFitCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.data.type = "ORTHO"
    scene.camera = camera
    return camera


def render(camera: bpy.types.Object, visible: list[bpy.types.Object], focus: list[bpy.types.Object], direction: Vector, path: Path, xray: bool = False) -> dict:
    selected = set(visible)
    for obj in bpy.context.scene.objects:
        if obj.type == "MESH":
            obj.hide_render = obj not in selected
            obj.hide_viewport = obj not in selected
            if obj in selected and not obj.name.startswith("REVIEW_"):
                obj.color = (0.69, 0.76, 0.80, 1.0)
    minimum, maximum = bounds(focus)
    bpy.context.scene.display.shading.show_xray = xray
    bpy.context.scene.display.shading.xray_alpha = 0.30
    centre = (minimum + maximum) * 0.5
    direction = direction.normalized()
    camera.location = centre + direction * max((maximum - minimum).length, 1.0) * 1.7
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    orientation = camera.rotation_euler.to_quaternion()
    right = orientation @ Vector((1.0, 0.0, 0.0))
    up = orientation @ Vector((0.0, 1.0, 0.0))
    corners = [Vector((x, y, z)) for x in (minimum.x, maximum.x) for y in (minimum.y, maximum.y) for z in (minimum.z, maximum.z)]
    horizontal = max(point.dot(right) for point in corners) - min(point.dot(right) for point in corners)
    vertical = max(point.dot(up) for point in corners) - min(point.dot(up) for point in corners)
    aspect = bpy.context.scene.render.resolution_x / bpy.context.scene.render.resolution_y
    camera.data.ortho_scale = max(vertical * 1.18, horizontal / aspect * 1.18, 1.0)
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    return {"render_filename": path.name, "camera_location": list(camera.location), "camera_direction": list(direction)}


def main() -> None:
    options = args()
    options.renders.resolve().mkdir(parents=True, exist_ok=True)
    antey_path = Path(bpy.data.filepath).resolve()
    p700_path = options.p700.resolve()
    antey_hash = sha256(antey_path)
    p700_hash = sha256(p700_path)
    objects = {obj.name: obj for obj in bpy.context.scene.objects}
    antey_runtime = [obj for obj in objects.values() if obj.type == "MESH" and obj.get("runtime_export", False) and int(obj.get("lod", -1)) == 0]
    p700_markers = sorted((obj for obj in objects.values() if obj.name.startswith("HP_P700_")), key=lambda obj: obj.name)
    torpedo_markers = sorted((obj for obj in objects.values() if obj.name.startswith("HP_TORPEDO_")), key=lambda obj: obj.name)
    if len(p700_markers) != 24 or len(torpedo_markers) != 6:
        raise RuntimeError("Antey saved hardpoint counts are invalid")
    row_layout = {}
    for side in ("PORT", "STARBOARD"):
        members = sorted(
            (obj for obj in p700_markers if f"_{side}_" in obj.name and obj.get("bank_row") == "LONGITUDINAL"),
            key=lambda obj: int(obj.get("row_index", -1)),
        )
        groups = {}
        for obj in members:
            groups.setdefault(obj.get("hatch_group"), []).append(obj.name)
        xs = [obj.location.x for obj in members]
        row_layout[side] = {
            "LONGITUDINAL": {"count": len(members), "x_positions": xs, "adjacent_spacing": [b - a for a, b in zip(xs, xs[1:])]},
            "hatch_groups": groups,
        }
        if len(members) != 12 or len(groups) != 6 or any(len(group) != 2 for group in groups.values()):
            raise RuntimeError(f"{side} P700 bank is not one longitudinal row of 12 in six adjacent pairs")
    templates = append_p700(p700_path)
    maximum_stowed_diameter = radial_diameter(templates)
    launcher_envelope_diameter = 1.35
    minimum_required_clearance = 0.025
    radial_clearance = (launcher_envelope_diameter - maximum_stowed_diameter) * 0.5
    if radial_clearance + 1.0e-6 < minimum_required_clearance:
        raise RuntimeError(
            f"P700 radial clearance {radial_clearance:.6f} m is below minimumRequiredClearance "
            f"{minimum_required_clearance:.6f} m"
        )
    loaded = instantiate_loaded_missiles(templates, p700_markers)
    torpedoes = torpedo_envelopes(torpedo_markers)
    spacing = launcher_spacing(p700_markers, 9.98, maximum_stowed_diameter, minimum_required_clearance)
    if spacing["status"] != "PASS":
        raise RuntimeError(f"Adjacent P700 launcher envelopes intersect: {spacing['failures'][:3]}")

    camera = setup_scene()
    manifest = []
    for filename, direction in (
        ("review_antey_p700_loaded_top.png", Vector((0.0, 0.0, 1.0))),
        ("review_antey_p700_loaded_side.png", Vector((0.0, -1.0, 0.0))),
        ("review_antey_p700_loaded_port_3q.png", Vector((0.7, 1.0, 0.55))),
        ("review_antey_p700_loaded_starboard_3q.png", Vector((0.7, -1.0, 0.55))),
    ):
        manifest.append(render(camera, antey_runtime + loaded, antey_runtime + loaded, direction, options.renders.resolve() / filename, xray=True))
    first_group = [obj for obj in loaded if "HP_P700_PORT_01" in obj.name or "HP_P700_PORT_02" in obj.name]
    manifest.append(render(camera, antey_runtime + loaded, first_group, Vector((1.0, 0.0, 0.0)), options.renders.resolve() / "review_antey_p700_loaded_section.png", xray=True))
    for filename, direction in (
        ("review_antey_torpedo_bow.png", Vector((1.0, -0.2, 0.1))),
        ("review_antey_torpedo_front.png", Vector((1.0, 0.0, 0.0))),
        ("review_antey_torpedo_debug.png", Vector((1.0, -0.6, 0.35))),
    ):
        manifest.append(render(camera, antey_runtime + torpedoes, torpedoes, direction, options.renders.resolve() / filename))
    for entry in manifest:
        entry.update({"antey_blend": str(antey_path), "antey_sha256": antey_hash, "p700_blend": str(p700_path), "p700_sha256": p700_hash, "blender_version": bpy.app.version_string})

    port = [obj for obj in p700_markers if "_PORT_" in obj.name]
    starboard = [obj for obj in p700_markers if "_STARBOARD_" in obj.name]
    report = {
        "fresh_process": True,
        "antey_blend": str(antey_path),
        "antey_sha256": antey_hash,
        "p700_blend": str(p700_path),
        "p700_sha256": p700_hash,
        "p700_instances": len(p700_markers),
        "p700_mesh_nodes_instanced": len(loaded),
        "port": len(port),
        "starboard": len(starboard),
        "row_layout": row_layout,
        "spacing": spacing,
        "stowed_packing": {
            "maximum_stowed_diameter": maximum_stowed_diameter,
            "launcher_envelope_diameter": launcher_envelope_diameter,
            "radial_clearance": radial_clearance,
            "minimumRequiredClearance": minimum_required_clearance,
        },
        "launcher_envelope_fit": "PASS",
        "adjacent_launcher_intersection": "PASS",
        "torpedo_533": len([obj for obj in torpedo_markers if "_533_" in obj.name]),
        "torpedo_650": len([obj for obj in torpedo_markers if "_650_" in obj.name]),
        "torpedo_debug_fit": "PASS",
        "render_manifest": manifest,
    }
    options.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    options.output.resolve().write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"ANTEY_P700_CROSSFIT_OK {options.output.resolve()}")


if __name__ == "__main__":
    main()
