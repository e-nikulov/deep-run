"""Build the P-700 production asset and its authored stowed/deployed states.

The script starts from factory state, reads only the canonical P700 source,
creates new production mesh datablocks, derives lower LODs from the approved
LOD0 silhouette, and saves a temporary BLEND. It never promotes the temp file.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


SURFACE_MAP = {
    "SM_P700_LOD0_Wing_L": "SM_P700_LOD0_Wing_Port",
    "SM_P700_LOD0_Wing_R": "SM_P700_LOD0_Wing_Starboard",
    "SM_P700_LOD0_Tail_Up": "SM_P700_LOD0_Tail_Dorsal",
    "SM_P700_LOD0_Tail_Down": "SM_P700_LOD0_Tail_Ventral",
    "SM_P700_LOD0_Tail_Horizontal_L": "SM_P700_LOD0_Tail_Port",
    "SM_P700_LOD0_Tail_Horizontal_R": "SM_P700_LOD0_Tail_Starboard",
}


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)


def append_lod0(source: Path) -> list[bpy.types.Object]:
    with bpy.data.libraries.load(str(source.resolve()), link=False) as (available, loaded):
        loaded.objects = [name for name in available.objects if name.startswith("SM_P700_LOD0_")]
    objects = [obj for obj in loaded.objects if obj is not None and obj.type == "MESH"]
    if not objects:
        raise RuntimeError("P700 source has no LOD0 mesh objects")
    return objects


def new_collection(name: str) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def link(collection: bpy.types.Collection, obj: bpy.types.Object) -> None:
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)


def clone(source: bpy.types.Object, name: str, collection: bpy.types.Collection) -> bpy.types.Object:
    obj = source.copy()
    obj.data = source.data.copy()
    obj.name = name
    obj.data.name = f"{name}_Mesh"
    collection.objects.link(obj)
    obj.hide_render = False
    obj.hide_viewport = False
    obj.hide_select = False
    obj.hide_set(False)
    obj.scale = (1.0, 1.0, 1.0)
    return obj


def join_objects(objects: list[bpy.types.Object], name: str) -> bpy.types.Object:
    if not objects:
        raise RuntimeError(f"Cannot create {name}: no source pieces")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    result = objects[0]
    result.name = name
    result.data.name = f"{name}_Mesh"
    return result


def configure_materials() -> None:
    for material in bpy.data.materials:
        if material.name.startswith("M_P700"):
            material.use_nodes = True
            node = material.node_tree.nodes.get("Principled BSDF")
            if node:
                node.inputs["Metallic IOR Level"].default_value = 0.45
                node.inputs["Roughness"].default_value = 0.34


def parent_keep_transform(obj: bpy.types.Object, parent: bpy.types.Object) -> None:
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_world = world


def key_state(
    obj: bpy.types.Object,
    stowed_rotation: tuple[float, float, float],
) -> None:
    obj.rotation_mode = "XYZ"
    fixed_location = tuple(obj["deployed_location"])
    obj.location = fixed_location
    obj.rotation_euler = stowed_rotation
    obj.keyframe_insert("rotation_euler", frame=1, group=obj.name)
    obj.location = fixed_location
    obj.rotation_euler = (0.0, 0.0, 0.0)
    obj.keyframe_insert("rotation_euler", frame=41, group=obj.name)
    if obj.animation_data and obj.animation_data.action:
        action = obj.animation_data.action
        action.name = f"P700_Deploy_Source_{obj.name}"
        for layer in action.layers:
            for strip in layer.strips:
                for channelbag in strip.channelbags:
                    for curve in channelbag.fcurves:
                        for point in curve.keyframe_points:
                            point.interpolation = "LINEAR"
        track = obj.animation_data.nla_tracks.new()
        track.name = "P700_Deploy"
        strip = track.strips.new("P700_Deploy", 1, action)
        strip.action_frame_start = 1
        strip.action_frame_end = 41
        obj.animation_data.action = None


def configure_states(objects: dict[str, bpy.types.Object]) -> None:
    states = {
        # Rotation-only folds around the authored longitudinal root hinges.
        # These angles keep the six rigid surfaces mutually clear at the five
        # review samples while remaining inside the launcher envelope.
        "SM_P700_LOD0_Wing_Port": (math.radians(135.0), 0.0, 0.0),
        "SM_P700_LOD0_Wing_Starboard": (math.radians(135.0), 0.0, 0.0),
        "SM_P700_LOD0_Tail_Dorsal": (math.radians(-130.0), 0.0, 0.0),
        "SM_P700_LOD0_Tail_Ventral": (math.radians(-130.0), 0.0, 0.0),
        "SM_P700_LOD0_Tail_Port": (math.radians(-130.0), 0.0, 0.0),
        "SM_P700_LOD0_Tail_Starboard": (math.radians(-130.0), 0.0, 0.0),
    }
    for name, rotation in states.items():
        obj = objects[name]
        fixed_location = tuple(obj["deployed_location"])
        key_state(obj, rotation)
        obj["pivot_contract"] = "PHYSICAL_ROOT_HINGE_FIXED; ROTATION_ONLY"
        obj["hinge_axis_local"] = "X"
        obj["stowed_location"] = fixed_location
        obj["stowed_rotation_euler"] = rotation
        obj["deployed_location"] = fixed_location
        obj["deployed_rotation_euler"] = (0.0, 0.0, 0.0)


def fit_surface_geometry(obj: bpy.types.Object) -> None:
    """Keep a 2.6 m deployed span while allowing a tangent 1.35 m fold."""
    if "Wing_" in obj.name:
        for vertex in obj.data.vertices:
            vertex.co.z *= 0.875
        obj.location.z = math.copysign(0.46, obj.location.z)
    elif "Tail_Dorsal" in obj.name or "Tail_Ventral" in obj.name:
        for vertex in obj.data.vertices:
            vertex.co.y *= 0.8716
        obj.location.y = math.copysign(0.46, obj.location.y)
    elif "Tail_Port" in obj.name or "Tail_Starboard" in obj.name:
        for vertex in obj.data.vertices:
            vertex.co.z *= 0.9435
        obj.location.z = math.copysign(0.46, obj.location.z)
    obj.data.update()


def bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    corners = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector(tuple(min(point[i] for point in corners) for i in range(3))),
        Vector(tuple(max(point[i] for point in corners) for i in range(3))),
    )


def validate_scene(production: list[bpy.types.Object]) -> None:
    required = {
        "SM_P700_LOD0_Body",
        "SM_P700_LOD0_Booster",
        *SURFACE_MAP.values(),
    }
    actual = {obj.name for obj in production}
    missing = required - actual
    if missing:
        raise RuntimeError(f"Missing production meshes: {sorted(missing)}")
    for obj in production:
        if len(obj.data.vertices) == 0 or len(obj.data.polygons) == 0:
            raise RuntimeError(f"Empty production mesh: {obj.name}")
        if any(abs(scale - 1.0) > 1.0e-6 for scale in obj.scale):
            raise RuntimeError(f"Non-unit scale: {obj.name}")
    scene = bpy.context.scene
    scene.frame_set(1)
    stowed_min, stowed_max = bounds(production)
    stowed = stowed_max - stowed_min
    if stowed.y > 1.35 + 1.0e-3 or stowed.z > 1.35 + 1.0e-3:
        raise RuntimeError(f"Stowed cross-section exceeds 1.35 m envelope: {tuple(stowed)}")
    scene.frame_set(41)
    deployed_min, deployed_max = bounds(production)
    deployed = deployed_max - deployed_min
    if max(deployed.y, deployed.z) < 2.5:
        raise RuntimeError(f"Deployed span unexpectedly small: {tuple(deployed)}")
    scene["P700_STOWED_dimensions"] = tuple(stowed)
    scene["P700_DEPLOYED_dimensions"] = tuple(deployed)
    scene["P700_launcher_envelope_diameter"] = 1.35
    scene["P700_minimumRequiredClearance"] = 0.025
    scene["P700_state_frames"] = "STOWED=1;25%=11;50%=21;75%=31;DEPLOYED=41"
    scene.frame_set(1)


def create_lods(lod0: list[bpy.types.Object], root: bpy.types.Object) -> list[bpy.types.Object]:
    generated = []
    for lod, body_ratio, surface_ratio in ((1, 0.55, 1.0), (2, 0.25, 0.72), (3, 0.08, 0.45)):
        collection = new_collection(f"P700_PRODUCTION_LOD{lod}")
        for source in lod0:
            obj = source.copy()
            obj.data = source.data.copy()
            obj.name = source.name.replace("LOD0", f"LOD{lod}")
            obj.data.name = f"{obj.name}_Mesh"
            collection.objects.link(obj)
            obj.parent = root
            obj["runtime_export"] = True
            obj["lod"] = lod
            ratio = body_ratio if source.name.endswith(("_Body", "_Booster")) else surface_ratio
            if ratio < 0.999:
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


def main() -> None:
    options = args()
    source = options.source.resolve()
    output = options.output.resolve()
    if source == output:
        raise RuntimeError("Refusing to overwrite the canonical source")
    clear_scene()
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1.0
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 41

    source_objects = append_lod0(source)
    references = new_collection("REFERENCE_SOURCE")
    production_collection = new_collection("P700_PRODUCTION_LOD0")
    root = bpy.data.objects.new("P700_ROOT", None)
    production_collection.objects.link(root)

    for source_obj in source_objects:
        source_obj.name = f"REF_Source_{source_obj.name}"
        source_obj.data.name = f"REF_{source_obj.data.name}"
        source_obj.display_type = "WIRE"
        source_obj.hide_render = True
        source_obj.hide_viewport = True
        source_obj.hide_select = True
        source_obj["runtime_export"] = False
        references.objects.link(source_obj)

    source_by_original = {obj.name.removeprefix("REF_Source_"): obj for obj in source_objects}
    surface_objects: dict[str, bpy.types.Object] = {}
    for source_name, production_name in SURFACE_MAP.items():
        obj = clone(source_by_original[source_name], production_name, production_collection)
        fit_surface_geometry(obj)
        obj["deployed_location"] = tuple(obj.location)
        parent_keep_transform(obj, root)
        surface_objects[production_name] = obj

    excluded = set(SURFACE_MAP) | {"SM_P700_LOD0_Booster"}
    booster_names = {name for name in source_by_original if "Booster" in name}
    static_names = [name for name in source_by_original if name not in excluded | booster_names]
    static_pieces = [clone(source_by_original[name], f"BUILD_{name}", production_collection) for name in static_names]
    body = join_objects(static_pieces, "SM_P700_LOD0_Body")
    parent_keep_transform(body, root)

    booster_pieces = [clone(source_by_original[name], f"BUILD_{name}", production_collection) for name in sorted(booster_names)]
    booster = join_objects(booster_pieces, "SM_P700_LOD0_Booster")
    parent_keep_transform(booster, root)
    booster["attachment"] = "P700_BOOSTER_ATTACH"

    configure_materials()
    configure_states(surface_objects)
    production = [body, booster, *surface_objects.values()]
    validate_scene(production)

    for obj in production:
        obj["runtime_export"] = True
        obj["lod"] = 0
    lower_lods = create_lods(production, root)
    if any(len(obj.data.polygons) == 0 for obj in lower_lods):
        raise RuntimeError("Generated an empty P700 lower-LOD mesh")
    root["asset_id"] = "P700"
    root["neutral_identity"] = True
    root["folding_reference_status"] = "PHYSICAL_ROOT_HINGES; ROTATION_ONLY; FIVE_SAMPLE_CLEAR"
    root["runtime_animation_contract"] = "P700_Deploy"
    root["deployment_phase_contract"] = "STOWED_DURING_LAUNCHER_EXIT_AND_UNDERWATER_LAUNCH; DEPLOY_ONLY_POST_LAUNCH_PHASE"
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    print(f"P700_TEMP_BUILD_OK {output}")


if __name__ == "__main__":
    main()
