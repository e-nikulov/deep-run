from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = json.loads((ROOT / "Content/submarines/Antey/Antey.compartments.json").read_text(encoding="utf-8"))


def record(source: dict, ordinal: int) -> dict:
    x, y, z = source["center"]
    return {
        "name": f"Antey_Compartment_{ordinal:02d}",
        "semanticId": source["semanticId"],
        "displayNameRu": source["displayNameRu"],
        "functionalRole": source["functionalRole"],
        "functionalRoleStatus": source["functionalRoleStatus"],
        "systemTags": source["systemTags"],
        "center": source["center"],
        "orientationQuaternionWXYZ": source["orientationQuaternionWXYZ"],
        "halfExtents": source["halfExtents"],
        "xRangeMeters": source["xRangeMeters"],
        "transform": [
            [1.0, 0.0, 0.0, float(x)],
            [0.0, 1.0, 0.0, float(y)],
            [0.0, 0.0, 1.0, float(z)],
            [0.0, 0.0, 0.0, 1.0],
        ],
    }


compartments = [record(source, ordinal) for ordinal, source in enumerate(CONTRACT["compartments"], start=1)]
for relative in (
    "Content/submarines/Antey/Antey.authoring.json",
    "Engine/Assets/submarines/Antey/Antey.authoring.json",
):
    path = ROOT / relative
    data = json.loads(path.read_text(encoding="utf-8"))
    data["compartments"] = compartments
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
