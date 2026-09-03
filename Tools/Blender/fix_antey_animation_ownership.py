"""Rename Antey cover QA animation data and mark it authoring-only."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy


QA_ANIMATION = "QA_Antey_P700_Covers_Open"
STATE_OWNER = "LauncherSystem.P700CoverState_Port_01..06 / P700CoverState_Starboard_01..06"


def args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", required=True, type=Path)
    return parser.parse_args(values)


def main() -> None:
    options = args()
    blend = options.blend.resolve()
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    changed_actions: set[str] = set()
    changed_tracks = 0
    changed_objects = 0
    covers = [obj for obj in bpy.context.scene.objects if obj.name.startswith("SM_Antey_P700_Cover_")]
    for obj in covers:
        obj["AUTHORING_ONLY"] = True
        obj["RUNTIME_EXPORT"] = False
        obj["P700_DEPLOYMENT_OPERATION"] = QA_ANIMATION
        changed_objects += 1
        if not obj.animation_data:
            continue
        for track in obj.animation_data.nla_tracks:
            if track.name == "P700_Deploy":
                track.name = QA_ANIMATION
                changed_tracks += 1
            try:
                track["AUTHORING_ONLY"] = True
                track["RUNTIME_EXPORT"] = False
            except TypeError:
                pass
            for strip in track.strips:
                if strip.name == "P700_Deploy":
                    strip.name = QA_ANIMATION
                action = strip.action
                if action is None:
                    continue
                if action.name.startswith("P700_Deploy_Source_"):
                    action.name = action.name.replace("P700_Deploy_Source_", f"{QA_ANIMATION}_Source_", 1)
                action["AUTHORING_ONLY"] = True
                action["RUNTIME_EXPORT"] = False
                changed_actions.add(action.name)
                for group in getattr(action, "groups", ()):
                    if group.name == "P700_Deploy":
                        group.name = QA_ANIMATION

    scene = bpy.context.scene
    scene["P700_DEPLOYMENT_OPERATION"] = QA_ANIMATION
    scene["P700_COVER_RUNTIME_ANIMATION"] = "NONE"
    scene["P700_COVER_STATE_OWNER"] = STATE_OWNER
    payload = scene.get("p700_animation_contract")
    if payload is not None:
        payload["logicalOperation"] = QA_ANIMATION
        payload["runtimeAnimation"] = "NONE"
        payload["stateOwner"] = STATE_OWNER
    launch_contract = scene.get("p700_launch_state_contract")
    if launch_contract is not None:
        animation_contract = launch_contract.get("p700_animation_contract")
        if animation_contract is not None:
            animation_contract["logicalOperation"] = QA_ANIMATION
            animation_contract["runtimeAnimation"] = "NONE"
            animation_contract["stateOwner"] = STATE_OWNER
    bpy.ops.wm.save_as_mainfile(filepath=str(blend), check_existing=False)
    print(
        f"ANTEY_ANIMATION_OWNERSHIP_FIX_OK {blend} covers={changed_objects} "
        f"tracks={changed_tracks} actions={len(changed_actions)} qa={QA_ANIMATION}"
    )


if __name__ == "__main__":
    main()
