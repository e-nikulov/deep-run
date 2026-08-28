"""Generate the DeepRun C0 prototype submarine source and runtime assets."""

from __future__ import annotations

import json
import math
from pathlib import Path
import struct
from typing import Iterable, Sequence

import bmesh
import bpy


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRECTORY = REPOSITORY_ROOT / "Content" / "submarines" / "prototype"
RUNTIME_DIRECTORY = REPOSITORY_ROOT / "Engine" / "Assets" / "submarines" / "prototype"
BLEND_PATH = SOURCE_DIRECTORY / "submarine_prototype.blend"
GLB_PATH = RUNTIME_DIRECTORY / "submarine_prototype.glb"

MATERIAL_NAME = "M_Submarine_Prototype"
HULL_NAME = "SM_Submarine_Prototype_Hull"
SAIL_NAME = "SM_Submarine_Prototype_Sail"
CONTROL_SURFACES_NAME = "SM_Submarine_Prototype_ControlSurfaces"
PROPELLER_NAME = "SM_Submarine_Prototype_Propeller"
PROPELLER_HUB_CENTER = (-49.0, 0.0, 0.0)
EXPECTED_OBJECT_NAMES = {HULL_NAME, SAIL_NAME, CONTROL_SURFACES_NAME, PROPELLER_NAME}
EPSILON = 1.0e-5


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(f"C0 validation failed: {message}")


def reset_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for datablocks in (
        bpy.data.meshes,
        bpy.data.materials,
        bpy.data.cameras,
        bpy.data.lights,
        bpy.data.curves,
        bpy.data.armatures,
        bpy.data.worlds,
    ):
        for datablock in list(datablocks):
            datablocks.remove(datablock)

    scene = bpy.context.scene
    scene.name = "SubmarinePrototype"
    scene.world = None
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    bpy.context.preferences.filepaths.save_version = 0


def create_material() -> bpy.types.Material:
    material = bpy.data.materials.new(MATERIAL_NAME)
    material.diffuse_color = (0.028, 0.075, 0.105, 1.0)
    principled = material.node_tree.nodes.get("Principled BSDF")
    require(principled is not None, "Principled BSDF node is unavailable")
    principled.inputs["Base Color"].default_value = (0.028, 0.075, 0.105, 1.0)
    principled.inputs["Roughness"].default_value = 0.62
    principled.inputs["Metallic"].default_value = 0.15
    return material


