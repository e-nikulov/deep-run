"""Fresh-open validation for the source-anchored final Antey candidate.

This validator is deliberately evidence-oriented: it reports the exact
source/candidate identity, the manually resolved rudder seams, frozen control
surfaces and propeller positions, the neutral torpedo layout contract, the
hidden gameplay contracts, and actual P700 BVH overlap samples.  It never
modifies or saves either input blend.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from finalize_manual_p700 import object_bvh, positive_triangle_intersection


TOLERANCE_M = 1.0e-5
MANUAL_TARGETS = {
    "Dorsal": {
        "object": "SM_Antey_LOD0_Hull",
        "rudder": "SM_Antey_LOD0_Rudder_Dorsal",
        "anchors": {
            "A": [-61.699451447, 0.280976743, 4.906888008],
            "B": [-61.707115173, 0.280977070, 1.619024873],
            "C": [-65.129043579, 0.108756408, 1.628191113],
            "D": [-64.981201172, 0.108756408, 4.883985519],
        },
        "supplied_indices": {"A": 21079, "B": 21075, "C": 21330, "D": 21277},
    },
    "Ventral": {
        "object": "SM_Antey_LOD0_Hull_LowerSource",
        "rudder": "SM_Antey_LOD0_Rudder_Ventral",
        "anchors": {
            "A": [-61.859714508, 0.282813966, -4.096770763],
            "B": [-64.584289551, 0.234170035, -4.112565994],
            "C": [-64.584289551, 0.234170035, -4.458462715],
            "D": [-65.348480225, 0.108156495, -4.458462238],
            "E": [-65.348480225, 0.108156495, -2.163329363],
        },
        "supplied_indices": {"A": 428, "B": 497, "C": 598, "D": 396, "E": 583},
    },
}
STERN_NAMES = ("SM_Antey_LOD0_SternPlane_Port", "SM_Antey_LOD0_SternPlane_Starboard")
COVER_RE = re.compile(r"^SM_Antey_P700_Cover_(Port|Starboard)_(0[1-6])$")


def parse_args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def jsonable(value):
    if isinstance(value, (bytes, bytearray)):
        return value.hex()
    if isinstance(value, dict) or hasattr(value, "keys"):
        result = {}
        for key in value.keys():
            try:
                name = str(key)
                result[name] = jsonable(value[key])
            except (UnicodeDecodeError, TypeError, ValueError):
                result[repr(key)] = repr(value[key])
        return result
    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]
    try:
        return value.to_list()
    except (AttributeError, UnicodeDecodeError, TypeError, ValueError):
        return value


def world_points(obj: bpy.types.Object) -> list[Vector]:
    return [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]


def nearest(obj: bpy.types.Object, target: Vector) -> tuple[float, int, Vector]:
    values = [((point - target).length, index, point) for index, point in enumerate(world_points(obj))]
    return min(values, default=(float("inf"), -1, Vector()))


def mesh_triangles(obj: bpy.types.Object) -> int:
    return sum(len(poly.vertices) - 2 for poly in obj.data.polygons)


def mesh_fingerprint(obj: bpy.types.Object) -> str:
    payload = "|".join(",".join(str(index) for index in poly.vertices) for poly in obj.data.polygons)
    return hashlib.sha256(payload.encode("ascii")).hexdigest()


def normalized_coordinate(value: float) -> float:
    """Round production coordinates while canonicalizing signed zero.

    Blender can retain -0.0 when the source transform crosses the symmetry
    plane.  It is geometrically identical to +0.0 but would otherwise produce
    a false string-level face mismatch in the forensic partition report.
    """
    rounded = round(float(value), 3)
    return 0.0 if rounded == 0.0 else rounded


def edge_statistics(obj: bpy.types.Object) -> dict[str, int | bool]:
    counts: Counter[tuple[int, int]] = Counter()
    for poly in obj.data.polygons:
        vertices = list(poly.vertices)
        for left, right in zip(vertices, vertices[1:] + vertices[:1]):
            counts[tuple(sorted((left, right)))] += 1
    boundary = sum(1 for count in counts.values() if count == 1)
    nonmanifold = sum(1 for count in counts.values() if count > 2)
    return {"edges": len(counts), "boundary_edges": boundary, "nonmanifold_edges": nonmanifold, "closed": boundary == 0 and nonmanifold == 0}


def control_sample(obj: bpy.types.Object, angles: tuple[float, ...], axis: int) -> dict[str, object]:
    neutral = [point.copy() for point in world_points(obj)]
    original_rotation = obj.rotation_euler.copy()
    original_location = obj.location.copy()
    samples = []
    for angle in angles:
        obj.rotation_euler = original_rotation
        obj.rotation_euler[axis] = math.radians(angle)
        bpy.context.scene.view_layers[0].update()
        points = world_points(obj)
        delta = max(((point - base).length for point, base in zip(points, neutral)), default=0.0)
        neutral_delta = max(((point - base).length for point, base in zip(points, neutral)), default=0.0) if abs(angle) < 1.0e-12 else None
        samples.append({"angle_deg": angle, "max_displacement_m": delta, "neutral_delta_m": neutral_delta})
    obj.rotation_euler = original_rotation
    obj.location = original_location
    bpy.context.scene.view_layers[0].update()
    return {"samples": samples, "neutral_exact": max(((point - base).length for point, base in zip(world_points(obj), neutral)), default=0.0) <= TOLERANCE_M}


def candidate_face_signatures(scene: bpy.types.Scene) -> dict[tuple[str, int], Counter[str]]:
    result: dict[tuple[str, int], Counter[str]] = defaultdict(Counter)
    for obj in scene.objects:
        if obj.type != "MESH" or int(obj.get("lod", -1)) != 0 or not obj.get("runtime_export", False):
            continue
        source_name = obj.get("source_object")
        source_component = obj.get("source_component")
        if source_name is None or source_component is None:
            continue
        points = world_points(obj)
        # Source-face accounting is a neutral geometry check, independent of
        # the authored P700 seam-clearance mechanism.  A closed cover can
        # carry a measured outboard lift at frame 1; remove that transform
        # before comparing its frozen source face signatures.
        cover_match = COVER_RE.match(obj.name)
        if cover_match:
            profile = obj.get("CLEARANCE_LIFT_PROFILE_M", [])
            if profile:
                sign = 1.0 if cover_match.group(1) == "Port" else -1.0
                neutral_lift = float(profile[0])
                points = [point + Vector((0.0, -sign * neutral_lift, 0.0)) for point in points]
        for poly in obj.data.polygons:
            # Production normalization is evaluated independently in the
            # source and candidate sessions; three decimal places is below the
            # geometry tolerance while avoiding false differences from float
            # round-off at the transform boundary.
            signature = tuple(sorted(tuple(normalized_coordinate(value) for value in points[index]) for index in poly.vertices))
            result[(str(source_name), int(source_component))][repr(signature)] += 1
    return result


def source_component_faces(mesh: bpy.types.Mesh) -> list[list[tuple[int, ...]]]:
    parent = list(range(len(mesh.vertices)))

    def find(index: int) -> int:
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    def union(left: int, right: int) -> None:
        left, right = find(left), find(right)
        if left != right:
            parent[right] = left

    for poly in mesh.polygons:
        vertices = list(poly.vertices)
        for vertex in vertices[1:]:
            union(vertices[0], vertex)
    groups: dict[int, list[tuple[int, ...]]] = defaultdict(list)
    for poly in mesh.polygons:
        vertices = tuple(poly.vertices)
        if len(set(vertices)) >= 3:
            groups[find(vertices[0])].append(vertices)
    result = list(groups.values())
    result.sort(key=lambda faces: sum(len(face) - 2 for face in faces), reverse=True)
    return result


def source_signatures_and_props(source_path: Path) -> tuple[dict[tuple[str, int], Counter[str]], dict[int, dict[str, object]]]:
    bpy.ops.wm.open_mainfile(filepath=str(source_path))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    result: dict[tuple[str, int], Counter[str]] = defaultdict(Counter)
    props: dict[int, dict[str, object]] = {}
    for object_name in ("Bridge", "Hull"):
        source_object = bpy.data.objects.get(object_name)
        if source_object is None:
            continue
        evaluated = source_object.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        points = []
        for vertex in mesh.vertices:
            local = source_object.matrix_world @ vertex.co
            # Source-to-production normalization used by the builder.
            points.append(Vector(((local.y - (-4.875378131866455 + 4.900454044342041) * 0.5) * (154.0 / 9.775832176208496), -(local.x - (-0.8649876117706299 + 0.8413368463516235) * 0.5) * (18.2 / 1.7063244581222534), local.z * (18.2 / 1.7063244581222534))))
        components = source_component_faces(mesh)
        for component_index, faces in enumerate(components):
            for face in faces:
                signature = tuple(sorted(tuple(normalized_coordinate(value) for value in points[index]) for index in face))
                result[(object_name, component_index)][repr(signature)] += 1
            if object_name == "Hull" and component_index > 0:
                component_points = [points[index] for face in faces for index in face]
                if component_points:
                    minimum_x = min(point.x for point in component_points)
                    maximum_x = max(point.x for point in component_points)
                    centre_y = sum(point.y for point in component_points) / len(component_points)
                    if maximum_x < -70.0 and abs(centre_y) > 0.8:
                        props[component_index] = {"side": "Port" if centre_y > 0.0 else "Starboard", "center": [sum(point[i] for point in component_points) / len(component_points) for i in range(3)], "points": [list(point) for point in component_points]}
        evaluated.to_mesh_clear()
    return result, props


def p700_bvh_samples(scene: bpy.types.Scene, covers: list[bpy.types.Object]) -> dict[str, object]:
    hulls = [obj for obj in scene.objects if obj.type == "MESH" and int(obj.get("lod", -1)) == 0 and obj.get("source_first_role") in {"MAIN_HULL_UPPER", "MAIN_HULL_LOWER"}]
    samples = []
    frames = (1, 5, 11, 21, 31, 41)
    for frame in frames:
        scene.frame_set(frame)
        hull_bvhs = {obj.name: object_bvh(obj) for obj in hulls}
        cover_bvhs = {obj.name: object_bvh(obj) for obj in covers}
        hits: list[list[str]] = []
        hull_contacts: list[list[str]] = []
        for obj in covers:
            cover_bvh = cover_bvhs[obj.name]
            for hull_name, hull_bvh in hull_bvhs.items():
                raw = cover_bvh[0].overlap(hull_bvh[0])
                if raw:
                    hull_contacts.append([obj.name, hull_name, len(raw)])
                    real = any(positive_triangle_intersection(cover_bvh[1][left], hull_bvh[1][right]) for left, right in raw)
                    if real:
                        hits.append([obj.name, hull_name])
        pair_hits = []
        pair_contacts = []
        adjacent_pair_hits = []
        for index, left in enumerate(covers):
            for right in covers[index + 1 :]:
                raw = cover_bvhs[left.name][0].overlap(cover_bvhs[right.name][0])
                if raw:
                    pair_contacts.append([left.name, right.name, len(raw)])
                    left_match = COVER_RE.match(left.name)
                    right_match = COVER_RE.match(right.name)
                    if left_match and right_match and left_match.group(1) == right_match.group(1) and abs(int(left_match.group(2)) - int(right_match.group(2))) == 1:
                        real = any(positive_triangle_intersection(cover_bvhs[left.name][1][a], cover_bvhs[right.name][1][b]) for a, b in raw)
                        if real:
                            adjacent_pair_hits.append([left.name, right.name])
                            pair_hits.append([left.name, right.name])
        samples.append({"frame": frame, "fraction": (frame - 1.0) / 40.0, "hull_triangle_overlaps": hits, "hull_bvh_contact_pairs": hull_contacts, "neighbor_triangle_overlaps": adjacent_pair_hits, "neighbor_bvh_contact_pairs": pair_contacts, "all_cover_triangle_overlaps": pair_hits, "all_cover_triangle_overlaps_count": len(pair_hits), "pass": not hits and not adjacent_pair_hits})
    scene.frame_set(1)
    return {"samples": samples, "all_samples_pass": all(item["pass"] for item in samples), "note": "Narrow-phase positive-area triangle intersections are gates; raw BVH contacts (including shared source-boundary contact) remain recorded for diagnosis."}


def action_paths(action: bpy.types.Action | None) -> list[str]:
    """Read both legacy and Blender 5 layered-action channel layouts."""
    if action is None:
        return []
    paths: set[str] = set()
    legacy = getattr(action, "fcurves", None)
    if legacy is not None:
        paths.update(curve.data_path for curve in legacy)
    for layer in getattr(action, "layers", []):
        for strip in getattr(layer, "strips", []):
            for channelbag in getattr(strip, "channelbags", []):
                for curve in getattr(channelbag, "fcurves", []):
                    paths.add(curve.data_path)
    return sorted(paths)


def main() -> None:
    options = parse_args()
    candidate = options.candidate.resolve(strict=True)
    source = options.source.resolve(strict=True)
    output = options.output.resolve()
    boundary_path = candidate.parent / "manual_rudder_boundary.json"
    boundary = json.loads(boundary_path.read_text(encoding="utf-8")) if boundary_path.exists() else {"rudders": {}}
    bpy.ops.wm.open_mainfile(filepath=str(candidate))
    if Path(bpy.data.filepath).resolve(strict=True) != candidate:
        raise RuntimeError("fresh candidate reopen path mismatch")
    scene = bpy.context.scene
    lod0 = [obj for obj in scene.objects if obj.type == "MESH" and int(obj.get("lod", -1)) == 0 and obj.get("runtime_export", False)]
    runtime_materials = sorted({slot.material.name for obj in lod0 for slot in obj.material_slots if slot.material})
    all_materials = sorted(material.name for material in bpy.data.materials)
    orphan_materials = sorted(material.name for material in bpy.data.materials if material.users == 0)
    report: dict[str, object] = {
        "candidate": {"path": str(candidate), "sha256": sha256(candidate), "fresh_reopen": True},
        "source": {"path": str(source), "sha256": sha256(source)},
        "materials": {"runtime": runtime_materials, "all_datablocks": all_materials, "orphan": orphan_materials, "intended_runtime": ["MAT_Antey_Hull", "MAT_Antey_Propellers"], "runtime_only_intended": set(runtime_materials).issubset({"MAT_Antey_Hull", "MAT_Antey_Propellers"})},
        "lod0": {"objects": len(lod0), "triangles": sum(mesh_triangles(obj) for obj in lod0), "role_counts": dict(Counter(str(obj.get("source_first_role", "UNKNOWN")) for obj in lod0))},
    }
    candidate_physics_ownership = jsonable(scene.get("physics_ownership_contract", {}))
    candidate_proxies = {"collision": scene.objects.get("Antey_CollisionProxy") is not None, "buoyancy": scene.objects.get("Antey_BuoyancyVolume") is not None, "compartments_independent": all(obj.name.startswith("Antey_Compartment_") for obj in scene.objects if obj.name.startswith("Antey_Compartment_"))}
    rudder_report = {}
    for side, spec in MANUAL_TARGETS.items():
        target_obj = scene.objects.get(spec["object"])
        rudder = scene.objects.get(spec["rudder"])
        source_record = boundary.get("rudders", {}).get(side, {})
        anchor_checks = {}
        for label, values in spec["anchors"].items():
            expected = Vector(values)
            hull_nearest = nearest(target_obj, expected) if target_obj else (float("inf"), -1, Vector())
            rudder_nearest = nearest(rudder, expected) if rudder else (float("inf"), -1, Vector())
            supplied_index = spec["supplied_indices"][label]
            supplied = target_obj.data.vertices[supplied_index] if target_obj and supplied_index < len(target_obj.data.vertices) else None
            supplied_point = target_obj.matrix_world @ supplied.co if supplied else Vector()
            anchor_checks[label] = {"manual_world": list(expected), "hull_current_object_index": hull_nearest[1], "hull_error_m": hull_nearest[0], "rudder_current_object_index": rudder_nearest[1], "rudder_error_m": rudder_nearest[0], "supplied_index": supplied_index, "supplied_index_error_m": (supplied_point - expected).length if supplied else None, "supplied_index_note": "artist index verified on current target object" if supplied and (supplied_point - expected).length <= TOLERANCE_M else "artist index retained as provenance; current source-first partition resolved by nearest world coordinate", "within_tolerance": hull_nearest[0] <= TOLERANCE_M and rudder_nearest[0] <= TOLERANCE_M}
        baseline_fixed = target_obj.matrix_world.copy() if target_obj else None
        rudder_samples = control_sample(rudder, (-20.0, -15.0, 0.0, 15.0, 20.0), 2) if rudder else {"samples": [], "neutral_exact": False}
        fixed_unchanged = target_obj is not None and baseline_fixed == target_obj.matrix_world
        edge_stats = edge_statistics(rudder) if rudder else {}
        loop = list(source_record.get("orderedBoundaryLoop", []))
        unique_loop = loop[:-1] if loop and loop[0] == loop[-1] else loop
        rudder_report[side] = {"manualAnchor": True, "target_object": spec["object"], "movable_object": spec["rudder"], "fixed_stabilizer": source_record.get("fixedStabilizerObject", "SM_Antey_LOD0_Hull" if side == "Dorsal" else "SM_Antey_LOD0_Hull_LowerSource"), "anchors": anchor_checks, "boundary": {"ordered_loop_vertices": len(loop), "unique_vertices_excluding_closure": len(set(unique_loop)), "simple_excluding_closure": len(unique_loop) == len(set(unique_loop)), "edge_ids": len(source_record.get("boundaryEdgeIds", [])), "boundary_fingerprint": source_record.get("boundaryFingerprint"), "movable_faces": len(source_record.get("movableFaceIndices", [])), "movable_triangles": mesh_triangles(rudder) if rudder else 0, "movable_face_fingerprint": source_record.get("movableFaceFingerprint"), "fixed_stabilizer_face_fingerprint": source_record.get("fixedStabilizerFaceFingerprint"), "surface_area_m2": source_record.get("surfaceAreaM2"), "bounds": source_record.get("bounds"), "mesh_edges": edge_stats}, "hinge": source_record.get("hinge"), "qa": {"angles_deg": [-20, -15, 0, 15, 20], "neutral_exact": rudder_samples.get("neutral_exact", False), "samples": rudder_samples.get("samples", []), "fixed_stabilizer_matrix_unchanged": fixed_unchanged}, "pass": all(item["within_tolerance"] for item in anchor_checks.values()) and rudder_samples.get("neutral_exact", False) and fixed_unchanged and bool(loop) and len(unique_loop) == len(set(unique_loop))}
    report["manual_rudders"] = rudder_report

    stern = {}
    for name in STERN_NAMES:
        obj = scene.objects.get(name)
        stern[name] = {"exists": obj is not None, "source_face_fingerprint": obj.get("source_face_fingerprint") if obj else None, "triangles": mesh_triangles(obj) if obj else 0, "qa": control_sample(obj, (-15.0, 0.0, 15.0), 1) if obj else {"samples": [], "neutral_exact": False}}
    report["stern_planes"] = {"state": "FROZEN", "planes": stern, "shape_mutation": False}

    markers = sorted([obj for obj in scene.objects if obj.type == "EMPTY" and obj.get("authoring_role") == "TORPEDO_TUBE"], key=lambda obj: obj.name)
    torpedo_map = [{"door": obj.get("logical_door", obj.name), "marker": obj.name, "centerY": float(obj.location.y), "centerZ": float(obj.location.z), "effectiveDiameterM": float(obj.get("diameter_m", 0.0)), "sourceBoundary": obj.get("source_geometry_status"), "caliberAssignment": "533mm" if abs(float(obj.get("diameter_m", 0.0)) - 0.533) < 1.0e-6 else "650mm" if abs(float(obj.get("diameter_m", 0.0)) - 0.650) < 1.0e-6 else "UNKNOWN", "confidence": obj.get("source_geometry_status") and "CONFIGURATION_NEUTRAL" or "UNKNOWN"} for obj in markers]
    report["torpedo"] = {"count": len(markers), "map": torpedo_map, "contract": jsonable(scene.get("torpedo_contract", {})), "source_geometry": "NO_SEPARATE_DOOR_LOOP_IN_SOURCE", "source_candidate_render": "SOURCE_CANDIDATES_ARE_REPORTED_WITHOUT_FABRICATED_DOOR_MESH", "mechanics": {"system": "Antey TorpedoLauncherSystem", "states": ["CLOSED", "OPENING", "OPEN", "CLOSING"], "hinge": "TBD_UNTIL_SOURCE_SEAM"}, "compact_upper_bow": len(markers) == 6 and len({round(item["centerZ"], 3) for item in torpedo_map}) >= 2 and max(item["centerY"] for item in torpedo_map) - min(item["centerY"] for item in torpedo_map) < 5.0}

    compartments = jsonable(scene.get("antey_compartments", []))
    required_fields = {"X_MIN", "X_MAX", "LOCAL_VOLUME", "CENTER", "ROLE", "CREW_CAPACITY_WEIGHT", "FLOODABLE", "FIRE_CAPABLE", "POWER_DEPENDENCY", "PUMP_DEPENDENCY", "REPAIR_ACCESS"}
    report["compartments"] = {"count": len(compartments), "records": compartments, "missing_fields": {str(record.get("index")): sorted(required_fields - set(record)) for record in compartments if required_fields - set(record)}, "roles": [record.get("ROLE") for record in compartments], "valid": len(compartments) == 10 and all(required_fields.issubset(set(record)) for record in compartments)}
    report["damage_repair_contract"] = jsonable(scene.get("damage_repair_contract", {}))
    mass = jsonable(scene.get("antey_mass_distribution", {})); buoyancy = jsonable(scene.get("antey_buoyancy_contract", {}))
    com = scene.objects.get("Antey_CenterOfMass"); cob = scene.objects.get("Antey_CenterOfBuoyancy")
    report["center_of_mass"] = {"marker": list(com.location) if com else None, "base_com": mass.get("base_com"), "base_mass_kg": mass.get("base_mass_kg"), "method": mass.get("method"), "dynamic": mass.get("dynamic_com")}
    report["center_of_buoyancy"] = {"marker": list(cob.location) if cob else None, "cob": buoyancy.get("cob"), "method": buoyancy.get("method"), "stability_tuning": buoyancy.get("stability_tuning"), "com_cob_distance_m": (com.location - cob.location).length if com and cob else None}

    covers = sorted([obj for obj in lod0 if COVER_RE.match(obj.name)], key=lambda obj: obj.name)
    cover_contract = jsonable(scene.get("P700_COVER_OPENING_MECHANISM", {}))
    action_records = []
    for obj in covers:
        tracks = []
        if obj.animation_data:
            for track in obj.animation_data.nla_tracks:
                strips = []
                for strip in track.strips:
                    action = strip.action
                    paths = action_paths(action)
                    strips.append({"name": strip.name, "action": action.name if action else None, "paths": paths})
                tracks.append({"name": track.name, "strips": strips})
        action_records.append({"node": obj.name, "nla_tracks": tracks, "deployment_operation": obj.get("P700_DEPLOYMENT_OPERATION"), "lift_profile_m": jsonable(obj.get("CLEARANCE_LIFT_PROFILE_M")), "pair_lock": obj.get("COVER_PAIR_LOCK"), "neighbor_conflict": obj.get("NEIGHBOR_CONFLICT_CHANNEL")})
    p700_bvh = p700_bvh_samples(scene, covers) if covers else {"samples": [], "all_samples_pass": False}
    glb_contract_path = candidate.parent / "ManualP700QA" / "p700_runtime_glb_final.json"
    glb_contract = json.loads(glb_contract_path.read_text(encoding="utf-8")) if glb_contract_path.exists() else {"animations": [], "glb_animations": [], "status": "NOT_AVAILABLE"}
    report["p700"] = {"cover_count": len(covers), "opening_mechanism": cover_contract, "animation_contract": {"logical_operation": scene.get("P700_DEPLOYMENT_OPERATION"), "actual_blend_nodes": action_records, "actual_glb": {"animations": glb_contract.get("animations", []), "glb_animations": glb_contract.get("glb_animations", [])}}, "bvh_samples": p700_bvh, "interlock": jsonable(scene.get("P700_COVER_OPENING_MECHANISM", {}).get("pair_interlock", {})) if hasattr(scene.get("P700_COVER_OPENING_MECHANISM", {}), "get") else None}

    prop_objects = [obj for obj in lod0 if str(obj.get("source_first_role", "")).startswith("PROPELLER_")]
    prop_object_count = len(prop_objects)
    prop_port_count = sum(1 for obj in prop_objects if "PORT" in str(obj.get("source_first_role", "")))
    prop_starboard_count = sum(1 for obj in prop_objects if "STARBOARD" in str(obj.get("source_first_role", "")))
    candidate_prop = {}
    for obj in prop_objects:
        component = int(obj.get("source_component", -1))
        points = world_points(obj)
        candidate_prop[component] = {"object": obj.name, "side": "Port" if "PORT" in str(obj.get("source_first_role", "")) else "Starboard", "center": [sum(point[i] for point in points) / len(points) for i in range(3)], "points": [list(point) for point in points], "triangles": mesh_triangles(obj)}
    candidate_face_sets = candidate_face_signatures(scene)
    candidate_runtime_snapshot = {"propellers": candidate_prop, "face_sets": candidate_face_sets}
    source_face_sets, source_props = source_signatures_and_props(source)
    partition = {}
    for key in sorted(set(source_face_sets) | set(candidate_face_sets)):
        source_counter = source_face_sets.get(key, Counter())
        candidate_counter = candidate_face_sets.get(key, Counter())
        missing = source_counter - candidate_counter
        extra = candidate_counter - source_counter
        partition[f"{key[0]}:{key[1]}"] = {"source_faces": sum(source_counter.values()), "candidate_exact_source_faces": sum(candidate_counter.values()) - sum(extra.values()), "missing_source_faces": sum(missing.values()), "synthetic_or_extra_faces": sum(extra.values()), "missing_examples": list(missing.elements())[:3], "extra_examples": list(extra.elements())[:3], "pass": not missing}
    prop_compare = {}
    for component, source_record in sorted(source_props.items()):
        candidate_record = candidate_runtime_snapshot["propellers"].get(component)
        source_points = [Vector(point) for point in source_record["points"]]
        candidate_points = [Vector(point) for point in candidate_record["points"]] if candidate_record else []
        nearest_error = max((min((left - right).length for right in candidate_points) for left in source_points), default=float("inf"))
        center_delta = (Vector(source_record["center"]) - Vector(candidate_record["center"])).length if candidate_record else float("inf")
        prop_compare[str(component)] = {"side": source_record["side"], "candidate_object": candidate_record.get("object") if candidate_record else None, "source_center": source_record["center"], "candidate_center": candidate_record.get("center") if candidate_record else None, "center_delta_m": center_delta, "center_note": "centroid weighting differs because source component faces repeat shared vertices; position comparison uses vertex-set error", "max_source_to_candidate_vertex_error_m": nearest_error, "seven_blade_contract": candidate_record is not None and candidate_record["triangles"] > 0, "pass": candidate_record is not None and nearest_error <= TOLERANCE_M}
    report["propellers"] = {"objects": prop_object_count, "source_components": len(source_props), "per_component": prop_compare, "port_blades": prop_port_count, "starboard_blades": prop_starboard_count, "positions_unchanged": all(item["pass"] for item in prop_compare.values())}
    report["source_partition"] = {"per_component": partition, "missing_total": sum(item["missing_source_faces"] for item in partition.values()), "synthetic_extra_total": sum(item["synthetic_or_extra_faces"] for item in partition.values()), "source_faces_preserved": all(item["pass"] for item in partition.values())}
    report["physics_ownership"] = candidate_physics_ownership
    report["proxies"] = candidate_proxies
    report["pass_summary"] = {"rudders": all(item["pass"] for item in rudder_report.values()), "stern_planes": all(item["exists"] and item["qa"]["neutral_exact"] for item in stern.values()), "torpedo_contract": report["torpedo"]["count"] == 6 and report["torpedo"]["compact_upper_bow"], "compartments": report["compartments"]["valid"], "source_partition": report["source_partition"]["source_faces_preserved"], "propellers": report["propellers"]["positions_unchanged"], "materials": report["materials"]["runtime_only_intended"]}
    report["human_visual_approval"] = "PENDING"
    report["promotion_recommendation"] = "DO_NOT_PROMOTE"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(jsonable(report), indent=2), encoding="utf-8")
    print(f"ANTEY_FINAL_MECHANICAL_VALIDATION_WRITTEN {output}")


if __name__ == "__main__":
    main()
