from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one replacement target, got {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "Engine/Assets/ModelAsset.h",
    '''    // Present only when this imported node contributes a drawable mesh node.\n    // This remains an Assets-layer resolution detail; semantic consumers retain\n    // only the opaque nodeBindings index.\n    std::optional<std::size_t> meshNodeIndex;\n''',
    '''    // Present only when this imported node contributes a drawable mesh node.\n    // This remains an Assets-layer resolution detail; semantic consumers retain\n    // only the opaque nodeBindings index.\n    std::optional<std::size_t> meshNodeIndex;\n    // Mesh nodes contributed by this binding's complete imported subtree. This lets\n    // presentation animate a semantic transform-only root without leaking child GLB\n    // names into Game code. Direct drawable bindings include their own mesh node.\n    std::vector<std::size_t> drawableMeshNodeIndices;\n''')

replace_once(
    "Engine/Assets/GltfModelLoader.cpp",
    '''        ModelNodeBindingData binding{\n            .name = std::string(sourceNode.name), .localToModel = ToModelTransform(localToModel)};\n\n        if (sourceNode.meshIndex.has_value())\n        {\n            binding.meshNodeIndex = output_.nodes.size();\n            auto primitiveIndices = ImportMesh(*sourceNode.meshIndex);\n''',
    '''        ModelNodeBindingData binding{\n            .name = std::string(sourceNode.name), .localToModel = ToModelTransform(localToModel)};\n\n        if (sourceNode.meshIndex.has_value())\n        {\n            binding.meshNodeIndex = output_.nodes.size();\n            binding.drawableMeshNodeIndices.push_back(*binding.meshNodeIndex);\n            auto primitiveIndices = ImportMesh(*sourceNode.meshIndex);\n''')

replace_once(
    "Engine/Assets/GltfModelLoader.cpp",
    '''        output_.nodeBindings.push_back(std::move(binding));\n\n        for (const std::size_t childIndex : sourceNode.children)\n        {\n            if (auto result = VisitNode(childIndex, localToModel); !result)\n            {\n                return result;\n            }\n        }\n        nodeVisitState_[nodeIndex] = 2;\n''',
    '''        const std::size_t bindingIndex = output_.nodeBindings.size();\n        output_.nodeBindings.push_back(std::move(binding));\n\n        for (const std::size_t childIndex : sourceNode.children)\n        {\n            const std::size_t childBindingIndex = output_.nodeBindings.size();\n            if (auto result = VisitNode(childIndex, localToModel); !result)\n            {\n                return result;\n            }\n            if (childBindingIndex >= output_.nodeBindings.size())\n            {\n                return std::unexpected(MakeError(\n                    AssetErrorCode::InvalidData, path_, id_, "scene child produced no imported node binding"));\n            }\n            const auto& childDrawables = output_.nodeBindings[childBindingIndex].drawableMeshNodeIndices;\n            auto& parentDrawables = output_.nodeBindings[bindingIndex].drawableMeshNodeIndices;\n            parentDrawables.insert(parentDrawables.end(), childDrawables.begin(), childDrawables.end());\n        }\n        nodeVisitState_[nodeIndex] = 2;\n''')

replace_once(
    "Engine/Render/ModelDraw.h",
    '''struct ModelNodeTransformOverride final\n{\n    std::size_t nodeIndex = 0;\n    Assets::ModelTransform nodeLocalPostTransform{};\n};\n\n''',
    '''struct ModelNodeTransformOverride final\n{\n    std::size_t nodeIndex = 0;\n    Assets::ModelTransform nodeLocalPostTransform{};\n};\n\n// Presentation-only transform applied at an imported semantic binding root. The\n// Assets layer expands the opaque binding to its drawable subtree, while Render\n// preserves parent-space transform order for every descendant mesh node.\nstruct ModelBindingTransformOverride final\n{\n    std::size_t bindingIndex = 0;\n    Assets::ModelTransform bindingLocalPostTransform{};\n};\n\n''')

replace_once(
    "Engine/Render/ModelDraw.h",
    '''[[nodiscard]] std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(\n    const Assets::ModelAsset& model,\n    const Assets::ModelTransform& modelToWorld = {},\n    std::span<const ModelNodeTransformOverride> nodeTransformOverrides = {});\n''',
    '''[[nodiscard]] std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(\n    const Assets::ModelAsset& model,\n    const Assets::ModelTransform& modelToWorld = {},\n    std::span<const ModelNodeTransformOverride> nodeTransformOverrides = {},\n    std::span<const ModelBindingTransformOverride> bindingTransformOverrides = {});\n''')

