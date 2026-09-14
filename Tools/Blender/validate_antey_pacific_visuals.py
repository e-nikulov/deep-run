from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import bpy

EXPECTED_BLACK = (0.0, 0.0, 0.0, 1.0)
EXPECTED_RED = (0.068478170, 0.004776953, 0.003676507, 1.0)
EXPECTED_SPLIT_Z = -0.65
EXPECTED_LOWER_HULL_ROLE = "MAIN_HULL_LOWER"
TOLERANCE = 2.0e-6


def args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    return parser.parse_args(values)


def close_tuple(actual, expected) -> bool:
    return len(actual) == len(expected) and all(abs(float(a) - float(b)) <= TOLERANCE for a, b in zip(actual, expected))


def material_state(name: str, expected_color, expected_roughness: float) -> dict[str, object]:
    material = bpy.data.materials.get(name)
    if material is None or not material.use_nodes:
        raise RuntimeError(f"missing production material {name}")
    principled = material.node_tree.nodes.get("Principled BSDF")
    if principled is None:
        raise RuntimeError(f"missing Principled BSDF for {name}")
    color = tuple(float(value) for value in principled.inputs["Base Color"].default_value)
    roughness = float(principled.inputs["Roughness"].default_value)
    metallic = float(principled.inputs["Metallic"].default_value)
    if not close_tuple(color, expected_color):
        raise RuntimeError(f"{name} base color mismatch: {color} != {expected_color}")
    if abs(roughness - expected_roughness) > TOLERANCE:
        raise RuntimeError(f"{name} roughness mismatch: {roughness} != {expected_roughness}")
    if abs(metallic) > TOLERANCE:
        raise RuntimeError(f"{name} must be dielectric: metallic={metallic}")
    return {"color": color, "roughness": roughness, "metallic": metallic}


def main() -> None:
    options = args()
    bpy.ops.wm.open_mainfile(filepath=str(options.candidate.resolve()))

    black_state = material_state("MAT_Antey_Hull", EXPECTED_BLACK, 0.86)
    red_state = material_state("MAT_Antey_LowerHull", EXPECTED_RED, 0.82)
    contract = bpy.context.scene.get("antey_visual_contract")
    if not contract:
        raise RuntimeError("missing antey_visual_contract")
    if str(contract.get("upperHull", "")) != "TECHNICAL_BLACK_SRGB_000000":
        raise RuntimeError("upper hull must use exact #000000 technical-black albedo")
    if abs(float(contract.get("lowerHullSplitZM", 999.0)) - EXPECTED_SPLIT_Z) > TOLERANCE:
        raise RuntimeError("unexpected lower-hull split contract")
    if str(contract.get("lowerHullRole", "")) != EXPECTED_LOWER_HULL_ROLE:
        raise RuntimeError("unexpected lower-hull role contract")
    if not bool(contract.get("smoothShading", False)) or bool(contract.get("topologyMutation", True)):
        raise RuntimeError("visual contract must require smooth shading without topology mutation")

    runtime = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and bool(obj.get("runtime_export", False))]
    if not runtime:
        raise RuntimeError("no runtime-export meshes")

    smooth = 0
    flat = 0
    black_faces = 0
    red_faces = 0
    mismatched_paint_faces = 0
    lower_hull_objects: list[str] = []
    red_material_leaks: list[str] = []

    for obj in runtime:
        material_names = [material.name if material else "" for material in obj.data.materials]
        role = str(obj.get("source_first_role", ""))
        is_propeller = any("Propeller" in name or "Propellers" in name for name in material_names) or role.startswith("PROPELLER")
        is_lower_hull = role == EXPECTED_LOWER_HULL_ROLE
        if is_lower_hull:
            lower_hull_objects.append(obj.name)
        if not is_lower_hull and "MAT_Antey_LowerHull" in material_names:
            red_material_leaks.append(obj.name)

        for polygon in obj.data.polygons:
            if polygon.use_smooth:
                smooth += 1
            else:
                flat += 1
            if is_propeller:
                continue
            name = material_names[polygon.material_index] if polygon.material_index < len(material_names) else ""
            expected_name = "MAT_Antey_LowerHull" if is_lower_hull else "MAT_Antey_Hull"
            if name == "MAT_Antey_LowerHull":
                red_faces += 1
            elif name == "MAT_Antey_Hull":
                black_faces += 1
            if name != expected_name:
                mismatched_paint_faces += 1

    if flat != 0:
        raise RuntimeError(f"runtime GLB source still contains flat-shaded polygons: {flat}")
    if not lower_hull_objects:
        raise RuntimeError(f"no {EXPECTED_LOWER_HULL_ROLE} runtime object")
    if red_faces <= 0 or black_faces <= 0:
        raise RuntimeError(f"missing paint regions black={black_faces} red={red_faces}")
    if mismatched_paint_faces != 0:
        raise RuntimeError(f"role-based paint mismatches: {mismatched_paint_faces}")
    if red_material_leaks:
        raise RuntimeError(f"red lower-hull material leaked onto non-lower-hull objects: {red_material_leaks}")

    report = {
        "materials": {"technicalBlack": black_state, "antifoulingRed": red_state},
        "runtimeMeshCount": len(runtime),
        "smoothPolygons": smooth,
        "flatPolygons": flat,
        "blackFaces": black_faces,
        "redFaces": red_faces,
        "lowerHullObjects": sorted(lower_hull_objects),
        "mismatchedPaintFaces": mismatched_paint_faces,
        "redMaterialLeaks": sorted(red_material_leaks),
        "status": "PASS",
    }
    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