def create_mesh_object(
    name: str,
    vertices: Sequence[tuple[float, float, float]],
    faces: Sequence[Sequence[int]],
    material: bpy.types.Material,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(material)
    mesh.update(calc_edges=True)

    editable_mesh = bmesh.new()
    editable_mesh.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(editable_mesh, faces=list(editable_mesh.faces))
    editable_mesh.to_mesh(mesh)
    editable_mesh.free()
    for polygon in mesh.polygons:
        polygon.use_smooth = True

    object_ = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(object_)
    object_["deeprun_asset_stage"] = "C0"
    object_["deeprun_forward_axis"] = "+X"
    object_["deeprun_up_axis"] = "+Z_AUTHORING_TO_+Y_RUNTIME"
    return object_


def create_hull(material: bpy.types.Material) -> bpy.types.Object:
    # The asymmetric stations make the +X bow contract visible in geometry.
    stations = (
        (-48.0, 0.0),
        (-45.0, 2.2),
        (-38.0, 4.3),
        (-24.0, 5.5),
        (12.0, 5.5),
        (32.0, 5.0),
        (44.0, 3.5),
        (50.0, 1.5),
        (52.0, 0.0),
    )
    radial_segments = 24
    vertices: list[tuple[float, float, float]] = []
    rings: list[list[int]] = []
    for x, radius in stations:
        if radius == 0.0:
            rings.append([len(vertices)])
            vertices.append((x, 0.0, 0.0))
            continue
        ring: list[int] = []
        for segment in range(radial_segments):
            angle = math.tau * segment / radial_segments
            ring.append(len(vertices))
            vertices.append((x, radius * math.sin(angle), radius * math.cos(angle)))
        rings.append(ring)

    faces: list[tuple[int, ...]] = []
    for previous, current in zip(rings, rings[1:]):
        if len(previous) == 1:
            for index in range(radial_segments):
                faces.append((previous[0], current[index], current[(index + 1) % radial_segments]))
        elif len(current) == 1:
            for index in range(radial_segments):
                faces.append((previous[index], current[0], previous[(index + 1) % radial_segments]))
        else:
            for index in range(radial_segments):
                next_index = (index + 1) % radial_segments
                faces.append((previous[index], current[index], current[next_index], previous[next_index]))
    return create_mesh_object(HULL_NAME, vertices, faces, material)


def append_xz_prism(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    profile: Sequence[tuple[float, float]],
    half_width_y: float,
) -> None:
    base = len(vertices)
    count = len(profile)
    vertices.extend((x, -half_width_y, z) for x, z in profile)
    vertices.extend((x, half_width_y, z) for x, z in profile)
    faces.append(tuple(base + index for index in reversed(range(count))))
    faces.append(tuple(base + count + index for index in range(count)))
    for index in range(count):
        next_index = (index + 1) % count
        faces.append((base + index, base + next_index, base + count + next_index, base + count + index))


def append_xy_prism(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    profile: Sequence[tuple[float, float]],
    half_height_z: float,
) -> None:
    base = len(vertices)
    count = len(profile)
    vertices.extend((x, y, -half_height_z) for x, y in profile)
    vertices.extend((x, y, half_height_z) for x, y in profile)
    faces.append(tuple(base + index for index in reversed(range(count))))
    faces.append(tuple(base + count + index for index in range(count)))
    for index in range(count):
        next_index = (index + 1) % count
        faces.append((base + index, base + next_index, base + count + next_index, base + count + index))


def append_yz_prism(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    profile: Sequence[tuple[float, float]],
    half_thickness_x: float,
) -> None:
    base = len(vertices)
    count = len(profile)
    vertices.extend((-half_thickness_x, y, z) for y, z in profile)
    vertices.extend((half_thickness_x, y, z) for y, z in profile)
    faces.append(tuple(base + index for index in reversed(range(count))))
    faces.append(tuple(base + count + index for index in range(count)))
    for index in range(count):
        next_index = (index + 1) % count
        faces.append((base + index, base + next_index, base + count + next_index, base + count + index))


def append_x_aligned_cylinder(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    half_length_x: float,
    radius: float,
    segments: int,
) -> None:
    base = len(vertices)
    for x in (-half_length_x, half_length_x):
        for segment in range(segments):
            angle = math.tau * segment / segments
            vertices.append((x, radius * math.cos(angle), radius * math.sin(angle)))

    faces.append(tuple(base + index for index in reversed(range(segments))))
    faces.append(tuple(base + segments + index for index in range(segments)))
    for index in range(segments):
        next_index = (index + 1) % segments
        faces.append((base + index, base + next_index, base + segments + next_index, base + segments + index))


def create_sail(material: bpy.types.Material) -> bpy.types.Object:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    append_xz_prism(
        vertices,
        faces,
        ((-9.0, 4.35), (9.0, 4.35), (6.5, 9.4), (2.5, 10.8), (-5.5, 10.3)),
        half_width_y=1.35,
    )
    return create_mesh_object(SAIL_NAME, vertices, faces, material)


def create_control_surfaces(material: bpy.types.Material) -> bpy.types.Object:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    append_xy_prism(
        vertices,
        faces,
        ((-43.0, 3.8), (-27.0, 3.8), (-33.0, 10.5), (-41.0, 10.5)),
        half_height_z=0.24,
    )
    append_xy_prism(
        vertices,
        faces,
        ((-43.0, -3.8), (-41.0, -10.5), (-33.0, -10.5), (-27.0, -3.8)),
        half_height_z=0.24,
    )
    append_xz_prism(
        vertices,
        faces,
        ((-44.0, 3.8), (-27.0, 3.8), (-34.0, 10.2), (-41.0, 11.4)),
        half_width_y=0.32,
    )
    append_xz_prism(
        vertices,
        faces,
        ((-44.0, -3.8), (-41.0, -8.0), (-34.0, -7.5), (-27.0, -3.8)),
        half_width_y=0.32,
    )
    return create_mesh_object(CONTROL_SURFACES_NAME, vertices, faces, material)


def create_propeller(material: bpy.types.Material) -> bpy.types.Object:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    append_x_aligned_cylinder(vertices, faces, half_length_x=1.0, radius=0.8, segments=12)

    blade_profile = (
        (0.65, -0.20),
        (1.65, -0.42),
        (3.75, -0.28),
        (4.15, 0.12),
        (3.25, 0.58),
        (1.35, 0.48),
    )
    for blade_index in range(5):
        angle = math.tau * blade_index / 5
        cosine = math.cos(angle)
        sine = math.sin(angle)
        rotated_profile = tuple(
            (radius * cosine - tangent * sine, radius * sine + tangent * cosine)
            for radius, tangent in blade_profile
        )
        append_yz_prism(vertices, faces, rotated_profile, half_thickness_x=0.16)

    propeller = create_mesh_object(PROPELLER_NAME, vertices, faces, material)
    propeller.location = PROPELLER_HUB_CENTER
    propeller["deeprun_origin"] = "HUB_CENTER"
    propeller["deeprun_rotation_axis"] = "+X"
    propeller["deeprun_presentation_only"] = True
    return propeller


def object_bounds(object_: bpy.types.Object) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    coordinates = [vertex.co for vertex in object_.data.vertices]
    require(bool(coordinates), f"{object_.name} has no vertices")
    minimum = tuple(min(coordinate[axis] for coordinate in coordinates) for axis in range(3))
    maximum = tuple(max(coordinate[axis] for coordinate in coordinates) for axis in range(3))
    return minimum, maximum


def combined_bounds(objects: Iterable[bpy.types.Object]) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    bpy.context.view_layer.update()
    coordinates = [object_.matrix_world @ vertex.co for object_ in objects for vertex in object_.data.vertices]
    require(bool(coordinates), "prototype has no vertices")
    minimum = tuple(min(coordinate[axis] for coordinate in coordinates) for axis in range(3))
    maximum = tuple(max(coordinate[axis] for coordinate in coordinates) for axis in range(3))
    return minimum, maximum


def validate_scene(objects: Sequence[bpy.types.Object], material: bpy.types.Material) -> None:
    scene_objects = list(bpy.context.scene.objects)
    require({object_.name for object_ in scene_objects} == EXPECTED_OBJECT_NAMES, "unexpected scene objects")
    require(all(object_.type == "MESH" for object_ in scene_objects), "scene contains a non-mesh object")
    require("Cube" not in bpy.data.objects, "default Cube remains in the scene")
    require(not bpy.data.cameras, "camera data must not be present")
    require(not bpy.data.lights, "light data must not be present")
    require(set(bpy.data.materials.keys()) == {MATERIAL_NAME}, "unexpected materials")
    require(bpy.context.scene.unit_settings.system == "METRIC", "Blender unit system is not Metric")
    require(abs(bpy.context.scene.unit_settings.scale_length - 1.0) < EPSILON, "unit scale is not 1.0")

    for object_ in objects:
        expected_location = PROPELLER_HUB_CENTER if object_.name == PROPELLER_NAME else (0.0, 0.0, 0.0)
        require(
            all(abs(object_.location[index] - expected_location[index]) < EPSILON for index in range(3)),
            f"{object_.name} origin location mismatch",
        )
        require(all(abs(value) < EPSILON for value in object_.rotation_euler), f"{object_.name} has unapplied rotation")
        require(all(abs(value - 1.0) < EPSILON for value in object_.scale), f"{object_.name} has unapplied scale")
        require(
            len(object_.data.materials) == 1 and object_.data.materials[0] == material,
            f"{object_.name} material mismatch",
        )
        require(object_["deeprun_forward_axis"] == "+X", f"{object_.name} forward metadata mismatch")

    hull_minimum, hull_maximum = object_bounds(bpy.data.objects[HULL_NAME])
    hull_dimensions = tuple(hull_maximum[index] - hull_minimum[index] for index in range(3))
    require(99.0 <= hull_dimensions[0] <= 101.0, f"hull length is {hull_dimensions[0]:.3f} m")
    require(10.0 <= hull_dimensions[1] <= 12.0, f"hull width is {hull_dimensions[1]:.3f} m")
    require(10.0 <= hull_dimensions[2] <= 12.0, f"hull height is {hull_dimensions[2]:.3f} m")
    require(hull_maximum[0] > abs(hull_minimum[0]) + 3.0, "geometry does not identify +X as the bow")

    propeller = bpy.data.objects[PROPELLER_NAME]
    propeller_minimum, propeller_maximum = object_bounds(propeller)
    propeller_dimensions = tuple(propeller_maximum[index] - propeller_minimum[index] for index in range(3))
    require(abs(propeller_minimum[0] + propeller_maximum[0]) < EPSILON, "propeller hub is not centered on local X")
    require(propeller_dimensions[0] <= 2.1, "propeller is unexpectedly thick along its rotation axis")
    require(propeller_dimensions[1] >= 7.5 and propeller_dimensions[2] >= 7.5, "propeller blades are too small")
    require(propeller["deeprun_origin"] == "HUB_CENTER", "propeller origin metadata mismatch")
    require(propeller["deeprun_rotation_axis"] == "+X", "propeller rotation axis mismatch")
    require(propeller["deeprun_presentation_only"] is True, "propeller presentation boundary mismatch")

    overall_minimum, overall_maximum = combined_bounds(objects)
    overall_length = overall_maximum[0] - overall_minimum[0]
    require(101.0 <= overall_length <= 103.0, f"overall length with propeller is {overall_length:.3f} m")
    require(overall_minimum[0] < 0.0 < overall_maximum[0], "prototype origin is outside the hull length")
    require(overall_minimum[2] < 0.0 < overall_maximum[2], "prototype origin is outside the vertical silhouette")


def read_glb_json(path: Path) -> dict:
    data = path.read_bytes()
    require(len(data) >= 20, "GLB is too small")
    magic, version, total_length = struct.unpack_from("<4sII", data, 0)
    require(magic == b"glTF", "GLB magic is invalid")
    require(version == 2, f"GLB version is {version}, expected 2")
    require(total_length == len(data), "GLB header length does not match file size")

    offset = 12
    json_chunk = None
    while offset < len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_length]
        offset += chunk_length
        if chunk_type == 0x4E4F534A:
            json_chunk = chunk
            break
    require(json_chunk is not None, "GLB has no JSON chunk")
    return json.loads(json_chunk.rstrip(b" \t\r\n\0").decode("utf-8"))