replace_once(
    "Engine/Render/ModelDraw.cpp",
    '''#include <cmath>\n#include <sstream>\n#include <utility>\n#include <vector>\n''',
    '''#include <cmath>\n#include <optional>\n#include <sstream>\n#include <utility>\n#include <vector>\n''')

replace_once(
    "Engine/Render/ModelDraw.cpp",
    '''bool IsFiniteAffineTransform(const Assets::ModelTransform& transform) noexcept\n{\n    for (const float value : transform.values)\n    {\n        if (!std::isfinite(value))\n        {\n            return false;\n        }\n    }\n    return transform.values[3] == 0.0F && transform.values[7] == 0.0F &&\n           transform.values[11] == 0.0F && transform.values[15] == 1.0F;\n}\n}\n''',
    '''bool IsFiniteAffineTransform(const Assets::ModelTransform& transform) noexcept\n{\n    for (const float value : transform.values)\n    {\n        if (!std::isfinite(value))\n        {\n            return false;\n        }\n    }\n    return transform.values[3] == 0.0F && transform.values[7] == 0.0F &&\n           transform.values[11] == 0.0F && transform.values[15] == 1.0F;\n}\n\nstd::optional<Assets::ModelTransform> InvertAffineTransform(const Assets::ModelTransform& transform) noexcept\n{\n    if (!IsFiniteAffineTransform(transform))\n    {\n        return std::nullopt;\n    }\n\n    const float a00 = Element(transform, 0, 0);\n    const float a01 = Element(transform, 0, 1);\n    const float a02 = Element(transform, 0, 2);\n    const float a10 = Element(transform, 1, 0);\n    const float a11 = Element(transform, 1, 1);\n    const float a12 = Element(transform, 1, 2);\n    const float a20 = Element(transform, 2, 0);\n    const float a21 = Element(transform, 2, 1);\n    const float a22 = Element(transform, 2, 2);\n    const float determinant =\n        a00 * (a11 * a22 - a12 * a21) -\n        a01 * (a10 * a22 - a12 * a20) +\n        a02 * (a10 * a21 - a11 * a20);\n    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-8F)\n    {\n        return std::nullopt;\n    }\n\n    const float inverseDeterminant = 1.0F / determinant;\n    const float inverseLinear[3][3]{\n        {(a11 * a22 - a12 * a21) * inverseDeterminant,\n         (a02 * a21 - a01 * a22) * inverseDeterminant,\n         (a01 * a12 - a02 * a11) * inverseDeterminant},\n        {(a12 * a20 - a10 * a22) * inverseDeterminant,\n         (a00 * a22 - a02 * a20) * inverseDeterminant,\n         (a02 * a10 - a00 * a12) * inverseDeterminant},\n        {(a10 * a21 - a11 * a20) * inverseDeterminant,\n         (a01 * a20 - a00 * a21) * inverseDeterminant,\n         (a00 * a11 - a01 * a10) * inverseDeterminant}};\n\n    Assets::ModelTransform inverse{};\n    for (std::size_t row = 0; row < 3; ++row)\n    {\n        for (std::size_t column = 0; column < 3; ++column)\n        {\n            SetElement(inverse, row, column, inverseLinear[row][column]);\n        }\n    }\n    const float tx = Element(transform, 0, 3);\n    const float ty = Element(transform, 1, 3);\n    const float tz = Element(transform, 2, 3);\n    for (std::size_t row = 0; row < 3; ++row)\n    {\n        SetElement(\n            inverse, row, 3,\n            -(inverseLinear[row][0] * tx + inverseLinear[row][1] * ty + inverseLinear[row][2] * tz));\n    }\n    return inverse;\n}\n}\n''')

replace_once(
    "Engine/Render/ModelDraw.cpp",
    '''std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(\n    const Assets::ModelAsset& model,\n    const Assets::ModelTransform& modelToWorld,\n    const std::span<const ModelNodeTransformOverride> nodeTransformOverrides)\n{\n    std::vector<const Assets::ModelTransform*> overridesByNode(model.nodes.size(), nullptr);\n''',
    '''std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(\n    const Assets::ModelAsset& model,\n    const Assets::ModelTransform& modelToWorld,\n    const std::span<const ModelNodeTransformOverride> nodeTransformOverrides,\n    const std::span<const ModelBindingTransformOverride> bindingTransformOverrides)\n{\n    std::vector<const Assets::ModelTransform*> overridesByNode(model.nodes.size(), nullptr);\n''')

