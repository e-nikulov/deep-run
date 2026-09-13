from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"expected patch anchor not found in {path}: {old[:100]!r}")
    if text.count(old) != 1:
        raise RuntimeError(f"patch anchor is not unique in {path}: {old[:100]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "Engine/Render/ModelDraw.h",
    "#include <expected>\n#include <span>",
    "#include <expected>\n#include <optional>\n#include <span>",
)
replace_once(
    "Engine/Render/ModelDraw.h",
    "struct ModelBindingTransformOverride final\n{\n    std::size_t bindingIndex = 0;\n    Assets::ModelTransform bindingLocalPostTransform{};\n};",
    "struct ModelBindingTransformOverride final\n{\n    std::size_t bindingIndex = 0;\n    Assets::ModelTransform bindingLocalPostTransform{};\n    // Optional model-space articulation pivot. When present, the binding-local linear transform is\n    // re-based around this point instead of the imported semantic root origin. This is presentation-only\n    // and is used by source-authored assemblies whose logical root is not exactly at the mechanical hub.\n    std::optional<Assets::ModelVector3> modelSpacePivot{};\n};",
)
replace_once(
    "Engine/Render/ModelDraw.cpp",
    "        const Assets::ModelTransform modelSpacePresentation = Multiply(\n            Multiply(binding.localToModel, overrideValue.bindingLocalPostTransform), *inverseRoot);\n        for (const std::size_t meshNodeIndex : binding.drawableMeshNodeIndices)",
    "        Assets::ModelTransform modelSpacePresentation = Multiply(\n            Multiply(binding.localToModel, overrideValue.bindingLocalPostTransform), *inverseRoot);\n        if (overrideValue.modelSpacePivot.has_value())\n        {\n            const Assets::ModelVector3& pivot = *overrideValue.modelSpacePivot;\n            if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z))\n            {\n                return std::unexpected(\"model binding transform override pivot must be finite\");\n            }\n            const std::array<float, 3> pivotValues{pivot.x, pivot.y, pivot.z};\n            // Keep the binding-local linear transform, but move its fixed point to the explicit production\n            // articulation pivot. For Q = linear(modelSpacePresentation), translation is P - Q*P.\n            for (std::size_t row = 0; row < 3U; ++row)\n            {\n                float transformedPivot = 0.0F;\n                for (std::size_t column = 0; column < 3U; ++column)\n                {\n                    transformedPivot += Element(modelSpacePresentation, row, column) * pivotValues[column];\n                }\n                SetElement(modelSpacePresentation, row, 3U, pivotValues[row] - transformedPivot);\n            }\n        }\n        for (const std::size_t meshNodeIndex : binding.drawableMeshNodeIndices)",
)

