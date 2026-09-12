import json
from pathlib import Path

records = [
    {
        "semanticId": "depth-plane.bow.01",
        "group": "BOW",
        "nodeReference": "SM_Antey_LOD0_BowPlane_Port",
        "articulation": "ROTATION",
        "hingeAxisSource": "LOCAL_Y",
        "simulationOwnsAngle": True,
    },
    {
        "semanticId": "depth-plane.bow.02",
        "group": "BOW",
        "nodeReference": "SM_Antey_LOD0_BowPlane_Starboard",
        "articulation": "ROTATION",
        "hingeAxisSource": "LOCAL_Y",
        "simulationOwnsAngle": True,
    },
    {
        "semanticId": "depth-plane.stern.01",
        "group": "STERN",
        "nodeReference": "SM_Antey_LOD0_TailPlane_Port",
        "articulation": "ROTATION",
        "hingeAxisSource": "LOCAL_Y",
        "simulationOwnsAngle": True,
    },
    {
        "semanticId": "depth-plane.stern.02",
        "group": "STERN",
        "nodeReference": "SM_Antey_LOD0_TailPlane_Starboard",
        "articulation": "ROTATION",
        "hingeAxisSource": "LOCAL_Y",
        "simulationOwnsAngle": True,
    },
]

for relative in (
    Path("Content/submarines/Antey/Antey.authoring.json"),
    Path("Engine/Assets/submarines/Antey/Antey.authoring.json"),
):
    data = json.loads(relative.read_text(encoding="utf-8"))
    data["controlSurfaces"] = records
    relative.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
