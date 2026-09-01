"""Export only runtime render nodes from a canonical production BLEND."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(values)


def main() -> None:
    options = args()
    output = options.output.resolve()
    runtime = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and bool(obj.get("runtime_export", False))]
    if not runtime:
        raise RuntimeError("Canonical BLEND contains no runtime render meshes")
    bpy.ops.object.select_all(action="DESELECT")
    selected = set(runtime)
    for obj in runtime:
        obj.hide_viewport = False
        obj.hide_render = False
        obj.hide_set(False)
        obj.select_set(True)
        parent = obj.parent
        while parent is not None:
            selected.add(parent)
            parent.hide_viewport = False
            parent.hide_set(False)
            parent.select_set(True)
            parent = parent.parent
    forbidden = ("REF_", "HP_", "VOL_", "COL_", "PHY_", "REVIEW_")
    if any(obj.name.startswith(forbidden) for obj in selected):
        raise RuntimeError("Forbidden helper selected for GLB export")
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_materials="EXPORT",
        export_animations=True,
        export_animation_mode="NLA_TRACKS",
        export_merge_animation="NLA_TRACK",
        export_cameras=False,
        export_lights=False,
    )
    print(f"RUNTIME_GLB_EXPORT_OK {output} meshes={len(runtime)}")


if __name__ == "__main__":
    main()