def validate_glb(path: Path) -> None:
    document = read_glb_json(path)
    require(document.get("asset", {}).get("version") == "2.0", "glTF asset version is not 2.0")
    require("animations" not in document, "GLB unexpectedly contains animations")
    require("cameras" not in document, "GLB unexpectedly contains cameras")

    extensions = set(document.get("extensionsUsed", []))
    require("KHR_draco_mesh_compression" not in extensions, "Draco compression is enabled")
    require("EXT_meshopt_compression" not in extensions, "meshopt compression is enabled")
    require("KHR_lights_punctual" not in extensions, "GLB unexpectedly contains lights")

    materials = document.get("materials", [])
    require(len(materials) == 1 and materials[0].get("name") == MATERIAL_NAME, "GLB material mismatch")
    pbr = materials[0].get("pbrMetallicRoughness", {})
    require("baseColorFactor" in pbr, "GLB material has no base color")
    require(abs(pbr.get("roughnessFactor", -1.0) - 0.62) < 1.0e-4, "GLB roughness mismatch")
    require(abs(pbr.get("metallicFactor", -1.0) - 0.15) < 1.0e-4, "GLB metallic mismatch")

    nodes = {node.get("name"): node for node in document.get("nodes", []) if node.get("name")}
    mesh_node_names = {name for name, node in nodes.items() if "mesh" in node}
    require(mesh_node_names == EXPECTED_OBJECT_NAMES, "GLB mesh node set mismatch")
    meshes = document.get("meshes", [])
    require(len(meshes) == len(EXPECTED_OBJECT_NAMES), "GLB mesh count mismatch")

    accessors = document.get("accessors", [])
    for name in EXPECTED_OBJECT_NAMES:
        node = nodes[name]
        require(node.get("extras", {}).get("deeprun_forward_axis") == "+X", f"{name} lost forward-axis metadata")
        forbidden_transform_keys = ("rotation", "scale", "matrix") if name == PROPELLER_NAME else (
            "translation",
            "rotation",
            "scale",
            "matrix",
        )
        for transform_key in forbidden_transform_keys:
            require(transform_key not in node, f"{name} has runtime node {transform_key}")

        if name == PROPELLER_NAME:
            translation = node.get("translation")
            require(translation is not None and len(translation) == 3, "propeller runtime translation is absent")
            require(
                all(abs(translation[index] - PROPELLER_HUB_CENTER[index]) < EPSILON for index in range(3)),
                "propeller runtime hub-center translation mismatch",
            )
            extras = node.get("extras", {})
            require(extras.get("deeprun_origin") == "HUB_CENTER", "propeller lost origin metadata")
            require(extras.get("deeprun_rotation_axis") == "+X", "propeller lost rotation-axis metadata")
            require(extras.get("deeprun_presentation_only") is True, "propeller lost presentation boundary metadata")

        mesh = meshes[node["mesh"]]
        require(mesh.get("primitives"), f"{name} has no GLB primitives")
        for primitive in mesh["primitives"]:
            attributes = primitive.get("attributes", {})
            require("POSITION" in attributes, f"{name} has no POSITION attribute")
            require("NORMAL" in attributes, f"{name} has no NORMAL attribute")
            require(primitive.get("material") == 0, f"{name} does not use {MATERIAL_NAME}")

    hull_node = nodes[HULL_NAME]
    hull_primitive = meshes[hull_node["mesh"]]["primitives"][0]
    hull_position = accessors[hull_primitive["attributes"]["POSITION"]]
    runtime_minimum = hull_position.get("min")
    runtime_maximum = hull_position.get("max")
    require(runtime_minimum is not None and runtime_maximum is not None, "hull POSITION bounds are absent")
    runtime_dimensions = [runtime_maximum[index] - runtime_minimum[index] for index in range(3)]
    require(99.0 <= runtime_dimensions[0] <= 101.0, "GLB +X length contract failed")
    require(10.0 <= runtime_dimensions[1] <= 12.0, "GLB +Y up contract failed")
    require(10.0 <= runtime_dimensions[2] <= 12.0, "GLB +Z depth contract failed")
    require(runtime_maximum[0] > abs(runtime_minimum[0]) + 3.0, "GLB +X bow contract failed")

    propeller_node = nodes[PROPELLER_NAME]
    propeller_primitive = meshes[propeller_node["mesh"]]["primitives"][0]
    propeller_position = accessors[propeller_primitive["attributes"]["POSITION"]]
    propeller_minimum = propeller_position.get("min")
    propeller_maximum = propeller_position.get("max")
    require(propeller_minimum is not None and propeller_maximum is not None, "propeller POSITION bounds are absent")
    propeller_dimensions = [propeller_maximum[index] - propeller_minimum[index] for index in range(3)]
    require(abs(propeller_minimum[0] + propeller_maximum[0]) < EPSILON, "GLB propeller hub is off local X origin")
    require(propeller_dimensions[0] <= 2.1, "GLB propeller is too thick along local X")
    require(propeller_dimensions[1] >= 7.5 and propeller_dimensions[2] >= 7.5, "GLB propeller blade span is too small")


