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
PERISCOPE_DOC_PATH = ROOT / "docs/development/periscope-ballast-gameplay.md"

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
    if secondary_count != 1:
        raise RuntimeError(f"{path}: exactly one SIGNAL-3 secondary periscope binding is required")

    write_json(path, payload)


def system_node(contract: dict, system_id: str) -> str:
    for system in contract["systems"]:
        if system.get("systemId") == system_id:
            node = system.get("nodeReference")
            if not isinstance(node, str) or not node:
                raise RuntimeError(f"{system_id} must have a confirmed production node binding")
            return node
    raise RuntimeError(f"Antey electronics contract is missing system {system_id}")


def private_sail_device_name(node_reference: str) -> str:
    prefix = "SM_Antey_LOD0_"
    return node_reference[len(prefix):] if node_reference.startswith(prefix) else node_reference


def sync_periscope_doc(contract: dict) -> None:
    primary = private_sail_device_name(system_node(contract, PRIMARY_PERISCOPE_SYSTEM))
    secondary = private_sail_device_name(system_node(contract, SECONDARY_PERISCOPE_SYSTEM))
    lines = PERISCOPE_DOC_PATH.read_text(encoding="utf-8").splitlines()
    matches = [index for index, line in enumerate(lines) if line.startswith("Direct production-GLB inspection") or line.startswith("Production authoring mapping now binds")]
    if len(matches) != 1:
        raise RuntimeError(f"{PERISCOPE_DOC_PATH}: expected exactly one periscope mapping paragraph, found {len(matches)}")
    lines[matches[0]] = (
        f"Production authoring mapping now binds private node `{primary}` to the PZNS-10S gameplay primary/attack periscope "
        f"and private node `{secondary}` to the SIGNAL-3 navigation/secondary periscope. Those node names do not cross "
        "into normal runtime: generated sidecars publish `PERISCOPE_PRIMARY` / `PERISCOPE_SECONDARY` plus semantic "
        "electronic `systemRoles`; gameplay resolves PZNS-10S as the primary combat optic while SIGNAL-3 is driven "
        "through the electronic-suite mast presentation. No exact classified optics performance is asserted. Both masts "
        "use their authored stowed/deployed transforms; deployment timing remains GAME POLICY."
    )
    PERISCOPE_DOC_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    contract = load_json(CONTRACT_PATH)
    node_roles = build_node_roles(contract)
    for path in AUTHORING_PATHS:
        sync_authoring(path, node_roles)
    sync_periscope_doc(contract)

    content_payload = load_json(AUTHORING_PATHS[0])
    engine_payload = load_json(AUTHORING_PATHS[1])
    if content_payload != engine_payload:
        raise RuntimeError("Content and Engine Antey authoring sidecars diverged after electronics sync")

    print("Antey electronics production mapping sync: PASS")


if __name__ == "__main__":
    main()
