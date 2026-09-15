from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


# Generic bounded local-light payload in the renderer presentation snapshot.
replace_once(
    "Engine/Render/ViewPathFog.h",
    "#include <array>\n#include <expected>\n#include <string>\n",
    "#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <expected>\n#include <string>\n",
)
replace_once(
    "Engine/Render/ViewPathFog.h",
    "namespace DeepRun::Render\n{\n// The complete small, Game-derived presentation snapshot for one frame.",
    "namespace DeepRun::Render\n{\ninline constexpr std::size_t ScenePresentationLocalLightCapacity = 2U;\n\nstruct ScenePresentationLocalLight final\n{\n    std::array<float, 3> worldPositionMeters{};\n    float radiusMeters = 0.0F;\n    std::array<float, 3> linearColor{};\n    float intensity = 0.0F;\n};\n\n// The complete small, Game-derived presentation snapshot for one frame.",
)
replace_once(
    "Engine/Render/ViewPathFog.h",
    "    float lightningFlashIntensity = 0.0F;\n    float lightningViewportX = 0.5F;\n    float lightningPatternOffset = 0.0F;\n};",
    "    float lightningFlashIntensity = 0.0F;\n    float lightningViewportX = 0.5F;\n    float lightningPatternOffset = 0.0F;\n    // Generic bounded transient lights. Game owns their semantic source; Render sees only finite photometric hints.\n    std::array<ScenePresentationLocalLight, ScenePresentationLocalLightCapacity> localLights{};\n    std::uint32_t activeLocalLightCount = 0U;\n};",
)

replace_once(
    "Engine/Render/ViewPathFog.cpp",
    "    if (!normalized(parameters.cloudCoverFraction) ||\n        !normalized(parameters.precipitationFraction) ||\n        !normalized(parameters.sunTransmittance) ||\n        !normalized(parameters.skyLuminanceMultiplier) ||\n        !normalized(parameters.horizonHazeFraction) ||\n        !std::isfinite(parameters.cloudAdvection) ||\n        !normalized(parameters.atmosphereBoundaryViewportY) ||\n        !normalized(parameters.cloudPatternOffset) ||\n        !normalized(parameters.lightningFlashIntensity) ||\n        !normalized(parameters.lightningViewportX) ||\n        !normalized(parameters.lightningPatternOffset))\n        return std::unexpected(\"scene presentation atmosphere controls are invalid\");\n    return {};",
    "    if (!normalized(parameters.cloudCoverFraction) ||\n        !normalized(parameters.precipitationFraction) ||\n        !normalized(parameters.sunTransmittance) ||\n        !normalized(parameters.skyLuminanceMultiplier) ||\n        !normalized(parameters.horizonHazeFraction) ||\n        !std::isfinite(parameters.cloudAdvection) ||\n        !normalized(parameters.atmosphereBoundaryViewportY) ||\n        !normalized(parameters.cloudPatternOffset) ||\n        !normalized(parameters.lightningFlashIntensity) ||\n        !normalized(parameters.lightningViewportX) ||\n        !normalized(parameters.lightningPatternOffset))\n        return std::unexpected(\"scene presentation atmosphere controls are invalid\");\n    if (parameters.activeLocalLightCount > ScenePresentationLocalLightCapacity)\n        return std::unexpected(\"scene presentation local-light count exceeds the bounded capacity\");\n    for (std::size_t index = 0U; index < parameters.activeLocalLightCount; ++index)\n    {\n        const auto& light = parameters.localLights[index];\n        if (!IsFiniteVector(light.worldPositionMeters) || !std::isfinite(light.radiusMeters) ||\n            light.radiusMeters <= 0.0F || light.radiusMeters > 80.0F ||\n            !IsFiniteVector(light.linearColor) || !std::isfinite(light.intensity) ||\n            light.intensity < 0.0F || light.intensity > 12.0F)\n            return std::unexpected(\"scene presentation local light is outside the bounded range\");\n        for (const float channel : light.linearColor)\n            if (channel < 0.0F || channel > 6.0F)\n                return std::unexpected(\"scene presentation local-light color is outside the bounded range\");\n    }\n    return {};",
)

