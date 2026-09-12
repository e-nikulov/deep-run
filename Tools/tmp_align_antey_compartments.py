from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "Content/submarines/Antey/Antey.compartments.json"
AUTHORING_PATHS = [
    ROOT / "Content/submarines/Antey/Antey.authoring.json",
    ROOT / "Engine/Assets/submarines/Antey/Antey.authoring.json",
]


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def authoring_record(record: dict, ordinal: int) -> dict:
    center = record["center"]
    transform = [
        [1.0, 0.0, 0.0, float(center[0])],
        [0.0, 1.0, 0.0, float(center[1])],
        [0.0, 0.0, 1.0, float(center[2])],
        [0.0, 0.0, 0.0, 1.0],
    ]
    return {
        "name": f"Antey_Compartment_{ordinal:02d}",
        "semanticId": record["semanticId"],
        "displayNameRu": record["displayNameRu"],
        "functionalRole": record["functionalRole"],
        "functionalRoleStatus": record["functionalRoleStatus"],
        "systemTags": record["systemTags"],
        "center": record["center"],
        "orientationQuaternionWXYZ": record["orientationQuaternionWXYZ"],
        "halfExtents": record["halfExtents"],
        "xRangeMeters": record["xRangeMeters"],
        "transform": transform,
    }


def patch_authoring() -> None:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    compartments = contract["compartments"]
    assert len(compartments) == 10
    expected_ids = [f"compartment.{index:02d}" for index in range(1, 11)]
    assert [record["semanticId"] for record in compartments] == expected_ids
    generated = [authoring_record(record, index) for index, record in enumerate(compartments, start=1)]
    for path in AUTHORING_PATHS:
        data = json.loads(path.read_text(encoding="utf-8"))
        data["compartments"] = generated
        write_json(path, data)