replace_once(
    "Engine/Render/Camera.h",
    "[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    float aspectRatio,\n    float horizontalSpan,\n    const Assets::ModelBounds& depthBounds);",
    "[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    float aspectRatio,\n    float horizontalSpan,\n    const Assets::ModelBounds& depthBounds,\n    float sideYawRadians = 0.0F);",
)
replace_once(
    "Engine/Render/Camera.cpp",
    "std::expected<OrthographicCamera, std::string> BuildOrthographicSideViewCamera(\n    const Assets::ModelVector3& target,\n    const float width,\n    const float height,\n    const Assets::ModelBounds& depthBounds,\n    const float cameraDistance)\n{\n    if (!IsFinite(target) || !IsFinite(depthBounds.minimum) || !IsFinite(depthBounds.maximum) ||\n        !std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F ||\n        depthBounds.maximum.z < depthBounds.minimum.z || !std::isfinite(cameraDistance) ||\n        cameraDistance <= 0.0F)\n    {\n        return std::unexpected(\"side-view camera requires finite target, spans, and depth bounds\");\n    }\n\n    OrthographicCamera camera;\n    camera.target = target;\n    camera.width = width;\n    camera.height = height;\n\n    camera.position = {target.x, target.y, target.z + cameraDistance};\n    const float nearestGeometry = camera.position.z - depthBounds.maximum.z;\n    const float farthestGeometry = camera.position.z - depthBounds.minimum.z;\n    camera.nearPlane = std::max(0.1F, nearestGeometry * 0.5F);\n    camera.farPlane = farthestGeometry + nearestGeometry * 0.5F;\n\n    SetElement(camera.view, 0, 3, -camera.position.x);\n    SetElement(camera.view, 1, 3, -camera.position.y);\n    SetElement(camera.view, 2, 3, -camera.position.z);",
    "std::expected<OrthographicCamera, std::string> BuildOrthographicSideViewCamera(\n    const Assets::ModelVector3& target,\n    const float width,\n    const float height,\n    const Assets::ModelBounds& depthBounds,\n    const float cameraDistance,\n    const float sideYawRadians)\n{\n    if (!IsFinite(target) || !IsFinite(depthBounds.minimum) || !IsFinite(depthBounds.maximum) ||\n        !std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F ||\n        depthBounds.maximum.z < depthBounds.minimum.z || !std::isfinite(cameraDistance) ||\n        cameraDistance <= 0.0F || !std::isfinite(sideYawRadians) || std::abs(sideYawRadians) > 0.5F)\n    {\n        return std::unexpected(\"side-view camera requires finite target, spans, depth bounds, and bounded yaw\");\n    }\n\n    const float sine = std::sin(sideYawRadians);\n    const float cosine = std::cos(sideYawRadians);\n    const Assets::ModelVector3 right{cosine, 0.0F, -sine};\n    const Assets::ModelVector3 backward{sine, 0.0F, cosine};\n\n    OrthographicCamera camera;\n    camera.target = target;\n    camera.width = width;\n    camera.height = height;\n    camera.viewDirection = {-sine, 0.0F, -cosine};\n    camera.position = {\n        target.x + backward.x * cameraDistance,\n        target.y,\n        target.z + backward.z * cameraDistance};\n\n    float nearestGeometry = std::numeric_limits<float>::infinity();\n    float farthestGeometry = 0.0F;\n    for (const float x : {depthBounds.minimum.x, depthBounds.maximum.x})\n    {\n        for (const float y : {depthBounds.minimum.y, depthBounds.maximum.y})\n        {\n            for (const float z : {depthBounds.minimum.z, depthBounds.maximum.z})\n            {\n                const float distance =\n                    (x - camera.position.x) * camera.viewDirection.x +\n                    (y - camera.position.y) * camera.viewDirection.y +\n                    (z - camera.position.z) * camera.viewDirection.z;\n                nearestGeometry = std::min(nearestGeometry, distance);\n                farthestGeometry = std::max(farthestGeometry, distance);\n            }\n        }\n    }\n    if (!std::isfinite(nearestGeometry) || !std::isfinite(farthestGeometry) || nearestGeometry <= 0.0F)\n    {\n        return std::unexpected(\"side-view camera depth bounds are not fully in front of the camera\");\n    }\n    camera.nearPlane = std::max(0.1F, nearestGeometry * 0.5F);\n    camera.farPlane = farthestGeometry + nearestGeometry * 0.5F;\n\n    SetElement(camera.view, 0, 0, right.x);\n    SetElement(camera.view, 0, 2, right.z);\n    SetElement(camera.view, 0, 3, -(right.x * camera.position.x + right.z * camera.position.z));\n    SetElement(camera.view, 1, 3, -camera.position.y);\n    SetElement(camera.view, 2, 0, backward.x);\n    SetElement(camera.view, 2, 2, backward.z);\n    SetElement(camera.view, 2, 3, -(backward.x * camera.position.x + backward.z * camera.position.z));",
)
replace_once(
    "Engine/Render/Camera.cpp",
    "    return BuildOrthographicSideViewCamera(target, width, height, bounds, cameraDistance);",
    "    return BuildOrthographicSideViewCamera(target, width, height, bounds, cameraDistance, 0.0F);",
)
replace_once(
    "Engine/Render/Camera.cpp",
    "std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    const float aspectRatio,\n    const float horizontalSpan,\n    const Assets::ModelBounds& depthBounds)\n{\n    if (!std::isfinite(aspectRatio) || !std::isfinite(horizontalSpan) || aspectRatio <= 0.0F ||\n        horizontalSpan <= 0.0F)\n    {\n        return std::unexpected(\"fixed-world side-view camera requires positive finite aspect and span\");\n    }\n\n    const float verticalSpan = horizontalSpan / aspectRatio;\n    const float depth = std::max(depthBounds.maximum.z - depthBounds.minimum.z, 1.0F);\n    const float frontOffset = std::max(depthBounds.maximum.z - target.z, 0.0F);\n    return BuildOrthographicSideViewCamera(\n        target,\n        horizontalSpan,\n        verticalSpan,\n        depthBounds,\n        frontOffset + depth * 2.0F);\n}",
    "std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    const float aspectRatio,\n    const float horizontalSpan,\n    const Assets::ModelBounds& depthBounds,\n    const float sideYawRadians)\n{\n    if (!std::isfinite(aspectRatio) || !std::isfinite(horizontalSpan) || aspectRatio <= 0.0F ||\n        horizontalSpan <= 0.0F || !std::isfinite(sideYawRadians) || std::abs(sideYawRadians) > 0.5F)\n    {\n        return std::unexpected(\"fixed-world side-view camera requires positive finite aspect/span and bounded yaw\");\n    }\n\n    const float verticalSpan = horizontalSpan / aspectRatio;\n    const float sine = std::sin(sideYawRadians);\n    const float cosine = std::cos(sideYawRadians);\n    float minimumProjectedDepth = std::numeric_limits<float>::infinity();\n    float maximumProjectedDepth = -std::numeric_limits<float>::infinity();\n    for (const float x : {depthBounds.minimum.x, depthBounds.maximum.x})\n    {\n        for (const float z : {depthBounds.minimum.z, depthBounds.maximum.z})\n        {\n            const float projected = sine * (x - target.x) + cosine * (z - target.z);\n            minimumProjectedDepth = std::min(minimumProjectedDepth, projected);\n            maximumProjectedDepth = std::max(maximumProjectedDepth, projected);\n        }\n    }\n    const float projectedDepth = std::max(maximumProjectedDepth - minimumProjectedDepth, 1.0F);\n    const float frontOffset = std::max(maximumProjectedDepth, 0.0F);\n    return BuildOrthographicSideViewCamera(\n        target,\n        horizontalSpan,\n        verticalSpan,\n        depthBounds,\n        frontOffset + projectedDepth * 2.0F,\n        sideYawRadians);\n}",
)
replace_once(
    "Engine/Render/Camera.cpp",
    "#include <algorithm>\n#include <cmath>",
    "#include <algorithm>\n#include <cmath>\n#include <limits>",
)