# Extend the existing b1 constant buffer without changing root-signature cost.
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "    float lightningFlashIntensity = 0.0F;\n    float lightningViewportX = 0.5F;\n    float lightningPatternOffset = 0.0F;\n    float padding2 = 0.0F;\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 144U);",
    "    float lightningFlashIntensity = 0.0F;\n    float lightningViewportX = 0.5F;\n    float lightningPatternOffset = 0.0F;\n    float padding2 = 0.0F;\n    std::array<float, 4> localLightPositionRadius0{};\n    std::array<float, 4> localLightColorIntensity0{};\n    std::array<float, 4> localLightPositionRadius1{};\n    std::array<float, 4> localLightColorIntensity1{};\n    std::array<float, 4> localLightControl{};\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 224U);",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    "            .cloudPatternOffset = parameters.cloudPatternOffset,\n            .lightningFlashIntensity = parameters.lightningFlashIntensity,\n            .lightningViewportX = parameters.lightningViewportX,\n            .lightningPatternOffset = parameters.lightningPatternOffset};",
    "            .cloudPatternOffset = parameters.cloudPatternOffset,\n            .lightningFlashIntensity = parameters.lightningFlashIntensity,\n            .lightningViewportX = parameters.lightningViewportX,\n            .lightningPatternOffset = parameters.lightningPatternOffset,\n            .localLightPositionRadius0 = {\n                parameters.localLights[0].worldPositionMeters[0], parameters.localLights[0].worldPositionMeters[1],\n                parameters.localLights[0].worldPositionMeters[2], parameters.localLights[0].radiusMeters},\n            .localLightColorIntensity0 = {\n                parameters.localLights[0].linearColor[0], parameters.localLights[0].linearColor[1],\n                parameters.localLights[0].linearColor[2], parameters.localLights[0].intensity},\n            .localLightPositionRadius1 = {\n                parameters.localLights[1].worldPositionMeters[0], parameters.localLights[1].worldPositionMeters[1],\n                parameters.localLights[1].worldPositionMeters[2], parameters.localLights[1].radiusMeters},\n            .localLightColorIntensity1 = {\n                parameters.localLights[1].linearColor[0], parameters.localLights[1].linearColor[1],\n                parameters.localLights[1].linearColor[2], parameters.localLights[1].intensity},\n            .localLightControl = {static_cast<float>(parameters.activeLocalLightCount), 0.0F, 0.0F, 0.0F}};",
)

replace_once(
    "Shaders/Model.hlsl",
    "    float LightningFlashIntensity;\n    float LightningViewportX;\n    float LightningPatternOffset;\n    float ScenePresentationPadding2;\n};",
    "    float LightningFlashIntensity;\n    float LightningViewportX;\n    float LightningPatternOffset;\n    float ScenePresentationPadding2;\n    float4 LocalLightPositionRadius0;\n    float4 LocalLightColorIntensity0;\n    float4 LocalLightPositionRadius1;\n    float4 LocalLightColorIntensity1;\n    float4 LocalLightControl;\n};",
)
replace_once(
    "Shaders/Model.hlsl",
    "PixelInput VSMain(VertexInput input)\n{\n",
    "float3 EvaluateSceneLocalLight(\n    const float3 worldPosition,\n    const float3 normal,\n    const float4 positionRadius,\n    const float4 colorIntensity)\n{\n    if (positionRadius.w <= 0.0F || colorIntensity.w <= 0.0F)\n        return 0.0F.xxx;\n    const float3 delta = positionRadius.xyz - worldPosition;\n    const float distanceMeters = length(delta);\n    if (distanceMeters <= 1.0e-4F || distanceMeters >= positionRadius.w)\n        return 0.0F.xxx;\n    const float3 directionToLight = delta / distanceMeters;\n    const float radial = saturate(1.0F - distanceMeters / positionRadius.w);\n    const float smoothRadial = radial * radial * (3.0F - 2.0F * radial);\n    const float diffuse = 0.12F + 0.88F * saturate(dot(normal, directionToLight));\n    const bool fragmentBelow = worldPosition.y < SurfaceLevelYMeters;\n    const bool lightBelow = positionRadius.y < SurfaceLevelYMeters;\n    if (fragmentBelow != lightBelow)\n        return 0.0F.xxx;\n    const float3 mediumTransmission = fragmentBelow\n        ? exp(-AttenuationPerMeterRgb * distanceMeters)\n        : 1.0F.xxx;\n    return colorIntensity.rgb * colorIntensity.w * smoothRadial * diffuse * mediumTransmission;\n}\n\nPixelInput VSMain(VertexInput input)\n{\n",
)
replace_once(
    "Shaders/Model.hlsl",
    "    const float3 deepAmbient = BaseColor.rgb * DeepAmbientRgb * (1.0F - transmission);\n    const float3 depthLitColor = surfaceLit * transmission + deepAmbient;",
    "    const float3 deepAmbient = BaseColor.rgb * DeepAmbientRgb * (1.0F - transmission);\n    float3 localLight = 0.0F.xxx;\n    const uint activeLocalLights = min((uint)(LocalLightControl.x + 0.5F), 2U);\n    if (activeLocalLights > 0U)\n        localLight += EvaluateSceneLocalLight(input.worldPosition, normal, LocalLightPositionRadius0, LocalLightColorIntensity0);\n    if (activeLocalLights > 1U)\n        localLight += EvaluateSceneLocalLight(input.worldPosition, normal, LocalLightPositionRadius1, LocalLightColorIntensity1);\n    const float3 depthLitColor = surfaceLit * transmission + deepAmbient + BaseColor.rgb * localLight;",
)

