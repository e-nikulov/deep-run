from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy

TECHNICAL_BLACK = (0.0, 0.0, 0.0, 1.0)
ANTIFOULING_RED = (0.068478170, 0.004776953, 0.003676507, 1.0)
LOWER_HULL_SPLIT_Z_M = -0.65
LOWER_HULL_ROLE = "MAIN_HULL_LOWER"


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
        "lowerHullMeshes": 0,
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

        # Source-first partitioning used to drop the source smooth-shading state. Re-applying smooth shading
        # here preserves topology and silhouette while removing the polygon-facet lighting that was visible on
        # the bow and side hull. Planar hard-surface faces remain planar; no subdivision or dimension change is
        # introduced by this repair.
        for polygon in mesh.polygons:
            polygon.use_smooth = True
            totals["smoothedPolygons"] += 1

        material_names = {material.name for material in mesh.materials if material is not None}
        role = str(obj.get("source_first_role", ""))
        is_propeller = any("Propeller" in name or "Propellers" in name for name in material_names) or role.startswith("PROPELLER")
        is_lower_hull = role == LOWER_HULL_ROLE
        black_count = 0
        red_count = 0

        if not is_propeller:
            # The antifouling coating belongs only to the authored lower main-hull partition. A simple world-Z
            # test leaked red onto the ventral rudder, stern planes and low P-700 covers, which is visually wrong.
            mesh.materials.clear()
            mesh.materials.append(red if is_lower_hull else black)
            totals["materializedMeshes"] += 1
            if is_lower_hull:
                totals["lowerHullMeshes"] += 1
                red_count = len(mesh.polygons)
            else:
                black_count = len(mesh.polygons)
            for polygon in mesh.polygons:
                polygon.material_index = 0
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
            "role": role,
            "vertices": after_vertices,
            "polygons": after_polygons,
            "blackPolygons": black_count,
            "redPolygons": red_count,
            "propeller": is_propeller,
            "lowerHull": is_lower_hull,
        })

    if totals["lowerHullMeshes"] <= 0:
        raise RuntimeError(f"no {LOWER_HULL_ROLE} runtime mesh found")
    if totals["redPolygons"] <= 0 or totals["blackPolygons"] <= 0:
        raise RuntimeError(f"paint split did not produce both hull colours: {totals}")
    if totals["geometryVerticesBefore"] != totals["geometryVerticesAfter"] or \
       totals["geometryPolygonsBefore"] != totals["geometryPolygonsAfter"]:
        raise RuntimeError("visual repair changed aggregate topology")

    bpy.context.scene["antey_visual_contract"] = {
        "upperHull": "TECHNICAL_BLACK_SRGB_000000",
        "lowerHull": "ANTIFOULING_RED_SRGB_4A0F0C",
        "lowerHullRole": LOWER_HULL_ROLE,
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
        "lowerHullRole": LOWER_HULL_ROLE,
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
