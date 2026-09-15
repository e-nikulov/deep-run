from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Tools/apply_antey_electronics_gameplay_integration.py"

spec = importlib.util.spec_from_file_location("antey_gameplay_patch", SOURCE)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load Antey gameplay patch module")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def corrected_physical_mast_presentation() -> None:
    path = ROOT / "Game/PhysicalPlayground.h"
    patch.replace_once(path, "#include <string>\n#include <vector>\n", "#include <string>\n#include <string_view>\n#include <utility>\n#include <vector>\n")
    patch.replace_once(path,
        "    [[nodiscard]] float PeriscopeDeploymentProgress() const noexcept\n    {\n        return primaryPeriscopeDeploymentProgress_;\n    }\n\n    // Presentation-only bridge from the P-700 lifecycle.",
        "    [[nodiscard]] float PeriscopeDeploymentProgress() const noexcept\n    {\n        return primaryPeriscopeDeploymentProgress_;\n    }\n\n"
        "    [[nodiscard]] std::expected<void, std::string> SetRetractableSystemPresentation(\n"
        "        const std::string_view systemRole, const bool raised)\n"
        "    {\n"
        "        for (auto& binding : retractableSystemPresentationBindings_)\n"
        "        {\n"
        "            for (auto& request : binding.systemRequests)\n"
        "            {\n"
        "                if (request.first == systemRole)\n"
        "                {\n"
        "                    request.second = raised;\n"
        "                    return {};\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        return std::unexpected(\"production retractable-system presentation binding is unavailable: \" +\n"
        "                               std::string(systemRole));\n"
        "    }\n\n"
        "    // Presentation-only bridge from the P-700 lifecycle.")
    patch.replace_once(path,
        "    // IG1-B.1 fixed submerged presentation state. These per-node post transforms are built from opaque\n"
        "    // IG1 production bindings once at initialization and never mutate ModelAsset or physics.\n"
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;\n",
        "    // IG1-B.1 fixed submerged presentation state. These per-node post transforms are built from opaque\n"
        "    // IG1 production bindings once at initialization and never mutate ModelAsset or physics.\n"
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;\n"
        "    struct RetractableSystemPresentationBinding final\n"
        "    {\n"
        "        std::size_t nodeIndex = 0U;\n"
        "        std::vector<std::pair<std::string, bool>> systemRequests;\n"
        "        Assets::ModelTransform stowedTransform{};\n"
        "        Assets::ModelTransform deployedTransform{};\n"
        "        float deploymentProgress = 0.0F;\n"
        "    };\n"
        "    std::vector<RetractableSystemPresentationBinding> retractableSystemPresentationBindings_;\n")

    path = ROOT / "Game/PhysicalPlayground.cpp"
    patch.replace_once(path,
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;\n"
        "    submergedSailDeviceOverrides.reserve(productionDefinition->retractableSailDevices.size());\n"
        "    std::optional<std::size_t> primaryPeriscopeNodeIndex;",
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;\n"
        "    submergedSailDeviceOverrides.reserve(productionDefinition->retractableSailDevices.size());\n"
        "    std::vector<RetractableSystemPresentationBinding> retractableSystemPresentationBindings;\n"
        "    retractableSystemPresentationBindings.reserve(productionDefinition->retractableSailDevices.size());\n"
        "    std::optional<std::size_t> primaryPeriscopeNodeIndex;")
    patch.replace_once(path,
        "        submergedSailDeviceOverrides.push_back(\n"
        "            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});\n"
        "        if (device.functionalRole == M5PrimaryPeriscopeFunctionalRole)\n",
        "        submergedSailDeviceOverrides.push_back(\n"
        "            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});\n"
        "        if (!device.systemRoles.empty() && device.functionalRole != M5PrimaryPeriscopeFunctionalRole)\n"
        "        {\n"
        "            RetractableSystemPresentationBinding presentationBinding{\n"
        "                .nodeIndex = *meshNodeIndex,\n"
        "                .stowedTransform = device.stowedLocalPostTransform,\n"
        "                .deployedTransform = device.deployedLocalPostTransform};\n"
        "            for (const std::string& systemRole : device.systemRoles)\n"
        "                presentationBinding.systemRequests.emplace_back(systemRole, false);\n"
        "            retractableSystemPresentationBindings.push_back(std::move(presentationBinding));\n"
        "        }\n"
        "        if (device.functionalRole == M5PrimaryPeriscopeFunctionalRole)\n")
    patch.replace_once(path,
        "    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);\n    primaryPeriscopeNodeIndex_ = primaryPeriscopeNodeIndex;",
        "    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);\n"
        "    retractableSystemPresentationBindings_ = std::move(retractableSystemPresentationBindings);\n"
        "    primaryPeriscopeNodeIndex_ = primaryPeriscopeNodeIndex;")
    patch.replace_once(path,
        "    // Presentation animation follows committed gameplay periscope state but never feeds physics/sensors.\n"
        "    const float periscopeStep = fixedDeltaSeconds / M5PrimaryPeriscopeDeploymentSeconds;",
        "    // Presentation animation follows committed gameplay mast state but never feeds physics/sensors.\n"
        "    const float periscopeStep = fixedDeltaSeconds / M5PrimaryPeriscopeDeploymentSeconds;\n"
        "    for (auto& binding : retractableSystemPresentationBindings_)\n"
        "    {\n"
        "        const bool requestedRaised = std::ranges::any_of(\n"
        "            binding.systemRequests, [](const auto& request) { return request.second; });\n"
        "        binding.deploymentProgress = std::clamp(\n"
        "            binding.deploymentProgress + (requestedRaised ? periscopeStep : -periscopeStep), 0.0F, 1.0F);\n"
        "    }\n")
    patch.replace_once(path,
        "    if (primaryPeriscopeNodeIndex_.has_value())\n    {",
        "    for (const auto& binding : retractableSystemPresentationBindings_)\n"
        "    {\n"
        "        Assets::ModelTransform transform = binding.stowedTransform;\n"
        "        for (std::size_t element = 0; element < transform.values.size(); ++element)\n"
        "        {\n"
        "            transform.values[element] = binding.stowedTransform.values[element] +\n"
        "                (binding.deployedTransform.values[element] - binding.stowedTransform.values[element]) *\n"
        "                    binding.deploymentProgress;\n"
        "        }\n"
        "        const auto existing = std::ranges::find_if(submarineNodeOverrides, [&](const auto& value) {\n"
        "            return value.nodeIndex == binding.nodeIndex;\n"
        "        });\n"
        "        if (existing == submarineNodeOverrides.end())\n"
        "            return std::unexpected(\"physical playground retractable-system stowed override disappeared\");\n"
        "        existing->nodeLocalPostTransform = transform;\n"
        "    }\n"
        "    if (primaryPeriscopeNodeIndex_.has_value())\n    {")


patch.patch_physical_mast_presentation = corrected_physical_mast_presentation
patch.main()