# Game-owned P-700 semantic mapping into generic local lights.
Path("Game/Weapons/P700LaunchLightingPresentation.h").write_text(r'''#pragma once

#include "Engine/Render/ViewPathFog.h"
#include "Simulation/Weapons/P700Granit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Armament
{
[[nodiscard]] inline std::expected<std::vector<Render::ScenePresentationLocalLight>, std::string>
BuildP700LaunchLocalLights(
    const std::span<const Weapons::P700GranitRuntimeState* const> missiles,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        return std::unexpected("P-700 launch-light presentation time must be finite and non-negative");

    struct Candidate final
    {
        Render::ScenePresentationLocalLight light{};
        float priority = 0.0F;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(missiles.size());
    for (const Weapons::P700GranitRuntimeState* missile : missiles)
    {
        if (missile == nullptr)
            continue;
        if (!missile->positionMeters.IsFinite() || !missile->launchForwardUnitVector.IsFinite() ||
            !std::isfinite(missile->phaseStartTimeSeconds) || simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 launch-light presentation observed invalid missile state");
        if (!missile->launchBoosterActive ||
            (missile->phase != Weapons::P700GranitPhase::UnderwaterLaunch &&
             missile->phase != Weapons::P700GranitPhase::WaterExit))
            continue;

        const float length = std::hypot(
            missile->launchForwardUnitVector.x,
            missile->launchForwardUnitVector.y,
            missile->launchForwardUnitVector.z);
        if (!std::isfinite(length) || length <= 1.0e-5F)
            return std::unexpected("P-700 launch-light direction is invalid");
        const float inverseLength = 1.0F / length;
        const float dx = missile->launchForwardUnitVector.x * inverseLength;
        const float dy = missile->launchForwardUnitVector.y * inverseLength;
        const float dz = missile->launchForwardUnitVector.z * inverseLength;
        const float phaseAge = static_cast<float>(simulationTimeSeconds - missile->phaseStartTimeSeconds);
        const float seedPhase = static_cast<float>((missile->terminalRandomSeed & 0xFFFFU) * (6.28318530718 / 65535.0));
        const float restrainedFlicker = 0.92F + 0.08F * std::sin(phaseAge * 37.0F + seedPhase);
        const bool aboveSurface = missile->phase == Weapons::P700GranitPhase::WaterExit;
        const float tailOffsetMeters = aboveSurface ? 5.2F : 4.4F;
        Render::ScenePresentationLocalLight light{
            .worldPositionMeters = {
                missile->positionMeters.x - dx * tailOffsetMeters,
                missile->positionMeters.y - dy * tailOffsetMeters,
                missile->positionMeters.z - dz * tailOffsetMeters},
            .radiusMeters = aboveSurface ? 30.0F : 18.0F,
            .linearColor = aboveSurface
                ? std::array<float, 3>{3.6F, 1.55F, 0.48F}
                : std::array<float, 3>{2.2F, 1.20F, 0.46F},
            .intensity = (aboveSurface ? 3.9F : 2.6F) * restrainedFlicker};
        candidates.push_back({.light = light, .priority = light.intensity * light.radiusMeters});
    }

    std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
        return left.priority > right.priority;
    });
    if (candidates.size() > Render::ScenePresentationLocalLightCapacity)
        candidates.resize(Render::ScenePresentationLocalLightCapacity);
    std::vector<Render::ScenePresentationLocalLight> lights;
    lights.reserve(candidates.size());
    for (const Candidate& candidate : candidates)
        lights.push_back(candidate.light);
    return lights;
}
} // namespace DeepRun::Game::Armament
''', encoding="utf-8")

