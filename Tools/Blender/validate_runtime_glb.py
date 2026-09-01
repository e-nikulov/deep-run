"""Import a runtime GLB into an empty fresh Blender process and audit it."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import bpy


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--asset", required=True, choices=("antey", "p700"))
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def glb_json(path: Path) -> dict:
    data = path.read_bytes()
    magic, version, total_length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total_length != len(data):
        raise RuntimeError("Invalid GLB 2.0 header")
    chunk_length, chunk_type = struct.unpack_from("<II", data, 12)
    if chunk_type != 0x4E4F534A:
        raise RuntimeError("GLB first chunk is not JSON")
    return json.loads(data[20 : 20 + chunk_length].decode("utf-8"))


def triangles(objects: list[bpy.types.Object]) -> int:
    for obj in objects:
        obj.data.calc_loop_triangles()
    return sum(len(obj.data.loop_triangles) for obj in objects)


def main() -> None:
    options = args()
    glb = options.glb.resolve()
    document = glb_json(glb)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(glb))
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    names = {obj.name for obj in meshes}
    forbidden = ("REF_", "HP_", "VOL_", "COL_", "PHY_", "REVIEW_")
    helpers = sorted(name for name in names if name.startswith(forbidden))
    if not meshes or helpers:
        raise RuntimeError(f"GLB runtime-node boundary failed: meshes={len(meshes)} helpers={helpers}")
    required = (
        {"SM_Antey_LOD0_Hull", "SM_Antey_LOD0_Sail", "SM_Propeller_Port", "SM_Propeller_Starboard"}
        if options.asset == "antey"
        else {
            "SM_P700_LOD0_Body",
            "SM_P700_LOD0_Wing_Port",
            "SM_P700_LOD0_Wing_Starboard",
            "SM_P700_LOD0_Tail_Dorsal",
            "SM_P700_LOD0_Tail_Ventral",
            "SM_P700_LOD0_Tail_Port",
            "SM_P700_LOD0_Tail_Starboard",
        }
    )
    missing = required - names
    if missing:
        raise RuntimeError(f"GLB lost required runtime geometry: {sorted(missing)}")
    lods = {}
    for lod in range(4):
        if options.asset == "antey" and lod == 0:
            members = [
                obj
                for obj in meshes
                if "LOD0" in obj.name
                or (obj.name.startswith(("SM_P700_Hatch_", "SM_Propeller_")) and "_LOD" not in obj.name)
            ]
        else:
            members = [obj for obj in meshes if f"LOD{lod}" in obj.name or (lod > 0 and obj.name.endswith(f"_LOD{lod}"))]
        lods[f"LOD{lod}"] = {
            "objects": len(members),
            "vertices": sum(len(obj.data.vertices) for obj in members),
            "triangles": triangles(members),
        }
    raw_nodes = document.get("nodes", [])
    raw_animations = []
    for animation in document.get("animations", []):
        targets = []
        for channel in animation.get("channels", []):
            target = channel.get("target", {})
            node_index = target.get("node")
            node_name = raw_nodes[node_index].get("name", f"node_{node_index}") if node_index is not None else None
            targets.append({"node": node_name, "path": target.get("path")})
        raw_animations.append({
            "name": animation.get("name", ""),
            "targeted_nodes": sorted({target["node"] for target in targets if target["node"] is not None}),
            "target_paths": sorted({target["path"] for target in targets if target["path"] is not None}),
            "channels": len(targets),
        })

    material_names = sorted(material.get("name", "") for material in document.get("materials", []))
    import_session_materials = sorted(material.name for material in bpy.data.materials)
    lod0_classification = None
    if options.asset == "antey":
        expected_materials = ["MAT_Antey_Hull", "MAT_Antey_Propellers"]
        if material_names != expected_materials:
            raise RuntimeError(f"Antey GLB material contract failed: {material_names}")
        base = [obj for obj in meshes if "SM_Antey_LOD0_" in obj.name]
        hatches = [obj for obj in meshes if obj.name.startswith("SM_P700_Hatch_") and "_LOD" not in obj.name]
        propellers = [obj for obj in meshes if obj.name.startswith("SM_Propeller_") and "_LOD" not in obj.name]
        classified = set(base + hatches + propellers)
        other = [obj for obj in meshes if int(obj.get("lod", 0)) == 0 and obj not in classified and "LOD1" not in obj.name and "LOD2" not in obj.name and "LOD3" not in obj.name]
        categories = {
            "LOD0_base_meshes": base,
            "P700_hatch_meshes": hatches,
            "propeller_meshes": propellers,
            "other_meshes": other,
        }
        lod0_classification = {
            key: {"objects": len(value), "triangles": triangles(value), "names": sorted(obj.name for obj in value)}
            for key, value in categories.items()
        }
        lod0_classification["TOTAL_RUNTIME_LOD0"] = {
            "objects": sum(entry["objects"] for entry in lod0_classification.values()),
            "triangles": sum(entry["triangles"] for entry in lod0_classification.values()),
        }
        if lod0_classification["other_meshes"]["objects"] or lod0_classification["TOTAL_RUNTIME_LOD0"]["objects"] != 34:
            raise RuntimeError(f"Antey LOD0 runtime classification is incomplete: {lod0_classification}")
    else:
        if len(raw_animations) != 1 or raw_animations[0]["name"] != "P700_Deploy":
            raise RuntimeError(f"P700 GLB animation contract failed: {raw_animations}")
        if raw_animations[0]["target_paths"] != ["rotation"]:
            raise RuntimeError(f"P700 GLB animation contains non-hinge channels: {raw_animations[0]}")
        expected_targets = sorted(obj.name for obj in meshes if "_Wing_" in obj.name or "_Tail_" in obj.name)
        if raw_animations[0]["targeted_nodes"] != expected_targets:
            raise RuntimeError("P700 GLB animation does not target every movable surface LOD exactly once")
    report = {
        "glb": str(glb),
        "sha256": sha256(glb),
        "blender_version": bpy.app.version_string,
        "fresh_empty_scene_import": True,
        "mesh_objects": len(meshes),
        "required_meshes": sorted(required),
        "forbidden_helpers": helpers,
        "cameras": sum(obj.type == "CAMERA" for obj in bpy.context.scene.objects),
        "lights": sum(obj.type == "LIGHT" for obj in bpy.context.scene.objects),
        "materials": material_names,
        "import_session_materials": import_session_materials,
        "animations": sorted(action.name for action in bpy.data.actions),
        "glb_animations": raw_animations,
        "lods": lods,
        "lod0_classification": lod0_classification,
        "status": "PASS",
    }
    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"RUNTIME_GLB_FRESH_REIMPORT_OK {output}")


if __name__ == "__main__":
    main()
