from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT_CONTRACT = ROOT / "Content/submarines/Antey/Antey.compartments.json"
STAGED_CONTRACT = ROOT / "Engine/Assets/submarines/Antey/Antey.compartments.json"
AUTHORING_PATHS = (
    ROOT / "Content/submarines/Antey/Antey.authoring.json",
    ROOT / "Engine/Assets/submarines/Antey/Antey.authoring.json",
)

EXPECTED_ROLES = (
    "TORPEDO",
    "CENTRAL",
    "THIRD_UNSPECIFIED",
    "HABITABILITY",
    "AUXILIARY_MACHINERY_1",
    "AUXILIARY_MACHINERY_2",
    "REACTOR",
    "TURBINE_FORWARD",
    "TURBINE_AFT",
    "PROPULSION_ELECTRIC",
)
EXPECTED_X_RANGES = (
    (46.5, 66.5),
    (34.0, 46.5),
    (23.0, 34.0),
    (6.0, 23.0),
    (-1.0, 6.0),
    (-8.0, -1.0),
    (-19.0, -8.0),
    (-33.0, -19.0),
    (-46.5, -33.0),
    (-57.0, -46.5),
)
EMBEDDED_FIELDS = (
    "semanticId",
    "displayNameRu",
    "functionalRole",
    "functionalRoleStatus",
    "systemTags",
    "center",
    "orientationQuaternionWXYZ",
    "halfExtents",
    "xRangeMeters",
)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def close(left: float, right: float) -> bool:
    return math.isclose(float(left), float(right), rel_tol=0.0, abs_tol=1.0e-6)


def validate_contract(contract: dict) -> None:
    require(contract.get("schemaVersion") == 1, "Antey compartment schemaVersion must be 1")
    require(contract.get("coordinateContract") == "+X bow; +Y port; +Z up; 1 BU = 1 m",
            "Antey compartment coordinate contract changed")
    basis = contract.get("referenceBasis", {})
    require(basis.get("platform") == "Project 949A Antey", "Antey compartment reference platform changed")
    require(basis.get("geometryStatus") == "REFERENCE_DERIVED_APPROXIMATE",
            "Antey compartment geometry provenance must stay explicit")
    require(close(basis.get("longitudinalToleranceMeters", -1.0), 1.0),
            "Antey compartment longitudinal tolerance must remain explicit")

    compartments = contract.get("compartments")
    require(isinstance(compartments, list) and len(compartments) == 10,
            "Antey production contract must contain exactly ten compartments")

    for index, (record, role, expected_range) in enumerate(zip(compartments, EXPECTED_ROLES, EXPECTED_X_RANGES), start=1):
        semantic_id = f"compartment.{index:02d}"
        require(record.get("semanticId") == semantic_id, f"Unexpected semantic ID at {semantic_id}")
        require(record.get("functionalRole") == role, f"Unexpected functional role for {semantic_id}")
        expected_status = "REFERENCE_UNSPECIFIED" if index == 3 else "REFERENCE_EXPLICIT"
        require(record.get("functionalRoleStatus") == expected_status,
                f"Unexpected functional-role provenance for {semantic_id}")
        require(isinstance(record.get("displayNameRu"), str) and record["displayNameRu"],
                f"Missing Russian display name for {semantic_id}")
        require(isinstance(record.get("systemTags"), list), f"systemTags must be an array for {semantic_id}")

        x_range = record.get("xRangeMeters")
        require(isinstance(x_range, list) and len(x_range) == 2, f"Invalid xRangeMeters for {semantic_id}")
        require(close(x_range[0], expected_range[0]) and close(x_range[1], expected_range[1]),
                f"Reference-derived X range drifted for {semantic_id}: {x_range}")
        center = record.get("center")
        extents = record.get("halfExtents")
        require(isinstance(center, list) and len(center) == 3, f"Invalid center for {semantic_id}")
        require(isinstance(extents, list) and len(extents) == 3, f"Invalid halfExtents for {semantic_id}")
        require(close(center[0], (x_range[0] + x_range[1]) * 0.5),
                f"Center/X range mismatch for {semantic_id}")
        require(close(extents[0], (x_range[1] - x_range[0]) * 0.5),
                f"Half extent/X range mismatch for {semantic_id}")
        require(float(extents[1]) > 0.0 and float(extents[2]) > 0.0,
                f"Non-positive compartment extent for {semantic_id}")

        if index > 1:
            previous = compartments[index - 2]["xRangeMeters"]
            require(close(previous[0], x_range[1]),
                    f"Pressure-compartment boundaries are not contiguous between {index - 1:02d} and {index:02d}")

    regions = {record.get("semanticId"): record for record in contract.get("nonCompartmentRegions", [])}
    bow = regions.get("external.bow_sonar", {})
    aft = regions.get("external.aft_shaft_steering", {})
    require(bow.get("role") == "MGK540_BOW_ARRAY" and bow.get("xRangeMeters") == [66.5, 77.0],
            "Forward sonar/nose region must remain outside compartment.01")
    require(aft.get("role") == "SHAFTING_AND_STEERING_GEAR" and aft.get("xRangeMeters") == [-77.0, -57.0],
            "Aft shaft/steering region must remain outside compartment.10")
    require("external.p700_banks" in regions, "P-700 external-bank region is missing")


def validate_embedded(contract: dict, path: Path) -> None:
    authoring = load(path)
    embedded = authoring.get("compartments")
    require(isinstance(embedded, list) and len(embedded) == 10,
            f"{path} must embed ten production compartment records")
    for index, (canonical, actual) in enumerate(zip(contract["compartments"], embedded), start=1):
        require(actual.get("name") == f"Antey_Compartment_{index:02d}",
                f"Unexpected authoring compartment name in {path}: {index:02d}")
        for field in EMBEDDED_FIELDS:
            require(actual.get(field) == canonical.get(field),
                    f"{path}: embedded compartment {index:02d} field {field} drifted from canonical contract")


def main() -> None:
    canonical = load(CONTENT_CONTRACT)
    staged = load(STAGED_CONTRACT)
    require(canonical == staged, "Content and staged Antey compartment contracts differ")
    validate_contract(canonical)
    for path in AUTHORING_PATHS:
        validate_embedded(canonical, path)
    print("Antey compartment production contract: PASS")


if __name__ == "__main__":
    main()