replace_once(
    "Game/PhysicalPlayground.h",
    "struct VesselPresentationTelemetry final\n{",
    "// Presentation-only side yaw keeps the canonical 2.5D framing while revealing enough depth for\n// twin shafts/propellers to read as volumetric production geometry. Vertical composition is unchanged.\ninline constexpr float M5ProductionCameraDepthCantRadians = 0.139626340F; // 8 degrees\n\nstruct VesselPresentationTelemetry final\n{",
)
replace_once(
    "Game/PhysicalPlayground.h",
    "        return Render::BuildFixedWorldSideViewCamera(\n            target, renderer.AspectRatio(), M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds);",
    "        return Render::BuildFixedWorldSideViewCamera(\n            target, renderer.AspectRatio(), M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds,\n            M5ProductionCameraDepthCantRadians);",
)
replace_once(
    "Game/PhysicalPlayground.h",
    "    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};\n    std::vector<std::size_t> propellerNodeBindingIndices_{};",
    "    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};\n    std::vector<std::pair<std::size_t, Assets::ModelVector3>> propellerPresentationBindings_{};",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    "    std::vector<std::size_t> propellerNodeBindingIndices;\n    propellerNodeBindingIndices.reserve(productionDefinition->propellers.size());",
    "    std::vector<std::pair<std::size_t, Assets::ModelVector3>> propellerPresentationBindings;\n    propellerPresentationBindings.reserve(productionDefinition->propellers.size());",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "        propellerNodeBindingIndices.push_back(propeller.presentationNodeBindingIndex);\n    }\n    if (propellerNodeBindingIndices.size() != 2U)",
    "        if (!std::isfinite(propeller.localOrigin.x) || !std::isfinite(propeller.localOrigin.y) ||\n            !std::isfinite(propeller.localOrigin.z))\n        {\n            return std::unexpected(\"physical playground production propeller hub pivot is non-finite\");\n        }\n        propellerPresentationBindings.emplace_back(propeller.presentationNodeBindingIndex, propeller.localOrigin);\n    }\n    if (propellerPresentationBindings.size() != 2U)",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    propellerNodeBindingIndices_ = std::move(propellerNodeBindingIndices);",
    "    propellerPresentationBindings_ = std::move(propellerPresentationBindings);",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    submarineBindingOverrides.reserve(propellerNodeBindingIndices_.size() + 1U);\n    for (const std::size_t bindingIndex : propellerNodeBindingIndices_)\n    {\n        submarineBindingOverrides.push_back(\n            {.bindingIndex = bindingIndex, .bindingLocalPostTransform = propellerPostTransform});\n    }",
    "    submarineBindingOverrides.reserve(propellerPresentationBindings_.size() + 1U);\n    for (const auto& [bindingIndex, hubPivotModelSpace] : propellerPresentationBindings_)\n    {\n        submarineBindingOverrides.push_back({\n            .bindingIndex = bindingIndex,\n            .bindingLocalPostTransform = propellerPostTransform,\n            .modelSpacePivot = hubPivotModelSpace});\n    }",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    const auto camera = Render::BuildFixedWorldSideViewCamera(\n        target,\n        renderer.AspectRatio(),\n        M2GameplayCameraHorizontalSpanMeters,\n        cameraDepthBounds);",
    "    const auto camera = Render::BuildFixedWorldSideViewCamera(\n        target,\n        renderer.AspectRatio(),\n        M2GameplayCameraHorizontalSpanMeters,\n        cameraDepthBounds,\n        M5ProductionCameraDepthCantRadians);",
)

