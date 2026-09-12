from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch_builder() -> None:
    path = ROOT / "Tools/Blender/build_antey_source_first.py"
    text = path.read_text(encoding="utf-8")
    if "def _load_antey_compartment_contract()" not in text:
        marker = "\ndef add_compartment_and_mass_contract() -> None:\n"
        helper = '''\ndef _load_antey_compartment_contract() -> dict:\n    path = Path(__file__).resolve().parents[2] / "Content/submarines/Antey/Antey.compartments.json"\n    contract = json.loads(path.read_text(encoding="utf-8"))\n    if contract.get("schemaVersion") != 1 or contract.get("coordinateContract") != "+X bow; +Y port; +Z up; 1 BU = 1 m":\n        raise RuntimeError("Antey compartment reference contract is invalid")\n    compartments = contract.get("compartments")\n    if not isinstance(compartments, list) or len(compartments) != 10:\n        raise RuntimeError("Antey compartment reference contract must contain exactly ten records")\n    return contract\n\n\ndef add_compartment_and_mass_contract() -> None:\n'''
        if marker not in text:
            raise RuntimeError("builder compartment-function marker not found")
        text = text.replace(marker, helper, 1)

    pattern = re.compile(
        r'def add_compartment_and_mass_contract\(\) -> None:\n'
        r'    """Author hidden functional zones and tunable mass/COM contracts only\."""\n'
        r'.*?'
        r'    bpy\.context\.scene\["antey_compartments"\] = compartment_records\n',
        re.DOTALL,
    )
    replacement = '''def add_compartment_and_mass_contract() -> None:\n    """Author hidden functional zones and tunable mass/COM contracts only."""\n    collection = bpy.data.collections.new("ANTEY_COMPARTMENTS")\n    bpy.context.scene.collection.children.link(collection)\n    contract = _load_antey_compartment_contract()\n    compartment_specs = contract["compartments"]\n    # Gameplay/mass numbers remain tunable authoring approximations. Their total\n    # equipment mass is preserved from the previous accepted contract, while the\n    # functional identity and spatial boundaries now come from the reviewed 949A\n    # reference contract rather than an invented even longitudinal partition.\n    tuning = [\n        (420000.0, 0.0, True, True, "BATTERY_MAIN", "PUMP_FORWARD", "TORPEDO_ROOM_ACCESS"),\n        (280000.0, 18.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "CENTRAL_ACCESS"),\n        (330000.0, 12.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "COMPARTMENT_03_ACCESS"),\n        (240000.0, 36.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "CREW_CORRIDOR"),\n        (475000.0, 0.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "AUXILIARY_ACCESS"),\n        (475000.0, 0.0, True, True, "DISTRIBUTION_MAIN", "PUMP_MAIN", "AUXILIARY_ACCESS"),\n        (2400000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_MAIN", "REACTOR_ACCESS"),\n        (760000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_AFT", "TURBINE_ACCESS"),\n        (760000.0, 0.0, True, True, "REACTOR_PLANT", "PUMP_AFT", "TURBINE_ACCESS"),\n        (620000.0, 0.0, True, True, "DISTRIBUTION_AFT", "PUMP_AFT", "MOTOR_ROOM_ACCESS"),\n    ]\n    compartment_records: list[dict[str, object]] = []\n    for index, (spec, (equipment_mass, crew_weight, floodable, fire_capable, power, pump, repair_access)) in enumerate(zip(compartment_specs, tuning), 1):\n        semantic_id = f"compartment.{index:02d}"\n        if spec.get("semanticId") != semantic_id:\n            raise RuntimeError(f"Unexpected Antey compartment semantic ID: {spec.get('semanticId')} != {semantic_id}")\n        x_min, x_max = (float(value) for value in spec["xRangeMeters"])\n        length = x_max - x_min\n        centre = tuple(float(value) for value in spec["center"])\n        half_extents = tuple(float(value) for value in spec["halfExtents"])\n        if not math.isclose(centre[0], (x_min + x_max) * 0.5, abs_tol=1.0e-6) or not math.isclose(half_extents[0], length * 0.5, abs_tol=1.0e-6):\n            raise RuntimeError(f"Antey compartment spatial contract is inconsistent: {semantic_id}")\n        dimensions = tuple(value * 2.0 for value in half_extents)\n        name = f"Antey_Compartment_{index:02d}"\n        volume = _make_hidden_volume(name, centre, dimensions, collection)\n        volume["compartment_id"] = index\n        volume["semantic_id"] = semantic_id\n        volume["display_name_ru"] = spec["displayNameRu"]\n        volume["role"] = spec["functionalRole"]\n        volume["functional_role_status"] = spec["functionalRoleStatus"]\n        volume["system_tags_json"] = json.dumps(spec["systemTags"], ensure_ascii=False)\n        volume["X_MIN"] = x_min\n        volume["X_MAX"] = x_max\n        volume["LOCAL_VOLUME_M3"] = dimensions[0] * dimensions[1] * dimensions[2]\n        volume["CENTER"] = list(centre)\n        volume["CREW_CAPACITY_WEIGHT_KG"] = crew_weight\n        volume["FLOODABLE"] = floodable\n        volume["FIRE_CAPABLE"] = fire_capable\n        volume["POWER_DEPENDENCY"] = power\n        volume["PUMP_DEPENDENCY"] = pump\n        volume["REPAIR_ACCESS"] = repair_access\n        volume["source_basis"] = contract["referenceBasis"]["geometryStatus"]\n        compartment_records.append({\n            "name": name, "index": index, "semanticId": semantic_id,\n            "displayNameRu": spec["displayNameRu"], "functionalRole": spec["functionalRole"],\n            "functionalRoleStatus": spec["functionalRoleStatus"], "systemTags": spec["systemTags"],\n            "X_MIN": x_min, "X_MAX": x_max,\n            "LOCAL_VOLUME": dimensions[0] * dimensions[1] * dimensions[2],\n            "CENTER": list(centre), "ROLE": spec["functionalRole"],\n            "CREW_CAPACITY_WEIGHT": crew_weight, "FLOODABLE": floodable,\n            "FIRE_CAPABLE": fire_capable, "POWER_DEPENDENCY": power,\n            "PUMP_DEPENDENCY": pump, "REPAIR_ACCESS": repair_access,\n            "equipment_mass_kg": equipment_mass,\n        })\n    bpy.context.scene["antey_compartments"] = compartment_records\n'''
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"builder compartment body replacement count={count}")
    path.write_text(text, encoding="utf-8")


