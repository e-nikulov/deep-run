from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT_DIR = ROOT / "Content/submarines/Antey"
ENGINE_DIR = ROOT / "Engine/Assets/submarines/Antey"
CONTRACT_PATH = CONTENT_DIR / "Antey.electronics.json"
AUTHORING_PATHS = (
    CONTENT_DIR / "Antey.authoring.json",
    ENGINE_DIR / "Antey.authoring.json",
)

PRIMARY_PERISCOPE_SYSTEM = "PZNS10S_ATTACK_PERISCOPE"
SECONDARY_PERISCOPE_SYSTEM = "SIGNAL3_NAV_PERISCOPE"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def build_node_roles(contract: dict) -> dict[str, list[str]]:
    systems = contract.get("systems")
    if not isinstance(systems, list) or len(systems) != 10:
        raise RuntimeError("Antey electronics contract must contain exactly ten diagram systems")

    node_roles: dict[str, list[str]] = {}
    diagram_numbers: set[int] = set()
    for system in systems:
        number = int(system["diagramNumber"])
        if number in diagram_numbers:
            raise RuntimeError(f"Duplicate Antey electronics diagram number: {number}")
        diagram_numbers.add(number)
        system_id = str(system["systemId"])
        node = system.get("nodeReference")
        if node is None:
            if system.get("mappingStatus") != "UNRESOLVED":
                raise RuntimeError(f"Unbound system {system_id} must be explicitly UNRESOLVED")
            continue
        if not isinstance(node, str) or not node:
            raise RuntimeError(f"Invalid node reference for system {system_id}")
        node_roles.setdefault(node, []).append(system_id)

    if diagram_numbers != set(range(1, 11)):
        raise RuntimeError("Antey electronics contract diagram numbers must be exactly 1..10")
    return node_roles


def sync_authoring(path: Path, node_roles: dict[str, list[str]]) -> None:
    payload = load_json(path)
    devices = payload.get("retractableSailDevices")
    if not isinstance(devices, list) or not devices:
        raise RuntimeError(f"{path}: retractableSailDevices is missing")

    known_nodes = {str(device["nodeReference"]) for device in devices}
    unknown_nodes = sorted(set(node_roles) - known_nodes)
    if unknown_nodes:
        raise RuntimeError(f"{path}: electronics mapping references unknown sail devices: {unknown_nodes}")

    primary_count = 0
    secondary_count = 0
    for device in devices:
        node = str(device["nodeReference"])
        roles = list(node_roles.get(node, []))
        device["systemRoles"] = roles
        if PRIMARY_PERISCOPE_SYSTEM in roles:
            device["functionalRole"] = "PERISCOPE_PRIMARY"
            primary_count += 1
        elif SECONDARY_PERISCOPE_SYSTEM in roles:
            device["functionalRole"] = "PERISCOPE_SECONDARY"
            secondary_count += 1
        else:
            device["functionalRole"] = "OTHER_RETRACTABLE"

    if primary_count != 1:
        raise RuntimeError(f"{path}: exactly one PZNS-10S primary periscope binding is required")
    if secondary_count > 1:
        raise RuntimeError(f"{path}: at most one SIGNAL-3 secondary periscope binding is allowed")

    write_json(path, payload)


def main() -> None:
    contract = load_json(CONTRACT_PATH)
    node_roles = build_node_roles(contract)
    for path in AUTHORING_PATHS:
        sync_authoring(path, node_roles)

    content_payload = load_json(AUTHORING_PATHS[0])
    engine_payload = load_json(AUTHORING_PATHS[1])
    if content_payload != engine_payload:
        raise RuntimeError("Content and Engine Antey authoring sidecars diverged after electronics sync")

    print("Antey electronics production mapping sync: PASS")


if __name__ == "__main__":
    main()