def export_assets(objects: Sequence[bpy.types.Object]) -> None:
    for object_ in bpy.context.scene.objects:
        object_.select_set(False)
    for object_ in objects:
        object_.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]

    save_result = bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH), check_existing=False)
    require(save_result == {"FINISHED"}, f"Blender save returned {save_result}")
    require(BLEND_PATH.is_file() and BLEND_PATH.stat().st_size > 0, ".blend was not saved")

    export_result = bpy.ops.export_scene.gltf(
        filepath=str(GLB_PATH),
        check_existing=False,
        export_format="GLB",
        use_selection=True,
        export_cameras=False,
        export_lights=False,
        export_animations=False,
        export_skins=False,
        export_morph=False,
        export_normals=True,
        export_tangents=False,
        export_texcoords=False,
        export_extras=True,
        export_yup=True,
        export_apply=False,
        export_draco_mesh_compression_enable=False,
        export_meshopt_compression_enable=False,
        export_use_gltfpack=False,
    )
    require(export_result == {"FINISHED"}, f"glTF export returned {export_result}")
    require(GLB_PATH.is_file() and GLB_PATH.stat().st_size > 0, ".glb was not exported")
    validate_glb(GLB_PATH)


def main() -> None:
    require(REPOSITORY_ROOT.joinpath("CMakeLists.txt").is_file(), "repository root was not found")
    SOURCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    RUNTIME_DIRECTORY.mkdir(parents=True, exist_ok=True)
    require(SOURCE_DIRECTORY.is_dir(), f"source directory does not exist: {SOURCE_DIRECTORY}")
    require(RUNTIME_DIRECTORY.is_dir(), f"runtime directory does not exist: {RUNTIME_DIRECTORY}")

    reset_scene()
    material = create_material()
    objects = (
        create_hull(material),
        create_sail(material),
        create_control_surfaces(material),
        create_propeller(material),
    )
    validate_scene(objects, material)
    export_assets(objects)

    hull_minimum, hull_maximum = object_bounds(bpy.data.objects[HULL_NAME])
    propeller = bpy.data.objects[PROPELLER_NAME]
    print("C0 submarine prototype generated successfully")
    print(f"Blender: {bpy.app.version_string}")
    print(f"Hull bounds (Blender XYZ): min={hull_minimum}, max={hull_maximum}")
    print(f"Propeller hub center: {tuple(propeller.location)}; local rotation axis: +X")
    print(f"Source: {BLEND_PATH} ({BLEND_PATH.stat().st_size} bytes)")
    print(f"Runtime: {GLB_PATH} ({GLB_PATH.stat().st_size} bytes)")
    print("Runtime contract: glTF +X=bow, +Y=up, clean rotations/scales; propeller hub translation preserved")


if __name__ == "__main__":
    main()