# Feed the generic lights through the existing PhysicalPlayground frame snapshot.
replace_once(
    "Game/PhysicalPlayground.h",
    "        double simulationTimeSeconds,\n        double presentationTimeSeconds,\n        std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances = {}) const;",
    "        double simulationTimeSeconds,\n        double presentationTimeSeconds,\n        std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances = {},\n        std::span<const Render::ScenePresentationLocalLight> localLights = {}) const;",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    const double simulationTimeSeconds,\n    const double presentationTimeSeconds,\n    const std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances) const",
    "    const double simulationTimeSeconds,\n    const double presentationTimeSeconds,\n    const std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances,\n    const std::span<const Render::ScenePresentationLocalLight> localLights) const",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "    const Render::ScenePresentationParameters scenePresentation{",
    "    Render::ScenePresentationParameters scenePresentation{",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "        .lightningViewportX = thunderstormPresentation.lightningViewportX,\n        .lightningPatternOffset = thunderstormPresentation.lightningPatternOffset};\n    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)",
    "        .lightningViewportX = thunderstormPresentation.lightningViewportX,\n        .lightningPatternOffset = thunderstormPresentation.lightningPatternOffset};\n    if (localLights.size() > Render::ScenePresentationLocalLightCapacity)\n        return std::unexpected(\"physical playground local-light presentation exceeds the bounded capacity\");\n    scenePresentation.activeLocalLightCount = static_cast<std::uint32_t>(localLights.size());\n    for (std::size_t index = 0U; index < localLights.size(); ++index)\n        scenePresentation.localLights[index] = localLights[index];\n    if (const auto configured = renderer.SetScenePresentation(scenePresentation); !configured)",
)

# Expose light composition beside the existing surface disturbance composition.
replace_once(
    "Game/Combat/CombatPlaygroundView.h",
    "#include \"Game/Weapons/P700LaunchVfx.h\"\n#include \"Game/Weapons/P700SurfacePresentation.h\"",
    "#include \"Game/Weapons/P700LaunchVfx.h\"\n#include \"Game/Weapons/P700LaunchLightingPresentation.h\"\n#include \"Game/Weapons/P700SurfacePresentation.h\"",
)
needle = '''    [[nodiscard]] std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>
    BuildP700SurfaceDisturbances(const CombatPlaygroundRuntime& runtime, const double simulationTimeSeconds) const
    {
        std::vector<const Weapons::P700GranitRuntimeState*> missiles;
        missiles.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());
        if (const auto& leader = runtime.PlayerP700(); leader) missiles.push_back(&*leader);
        for (const auto& wingman : runtime.PlayerP700Wingmen()) missiles.push_back(&wingman);
        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles()) missiles.push_back(&ripple);
        return Armament::BuildP700SurfaceDisturbances(missiles, p700VfxSystem_.Tuning(), simulationTimeSeconds);
    }
'''
replacement = needle + '''
    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationLocalLight>, std::string>
    BuildP700LocalLights(const CombatPlaygroundRuntime& runtime, const double simulationTimeSeconds) const
    {
        std::vector<const Weapons::P700GranitRuntimeState*> missiles;
        missiles.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());
        if (const auto& leader = runtime.PlayerP700(); leader) missiles.push_back(&*leader);
        for (const auto& wingman : runtime.PlayerP700Wingmen()) missiles.push_back(&wingman);
        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles()) missiles.push_back(&ripple);
        return Armament::BuildP700LaunchLocalLights(missiles, simulationTimeSeconds);
    }
'''
replace_once("Game/Combat/CombatPlaygroundView.h", needle, replacement)
replace_once(
    "Game/Combat/CombatPlaygroundWindowedComposition.h",
    "    [[nodiscard]] std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>\n    BuildP700SurfaceDisturbances(const double simulationTimeSeconds) const\n    {\n        if (!runtime_.has_value()) return std::vector<Render::GerstnerSurfaceTransientDisturbance>{};\n        return view_.BuildP700SurfaceDisturbances(*runtime_, simulationTimeSeconds);\n    }",
    "    [[nodiscard]] std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>\n    BuildP700SurfaceDisturbances(const double simulationTimeSeconds) const\n    {\n        if (!runtime_.has_value()) return std::vector<Render::GerstnerSurfaceTransientDisturbance>{};\n        return view_.BuildP700SurfaceDisturbances(*runtime_, simulationTimeSeconds);\n    }\n\n    [[nodiscard]] std::expected<std::vector<Render::ScenePresentationLocalLight>, std::string>\n    BuildP700LocalLights(const double simulationTimeSeconds) const\n    {\n        if (!runtime_.has_value()) return std::vector<Render::ScenePresentationLocalLight>{};\n        return view_.BuildP700LocalLights(*runtime_, simulationTimeSeconds);\n    }",
)