def replace_exact(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"expected migration anchor not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def patch_runtime_header() -> None:
    path = ROOT / "Game/Submarine/ProductionAnteyAsset.h"
    old = '''struct ProductionCompartment final
{
    std::string semanticId;
    Assets::ModelVector3 localCenter{};
    std::array<float, 4> orientationQuaternionWxyz{};
    Assets::ModelVector3 halfExtents{};
};'''
    new = '''struct ProductionCompartment final
{
    std::string semanticId;
    std::string displayNameRu;
    std::string functionalRole;
    std::string functionalRoleStatus;
    std::vector<std::string> systemTags;
    Assets::ModelVector3 localCenter{};
    std::array<float, 4> orientationQuaternionWxyz{};
    Assets::ModelVector3 halfExtents{};
};'''
    replace_exact(path, old, new)


def patch_runtime_cpp() -> None:
    path = ROOT / "Game/Submarine/ProductionAnteyAsset.cpp"
    old = '''        const Json& compartments = authoring.at("compartments");
        Require(compartments.is_array() && compartments.size() == 10U, "Antey must have ten compartment records");
        std::size_t compartmentOrdinal = 0;
        std::unordered_set<std::string> compartmentReferences;
        std::vector<Assets::ModelVector3> compartmentCenters;
        for (const Json& compartment : compartments)
        {
            ++compartmentOrdinal;
            const std::string privateReference = compartment.at("name").get<std::string>();
            Require(compartmentReferences.insert(privateReference).second,
                    "Antey compartment source references must be unique");
            const Assets::ModelVector3 sourceCenter = ReadVector3(compartment.at("center"), "compartment center");
            const Assets::ModelVector3 halfExtents = ReadVector3(compartment.at("halfExtents"), "compartment half extents");
            Require(halfExtents.x > 0.0F && halfExtents.y > 0.0F && halfExtents.z > 0.0F,
                    "Antey compartment half extents must be positive");
            const Assets::ModelVector3 runtimeCenter = ConvertAnteyAuthoringVector(sourceCenter);
            compartmentCenters.push_back(runtimeCenter);
            definition.compartments.push_back({
                .semanticId = std::format("compartment.{:02}", compartmentOrdinal),
                .localCenter = runtimeCenter,
                .orientationQuaternionWxyz = ConvertAnteyAuthoringQuaternionWxyz(
                    ReadQuaternion(compartment.at("orientationQuaternionWXYZ"), "compartment orientation")),
                .halfExtents = ConvertAnteyAuthoringExtent(halfExtents)});
        }
'''
    new = '''        const Json& compartments = authoring.at("compartments");
        Require(compartments.is_array() && compartments.size() == 10U, "Antey must have ten compartment records");
        std::size_t compartmentOrdinal = 0;
        std::unordered_set<std::string> compartmentReferences;
        std::unordered_set<std::string> compartmentSemanticIds;
        std::vector<Assets::ModelVector3> compartmentCenters;
        for (const Json& compartment : compartments)
        {
            ++compartmentOrdinal;
            const std::string privateReference = compartment.at("name").get<std::string>();
            Require(compartmentReferences.insert(privateReference).second,
                    "Antey compartment source references must be unique");
            const std::string semanticId = compartment.at("semanticId").get<std::string>();
            Require(semanticId == std::format("compartment.{:02}", compartmentOrdinal),
                    "Antey compartment semantic IDs must be stable and ordered 01..10");
            Require(compartmentSemanticIds.insert(semanticId).second,
                    "Antey compartment semantic IDs must be unique");
            const std::string displayNameRu = compartment.at("displayNameRu").get<std::string>();
            const std::string functionalRole = compartment.at("functionalRole").get<std::string>();
            const std::string functionalRoleStatus = compartment.at("functionalRoleStatus").get<std::string>();
            Require(!displayNameRu.empty() && !functionalRole.empty() && !functionalRoleStatus.empty(),
                    "Antey compartment functional metadata must be non-empty");
            std::vector<std::string> systemTags;
            const Json& sourceSystemTags = compartment.at("systemTags");
            Require(sourceSystemTags.is_array(), "Antey compartment systemTags must be an array");
            for (const Json& tag : sourceSystemTags)
            {
                const std::string value = tag.get<std::string>();
                Require(!value.empty(), "Antey compartment systemTags must not contain empty values");
                systemTags.push_back(value);
            }
            const Assets::ModelVector3 sourceCenter = ReadVector3(compartment.at("center"), "compartment center");
            const Assets::ModelVector3 halfExtents = ReadVector3(compartment.at("halfExtents"), "compartment half extents");
            Require(halfExtents.x > 0.0F && halfExtents.y > 0.0F && halfExtents.z > 0.0F,
                    "Antey compartment half extents must be positive");
            const Json& xRange = compartment.at("xRangeMeters");
            Require(xRange.is_array() && xRange.size() == 2U, "Antey compartment xRangeMeters must contain two values");
            const float xMinimum = ReadFiniteFloat(xRange[0], "compartment x range minimum");
            const float xMaximum = ReadFiniteFloat(xRange[1], "compartment x range maximum");
            Require(xMaximum > xMinimum, "Antey compartment x range must be ordered");
            Require(std::abs(sourceCenter.x - 0.5F * (xMinimum + xMaximum)) <= 1.0e-4F &&
                        std::abs(halfExtents.x - 0.5F * (xMaximum - xMinimum)) <= 1.0e-4F,
                    "Antey compartment center/extents must agree with xRangeMeters");
            const Assets::ModelVector3 runtimeCenter = ConvertAnteyAuthoringVector(sourceCenter);
            compartmentCenters.push_back(runtimeCenter);
            definition.compartments.push_back({
                .semanticId = semanticId,
                .displayNameRu = displayNameRu,
                .functionalRole = functionalRole,
                .functionalRoleStatus = functionalRoleStatus,
                .systemTags = std::move(systemTags),
                .localCenter = runtimeCenter,
                .orientationQuaternionWxyz = ConvertAnteyAuthoringQuaternionWxyz(
                    ReadQuaternion(compartment.at("orientationQuaternionWXYZ"), "compartment orientation")),
                .halfExtents = ConvertAnteyAuthoringExtent(halfExtents)});
        }
'''
    replace_exact(path, old, new)


def patch_docs() -> None:
    path = ROOT / "docs/content/antey-asset.md"
    text = path.read_text(encoding="utf-8")
    anchor = "Ten `VOL_COMP_*` logical volumes prepare future damage/flooding authoring."
    replacement = '''Ten stable `compartment.01` .. `compartment.10` logical volumes prepare future damage/flooding authoring. Their longitudinal boundaries and functional roles are aligned to the supplied Project 949A longitudinal-section reference through `Antey.compartments.json`. The bulkhead X positions are `REFERENCE_DERIVED_APPROXIMATE` (nominal tolerance about +/-1 m), because the supplied drawing is not dimensioned shipyard documentation. Compartment 03 intentionally remains `THIRD_UNSPECIFIED` rather than inventing a function absent from the supplied legend. P-700 containers, bow sonar allocation, VVD bottles, shafting, steering gear, and sail equipment remain outside the pressure-compartment contract.'''
    if anchor not in text:
        raise RuntimeError("Antey asset documentation anchor not found")
    path.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")


def main() -> None:
    patch_authoring()
    patch_runtime_header()
    patch_runtime_cpp()
    patch_docs()


if __name__ == "__main__":
    main()
