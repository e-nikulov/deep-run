"""Regression checks for logical propeller roots in the sidecar writer."""
from __future__ import annotations

import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from write_production_sidecars import propeller_metadata, propeller_meshes


def triangle_mesh(name: str) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def expect_missing_root() -> None:
    try:
        propeller_meshes(None, "SM_Propeller_Missing")
    except RuntimeError as error:
        assert "Missing logical propeller root" in str(error)
        return
    raise AssertionError("missing propeller root was accepted")


def main() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    empty = bpy.data.objects.new("SM_Propeller_Empty", None)
    bpy.context.scene.collection.objects.link(empty)
    empty.location = (3.0, 4.0, 5.0)
    empty["SOURCE_BLADE_COUNT"] = 7
    child = triangle_mesh("SM_Propeller_Empty_Blade_01")
    child.parent = empty
    child["lod"] = 0
    empty_record = propeller_metadata(empty, empty.name)
    assert empty_record["origin"] == [3.0, 4.0, 5.0]
    assert empty_record["visibleBlades"] == 7
    assert empty_record["triangles"] == 1

    mesh_root = triangle_mesh("SM_Propeller_Mesh")
    mesh_root["visible_blades"] = 5
    mesh_record = propeller_metadata(mesh_root, mesh_root.name)
    assert mesh_record["visibleBlades"] == 5
    assert mesh_record["triangles"] == 1

    expect_missing_root()
    print("PROPELLER_SIDECAR_SCHEMA_REGRESSION_OK empty_root mesh_root missing_root")


if __name__ == "__main__":
    main()