replace_once(
    "Tests/TestMain.cpp",
    "    const auto model = assets.LoadModel(AnteyModelPath);\n    if (!model)\n    {\n        return false;\n    }\n    for (const auto& propeller : definition->propellers)",
    "    const auto model = assets.LoadModel(AnteyModelPath);\n    if (!model)\n    {\n        return false;\n    }\n    const auto baselineDraws = DeepRun::Render::PrepareModelDraws(*model->Get());\n    if (!baselineDraws)\n    {\n        return false;\n    }\n    DeepRun::Assets::ModelTransform quarterTurn{};\n    quarterTurn.values[5] = 0.0F;\n    quarterTurn.values[6] = 1.0F;\n    quarterTurn.values[9] = -1.0F;\n    quarterTurn.values[10] = 0.0F;\n    const auto transformPoint = [](const DeepRun::Assets::ModelTransform& transform,\n                                   const DeepRun::Assets::ModelVector3& point)\n    {\n        return DeepRun::Assets::ModelVector3{\n            .x = transform.values[0] * point.x + transform.values[4] * point.y +\n                 transform.values[8] * point.z + transform.values[12],\n            .y = transform.values[1] * point.x + transform.values[5] * point.y +\n                 transform.values[9] * point.z + transform.values[13],\n            .z = transform.values[2] * point.x + transform.values[6] * point.y +\n                 transform.values[10] * point.z + transform.values[14]};\n    };\n    const auto distanceSquared = [](const DeepRun::Assets::ModelVector3& left,\n                                    const DeepRun::Assets::ModelVector3& right)\n    {\n        const float dx = left.x - right.x;\n        const float dy = left.y - right.y;\n        const float dz = left.z - right.z;\n        return dx * dx + dy * dy + dz * dz;\n    };\n    for (const auto& propeller : definition->propellers)",
)
replace_once(
    "Tests/TestMain.cpp",
    "        if (binding.drawableMeshNodeIndices.empty() ||\n            std::any_of(\n                binding.drawableMeshNodeIndices.begin(), binding.drawableMeshNodeIndices.end(),\n                [&model](const std::size_t index) { return index >= model->Get()->nodes.size(); }))\n        {\n            return false;\n        }\n    }\n\n    // Detailed geometry and source-first semantic checks live in the focused",
    "        if (binding.drawableMeshNodeIndices.empty() ||\n            std::any_of(\n                binding.drawableMeshNodeIndices.begin(), binding.drawableMeshNodeIndices.end(),\n                [&model](const std::size_t index) { return index >= model->Get()->nodes.size(); }))\n        {\n            return false;\n        }\n        const std::array<DeepRun::Render::ModelBindingTransformOverride, 1> overrides{{\n            {.bindingIndex = propeller.presentationNodeBindingIndex,\n             .bindingLocalPostTransform = quarterTurn,\n             .modelSpacePivot = propeller.localOrigin}}};\n        const auto rotatedDraws = DeepRun::Render::PrepareModelDraws(*model->Get(), {}, {}, overrides);\n        if (!rotatedDraws)\n        {\n            return false;\n        }\n        bool checkedGeometry = false;\n        for (const auto& baselineDraw : *baselineDraws)\n        {\n            if (std::find(binding.drawableMeshNodeIndices.begin(), binding.drawableMeshNodeIndices.end(),\n                          baselineDraw.nodeIndex) == binding.drawableMeshNodeIndices.end())\n            {\n                continue;\n            }\n            const auto rotatedDraw = std::find_if(rotatedDraws->begin(), rotatedDraws->end(), [&](const auto& candidate) {\n                return candidate.nodeIndex == baselineDraw.nodeIndex &&\n                       candidate.primitiveIndex == baselineDraw.primitiveIndex;\n            });\n            if (rotatedDraw == rotatedDraws->end() || baselineDraw.primitiveIndex >= model->Get()->primitives.size())\n            {\n                return false;\n            }\n            const auto& primitive = model->Get()->primitives[baselineDraw.primitiveIndex];\n            for (const auto& vertex : primitive.vertices)\n            {\n                const auto before = transformPoint(baselineDraw.modelToWorld, vertex.position);\n                const auto after = transformPoint(rotatedDraw->modelToWorld, vertex.position);\n                const float beforeRadiusSquared = distanceSquared(before, propeller.localOrigin);\n                const float afterRadiusSquared = distanceSquared(after, propeller.localOrigin);\n                const float tolerance = 2.0e-3F * std::max(1.0F, beforeRadiusSquared);\n                if (!std::isfinite(beforeRadiusSquared) || !std::isfinite(afterRadiusSquared) ||\n                    std::abs(beforeRadiusSquared - afterRadiusSquared) > tolerance)\n                {\n                    return false;\n                }\n                checkedGeometry = true;\n            }\n        }\n        if (!checkedGeometry)\n        {\n            return false;\n        }\n    }\n\n    const auto cantedCamera = DeepRun::Render::BuildFixedWorldSideViewCamera(\n        {}, 16.0F / 9.0F, 600.0F, model->Get()->bounds, 0.139626340F);\n    if (!cantedCamera || std::abs(cantedCamera->viewDirection.x) < 0.10F ||\n        std::abs(cantedCamera->viewDirection.y) > 1.0e-6F || cantedCamera->viewDirection.z > -0.98F)\n    {\n        return false;\n    }\n\n    // Detailed geometry and source-first semantic checks live in the focused",
)