replace_once(
    "Engine/Render/ModelDraw.cpp",
    '''        overridesByNode[overrideValue.nodeIndex] = &overrideValue.nodeLocalPostTransform;\n    }\n\n    std::vector<ModelDrawInstance> draws;\n''',
    '''        overridesByNode[overrideValue.nodeIndex] = &overrideValue.nodeLocalPostTransform;\n    }\n\n    std::vector<std::optional<Assets::ModelTransform>> bindingTransformsByNode(model.nodes.size());\n    for (const ModelBindingTransformOverride& overrideValue : bindingTransformOverrides)\n    {\n        if (overrideValue.bindingIndex >= model.nodeBindings.size())\n        {\n            return std::unexpected("model binding transform override index is out of range");\n        }\n        if (!IsFiniteAffineTransform(overrideValue.bindingLocalPostTransform))\n        {\n            return std::unexpected("model binding transform override must be finite and affine");\n        }\n        const Assets::ModelNodeBindingData& binding = model.nodeBindings[overrideValue.bindingIndex];\n        if (binding.drawableMeshNodeIndices.empty())\n        {\n            return std::unexpected("model binding transform override has no drawable descendants");\n        }\n        const auto inverseRoot = InvertAffineTransform(binding.localToModel);\n        if (!inverseRoot.has_value())\n        {\n            return std::unexpected("model binding transform override root is singular or non-finite");\n        }\n        const Assets::ModelTransform modelSpacePresentation = Multiply(\n            Multiply(binding.localToModel, overrideValue.bindingLocalPostTransform), *inverseRoot);\n        for (const std::size_t meshNodeIndex : binding.drawableMeshNodeIndices)\n        {\n            if (meshNodeIndex >= model.nodes.size())\n            {\n                return std::unexpected("model binding transform override references an invalid drawable node");\n            }\n            if (overridesByNode[meshNodeIndex] != nullptr || bindingTransformsByNode[meshNodeIndex].has_value())\n            {\n                return std::unexpected("model transform overrides overlap on a drawable node");\n            }\n            bindingTransformsByNode[meshNodeIndex] = modelSpacePresentation;\n        }\n    }\n\n    std::vector<ModelDrawInstance> draws;\n''')

replace_once(
    "Engine/Render/ModelDraw.cpp",
    '''        const Assets::MeshNodeData& node = model.nodes[nodeIndex];\n        Assets::ModelTransform combined = Multiply(modelToWorld, node.localToModel);\n        if (overridesByNode[nodeIndex] != nullptr)\n        {\n            // POST-transform is intentional: modelToWorld * authored node transform * presentation transform.\n            combined = Multiply(combined, *overridesByNode[nodeIndex]);\n        }\n''',
    '''        const Assets::MeshNodeData& node = model.nodes[nodeIndex];\n        Assets::ModelTransform combined = bindingTransformsByNode[nodeIndex].has_value()\n            ? Multiply(Multiply(modelToWorld, *bindingTransformsByNode[nodeIndex]), node.localToModel)\n            : Multiply(modelToWorld, node.localToModel);\n        if (overridesByNode[nodeIndex] != nullptr)\n        {\n            // POST-transform is intentional: modelToWorld * authored node transform * presentation transform.\n            combined = Multiply(combined, *overridesByNode[nodeIndex]);\n        }\n''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    std::vector<std::size_t> propellerMeshNodeIndices;\n    propellerMeshNodeIndices.reserve(productionDefinition->propellers.size());\n    for (const Submarine::ProductionPropellerAnchor& propeller : productionDefinition->propellers)\n    {\n        if (propeller.rotationAxis != "+X" ||\n            propeller.presentationNodeBindingIndex >= (*model)->nodeBindings.size())\n        {\n            return std::unexpected("physical playground production propeller semantic binding is invalid");\n        }\n        const auto meshNodeIndex = (*model)->nodeBindings[propeller.presentationNodeBindingIndex].meshNodeIndex;\n        if (!meshNodeIndex.has_value())\n        {\n            return std::unexpected("physical playground production propeller binding is not drawable");\n        }\n        propellerMeshNodeIndices.push_back(*meshNodeIndex);\n    }\n    if (propellerMeshNodeIndices.size() != 2U)\n    {\n        return std::unexpected("physical playground requires two production Antey propeller nodes");\n    }\n''',
    '''    std::vector<std::size_t> propellerNodeBindingIndices;\n    propellerNodeBindingIndices.reserve(productionDefinition->propellers.size());\n    for (const Submarine::ProductionPropellerAnchor& propeller : productionDefinition->propellers)\n    {\n        if (propeller.rotationAxis != "+X" ||\n            propeller.presentationNodeBindingIndex >= (*model)->nodeBindings.size())\n        {\n            return std::unexpected("physical playground production propeller semantic binding is invalid");\n        }\n        const Assets::ModelNodeBindingData& binding =\n            (*model)->nodeBindings[propeller.presentationNodeBindingIndex];\n        if (binding.drawableMeshNodeIndices.empty())\n        {\n            return std::unexpected("physical playground production propeller binding has no drawable subtree");\n        }\n        propellerNodeBindingIndices.push_back(propeller.presentationNodeBindingIndex);\n    }\n    if (propellerNodeBindingIndices.size() != 2U)\n    {\n        return std::unexpected("physical playground requires two production Antey propeller bindings");\n    }\n''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    depthPlaneMeshNodeIndices_ = std::move(depthPlaneMeshNodeIndices);\n    propellerMeshNodeIndices_ = std::move(propellerMeshNodeIndices);\n    facingState_ = {};\n''',
    '''    depthPlaneMeshNodeIndices_ = std::move(depthPlaneMeshNodeIndices);\n    propellerNodeBindingIndices_ = std::move(propellerNodeBindingIndices);\n    facingState_ = {};\n''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    submarineNodeOverrides.reserve(\n        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +\n        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size() + propellerMeshNodeIndices_.size());\n''',
    '''    submarineNodeOverrides.reserve(\n        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +\n        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size());\n''')