# Hero acceptance camera: stage-specific close framing instead of a generic kilometre-wide view.
replace_once(
    "DeepRun/Main.cpp",
    "                        float targetOffsetXMeters = 0.0F;\n                        float spanMeters = 900.0F;\n                        if (combatPlayground->Runtime()->PlayerP700().has_value() && initialOwnshipNavigationPositionMeters.has_value())\n                        {\n                            const auto& missile = *combatPlayground->Runtime()->PlayerP700();\n                            targetOffsetXMeters = missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x;\n                            spanMeters = missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal ? 1'800.0F : 1'200.0F;\n                        }\n                        const auto appliedFraming = playground.SetPresentationCameraFraming(\n                            targetOffsetXMeters, 0.0F, spanMeters, renderer.AspectRatio());",
    "                        float targetOffsetXMeters = 0.0F;\n                        float targetOffsetYMeters = 0.0F;\n                        float spanMeters = 900.0F;\n                        if (initialOwnshipNavigationPositionMeters.has_value())\n                        {\n                            if (combatPlayground->Runtime()->PlayerP700().has_value())\n                            {\n                                const auto& missile = *combatPlayground->Runtime()->PlayerP700();\n                                targetOffsetXMeters = missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x;\n                                switch (missile.phase)\n                                {\n                                case DeepRun::Weapons::P700GranitPhase::HatchOpening:\n                                case DeepRun::Weapons::P700GranitPhase::UnderwaterLaunch:\n                                    spanMeters = 240.0F;\n                                    targetOffsetYMeters = missile.positionMeters.y - initialOwnshipNavigationPositionMeters->y + 6.0F;\n                                    break;\n                                case DeepRun::Weapons::P700GranitPhase::WaterExit:\n                                case DeepRun::Weapons::P700GranitPhase::PostExitTransition:\n                                    spanMeters = 180.0F;\n                                    targetOffsetYMeters = missile.surfaceLevelYMeters - initialOwnshipNavigationPositionMeters->y - 4.0F;\n                                    break;\n                                case DeepRun::Weapons::P700GranitPhase::AirborneDeploying:\n                                    spanMeters = 260.0F;\n                                    targetOffsetYMeters = missile.positionMeters.y - initialOwnshipNavigationPositionMeters->y - 12.0F;\n                                    break;\n                                case DeepRun::Weapons::P700GranitPhase::Terminal:\n                                    spanMeters = 1'800.0F;\n                                    targetOffsetYMeters = missile.positionMeters.y - initialOwnshipNavigationPositionMeters->y;\n                                    break;\n                                default:\n                                    spanMeters = 900.0F;\n                                    targetOffsetYMeters = missile.positionMeters.y - initialOwnshipNavigationPositionMeters->y;\n                                    break;\n                                }\n                            }\n                            else if (combatPlayground->Runtime()->LastExplosion().has_value())\n                            {\n                                const auto& explosion = *combatPlayground->Runtime()->LastExplosion();\n                                targetOffsetXMeters = explosion.positionMeters.x - initialOwnshipNavigationPositionMeters->x;\n                                targetOffsetYMeters = explosion.positionMeters.y - initialOwnshipNavigationPositionMeters->y;\n                                spanMeters = 480.0F;\n                            }\n                        }\n                        const auto appliedFraming = playground.SetPresentationCameraFraming(\n                            targetOffsetXMeters, targetOffsetYMeters, spanMeters, renderer.AspectRatio());",
)

