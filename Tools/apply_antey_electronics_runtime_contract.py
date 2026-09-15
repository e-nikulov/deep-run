from __future__ import annotations

from pathlib import Path

# This patch is intentionally executed on the branch head by CI; keep it separate from the generated mapping sync.
ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one patch marker, found {count}: {old[:100]!r}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def patch_header() -> None:
    path = ROOT / "Game/Submarine/ProductionAnteyAsset.h"
    replace_once(
        path,
        "    // Semantic equipment role crosses the authoring/runtime boundary; raw source node identity does not.\n"
        "    std::string functionalRole;\n",
        "    // Semantic equipment role crosses the authoring/runtime boundary; raw source node identity does not.\n"
        "    std::string functionalRole;\n"
        "    // One physical mast may host multiple gameplay systems (for example ZONA+RADIAN or KORA+RKP).\n"
        "    std::vector<std::string> systemRoles;\n",
    )


def patch_loader() -> None:
    path = ROOT / "Game/Submarine/ProductionAnteyAsset.cpp"
    replace_once(
        path,
        "        std::size_t primaryPeriscopeCount = 0U;\n"
        "        std::size_t secondaryPeriscopeCount = 0U;\n",
        "        std::size_t primaryPeriscopeCount = 0U;\n"
        "        std::size_t secondaryPeriscopeCount = 0U;\n"
        "        std::unordered_set<std::string> electronicSystemRolesSeen;\n",
    )
    replace_once(
        path,
        "            const std::string semanticId = device.at(\"semanticId\").get<std::string>();\n"
        "            const std::string functionalRole = device.at(\"functionalRole\").get<std::string>();\n"
        "            Require(functionalRole == \"OTHER_RETRACTABLE\" || functionalRole == \"PERISCOPE_PRIMARY\" ||\n"
        "                        functionalRole == \"PERISCOPE_SECONDARY\",\n"
        "                    \"Antey retractable sail-device functional role is unexpected\");\n"
        "            primaryPeriscopeCount += functionalRole == \"PERISCOPE_PRIMARY\" ? 1U : 0U;\n"
        "            secondaryPeriscopeCount += functionalRole == \"PERISCOPE_SECONDARY\" ? 1U : 0U;\n",
        "            const std::string semanticId = device.at(\"semanticId\").get<std::string>();\n"
        "            const std::string functionalRole = device.at(\"functionalRole\").get<std::string>();\n"
        "            Require(functionalRole == \"OTHER_RETRACTABLE\" || functionalRole == \"PERISCOPE_PRIMARY\" ||\n"
        "                        functionalRole == \"PERISCOPE_SECONDARY\",\n"
        "                    \"Antey retractable sail-device functional role is unexpected\");\n"
        "            const Json& sourceSystemRoles = device.at(\"systemRoles\");\n"
        "            Require(sourceSystemRoles.is_array(), \"Antey retractable sail-device systemRoles must be an array\");\n"
        "            std::vector<std::string> systemRoles;\n"
        "            for (const Json& roleValue : sourceSystemRoles)\n"
        "            {\n"
        "                const std::string role = roleValue.get<std::string>();\n"
        "                const bool knownRole =\n"
        "                    role == \"SYNTHESIS_SATNAV\" || role == \"ZONA_RDF_ESM\" || role == \"ANIS_RADIO\" ||\n"
        "                    role == \"MRSC2_TARGETING\" || role == \"RADIAN_SURFACE_RADAR\" ||\n"
        "                    role == \"KORA_MOLNIYA_M\" || role == \"RKP_COMPRESSOR_INTAKE\" ||\n"
        "                    role == \"SELENA_KORALL\" || role == \"SIGNAL3_NAV_PERISCOPE\" ||\n"
        "                    role == \"PZNS10S_ATTACK_PERISCOPE\";\n"
        "                Require(knownRole, \"Antey retractable sail-device system role is unexpected\");\n"
        "                Require(electronicSystemRolesSeen.insert(role).second,\n"
        "                        \"Antey electronic system role must bind to only one physical sail device\");\n"
        "                systemRoles.push_back(role);\n"
        "            }\n"
        "            const bool hasPzns10s = std::ranges::find(systemRoles, \"PZNS10S_ATTACK_PERISCOPE\") != systemRoles.end();\n"
        "            const bool hasSignal3 = std::ranges::find(systemRoles, \"SIGNAL3_NAV_PERISCOPE\") != systemRoles.end();\n"
        "            Require((functionalRole == \"PERISCOPE_PRIMARY\") == hasPzns10s,\n"
        "                    \"Antey primary periscope role must correspond to PZNS-10S\");\n"
        "            Require((functionalRole == \"PERISCOPE_SECONDARY\") == hasSignal3,\n"
        "                    \"Antey secondary periscope role must correspond to SIGNAL-3 when mapped\");\n"
        "            primaryPeriscopeCount += functionalRole == \"PERISCOPE_PRIMARY\" ? 1U : 0U;\n"
        "            secondaryPeriscopeCount += functionalRole == \"PERISCOPE_SECONDARY\" ? 1U : 0U;\n",
    )
    replace_once(
        path,
        "                .semanticId = std::move(semanticId),\n"
        "                .functionalRole = functionalRole,\n"
        "                .presentationNodeBindingIndex = bindingIndex,\n",
        "                .semanticId = std::move(semanticId),\n"
        "                .functionalRole = functionalRole,\n"
        "                .systemRoles = std::move(systemRoles),\n"
        "                .presentationNodeBindingIndex = bindingIndex,\n",
    )
    replace_once(
        path,
        "        Require(primaryPeriscopeCount == 1U && secondaryPeriscopeCount == 1U,\n"
        "                \"Antey must expose exactly one primary and one secondary production periscope role\");\n",
        "        Require(primaryPeriscopeCount == 1U && secondaryPeriscopeCount <= 1U,\n"
        "                \"Antey must expose exactly one PZNS-10S primary periscope and at most one mapped SIGNAL-3 secondary\");\n"
        "        Require(electronicSystemRolesSeen.contains(\"SYNTHESIS_SATNAV\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"ZONA_RDF_ESM\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"ANIS_RADIO\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"MRSC2_TARGETING\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"RADIAN_SURFACE_RADAR\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"KORA_MOLNIYA_M\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"RKP_COMPRESSOR_INTAKE\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"SELENA_KORALL\") &&\n"
        "                    electronicSystemRolesSeen.contains(\"PZNS10S_ATTACK_PERISCOPE\"),\n"
        "                \"Antey electronics mapping is missing one or more confirmed production bindings\");\n",
    )


