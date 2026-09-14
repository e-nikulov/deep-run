from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
from mathutils import Vector

TECHNICAL_BLACK = (0.001517635, 0.001517635, 0.001517635, 1.0)
ANTIFOULING_RED = (0.068478170, 0.004776953, 0.003676507, 1.0)
LOWER_HULL_SPLIT_Z_M = -0.65


def args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    return parser.parse_args(values)


def configure_material(name: str, colour: tuple[float, float, float, float], roughness: float) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = colour
    material.use_nodes = True
    principled = material.node_tree.nodes.get("Principled BSDF")
    if principled is None:
        raise RuntimeError(f"material {name} has no Principled BSDF")
    principled.inputs["Base Color"].default_value = colour
    principled.inputs["Roughness"].default_value = roughness
    principled.inputs["Metallic"].default_value = 0.0
    return material


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def main() -> None:
    options = args()
    source = options.input.resolve()
    output = options.output.resolve()
    report_path = options.report.resolve()
    if not source.exists():
        raise FileNotFoundError(source)

    bpy.ops.wm.open_mainfile(filepath=str(source))
    black = configure_material("MAT_Antey_Hull", TECHNICAL_BLACK, 0.86)
    red = configure_material("MAT_Antey_LowerHull", ANTIFOULING_RED, 0.82)

    runtime_meshes = [
        obj for obj in bpy.context.scene.objects
        if obj.type == "MESH" and bool(obj.get("runtime_export", False))
    ]
    if not runtime_meshes:
        raise RuntimeError("no runtime-export Antey meshes found")

    totals = {
        "runtimeMeshes": len(runtime_meshes),
        "smoothedPolygons": 0,
        "blackPolygons": 0,
        "redPolygons": 0,
        "geometryVerticesBefore": 0,
        "geometryVerticesAfter": 0,
        "geometryPolygonsBefore": 0,
        "geometryPolygonsAfter": 0,
        "materializedMeshes": 0,
    }
    object_records: list[dict[str, object]] = []

    for obj in runtime_meshes:
        mesh = obj.data
        before_vertices = len(mesh.vertices)
        before_polygons = len(mesh.polygons)
        totals["geometryVerticesBefore"] += before_vertices
        totals["geometryPolygonsBefore"] += before_polygons

        for polygon in mesh.polygons:
            polygon.use_smooth = True
            totals["smoothedPolygons"] += 1

        material_names = {material.name for material in mesh.materials if material is not None}
        is_propeller = any("Propeller" in name or "Propellers" in name for name in material_names) or \
                       str(obj.get("source_first_role", "")).startswith("PROPELLER")
        black_count = 0
        red_count = 0
        if not is_propeller:
            mesh.materials.clear()
            mesh.materials.append(black)
            mesh.materials.append(red)
            totals["materializedMeshes"] += 1
            for polygon in mesh.polygons:
                world_center = obj.matrix_world @ polygon.center
                if world_center.z <= LOWER_HULL_SPLIT_Z_M:
                    polygon.material_index = 1
                    red_count += 1
                else:
                    polygon.material_index = 0
                    black_count += 1
            totals["blackPolygons"] += black_count
            totals["redPolygons"] += red_count

        mesh.update()
        after_vertices = len(mesh.vertices)
        after_polygons = len(mesh.polygons)
        totals["geometryVerticesAfter"] += after_vertices
        totals["geometryPolygonsAfter"] += after_polygons
        if before_vertices != after_vertices or before_polygons != after_polygons:
            raise RuntimeError(f"visual repair changed topology for {obj.name}")
        object_records.append({
            "name": obj.name,
            "vertices": after_vertices,
            "polygons": after_polygons,
            "blackPolygons": black_count,
            "redPolygons": red_count,
            "propeller": is_propeller,
        })

    if totals["redPolygons"] <= 0 or totals["blackPolygons"] <= 0:
        raise RuntimeError(f"paint split did not produce both hull colours: {totals}")
    if totals["geometryVerticesBefore"] != totals["geometryVerticesAfter"] or \
       totals["geometryPolygonsBefore"] != totals["geometryPolygonsAfter"]:
        raise RuntimeError("visual repair changed aggregate topology")

    bpy.context.scene["antey_visual_contract"] = {
        "upperHull": "TECHNICAL_BLACK_SRGB_050505",
        "lowerHull": "ANTIFOULING_RED_SRGB_4A0F0C",
        "lowerHullSplitZM": LOWER_HULL_SPLIT_Z_M,
        "smoothShading": True,
        "topologyMutation": False,
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "input": str(source),
        "output": str(output),
        "inputSha256": sha256(source),
        "outputSha256": sha256(output),
        "technicalBlackLinear": TECHNICAL_BLACK,
        "antifoulingRedLinear": ANTIFOULING_RED,
        "lowerHullSplitZM": LOWER_HULL_SPLIT_Z_M,
        "totals": totals,
        "objects": object_records,
        "status": "PASS",
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["totals"], indent=2))
    print("Antey production visual repair: PASS")


if __name__ == "__main__":
    main()