# Build local lights in the same pre-render phase as the surface impulse and pass both into the scene snapshot.
replace_once(
    "DeepRun/Main.cpp",
    "                std::vector<DeepRun::Render::GerstnerSurfaceTransientDisturbance> p700SurfaceDisturbances;\n                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())\n                {\n                    const auto disturbances = combatPlayground->BuildP700SurfaceDisturbances(simulationTimeSeconds);",
    "                std::vector<DeepRun::Render::GerstnerSurfaceTransientDisturbance> p700SurfaceDisturbances;\n                std::vector<DeepRun::Render::ScenePresentationLocalLight> p700LocalLights;\n                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())\n                {\n                    const auto disturbances = combatPlayground->BuildP700SurfaceDisturbances(simulationTimeSeconds);",
)
replace_once(
    "DeepRun/Main.cpp",
    "                    p700SurfaceDisturbances = *disturbances;\n                }\n                const auto rendered = playground.Render(\n                    renderer, simulationTimeSeconds, presentationTimeSeconds,\n                    std::span<const DeepRun::Render::GerstnerSurfaceTransientDisturbance>(\n                        p700SurfaceDisturbances.data(), p700SurfaceDisturbances.size()));",
    "                    p700SurfaceDisturbances = *disturbances;\n                    const auto localLights = combatPlayground->BuildP700LocalLights(simulationTimeSeconds);\n                    if (!localLights)\n                    {\n                        std::cerr << \"[Game][ERROR] P-700 local-light composition failed: \"\n                                  << localLights.error() << '\\n';\n                        return false;\n                    }\n                    p700LocalLights = *localLights;\n                }\n                const auto rendered = playground.Render(\n                    renderer, simulationTimeSeconds, presentationTimeSeconds,\n                    std::span<const DeepRun::Render::GerstnerSurfaceTransientDisturbance>(\n                        p700SurfaceDisturbances.data(), p700SurfaceDisturbances.size()),\n                    std::span<const DeepRun::Render::ScenePresentationLocalLight>(\n                        p700LocalLights.data(), p700LocalLights.size()));",
)

# Acceptance now captures actual underwater launch VFX and permits the intentional hero-scale camera per phase.
replace_once(
    "Game/Combat/P700VisualAcceptance.h",
    "            if ((missile.phase == Weapons::P700GranitPhase::HatchOpening && missile.hatchOpenProgress > 0.0F) ||\n                missile.phase == Weapons::P700GranitPhase::UnderwaterLaunch)\n            {\n                mark(M5P700AcceptanceCheckpoint::Launch);\n            }",
    "            if (missile.phase == Weapons::P700GranitPhase::UnderwaterLaunch)\n            {\n                mark(M5P700AcceptanceCheckpoint::Launch);\n            }",
)
replace_once(
    "Game/Combat/P700VisualAcceptance.h",
    "        if (camera.width < 850.0F || camera.width > 1'850.0F)\n        {\n            return std::unexpected(\"P-700 acceptance camera left the bounded 0.9-1.8 km presentation scale\");\n        }\n\n        for (std::size_t index = 0; index < records_.size(); ++index)",
    "        for (std::size_t index = 0; index < records_.size(); ++index)",
)
replace_once(
    "Game/Combat/P700VisualAcceptance.h",
    "            const auto checkpoint = records_[index]->checkpoint;\n            if (!PresentationMatches(checkpoint, presentation, explosionDrawn))\n                continue;\n\n            if (presentation.playerP700.has_value() && checkpoint != M5P700AcceptanceCheckpoint::Impact &&",
    "            const auto checkpoint = records_[index]->checkpoint;\n            if (!PresentationMatches(checkpoint, presentation, explosionDrawn))\n                continue;\n            const auto cameraRange = [checkpoint]() noexcept -> std::pair<float, float>\n            {\n                switch (checkpoint)\n                {\n                case M5P700AcceptanceCheckpoint::Launch: return {200.0F, 300.0F};\n                case M5P700AcceptanceCheckpoint::WaterExit: return {150.0F, 220.0F};\n                case M5P700AcceptanceCheckpoint::Deploy: return {220.0F, 320.0F};\n                case M5P700AcceptanceCheckpoint::CruiseTerminal: return {850.0F, 1'850.0F};\n                case M5P700AcceptanceCheckpoint::Impact: return {400.0F, 600.0F};\n                }\n                return {0.0F, 0.0F};\n            }();\n            if (camera.width < cameraRange.first || camera.width > cameraRange.second)\n                return std::unexpected(\"P-700 acceptance camera left the phase-specific hero framing range\");\n\n            if (presentation.playerP700.has_value() && checkpoint != M5P700AcceptanceCheckpoint::Impact &&",
)