replace_once(
    "Game/PhysicalPlayground.cpp",
    '''    const Assets::ModelTransform propellerPostTransform =\n        PropellerPostTransform(propellerPresentationAngleRadians_);\n    for (const std::size_t meshNodeIndex : propellerMeshNodeIndices_)\n    {\n        submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = propellerPostTransform});\n    }\n    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submarineNodeOverrides);\n''',
    '''    const Assets::ModelTransform propellerPostTransform =\n        PropellerPostTransform(propellerPresentationAngleRadians_);\n    std::vector<Render::ModelBindingTransformOverride> submarineBindingOverrides;\n    submarineBindingOverrides.reserve(propellerNodeBindingIndices_.size());\n    for (const std::size_t bindingIndex : propellerNodeBindingIndices_)\n    {\n        submarineBindingOverrides.push_back(\n            {.bindingIndex = bindingIndex, .bindingLocalPostTransform = propellerPostTransform});\n    }\n    const auto draws = Render::PrepareModelDraws(\n        *modelAsset_, modelToWorld, submarineNodeOverrides, submarineBindingOverrides);\n''')

replace_once(
    "Game/PhysicalPlayground.h",
    '''    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};\n    std::vector<std::size_t> propellerMeshNodeIndices_{};\n    Submarine::AnteyFacingState facingState_{};\n''',
    '''    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};\n    std::vector<std::size_t> propellerNodeBindingIndices_{};\n    Submarine::AnteyFacingState facingState_{};\n''')

replace_once(
    "Tests/TestMain.cpp",
    '''    // Detailed geometry and source-first semantic checks live in the focused\n    // IG1-B / IG1-B.1 tests below; this gate only proves staged loading.\n    return true;\n}\n''',
    '''    const auto model = assets.LoadModel(AnteyModelPath);\n    if (!model)\n    {\n        return false;\n    }\n    for (const auto& propeller : definition->propellers)\n    {\n        if (propeller.presentationNodeBindingIndex >= model->Get()->nodeBindings.size())\n        {\n            return false;\n        }\n        const auto& binding = model->Get()->nodeBindings[propeller.presentationNodeBindingIndex];\n        if (binding.drawableMeshNodeIndices.empty() ||\n            std::any_of(\n                binding.drawableMeshNodeIndices.begin(), binding.drawableMeshNodeIndices.end(),\n                [&model](const std::size_t index) { return index >= model->Get()->nodes.size(); }))\n        {\n            return false;\n        }\n    }\n\n    // Detailed geometry and source-first semantic checks live in the focused\n    // IG1-B / IG1-B.1 tests below; this gate also proves opaque transform-only\n    // presentation roots resolve to a non-empty drawable subtree.\n    return true;\n}\n''')

print("M5 production propeller binding repair applied")
