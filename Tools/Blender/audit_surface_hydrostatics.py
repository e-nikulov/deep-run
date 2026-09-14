from __future__ import annotations

import json
from pathlib import Path

import bpy
from mathutils import Vector


def world_bounds(objects):
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return {
        "min": [min(p[i] for p in points) for i in range(3)],
        "max": [max(p[i] for p in points) for i in range(3)],
    }


def record(obj):
    bounds = world_bounds([obj])
    return {
        "name": obj.name,
        "type": obj.type,
        "location": [float(v) for v in obj.matrix_world.translation],
        "min": bounds["min"],
        "max": bounds["max"],
    }


bow_planes = [
    obj for obj in bpy.data.objects
    if obj.type == "MESH" and "BowPlane_" in obj.name
]
propellers = [
    obj for obj in bpy.data.objects
    if obj.type == "MESH" and "Propeller_" in obj.name
]

if not bow_planes:
    raise RuntimeError("No bow-plane meshes found")
if not propellers:
    raise RuntimeError("No propeller meshes found")

bow_bounds = world_bounds(bow_planes)
prop_bounds = world_bounds(propellers)

# Contract requested by the user: level boat, bow planes fully clear of the
# surface by 0.50 m, propellers fully submerged. For a horizontal body with
# local +Z up, the waterline is one horizontal local-Z plane.
required_waterline_max_z = bow_bounds["min"][2] - 0.50
required_waterline_min_z = prop_bounds["max"][2]

payload = {
    "bowPlanes": [record(obj) for obj in sorted(bow_planes, key=lambda o: o.name)],
    "propellers": [record(obj) for obj in sorted(propellers, key=lambda o: o.name)],
    "combined": {
        "bowPlaneMinZ": bow_bounds["min"][2],
        "bowPlaneMaxZ": bow_bounds["max"][2],
        "propellerMinZ": prop_bounds["min"][2],
        "propellerMaxZ": prop_bounds["max"][2],
    },
    "requestedContract": {
        "bowPlaneClearanceMeters": 0.50,
        "waterlineMustBeAtOrBelowLocalZ": required_waterline_max_z,
        "waterlineMustBeAtOrAboveLocalZ": required_waterline_min_z,
        "geometricallyFeasibleWithoutChangingGeometryOrPitch": required_waterline_min_z <= required_waterline_max_z,
    },
}

out = Path("artifacts/surface-hydrostatics/audit.json")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
print(json.dumps(payload, indent=2))