def patch_sidecar_writer() -> None:
    path = ROOT / "Tools/Blender/write_production_sidecars.py"
    replace_once(
        path,
        "# Reviewed production-device identity from the accepted GLB + public 949A retractable-device layout.\n"
        "# Private node names stop here; runtime consumes only semantic functional roles.\n"
        "ANTEY_RETRACTABLE_SAIL_DEVICE_ROLES = {\n"
        "    \"SM_Antey_LOD0_SailDevice_11\": \"PERISCOPE_PRIMARY\",\n"
        "    \"SM_Antey_LOD0_SailDevice_17\": \"PERISCOPE_SECONDARY\",\n"
        "}\n",
        "# Canonical retractable-device identity is maintained in Antey.electronics.json.\n"
        "# Private node names stop in authoring metadata; runtime consumes semantic system roles.\n"
        "ANTEY_PRIMARY_PERISCOPE_SYSTEM = \"PZNS10S_ATTACK_PERISCOPE\"\n"
        "ANTEY_SECONDARY_PERISCOPE_SYSTEM = \"SIGNAL3_NAV_PERISCOPE\"\n\n"
        "def antey_retractable_system_roles() -> dict[str, list[str]]:\n"
        "    contract_path = Path(__file__).resolve().parents[2] / \"Content/submarines/Antey/Antey.electronics.json\"\n"
        "    contract = json.loads(contract_path.read_text(encoding=\"utf-8\"))\n"
        "    result: dict[str, list[str]] = {}\n"
        "    for system in contract.get(\"systems\", []):\n"
        "        node = system.get(\"nodeReference\")\n"
        "        if node is None:\n"
        "            continue\n"
        "        result.setdefault(str(node), []).append(str(system[\"systemId\"]))\n"
        "    return result\n",
    )
    replace_once(
        path,
        "    retractable = []\n"
        "    for obj in sail_devices:\n",
        "    retractable = []\n"
        "    system_roles_by_node = antey_retractable_system_roles()\n"
        "    for obj in sail_devices:\n",
    )
    replace_once(
        path,
        "        retractable.append(\n"
        "            {\n"
        "                \"semanticId\": f\"sail.retractable.{len(retractable) + 1:02d}\",\n"
        "                \"nodeReference\": obj.name,\n"
        "                \"classification\": \"RETRACTABLE\",\n"
        "                \"functionalRole\": ANTEY_RETRACTABLE_SAIL_DEVICE_ROLES.get(obj.name, \"OTHER_RETRACTABLE\"),\n"
        "                \"defaultState\": \"STOWED\",\n",
        "        system_roles = list(system_roles_by_node.get(obj.name, []))\n"
        "        functional_role = (\n"
        "            \"PERISCOPE_PRIMARY\" if ANTEY_PRIMARY_PERISCOPE_SYSTEM in system_roles else\n"
        "            \"PERISCOPE_SECONDARY\" if ANTEY_SECONDARY_PERISCOPE_SYSTEM in system_roles else\n"
        "            \"OTHER_RETRACTABLE\"\n"
        "        )\n"
        "        retractable.append(\n"
        "            {\n"
        "                \"semanticId\": f\"sail.retractable.{len(retractable) + 1:02d}\",\n"
        "                \"nodeReference\": obj.name,\n"
        "                \"classification\": \"RETRACTABLE\",\n"
        "                \"functionalRole\": functional_role,\n"
        "                \"systemRoles\": system_roles,\n"
        "                \"defaultState\": \"STOWED\",\n",
    )
    replace_once(
        path,
        "    if roles.count(\"PERISCOPE_PRIMARY\") != 1 or roles.count(\"PERISCOPE_SECONDARY\") != 1:\n"
        "        raise RuntimeError(\"Antey production sidecar must expose exactly one primary and one secondary periscope\")\n",
        "    if roles.count(\"PERISCOPE_PRIMARY\") != 1 or roles.count(\"PERISCOPE_SECONDARY\") > 1:\n"
        "        raise RuntimeError(\"Antey production sidecar must expose one PZNS-10S primary and at most one mapped SIGNAL-3 secondary\")\n",
    )


def patch_docs() -> None:
    path = ROOT / "docs/development/m5-combat-playground.md"
    text = path.read_text(encoding="utf-8")
    text = text.replace("`sail.retractable.06`, production `SailDevice_11`", "`sail.retractable.05`, production `SailDevice_10` (PZNS-10S)")
    path.write_text(text, encoding="utf-8")

    path = ROOT / "docs/development/periscope-ballast-gameplay.md"
    text = path.read_text(encoding="utf-8")
    old = "private authoring node `SailDevice_11` as the gameplay primary periscope and `SailDevice_17` as the secondary"
    new = "private authoring node `SailDevice_10` as the PZNS-10S gameplay primary periscope; the SIGNAL-3 node remains intentionally unbound until explicitly confirmed"
    if old not in text:
        raise RuntimeError(f"{path}: stale periscope mapping sentence was not found")
    path.write_text(text.replace(old, new), encoding="utf-8")


def main() -> None:
    patch_header()
    patch_loader()
    patch_sidecar_writer()
    patch_docs()
    print("Antey electronics runtime/content contract patch: PASS")


if __name__ == "__main__":
    main()