def patch_writer() -> None:
    path = ROOT / "Tools/Blender/write_production_sidecars.py"
    text = path.read_text(encoding="utf-8")
    if "def canonical_antey_compartment_records()" not in text:
        marker = "\ndef validate_semantic_spatial_metadata(\n"
        helper = '''\ndef canonical_antey_compartment_records() -> list[dict]:\n    contract_path = Path(__file__).resolve().parents[2] / "Content/submarines/Antey/Antey.compartments.json"\n    contract = json.loads(contract_path.read_text(encoding="utf-8"))\n    if contract.get("schemaVersion") != 1 or contract.get("coordinateContract") != "+X bow; +Y port; +Z up; 1 BU = 1 m":\n        raise RuntimeError("Antey compartment reference contract is invalid")\n    source = contract.get("compartments")\n    if not isinstance(source, list) or len(source) != 10:\n        raise RuntimeError("Antey compartment reference contract must contain exactly ten records")\n    records = []\n    for index, item in enumerate(source, 1):\n        semantic_id = f"compartment.{index:02d}"\n        if item.get("semanticId") != semantic_id:\n            raise RuntimeError(f"Unexpected Antey compartment semantic ID: {item.get('semanticId')} != {semantic_id}")\n        center = [float(value) for value in item["center"]]\n        half_extents = [float(value) for value in item["halfExtents"]]\n        x_min, x_max = [float(value) for value in item["xRangeMeters"]]\n        if not math.isclose(center[0], (x_min + x_max) * 0.5, abs_tol=1.0e-6) or not math.isclose(half_extents[0], (x_max - x_min) * 0.5, abs_tol=1.0e-6):\n            raise RuntimeError(f"Antey compartment spatial contract is inconsistent: {semantic_id}")\n        transform = identity_matrix_values()\n        transform[0][3], transform[1][3], transform[2][3] = center\n        records.append({\n            "name": f"Antey_Compartment_{index:02d}",\n            "semanticId": semantic_id,\n            "displayNameRu": item["displayNameRu"],\n            "functionalRole": item["functionalRole"],\n            "functionalRoleStatus": item["functionalRoleStatus"],\n            "systemTags": item["systemTags"],\n            "center": center,\n            "orientationQuaternionWXYZ": [float(value) for value in item["orientationQuaternionWXYZ"]],\n            "halfExtents": half_extents,\n            "xRangeMeters": [x_min, x_max],\n            "transform": transform,\n        })\n    return records\n\n\ndef validate_semantic_spatial_metadata(\n'''
        if marker not in text:
            raise RuntimeError("writer validation marker not found")
        text = text.replace(marker, helper, 1)

    old = '''    compartments = []\n    compartment_prefix = "VOL_COMP_" if any(obj.name.startswith("VOL_COMP_") for obj in objects.values()) else "Antey_Compartment_"\n    for obj in sorted((obj for obj in objects.values() if obj.name.startswith(compartment_prefix)), key=lambda item: item.name):\n        compartments.append(compartment_metadata(obj))\n'''
    new = '''    compartment_prefix = "VOL_COMP_" if any(obj.name.startswith("VOL_COMP_") for obj in objects.values()) else "Antey_Compartment_"\n    authored_compartment_objects = sorted((obj for obj in objects.values() if obj.name.startswith(compartment_prefix)), key=lambda item: item.name)\n    if len(authored_compartment_objects) != 10:\n        raise RuntimeError(f"Production Antey requires ten hidden compartment authoring objects, found {len(authored_compartment_objects)}")\n    # Spatial/function metadata is reference-derived and canonical in the JSON\n    # contract. The hidden BLEND objects remain authoring helpers; they are not\n    # allowed to silently overwrite reviewed bulkhead positions on sidecar regen.\n    compartments = canonical_antey_compartment_records()\n'''
    if old not in text:
        raise RuntimeError("writer compartment serialization block not found")
    text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8")


def main() -> None:
    patch_builder()
    patch_writer()


if __name__ == "__main__":
    main()
