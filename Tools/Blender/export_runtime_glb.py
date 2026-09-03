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
    parser.add_argument("--lod", type=int, default=0, choices=(0, 1, 2, 3), help="single production LOD artifact to export")
    parser.add_argument(
        "--animation-contract",
        choices=("none", "P700_Deploy"),
        default="none",
        help="explicit runtime animation contract; 'none' disables animation export",
    )
    return parser.parse_args(values)


def main() -> None:
    options = args()
    output = options.output.resolve()
    # The current runtime asset contract consumes one chosen LOD artifact per
    # GLB.  Exporting every authoring LOD simultaneously creates duplicate
    # draw sets; articulation remains separate within the selected level.
    runtime = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and bool(obj.get("runtime_export", False)) and int(obj.get("lod", -1)) == options.lod]
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
    runtime_tracks = [
        track
        for obj in runtime
        if obj.animation_data
        for track in obj.animation_data.nla_tracks
    ]
    if options.animation_contract != "none" and not any(
        track.name == options.animation_contract for track in runtime_tracks
    ):
        raise RuntimeError(
            f"Requested runtime animation contract is not authored on selected meshes: {options.animation_contract}"
        )

    # Runtime animation is an explicit asset contract.  Authoring/QA NLA
    # tracks are muted for a named runtime export, while the default contract
    # exports no animation at all (as required by Antey).
    muted_tracks: list[tuple[bpy.types.NlaTrack, bool]] = []
    if options.animation_contract != "none":
        for obj in bpy.context.scene.objects:
            if not obj.animation_data:
                continue
            for track in obj.animation_data.nla_tracks:
                if track.name != options.animation_contract:
                    muted_tracks.append((track, track.mute))
                    track.mute = True
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        bpy.ops.export_scene.gltf(
            filepath=str(output),
            export_format="GLB",
            use_selection=True,
            export_yup=True,
            export_materials="EXPORT",
            export_animations=options.animation_contract != "none",
            export_animation_mode="NLA_TRACKS",
            export_merge_animation="NLA_TRACK",
            export_cameras=False,
            export_lights=False,
        )
    finally:
        for track, was_muted in muted_tracks:
            track.mute = was_muted
    print(
        f"RUNTIME_GLB_EXPORT_OK {output} lod={options.lod} meshes={len(runtime)} "
        f"animation={options.animation_contract}"
    )


if __name__ == "__main__":
    main()