replace_once(
    "docs/design/antey-handling.md",
    "The canonical runtime GLB exposes `SM_Propeller_Port` and `SM_Propeller_Starboard` as transform-only semantic roots. Their visible hub/blade geometry lives in drawable child nodes. Game code therefore retains only the opaque production root binding; the Assets layer resolves its complete drawable subtree and Render applies the signed shaft rotation in root space to every descendant. Child GLB names are not gameplay API, and the hierarchy must not be flattened into hard-coded blade/hub bindings in `PhysicalPlayground`.\n",
    "The canonical runtime GLB exposes `SM_Propeller_Port` and `SM_Propeller_Starboard` as transform-only semantic roots. Their visible hub/blade geometry lives in drawable child nodes. Game code therefore retains only the opaque production root binding; the Assets layer resolves its complete drawable subtree and Render applies the signed shaft rotation to every descendant. Child GLB names are not gameplay API, and the hierarchy must not be flattened into hard-coded blade/hub bindings in `PhysicalPlayground`.\n\nPropeller articulation is explicitly re-based around the geometry-derived production `localOrigin` (hub centre) from the Antey sidecar, not around the imported transform-only root origin. Rotation therefore cannot translate/orbit or visually enlarge the assembly. The normal production camera uses an 8-degree presentation-only side yaw: world Y remains the screen vertical axis, while the small depth cant makes both real twin propellers volumetrically readable without changing simulation, collision, shaft orientation, or the canonical `Antey.glb`.\n",
)

print("Antey propeller runtime presentation patch applied")