# Dedicated regression for bounded launch lights.
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    "#include \"Game/Weapons/P700LaunchVfx.h\"\n#include \"Game/Weapons/P700SurfacePresentation.h\"",
    "#include \"Game/Weapons/P700LaunchVfx.h\"\n#include \"Game/Weapons/P700LaunchLightingPresentation.h\"\n#include \"Game/Weapons/P700SurfacePresentation.h\"",
)
insert_before = "[[nodiscard]] bool RunSalvoBudgetCheck(const std::size_t missileCount)\n{"
lighting_check = r'''[[nodiscard]] bool RunLaunchLightingChecks()
{
    auto underwater = MakeMissile(604U, P700GranitPhase::UnderwaterLaunch, 10.0F, -8.0F, 1.0);
    underwater.launchBoosterActive = true;
    auto breach = MakeMissile(605U, P700GranitPhase::WaterExit, 14.0F, 0.5F, 1.0);
    breach.launchBoosterActive = true;
    std::array<const P700GranitRuntimeState*, 2> pair{&underwater, &breach};
    const auto lights = DeepRun::Game::Armament::BuildP700LaunchLocalLights(pair, 1.10);
    if (!lights || lights->size() != 2U ||
        lights->front().radiusMeters <= 0.0F || lights->front().intensity <= 0.0F)
    {
        std::cerr << "P-700 launch lighting did not produce the bounded transient sources\n";
        return false;
    }
    DeepRun::Render::ScenePresentationParameters scene{};
    scene.cameraViewDirection = {0.0F, 0.0F, -1.0F};
    scene.activeLocalLightCount = static_cast<std::uint32_t>(lights->size());
    for (std::size_t index = 0U; index < lights->size(); ++index)
        scene.localLights[index] = (*lights)[index];
    if (!DeepRun::Render::ValidateScenePresentationParameters(scene))
    {
        std::cerr << "P-700 launch lighting produced an invalid generic scene snapshot\n";
        return false;
    }

    std::array<P700GranitRuntimeState, 6> salvo{};
    std::array<const P700GranitRuntimeState*, 6> pointers{};
    for (std::size_t index = 0U; index < salvo.size(); ++index)
    {
        salvo[index] = MakeMissile(700U + index, P700GranitPhase::WaterExit,
                                   static_cast<float>(index) * 2.15F, 0.4F, 1.0);
        salvo[index].launchBoosterActive = true;
        pointers[index] = &salvo[index];
    }
    const auto bounded = DeepRun::Game::Armament::BuildP700LaunchLocalLights(pointers, 1.12);
    if (!bounded || bounded->size() != DeepRun::Render::ScenePresentationLocalLightCapacity)
    {
        std::cerr << "P-700 launch lighting salvo did not enforce the two-light scene bound\n";
        return false;
    }
    return true;
}

'''
replace_once("Tests/P700LaunchVfxTest.cpp", insert_before, lighting_check + insert_before)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    "    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||\n        !RunSalvoBudgetCheck(1U) ||",
    "    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||\n        !RunLaunchLightingChecks() || !RunSalvoBudgetCheck(1U) ||",
)

print("P-700 final visual patch applied")
