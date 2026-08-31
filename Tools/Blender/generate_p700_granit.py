"""Generate and validate the DeepRun P-700 Granit game asset.

The authored asset follows the accepted DeepRun coordinate contract:
X = missile forward, Y = up, Z = toward the camera.  Blender's glTF exporter
performs its normal Y-up conversion; no corrective object rotations are used.
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path
from typing import Iterable, Sequence

import bmesh
import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "Content" / "Weapons" / "P700"
REFS = ASSET / "References"
TEXTURES = ASSET / "Textures"
BLEND = ASSET / "P700_Granit.blend"
GLB = ASSET / "P700_Granit.glb"
PREVIEWS = ASSET / "Previews"
REPORT = ASSET / "validation_report.txt"

BODY_MATERIAL = "MAT_P700_Granit"
INLET_MATERIAL = "MAT_P700_Granit_Inlet"
COLLISION_MATERIAL = "MAT_P700_Collision_Debug"
LOD_NAMES = ("LOD0", "LOD1", "LOD2", "LOD3")
EPSILON = 1.0e-4


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(f"P700 validation failed: {message}")


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
        bpy.data.images,
    ):
        for datablock in list(datablocks):
            datablocks.remove(datablock)
    scene = bpy.context.scene
    scene.name = "P700_Granit_Asset"
    scene.world = None
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    bpy.context.preferences.filepaths.save_version = 0


def make_collection(name: str) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def make_texture(name: str, color: tuple[float, float, float, float]) -> bpy.types.Image:
    path = TEXTURES / f"{name}.png"
    image = bpy.data.images.new(name, width=2048, height=2048, alpha=False)
    image.generated_color = color
    image.filepath_raw = str(path)
    image.file_format = "PNG"
    image.save()
    return image


def make_materials() -> tuple[bpy.types.Material, bpy.types.Material, bpy.types.Material]:
    TEXTURES.mkdir(parents=True, exist_ok=True)
    base = make_texture("P700_Granit_BaseColor_2K", (0.105, 0.135, 0.145, 1.0))
    normal = make_texture("P700_Granit_Normal_2K", (0.5, 0.5, 1.0, 1.0))
    roughness = make_texture("P700_Granit_Roughness_2K", (0.68, 0.68, 0.68, 1.0))
    metallic = make_texture("P700_Granit_Metallic_2K", (0.78, 0.78, 0.78, 1.0))
    make_texture("P700_Granit_AO_2K", (1.0, 1.0, 1.0, 1.0))

    material = bpy.data.materials.new(BODY_MATERIAL)
    material.use_nodes = True
    material.diffuse_color = (0.105, 0.135, 0.145, 1.0)
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    bsdf = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    require(bsdf is not None, "Principled BSDF is missing")
    base_node = nodes.new("ShaderNodeTexImage")
    base_node.name = "P700 Base Color 2K"
    base_node.image = base
    rough_node = nodes.new("ShaderNodeTexImage")
    rough_node.name = "P700 Roughness 2K"
    rough_node.image = roughness
    rough_node.image.colorspace_settings.name = "Non-Color"
    metal_node = nodes.new("ShaderNodeTexImage")
    metal_node.name = "P700 Metallic 2K"
    metal_node.image = metallic
    metal_node.image.colorspace_settings.name = "Non-Color"
    normal_node = nodes.new("ShaderNodeTexImage")
    normal_node.name = "P700 Normal 2K"
    normal_node.image = normal
    normal_node.image.colorspace_settings.name = "Non-Color"
    normal_map = nodes.new("ShaderNodeNormalMap")
    links.new(base_node.outputs["Color"], bsdf.inputs["Base Color"])
    links.new(rough_node.outputs["Color"], bsdf.inputs["Roughness"])
    links.new(metal_node.outputs["Color"], bsdf.inputs["Metallic"])
    links.new(normal_node.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Roughness"].default_value = 0.68
    bsdf.inputs["Metallic"].default_value = 0.78
    material["deeprun_texture_resolution"] = "2048x2048"
    material["deeprun_texture_set"] = "BaseColor, Normal, Roughness, Metallic, AO"

    inlet = bpy.data.materials.new(INLET_MATERIAL)
    inlet.use_nodes = True
    inlet.diffuse_color = (0.012, 0.016, 0.018, 1.0)
    inlet_bsdf = next((node for node in inlet.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    require(inlet_bsdf is not None, "inlet Principled BSDF is missing")
    inlet_bsdf.inputs["Base Color"].default_value = (0.012, 0.016, 0.018, 1.0)
    inlet_bsdf.inputs["Metallic"].default_value = 0.82
    inlet_bsdf.inputs["Roughness"].default_value = 0.38

    collision = bpy.data.materials.new(COLLISION_MATERIAL)
    collision.diffuse_color = (0.8, 0.06, 0.02, 1.0)
    return material, inlet, collision


def link_object(object_: bpy.types.Object, collection: bpy.types.Collection) -> None:
    collection.objects.link(object_)


def create_mesh(
    name: str,
    vertices: Sequence[tuple[float, float, float]],
    faces: Sequence[Sequence[int]],
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    parent: bpy.types.Object | None = None,
    smooth: bool = True,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(material)
    mesh.update(calc_edges=True)
    edit = bmesh.new()
    edit.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(edit, faces=list(edit.faces))
    edit.to_mesh(mesh)
    edit.free()
    if smooth:
        for polygon in mesh.polygons:
            polygon.use_smooth = True
    object_ = bpy.data.objects.new(name, mesh)
    link_object(object_, collection)
    object_.parent = parent
    object_["deeprun_forward_axis"] = "+X"
    object_["deeprun_up_axis"] = "+Y"
    object_["deeprun_asset"] = "P700_Granit"
    return object_


def add_bevel(object_: bpy.types.Object, width: float, segments: int) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    object_.select_set(True)
    bpy.context.view_layer.objects.active = object_
    modifier = object_.modifiers.new("Controlled edge bevel", "BEVEL")
    modifier.width = width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    object_.select_set(False)


def add_subdivision(object_: bpy.types.Object, levels: int) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    object_.select_set(True)
    bpy.context.view_layer.objects.active = object_
    modifier = object_.modifiers.new("Silhouette subdivision", "SUBSURF")
    modifier.subdivision_type = "CATMULL_CLARK"
    modifier.levels = levels
    modifier.render_levels = levels
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    object_.select_set(False)


def append_loft(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    stations: Sequence[tuple[float, float]],
    radial: int,
    cap_start: bool = True,
    cap_end: bool = True,
) -> tuple[list[int], list[int]]:
    rings: list[list[int]] = []
    for x, radius in stations:
        ring: list[int] = []
        for index in range(radial):
            angle = math.tau * index / radial
            ring.append(len(vertices))
            vertices.append((x, radius * math.cos(angle), radius * math.sin(angle)))
        rings.append(ring)
    for previous, current in zip(rings, rings[1:]):
        for index in range(radial):
            nxt = (index + 1) % radial
            faces.append((previous[index], current[index], current[nxt], previous[nxt]))
    if cap_start:
        ring_index, station = 0, stations[0]
        center = len(vertices)
        vertices.append((station[0], 0.0, 0.0))
        ring = rings[ring_index]
        for index in range(radial):
            nxt = (index + 1) % radial
            faces.append((center, ring[nxt], ring[index]))
    if cap_end:
        ring_index, station = len(rings) - 1, stations[-1]
        center = len(vertices)
        vertices.append((station[0], 0.0, 0.0))
        ring = rings[ring_index]
        for index in range(radial):
            nxt = (index + 1) % radial
            faces.append((center, ring[index], ring[nxt]))
    return rings[0], rings[-1]


def append_inner_cavity(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    front_ring: Sequence[int],
    x_back: float,
    radius_back: float,
    x_front: float,
    radius_front: float,
    radial: int,
) -> None:
    """Close a shallow internal cavity while leaving its front opening open."""
    back_ring: list[int] = []
    inner_front: list[int] = []
    for x, radius, target in ((x_back, radius_back, back_ring), (x_front, radius_front, inner_front)):
        for index in range(radial):
            angle = math.tau * index / radial
            target.append(len(vertices))
            vertices.append((x, radius * math.cos(angle), radius * math.sin(angle)))
    for index in range(radial):
        nxt = (index + 1) % radial
        faces.append((back_ring[index], back_ring[nxt], inner_front[nxt], inner_front[index]))
    center = len(vertices)
    vertices.append((x_back, 0.0, 0.0))
    for index in range(radial):
        nxt = (index + 1) % radial
        faces.append((center, back_ring[index], back_ring[nxt]))
    for index in range(radial):
        nxt = (index + 1) % radial
        faces.append((front_ring[index], front_ring[nxt], inner_front[nxt], inner_front[index]))


def append_x_prism(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    profile: Sequence[tuple[float, float]],
    half_depth_z: float,
    pivot: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> None:
    base = len(vertices)
    px, py, pz = pivot
    vertices.extend((x - px, y - py, -half_depth_z - pz) for x, y in profile)
    vertices.extend((x - px, y - py, half_depth_z - pz) for x, y in profile)
    count = len(profile)
    faces.append(tuple(base + index for index in reversed(range(count))))
    faces.append(tuple(base + count + index for index in range(count)))
    for index in range(count):
        nxt = (index + 1) % count
        faces.append((base + index, base + nxt, base + count + nxt, base + count + index))


def append_z_prism(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    profile: Sequence[tuple[float, float]],
    half_height_y: float,
    pivot: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> None:
    base = len(vertices)
    px, py, pz = pivot
    vertices.extend((x - px, -half_height_y - py, z - pz) for x, z in profile)
    vertices.extend((x - px, half_height_y - py, z - pz) for x, z in profile)
    count = len(profile)
    faces.append(tuple(base + index for index in reversed(range(count))))
    faces.append(tuple(base + count + index for index in range(count)))
    for index in range(count):
        nxt = (index + 1) % count
        faces.append((base + index, base + nxt, base + count + nxt, base + count + index))


def append_cylinder_x(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    x0: float,
    x1: float,
    radius: float,
    radial: int,
    center_y: float = 0.0,
    center_z: float = 0.0,
) -> None:
    base = len(vertices)
    for x in (x0, x1):
        for index in range(radial):
            angle = math.tau * index / radial
            vertices.append((x, center_y + radius * math.cos(angle), center_z + radius * math.sin(angle)))
    faces.append(tuple(base + index for index in reversed(range(radial))))
    faces.append(tuple(base + radial + index for index in range(radial)))
    for index in range(radial):
        nxt = (index + 1) % radial
        faces.append((base + index, base + nxt, base + radial + nxt, base + radial + index))


def append_annular_ring(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    x0: float,
    x1: float,
    outer0: float,
    outer1: float,
    inner0: float,
    inner1: float,
    radial: int,
) -> None:
    rings: list[list[int]] = []
    for x, radius in ((x0, outer0), (x1, outer1), (x1, inner1), (x0, inner0)):
        ring = []
        for index in range(radial):
            angle = math.tau * index / radial
            ring.append(len(vertices))
            vertices.append((x, radius * math.cos(angle), radius * math.sin(angle)))
        rings.append(ring)
    for a, b in ((0, 1), (1, 2), (2, 3), (3, 0)):
        for index in range(radial):
            nxt = (index + 1) % radial
            faces.append((rings[a][index], rings[b][index], rings[b][nxt], rings[a][nxt]))


def append_tapered_tangent_box(
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    x0: float,
    x1: float,
    radius0: float,
    radius1: float,
    angle: float,
    width: float,
    depth: float,
) -> None:
    """Append a closed intake slot following the tapered nose surface."""
    radial = Vector((0.0, math.cos(angle), math.sin(angle)))
    tangent = Vector((0.0, -math.sin(angle), math.cos(angle)))
    corners: list[int] = []
    for x, radius in ((x0, radius0), (x1, radius1)):
        for radial_offset, tangent_offset in (
            (-depth * 0.5, -width * 0.5),
            (-depth * 0.5, width * 0.5),
            (depth * 0.5, width * 0.5),
            (depth * 0.5, -width * 0.5),
        ):
            point = Vector((x, 0.0, 0.0)) + radial * (radius + radial_offset) + tangent * tangent_offset
            corners.append(len(vertices))
            vertices.append(tuple(point))
    faces.extend((
        (corners[0], corners[1], corners[2], corners[3]),
        (corners[4], corners[7], corners[6], corners[5]),
        (corners[0], corners[4], corners[5], corners[1]),
        (corners[1], corners[5], corners[6], corners[2]),
        (corners[2], corners[6], corners[7], corners[3]),
        (corners[3], corners[7], corners[4], corners[0]),
    ))


def create_body(lod: int, material: bpy.types.Material, collection: bpy.types.Collection, root: bpy.types.Object) -> list[bpy.types.Object]:
    radial = (128, 88, 64, 16)[lod]
    stations = (
        (-4.38, 0.285), (-4.18, 0.34), (-3.92, 0.38), (-3.40, 0.412),
        (-2.55, 0.432), (-1.45, 0.44), (-0.20, 0.442), (1.10, 0.438),
        (2.15, 0.425), (2.90, 0.405), (3.50, 0.370), (4.08, 0.327),
    )
    if lod == 0:
        dense: list[tuple[float, float]] = []
        for index in range(len(stations) - 1):
            start, end = stations[index], stations[index + 1]
            dense.append(start)
            for step in range(1, 4):
                t = step / 4.0
                dense.append((start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t))
        dense.append(stations[-1])
        stations = tuple(dense)
    elif lod == 1:
        stations = tuple(stations)
    elif lod == 2:
        sampled = tuple(stations[::2])
        stations = sampled if sampled[-1] == stations[-1] else sampled + (stations[-1],)
    elif lod >= 3:
        sampled = tuple(stations[::3])
        stations = sampled if sampled[-1] == stations[-1] else sampled + (stations[-1],)
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    _, front_ring = append_loft(vertices, faces, stations, radial, cap_end=False)
    append_inner_cavity(vertices, faces, front_ring, stations[-1][0] - 0.20, 0.225, stations[-1][0], 0.235, radial)
    body = create_mesh(f"P700_LOD{lod}_Body", vertices, faces, material, collection, root)
    body["deeprun_role"] = "primary missile body"
    if lod <= 1:
        add_bevel(body, 0.006 if lod == 0 else 0.004, 2 if lod == 0 else 1)
    if lod == 1:
        add_subdivision(body, 1)

    objects = [body]
    if lod <= 1:
        band_count = 4 if lod == 0 else 2
        # The supplied orthographic scheme has section breaks at the aft
        # shoulder, central body, forebody and intake-band rear edge.  Use the
        # local interpolated body radius so these remain seams, not collars.
        band_positions = (-4.05, -1.99, 1.58, 3.62) if lod == 0 else (-4.05, 1.58)
        for index, x in enumerate(band_positions[:band_count]):
            band_vertices: list[tuple[float, float, float]] = []
            band_faces: list[tuple[int, ...]] = []
            radius = next((station_radius for station_x, station_radius in stations if abs(station_x - x) < EPSILON), None)
            if radius is None:
                for start, end in zip(stations, stations[1:]):
                    if start[0] <= x <= end[0]:
                        t = (x - start[0]) / (end[0] - start[0])
                        radius = start[1] + (end[1] - start[1]) * t
                        break
            require(radius is not None, f"panel band {index + 1:02d} has no body station")
            radius += 0.006
            append_annular_ring(band_vertices, band_faces, x - 0.014, x + 0.014, radius, radius, radius * 0.998, radius * 0.998, radial)
            band = create_mesh(f"P700_LOD{lod}_PanelBand_{index + 1:02d}", band_vertices, band_faces, material, collection, root)
            band["deeprun_role"] = "forebody panel seam"
            objects.append(band)
    return objects


def create_inlet(lod: int, material: bpy.types.Material, inlet_material: bpy.types.Material, collection: bpy.types.Collection, root: bpy.types.Object) -> list[bpy.types.Object]:
    radial = (128, 88, 64, 16)[lod]
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    # The SVG has a continuous tapered nose silhouette.  The annular intake
    # follows that taper and stays contour-neutral; it is not an external
    # flare or a free-standing torus.
    append_annular_ring(vertices, faces, 3.62, 4.43, 0.365, 0.300, 0.245, 0.220, radial)
    ring = create_mesh(f"P700_LOD{lod}_InletRing", vertices, faces, material, collection, root)
    ring["deeprun_role"] = "continuous tapered annular intake cowl; no external flange"
    objects = [ring]

    duct_vertices: list[tuple[float, float, float]] = []
    duct_faces: list[tuple[int, ...]] = []
    append_annular_ring(duct_vertices, duct_faces, 3.42, 4.43, 0.245, 0.220, 0.205, 0.180, radial)
    # Eight equal shallow recessed intake marks wrap around the complete
    # tapered intake cone with uniform angular spacing.
    for index in range(8):
        angle = math.tau * index / 8.0
        append_tapered_tangent_box(duct_vertices, duct_faces, 3.72, 4.30, 0.358, 0.309, angle, 0.055, 0.008)
    duct = create_mesh(f"P700_LOD{lod}_InletDuct", duct_vertices, duct_faces, inlet_material, collection, root)
    duct["deeprun_role"] = "circumferential intake duct with eight equally spaced nose slots"
    objects.append(duct)

    nose_vertices: list[tuple[float, float, float]] = []
    nose_faces: list[tuple[int, ...]] = []
    # Pointed intake centerbody: its larger root follows the reference,
    # while the reduced diameter at the cowl leaves a clear annular passage.
    append_loft(
        nose_vertices,
        nose_faces,
        ((3.62, 0.190), (4.00, 0.180), (4.43, 0.150), (4.64, 0.120), (4.82, 0.080), (4.94, 0.040), (5.0, 0.010)),
        radial,
    )
    nose = create_mesh(f"P700_LOD{lod}_NoseCone", nose_vertices, nose_faces, material, collection, root)
    objects.append(nose)

    cap_vertices: list[tuple[float, float, float]] = []
    cap_faces: list[tuple[int, ...]] = []
    append_loft(cap_vertices, cap_faces, ((4.18, 0.30), (4.43, 0.415), (4.70, 0.235), (5.0, 0.035)), radial)
    cap = create_mesh(f"P700_LOD{lod}_Launch_IntakeCap", cap_vertices, cap_faces, material, collection, root)
    cap["deeprun_role"] = "underwater launch intake plug; discarded after water exit"
    cap["deeprun_state"] = "LAUNCH_ONLY"
    objects.append(cap)
    return objects


def create_fins(lod: int, material: bpy.types.Material, collection: bpy.types.Collection, root: bpy.types.Object) -> list[bpy.types.Object]:
    objects: list[bpy.types.Object] = []
    # In the project axis contract the deployable wings use +/-Z, while the
    # vertical tail surfaces use +/-Y.  The same geometry is mirrored without
    # introducing negative object scales.
    # The plan-view reference places the swept main wings behind the forebody
    # midpoint and gives them a shorter, more trapezoidal spanwise chord.
    wing_profile = ((0.78, 0.34), (-0.15, 1.30), (-1.86, 1.27), (-1.66, 0.34))
    for side, sign in (("L", 1.0), ("R", -1.0)):
        profile = tuple((x, z * sign) for x, z in wing_profile)
        vertices: list[tuple[float, float, float]] = []
        faces: list[tuple[int, ...]] = []
        pivot = (0.34, 0.0, sign * 0.34)
        append_z_prism(vertices, faces, profile, 0.040 if lod < 2 else 0.025, pivot)
        wing = create_mesh(f"P700_LOD{lod}_Wing_{side}", vertices, faces, material, collection, root)
        wing.location = pivot
        wing["deeprun_role"] = "deployable main wing"
        wing["deeprun_pivot"] = "body junction near x=0.34"
        if lod <= 1:
            add_bevel(wing, 0.025 if lod == 0 else 0.014, 2 if lod == 0 else 1)
        objects.append(wing)

    # The SVG side elevation puts the cross-tail farther aft, nearly over the
    # rear flight/booster interface.
    vertical_profile = ((-2.62, 0.27), (-3.90, 1.19), (-4.36, 1.15), (-4.63, 0.29))
    for name, sign in (("Up", 1.0), ("Down", -1.0)):
        profile = tuple((x, z * sign) for x, z in vertical_profile)
        vertices = []
        faces = []
        pivot = (-3.02, sign * 0.27, 0.0)
        append_x_prism(vertices, faces, profile, 0.040 if lod < 2 else 0.025, pivot)
        tail = create_mesh(f"P700_LOD{lod}_Tail_{name}", vertices, faces, material, collection, root)
        tail.location = pivot
        tail["deeprun_role"] = "folding cross-tail surface"
        if lod <= 1:
            add_bevel(tail, 0.022 if lod == 0 else 0.012, 2 if lod == 0 else 1)
        objects.append(tail)

    horizontal_profile = ((-2.62, 0.27), (-3.90, 1.12), (-4.36, 1.08), (-4.63, 0.29))
    for name, sign in (("L", 1.0), ("R", -1.0)):
        profile = tuple((x, z * sign) for x, z in horizontal_profile)
        vertices = []
        faces = []
        pivot = (-3.02, 0.0, sign * 0.27)
        append_z_prism(vertices, faces, profile, 0.040 if lod < 2 else 0.025, pivot)
        tail = create_mesh(f"P700_LOD{lod}_Tail_Horizontal_{name}", vertices, faces, material, collection, root)
        tail.location = pivot
        tail["deeprun_role"] = "folding cross-tail surface"
        if lod <= 1:
            add_bevel(tail, 0.022 if lod == 0 else 0.012, 2 if lod == 0 else 1)
        objects.append(tail)
    return objects


def create_booster_and_engine(lod: int, material: bpy.types.Material, inlet_material: bpy.types.Material, collection: bpy.types.Collection, root: bpy.types.Object) -> list[bpy.types.Object]:
    radial = (64, 48, 24, 12)[lod]
    objects: list[bpy.types.Object] = []
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    append_cylinder_x(vertices, faces, -4.98, -3.95, 0.39, radial)
    booster = create_mesh(f"P700_LOD{lod}_Booster", vertices, faces, material, collection, root)
    booster["deeprun_role"] = "external launch booster; detachable"
    booster["deeprun_uncertainty"] = "external canister/booster proportions are reference-led"
    objects.append(booster)

    engine_vertices: list[tuple[float, float, float]] = []
    engine_faces: list[tuple[int, ...]] = []
    # The flight engine is the tapered rear continuation of the missile body.
    # Its forward shoulder overlaps the body, so no undersized floating stub
    # or axial gap is left at the body/engine junction.
    append_loft(engine_vertices, engine_faces, ((-4.90, 0.205), (-4.65, 0.235), (-4.38, 0.285)), radial)
    engine = create_mesh(f"P700_LOD{lod}_Engine", engine_vertices, engine_faces, material, collection, root)
    engine["deeprun_role"] = "external sustainer engine housing"
    objects.append(engine)

    nozzle_vertices: list[tuple[float, float, float]] = []
    nozzle_faces: list[tuple[int, ...]] = []
    # This is the attached flight sustainer exhaust nozzle, not the underwater
    # booster nozzle.  It starts exactly on the rear Engine section.
    append_annular_ring(nozzle_vertices, nozzle_faces, -4.90, -4.98, 0.205, 0.210, 0.145, 0.160, radial)
    nozzle = create_mesh(f"P700_LOD{lod}_Nozzle", nozzle_vertices, nozzle_faces, inlet_material, collection, root)
    nozzle["deeprun_role"] = "flight sustainer exhaust nozzle; attached to Engine"
    nozzle["deeprun_state"] = "FLIGHT"
    objects.append(nozzle)

    if lod <= 1:
        for index, (y, z) in enumerate(((-0.14, -0.14), (-0.14, 0.14), (0.14, -0.14), (0.14, 0.14)), 1):
            nv: list[tuple[float, float, float]] = []
            nf: list[tuple[int, ...]] = []
            append_cylinder_x(nv, nf, -4.98, -4.84, 0.052, max(12, radial // 4), y, z)
            nozzle = create_mesh(f"P700_LOD{lod}_BoosterNozzle_{index:02d}", nv, nf, inlet_material, collection, root)
            nozzle["deeprun_role"] = "underwater launch booster nozzle; discarded after water exit"
            nozzle["deeprun_state"] = "LAUNCH_ONLY"
            objects.append(nozzle)
    return objects


def create_lod(lod: int, material: bpy.types.Material, inlet_material: bpy.types.Material, collection: bpy.types.Collection, root: bpy.types.Object) -> list[bpy.types.Object]:
    return create_body(lod, material, collection, root) + create_inlet(lod, material, inlet_material, collection, root) + create_fins(lod, material, collection, root) + create_booster_and_engine(lod, material, inlet_material, collection, root)


def create_collision(collection: bpy.types.Collection, material: bpy.types.Material) -> bpy.types.Object:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    append_loft(vertices, faces, ((-4.98, 0.31), (-4.3, 0.40), (-2.0, 0.45), (2.0, 0.45), (4.25, 0.28), (5.0, 0.08)), 12)
    append_z_prism(vertices, faces, ((1.25, 0.30), (-0.35, 1.30), (-1.75, 1.25), (-1.45, 0.30)), 0.06)
    append_z_prism(vertices, faces, ((1.25, -0.30), (-0.35, -1.30), (-1.75, -1.25), (-1.45, -0.30)), 0.06)
    append_x_prism(vertices, faces, ((-3.0, 0.25), (-3.3, 1.18), (-4.1, 1.12), (-4.2, 0.25)), 0.06)
    append_x_prism(vertices, faces, ((-3.0, -0.25), (-3.3, -1.18), (-4.1, -1.12), (-4.2, -0.25)), 0.06)
    collision = create_mesh("P700_COLLISION", vertices, faces, material, collection, None, smooth=False)
    collision["deeprun_role"] = "coarse physics proxy; non-rendering"
    return collision


def create_helpers(helper_collection: bpy.types.Collection) -> bpy.types.Object:
    root = bpy.data.objects.new("P700_Granit_ROOT", None)
    helper_collection.objects.link(root)
    root.empty_display_type = "PLAIN_AXES"
    root["deeprun_asset"] = "P700_Granit"
    root["deeprun_forward_axis"] = "+X"
    root["deeprun_up_axis"] = "+Y"
    root["deeprun_authoring_contract"] = "DeepRun ADR-0006; glTF export_yup conversion"
    for name, location, role in (
        ("P700_Reference_Nose", (5.0, 0.0, 0.0), "front-most missile point"),
        ("P700_Reference_Tail", (-4.98, 0.0, 0.0), "tail centerline"),
    ):
        marker = bpy.data.objects.new(name, None)
        helper_collection.objects.link(marker)
        marker.location = location
        marker.empty_display_type = "SPHERE"
        marker.empty_display_size = 0.12
        marker["deeprun_role"] = role
        marker.parent = root
    return root


def unwrap(objects: Iterable[bpy.types.Object]) -> None:
    for object_ in objects:
        if object_.type != "MESH":
            continue
        bpy.ops.object.select_all(action="DESELECT")
        object_.hide_set(False)
        object_.select_set(True)
        bpy.context.view_layer.objects.active = object_
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.03)
        bpy.ops.object.mode_set(mode="OBJECT")
        object_.select_set(False)


def mesh_bounds(objects: Iterable[bpy.types.Object]) -> tuple[Vector, Vector]:
    bpy.context.view_layer.update()
    coordinates = [object_.matrix_world @ vertex.co for object_ in objects for vertex in object_.data.vertices]
    require(bool(coordinates), "no vertices for bounds")
    return Vector((min(v.x for v in coordinates), min(v.y for v in coordinates), min(v.z for v in coordinates))), Vector((max(v.x for v in coordinates), max(v.y for v in coordinates), max(v.z for v in coordinates)))


def triangle_count(objects: Iterable[bpy.types.Object]) -> tuple[int, int]:
    vertices = 0
    triangles = 0
    for object_ in objects:
        if object_.type != "MESH":
            continue
        vertices += len(object_.data.vertices)
        triangles += sum(max(0, len(polygon.vertices) - 2) for polygon in object_.data.polygons)
    return vertices, triangles


def validate_mesh_hygiene(objects: Sequence[bpy.types.Object]) -> None:
    require(all(object_.type == "MESH" for object_ in objects), "LOD list contains a non-mesh")
    require(all(object_.scale == Vector((1.0, 1.0, 1.0)) for object_ in objects), "unapplied scale")
    require(all(sum(abs(value) for value in object_.rotation_euler) < EPSILON for object_ in objects), "unapplied rotation")
    for object_ in objects:
        require(len(object_.data.materials) == 1, f"{object_.name} has unexpected material slots")
        require(len(object_.data.uv_layers) > 0, f"{object_.name} has no UV map")
        require(not object_.data.validate(verbose=False, clean_customdata=False), f"{object_.name} has invalid mesh data")
        edit = bmesh.new()
        edit.from_mesh(object_.data)
        require(not any(edge.is_boundary for edge in edit.edges), f"{object_.name} has open boundary edges")
        require(all(face.normal.length > EPSILON for face in edit.faces), f"{object_.name} has invalid face normals")
        edit.free()
        for polygon in object_.data.polygons:
            require(polygon.area > 1.0e-8, f"{object_.name} has degenerate face")


def look_at(camera: bpy.types.Object, target: Vector) -> None:
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()


def render_preview(
    name: str,
    camera_location: tuple[float, float, float],
    target: tuple[float, float, float],
    water: bool = False,
    orthographic: bool = False,
    ortho_scale: float = 7.0,
) -> None:
    scene = bpy.context.scene
    camera_data = bpy.data.cameras.new(f"{name}_Camera")
    camera = bpy.data.objects.new(f"{name}_Camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = camera_location
    if orthographic:
        camera_data.type = "ORTHO"
        camera_data.ortho_scale = ortho_scale
    else:
        camera_data.lens = 58.0
    look_at(camera, Vector(target))
    scene.camera = camera
    light_data = bpy.data.lights.new(f"{name}_Key", "AREA")
    light_data.energy = 1300.0
    light_data.shape = "DISK"
    light_data.size = 8.0
    light = bpy.data.objects.new(f"{name}_Key", light_data)
    scene.collection.objects.link(light)
    light.location = (1.5, 6.0, 8.0)
    look_at(light, Vector((0.0, 0.0, 0.0)))
    fill_data = bpy.data.lights.new(f"{name}_Fill", "AREA")
    fill_data.energy = 650.0
    fill_data.size = 12.0
    fill = bpy.data.objects.new(f"{name}_Fill", fill_data)
    scene.collection.objects.link(fill)
    fill.location = (-5.0, 2.0, -6.0)
    look_at(fill, Vector((0.0, 0.0, 0.0)))
    water_object = None
    if water:
        water_data = bpy.data.meshes.new(f"{name}_WaterMesh")
        water_data.from_pydata([(-12, -0.03, -6), (12, -0.03, -6), (12, -0.03, 6), (-12, -0.03, 6)], [], [(0, 1, 2, 3)])
        water_object = bpy.data.objects.new(f"{name}_WaterValidationOnly", water_data)
        scene.collection.objects.link(water_object)
        water_object.data.materials.append(bpy.data.materials[INLET_MATERIAL])
    scene.render.resolution_x = 960
    scene.render.resolution_y = 640
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(PREVIEWS / f"{name}.png")
    scene.render.film_transparent = False
    launch_visibility = {
        object_: object_.hide_render
        for object_ in scene.objects
        if object_.type == "MESH" and "launch" in str(object_.get("deeprun_role", ""))
    }
    if water:
        for object_ in launch_visibility:
            if object_.name.startswith("P700_LOD0_"):
                object_.hide_render = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("P700_PreviewWorld")
    scene.world.color = (0.012, 0.018, 0.028)
    bpy.ops.render.render(write_still=True)
    for object_, hidden in launch_visibility.items():
        object_.hide_render = hidden
    for object_ in (camera, light, fill, water_object):
        if object_ is not None:
            bpy.data.objects.remove(object_, do_unlink=True)
    scene.camera = None


def create_previews() -> None:
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    render_preview("preview_side", (0.0, 0.0, 22.0), (0.0, 0.0, 0.0), orthographic=True, ortho_scale=12.0)
    render_preview("preview_front_3q", (15.0, 6.0, 15.0), (0.0, 0.0, 0.0))
    render_preview("preview_rear_3q", (-15.0, 6.0, 13.0), (-0.8, 0.0, 0.0))
    render_preview("preview_top", (0.0, 22.0, 0.0), (0.0, 0.0, 0.0), orthographic=True, ortho_scale=12.0)
    render_preview("preview_water_launch", (17.0, 7.0, 18.0), (0.0, 0.0, 0.0), water=True)
    render_reference_overlay()
    for datablock in list(bpy.data.cameras):
        bpy.data.cameras.remove(datablock)
    for datablock in list(bpy.data.lights):
        bpy.data.lights.remove(datablock)


def render_reference_overlay() -> None:
    scene = bpy.context.scene
    camera_data = bpy.data.cameras.new("ReferenceOverlay_Camera")
    camera = bpy.data.objects.new("ReferenceOverlay_Camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = (0.0, 0.0, 22.0)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 7.0
    look_at(camera, Vector((0.0, 0.0, 0.0)))
    scene.camera = camera
    bpy.ops.object.select_all(action="DESELECT")
    import_result = bpy.ops.import_curve.svg(filepath=str(REFS / "P-700-Granit_sketch.svg"))
    require(import_result == {"FINISHED"}, "SVG reference import failed")
    reference_objects = [object_ for object_ in bpy.context.scene.objects if object_.type == "CURVE"]
    require(reference_objects, "SVG reference imported no curves")
    for object_ in reference_objects:
        object_.scale = (30.0, 30.0, 30.0)
        object_.location.z = -0.72
    bpy.context.view_layer.update()
    reference_points = [object_.matrix_world @ Vector(corner) for object_ in reference_objects for corner in object_.bound_box]
    reference_minimum = Vector((min(point.x for point in reference_points), min(point.y for point in reference_points), min(point.z for point in reference_points)))
    reference_maximum = Vector((max(point.x for point in reference_points), max(point.y for point in reference_points), max(point.z for point in reference_points)))
    reference_center = (reference_minimum + reference_maximum) * 0.5
    for object_ in reference_objects:
        object_.location.x -= reference_center.x
        object_.location.y -= reference_center.y
    root = bpy.data.objects.get("P700_Granit_ROOT")
    require(root is not None, "asset root is missing for reference overlay")
    original_root_location = root.location.copy()
    scene.render.resolution_x = 960
    scene.render.resolution_y = 640
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(PREVIEWS / "preview_reference_overlay.png")
    scene.render.film_transparent = False
    scene.compositing_node_group = None
    light_data = bpy.data.lights.new("ReferenceOverlay_Key", "AREA")
    light_data.energy = 2400.0
    light_data.size = 10.0
    light = bpy.data.objects.new("ReferenceOverlay_Key", light_data)
    scene.collection.objects.link(light)
    light.location = (0.0, 8.0, 10.0)
    look_at(light, Vector((0.0, 0.0, 0.0)))
    body_material = bpy.data.materials[BODY_MATERIAL]
    body_bsdf = next((node for node in body_material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    require(body_bsdf is not None, "overlay Principled BSDF is missing")
    base_link = next((link for link in body_material.node_tree.links if link.to_node == body_bsdf and link.to_socket == body_bsdf.inputs["Base Color"]), None)
    base_from_socket = base_link.from_socket if base_link is not None else None
    base_to_socket = base_link.to_socket if base_link is not None else None
    if base_link is not None:
        body_material.node_tree.links.remove(base_link)
    old_base = body_bsdf.inputs["Base Color"].default_value[:]
    old_metallic = body_bsdf.inputs["Metallic"].default_value
    old_roughness = body_bsdf.inputs["Roughness"].default_value
    body_bsdf.inputs["Base Color"].default_value = (0.42, 0.08, 0.025, 1.0)
    body_bsdf.inputs["Metallic"].default_value = 0.12
    body_bsdf.inputs["Roughness"].default_value = 0.42
    bpy.ops.render.render(write_still=True)
    body_bsdf.inputs["Base Color"].default_value = old_base
    body_bsdf.inputs["Metallic"].default_value = old_metallic
    body_bsdf.inputs["Roughness"].default_value = old_roughness
    if base_from_socket is not None and base_to_socket is not None:
        body_material.node_tree.links.new(base_from_socket, base_to_socket)
    root.location = original_root_location
    for object_ in reference_objects:
        curve_data = object_.data
        bpy.data.objects.remove(object_, do_unlink=True)
        if curve_data.users == 0:
            bpy.data.curves.remove(curve_data)
    for material in list(bpy.data.materials):
        if material.name not in {BODY_MATERIAL, INLET_MATERIAL, COLLISION_MATERIAL}:
            bpy.data.materials.remove(material)
    bpy.data.objects.remove(light, do_unlink=True)
    bpy.data.lights.remove(light_data)
    bpy.data.objects.remove(camera, do_unlink=True)
    scene.camera = None


def glb_json(path: Path) -> dict:
    data = path.read_bytes()
    require(data[:4] == b"glTF", "GLB magic")
    require(struct.unpack_from("<I", data, 4)[0] == 2, "GLB version")
    offset = 12
    while offset < len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + length]
        offset += length
        if kind == 0x4E4F534A:
            return json.loads(chunk.rstrip(b" \t\r\n\0").decode("utf-8"))
    raise RuntimeError("GLB JSON chunk is absent")


def export_glb(render_objects: Sequence[bpy.types.Object]) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for object_ in render_objects:
        object_.select_set(True)
    bpy.context.view_layer.objects.active = render_objects[0]
    result = bpy.ops.export_scene.gltf(
        filepath=str(GLB),
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
        export_texcoords=True,
        export_extras=True,
        export_yup=True,
        export_apply=False,
        export_draco_mesh_compression_enable=False,
        export_meshopt_compression_enable=False,
        export_use_gltfpack=False,
    )
    require(result == {"FINISHED"}, f"GLB export result is {result}")
    require(GLB.is_file() and GLB.stat().st_size > 0, "GLB was not written")
    document = glb_json(GLB)
    require(document.get("asset", {}).get("version") == "2.0", "invalid glTF asset version")
    require("cameras" not in document and "animations" not in document, "GLB contains forbidden data")
    require("KHR_lights_punctual" not in document.get("extensionsUsed", []), "GLB contains lights")


def write_report(lod_objects: dict[str, list[bpy.types.Object]], all_render: Sequence[bpy.types.Object], bounds: tuple[Vector, Vector]) -> None:
    lines = ["P700 Granit asset validation", f"Blender = {bpy.app.version_string}", ""]
    for lod in LOD_NAMES:
        vertices, triangles = triangle_count(lod_objects[lod])
        lines.append(f"{lod}: vertices = {vertices}; triangles = {triangles}")
    minimum, maximum = bounds
    lines.extend((
        "",
        f"Meshes = {len(all_render)} LOD render meshes + 1 collision mesh",
        f"Dimensions: length = {maximum.x - minimum.x:.4f} m; width = {maximum.z - minimum.z:.4f} m; height = {maximum.y - minimum.y:.4f} m",
        "Materials = MAT_P700_Granit, MAT_P700_Granit_Inlet",
        "Textures = 5 x 2048x2048 PNG (BaseColor, Normal, Roughness, Metallic, AO)",
        f"GLB size = {GLB.stat().st_size} bytes",
        "GLB JSON validation = passed (glTF 2.0, no cameras/lights/animations)",
        "Collision = P700_COLLISION, coarse body + wing/tail proxy, excluded from GLB",
        "Launch assembly = detachable P700_*_Booster + four booster nozzles + LAUNCH_ONLY P700_*_Launch_IntakeCap",
        "Flight nose = pointed centerbody inside a wide tapered annular cowl, eight equally spaced rectangular intake slots",
        "Flight rear = body-overlapping sustainer Engine + attached flared flight Nozzle; booster nozzles are LAUNCH_ONLY",
        "Primary visual references = References/P-700-Granit_sketch.svg (flight silhouette) and References/granit_3.jpg (underwater launch state)",
        "Visual previews = orthographic side/top, front_3q, rear_3q, water_launch, reference_overlay",
        "Coordinate contract = DeepRun ADR-0006: +X forward, +Y up, +Z toward camera",
    ))
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    for directory in (ASSET, REFS, TEXTURES, PREVIEWS):
        directory.mkdir(parents=True, exist_ok=True)
    for temporary in PREVIEWS.glob("_p700_reference_overlay_*"):
        temporary.unlink(missing_ok=True)
    reset_scene()
    render_collection = make_collection("P700_RENDER")
    helper_collection = make_collection("P700_HELPERS")
    material, inlet_material, collision_material = make_materials()
    root = create_helpers(helper_collection)

    lod_objects: dict[str, list[bpy.types.Object]] = {}
    all_render: list[bpy.types.Object] = []
    for lod in range(4):
        objects = create_lod(lod, material, inlet_material, render_collection, root)
        lod_objects[f"LOD{lod}"] = objects
        all_render.extend(objects)
        for object_ in objects:
            object_.hide_render = lod != 0
            object_.hide_set(lod != 0)
    for object_ in all_render:
        if "launch" in str(object_.get("deeprun_role", "")):
            object_.hide_render = True
    collision = create_collision(helper_collection, collision_material)
    unwrap(all_render + [collision])
    collision.hide_render = True
    collision.hide_viewport = True
    collision.hide_set(True)
    validate_mesh_hygiene(all_render)
    minimum, maximum = mesh_bounds(lod_objects["LOD0"])
    length = maximum.x - minimum.x
    width = maximum.z - minimum.z
    height = maximum.y - minimum.y
    require(9.98 <= length <= 10.05, f"length is {length:.3f} m")
    require(2.55 <= width <= 2.70, f"wing span is {width:.3f} m")
    require(0.82 <= max(width, height) <= 2.70, "body/silhouette dimensions are implausible")
    require(minimum.x < 0.0 < maximum.x, "root is not near geometric center")
    counts = {lod: triangle_count(objects)[1] for lod, objects in lod_objects.items()}
    require(20000 <= counts["LOD0"] <= 55000, f"LOD0 target missed: {counts['LOD0']}")
    require(9000 <= counts["LOD1"] <= 20000, f"LOD1 target missed: {counts['LOD1']}")
    require(2500 <= counts["LOD2"] <= 7000, f"LOD2 target missed: {counts['LOD2']}")
    require(500 <= counts["LOD3"] <= 1800, f"LOD3 target missed: {counts['LOD3']}")

    create_previews()
    # Cameras, lights and water are temporary validation-only data and have
    # already been removed by create_previews().
    require(not bpy.data.cameras and not bpy.data.lights, "temporary render data was not removed")
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND), check_existing=False)
    require(BLEND.is_file() and BLEND.stat().st_size > 0, "BLEND was not saved")
    export_glb(lod_objects["LOD0"] + lod_objects["LOD1"] + lod_objects["LOD2"] + lod_objects["LOD3"])
    write_report(lod_objects, all_render, (minimum, maximum))
    print("P700 Granit generated successfully")
    print(f"LOD counts: {counts}")
    print(f"Dimensions: length={length:.4f} width={width:.4f} height={height:.4f}")
    print(f"BLEND: {BLEND} ({BLEND.stat().st_size} bytes)")
    print(f"GLB: {GLB} ({GLB.stat().st_size} bytes)")
    print(f"REPORT: {REPORT}")


if __name__ == "__main__":
    main()
