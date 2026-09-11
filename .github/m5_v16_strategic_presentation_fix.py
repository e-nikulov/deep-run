from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# 1) Smooth vertical composition through every scale transition.
path = ROOT / 'Game/Environment/VerticalOceanGameplayContract.h'
text = path.read_text(encoding='utf-8')
old = '''[[nodiscard]] inline float AboveWaterFractionForPresentationSpanMeters(const float horizontalSpanMeters) noexcept
{
    if (!std::isfinite(horizontalSpanMeters) || horizontalSpanMeters <= 0.0F)
    {
        return NormalGameplayAboveWaterFraction;
    }
    if (horizontalSpanMeters <= LocalCompositionReferenceHorizontalSpanMeters)
    {
        return NormalGameplayAboveWaterFraction;
    }
    if (horizontalSpanMeters < TacticalCompositionHorizontalSpanMeters)
    {
        const float transition =
            (horizontalSpanMeters - LocalCompositionReferenceHorizontalSpanMeters) /
            (TacticalCompositionHorizontalSpanMeters - LocalCompositionReferenceHorizontalSpanMeters);
        return NormalGameplayAboveWaterFraction +
            transition * (TacticalGameplayAboveWaterFraction - NormalGameplayAboveWaterFraction);
    }
    if (horizontalSpanMeters < OperationalCompositionHorizontalSpanMeters)
    {
        return TacticalGameplayAboveWaterFraction;
    }
    if (horizontalSpanMeters < StrategicCompositionHorizontalSpanMeters)
    {
        return OperationalGameplayAboveWaterFraction;
    }
    return StrategicGameplayAboveWaterFraction;
}
'''
new = '''[[nodiscard]] inline float AboveWaterFractionForPresentationSpanMeters(const float horizontalSpanMeters) noexcept
{
    if (!std::isfinite(horizontalSpanMeters) || horizontalSpanMeters <= 0.0F)
    {
        return NormalGameplayAboveWaterFraction;
    }
    const auto smoothTransition = [](const float value, const float begin, const float end,
                                     const float beginFraction, const float endFraction) noexcept
    {
        const float linear = std::clamp((value - begin) / (end - begin), 0.0F, 1.0F);
        const float smooth = linear * linear * (3.0F - 2.0F * linear);
        return beginFraction + (endFraction - beginFraction) * smooth;
    };

    if (horizontalSpanMeters <= LocalCompositionReferenceHorizontalSpanMeters)
    {
        return NormalGameplayAboveWaterFraction;
    }
    if (horizontalSpanMeters < TacticalCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            LocalCompositionReferenceHorizontalSpanMeters,
            TacticalCompositionHorizontalSpanMeters,
            NormalGameplayAboveWaterFraction,
            TacticalGameplayAboveWaterFraction);
    }
    if (horizontalSpanMeters < OperationalCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            TacticalCompositionHorizontalSpanMeters,
            OperationalCompositionHorizontalSpanMeters,
            TacticalGameplayAboveWaterFraction,
            OperationalGameplayAboveWaterFraction);
    }
    if (horizontalSpanMeters < StrategicCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            OperationalCompositionHorizontalSpanMeters,
            StrategicCompositionHorizontalSpanMeters,
            OperationalGameplayAboveWaterFraction,
            StrategicGameplayAboveWaterFraction);
    }
    return StrategicGameplayAboveWaterFraction;
}
'''
text = replace_once(text, old, new, 'smooth vertical composition')
path.write_text(text, encoding='utf-8')

# 2) Keep this authored M5 regional bathymetry available across the entire engineering/player zoom range.
path = ROOT / 'Game/Environment/ScalableEnvironmentPresentation.h'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    "inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 120'000.0F;",
    "inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 600'000.0F;",
    'strategic seabed max span')
text = replace_once(
    text,
    '    constexpr int ExtentKilometers = 60;',
    '    constexpr int ExtentKilometers = 300;',
    'strategic profile extent')
path.write_text(text, encoding='utf-8')

# 3) Expose only the perceived selected position to presentation.
path = ROOT / 'Game/Combat/PlayerCombatCommandRuntime.h'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    bool selectedTrackHasEstimatedPosition = false;\n    bool selectedTrackWeaponQualified = false;',
    '    bool selectedTrackHasEstimatedPosition = false;\n    std::optional<Physics::PhysicsVector3> selectedTrackEstimatedPositionMeters{};\n    bool selectedTrackWeaponQualified = false;',
    'perceived track position field')
text = replace_once(
    text,
    '        snapshot.selectedTrackHasEstimatedPosition = selected->estimatedPositionMeters.has_value();\n        snapshot.selectedTrackWeaponQualified = Weapons::ValidateTrackForWeapon(definition_, *selected).has_value();',
    '        snapshot.selectedTrackHasEstimatedPosition = selected->estimatedPositionMeters.has_value();\n        snapshot.selectedTrackEstimatedPositionMeters = selected->estimatedPositionMeters;\n        snapshot.selectedTrackWeaponQualified = Weapons::ValidateTrackForWeapon(definition_, *selected).has_value();',
    'perceived track position assignment')
path.write_text(text, encoding='utf-8')

# 4) Declare operational/strategic symbol overlay.
path = ROOT / 'Game/Combat/CombatCommandUi.h'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '#include "Game/Camera/MultiScaleTacticalCamera.h"\n#include "Game/Combat/PlayerCombatCommandRuntime.h"\n\n#include <cstdint>',
    '#include "Engine/Physics/PhysicsTypes.h"\n#include "Engine/Render/Camera.h"\n#include "Game/Camera/MultiScaleTacticalCamera.h"\n#include "Game/Combat/PlayerCombatCommandRuntime.h"\n\n#include <cstdint>\n#include <optional>',
    'combat ui includes')
text = replace_once(
    text,
    'void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);\n',
    '''void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);\n\nvoid DrawTacticalSituationOverlay(\n    Camera::MultiScaleCameraBand band,\n    const Render::OrthographicCamera& camera,\n    const Physics::PhysicsVector3& ownshipPositionMeters,\n    const std::optional<Physics::PhysicsVector3>& selectedTrackEstimatedPositionMeters,\n    const std::optional<std::uint64_t>& selectedTrackId,\n    const std::optional<Physics::PhysicsVector3>& playerTorpedoPositionMeters);\n''',
    'strategic overlay declaration')
path.write_text(text, encoding='utf-8')

# 5) Implement symbols in ImGui screen space. No hostile truth enters this function.
path = ROOT / 'Game/Combat/CombatCommandUi.cpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '#include <cmath>\n',
    '#include <cmath>\n#include <optional>\n',
    'combat ui optional include')
insert_after = '''const char* CameraBandName(const Camera::MultiScaleCameraBand band) noexcept
{
    switch (band)
    {
    case Camera::MultiScaleCameraBand::Detail: return "DETAIL";
    case Camera::MultiScaleCameraBand::Local: return "LOCAL";
    case Camera::MultiScaleCameraBand::Tactical: return "TACTICAL";
    case Camera::MultiScaleCameraBand::Operational: return "OPERATIONAL";
    case Camera::MultiScaleCameraBand::Strategic: return "STRATEGIC";
    }
    return "UNKNOWN";
}
'''
projection_helpers = insert_after + '''\nstd::optional<ImVec2> ProjectWorldToMainViewport(\n    const Render::OrthographicCamera& camera,\n    const Physics::PhysicsVector3& positionMeters)\n{\n    if (!positionMeters.IsFinite())\n    {\n        return std::nullopt;\n    }\n    const auto clip = Render::TransformPoint(\n        camera.viewProjection,\n        {.x = positionMeters.x, .y = positionMeters.y, .z = positionMeters.z});\n    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) ||\n        std::abs(clip[3]) <= 1.0e-6F)\n    {\n        return std::nullopt;\n    }\n    const float ndcX = clip[0] / clip[3];\n    const float ndcY = clip[1] / clip[3];\n    if (ndcX < -1.05F || ndcX > 1.05F || ndcY < -1.05F || ndcY > 1.05F)\n    {\n        return std::nullopt;\n    }\n    const ImGuiViewport* viewport = ImGui::GetMainViewport();\n    if (viewport == nullptr)\n    {\n        return std::nullopt;\n    }\n    return ImVec2(\n        viewport->WorkPos.x + (ndcX * 0.5F + 0.5F) * viewport->WorkSize.x,\n        viewport->WorkPos.y + (0.5F - ndcY * 0.5F) * viewport->WorkSize.y);\n}\n'''
text = replace_once(text, insert_after, projection_helpers, 'world to viewport helper')
text = replace_once(
    text,
    '    ImGui::End();\n}\n} // namespace DeepRun::Game::Combat\n',
    '''    ImGui::End();\n}\n\nvoid DrawTacticalSituationOverlay(\n    const Camera::MultiScaleCameraBand band,\n    const Render::OrthographicCamera& camera,\n    const Physics::PhysicsVector3& ownshipPositionMeters,\n    const std::optional<Physics::PhysicsVector3>& selectedTrackEstimatedPositionMeters,\n    const std::optional<std::uint64_t>& selectedTrackId,\n    const std::optional<Physics::PhysicsVector3>& playerTorpedoPositionMeters)\n{\n    if (band != Camera::MultiScaleCameraBand::Operational && band != Camera::MultiScaleCameraBand::Strategic)\n    {\n        return;\n    }\n\n    ImDrawList* drawList = ImGui::GetForegroundDrawList();\n    if (drawList == nullptr)\n    {\n        return;\n    }\n    const ImGuiViewport* viewport = ImGui::GetMainViewport();\n    if (viewport == nullptr)\n    {\n        return;\n    }\n\n    const ImU32 ownshipColor = IM_COL32(90, 220, 255, 255);\n    const ImU32 trackColor = IM_COL32(255, 210, 80, 255);\n    const ImU32 torpedoColor = IM_COL32(255, 245, 150, 255);\n    const ImU32 labelColor = IM_COL32(235, 245, 255, 230);\n\n    if (const auto ownship = ProjectWorldToMainViewport(camera, ownshipPositionMeters))\n    {\n        constexpr float radius = 6.0F;\n        drawList->AddTriangleFilled(\n            ImVec2(ownship->x + radius, ownship->y),\n            ImVec2(ownship->x - radius, ownship->y - radius * 0.75F),\n            ImVec2(ownship->x - radius, ownship->y + radius * 0.75F),\n            ownshipColor);\n        drawList->AddText(ImVec2(ownship->x + 9.0F, ownship->y - 7.0F), labelColor, "OWN");\n    }\n\n    if (selectedTrackEstimatedPositionMeters)\n    {\n        if (const auto track = ProjectWorldToMainViewport(camera, *selectedTrackEstimatedPositionMeters))\n        {\n            constexpr float radius = 7.0F;\n            drawList->AddCircle(*track, radius, trackColor, 12, 2.0F);\n            drawList->AddLine(ImVec2(track->x - 9.0F, track->y), ImVec2(track->x + 9.0F, track->y), trackColor, 1.0F);\n            drawList->AddLine(ImVec2(track->x, track->y - 9.0F), ImVec2(track->x, track->y + 9.0F), trackColor, 1.0F);\n            const std::string label = selectedTrackId\n                ? "TRK #" + std::to_string(static_cast<unsigned long long>(*selectedTrackId))\n                : std::string("TRK");\n            drawList->AddText(ImVec2(track->x + 10.0F, track->y - 7.0F), labelColor, label.c_str());\n        }\n    }\n\n    if (playerTorpedoPositionMeters)\n    {\n        if (const auto torpedo = ProjectWorldToMainViewport(camera, *playerTorpedoPositionMeters))\n        {\n            constexpr float radius = 5.0F;\n            const ImVec2 points[4]{\n                ImVec2(torpedo->x, torpedo->y - radius),\n                ImVec2(torpedo->x + radius, torpedo->y),\n                ImVec2(torpedo->x, torpedo->y + radius),\n                ImVec2(torpedo->x - radius, torpedo->y)};\n            drawList->AddConvexPolyFilled(points, 4, torpedoColor);\n            drawList->AddText(ImVec2(torpedo->x + 8.0F, torpedo->y - 7.0F), labelColor, "TORP");\n        }\n    }\n\n    const char* tierLabel = band == Camera::MultiScaleCameraBand::Strategic\n        ? "STRATEGIC SYMBOL VIEW"\n        : "OPERATIONAL SYMBOL VIEW";\n    const ImVec2 textSize = ImGui::CalcTextSize(tierLabel);\n    drawList->AddText(\n        ImVec2(viewport->WorkPos.x + 0.5F * (viewport->WorkSize.x - textSize.x), viewport->WorkPos.y + 18.0F),\n        IM_COL32(220, 235, 245, 210),\n        tierLabel);\n}\n} // namespace DeepRun::Game::Combat\n''',
    'strategic overlay implementation')
path.write_text(text, encoding='utf-8')

# 6) Wire only safe presentation sources: ownship physical self-state, selected perceived Track, own torpedo.
path = ROOT / 'DeepRun/Main.cpp'
text = path.read_text(encoding='utf-8')
old = '''                        if (ownshipLengthMeters)
                        {
                            DeepRun::Game::Combat::DrawCameraScaleHud({
                                .band = framing.band,
                                .horizontalSpanMeters = framing.horizontalSpanMeters,
                                .maximumHorizontalSpanMeters = multiScaleCamera.MaximumHorizontalSpanMeters(),
                                .ownshipProjectedPixels = DeepRun::Game::Camera::ProjectedHorizontalPixels(
                                    *ownshipLengthMeters, framing.horizontalSpanMeters, viewportWidthPixels)});
                        }
'''
new = '''                        if (ownshipLengthMeters)
                        {
                            DeepRun::Game::Combat::DrawCameraScaleHud({
                                .band = framing.band,
                                .horizontalSpanMeters = framing.horizontalSpanMeters,
                                .maximumHorizontalSpanMeters = multiScaleCamera.MaximumHorizontalSpanMeters(),
                                .ownshipProjectedPixels = DeepRun::Game::Camera::ProjectedHorizontalPixels(
                                    *ownshipLengthMeters, framing.horizontalSpanMeters, viewportWidthPixels)});
                        }

                        if (framing.band == DeepRun::Game::Camera::MultiScaleCameraBand::Operational ||
                            framing.band == DeepRun::Game::Camera::MultiScaleCameraBand::Strategic)
                        {
                            const auto ownship = playground.BuildPhysicalCollisionProxySnapshot();
                            if (!ownship)
                            {
                                std::cerr << "[Game][ERROR] M5 symbol-view ownship snapshot failed: "
                                          << ownship.error() << '\\n';
                                return false;
                            }
                            std::optional<DeepRun::Physics::PhysicsVector3> playerTorpedoPosition{};
                            if (combatRendered->presentation.playerTorpedo)
                            {
                                playerTorpedoPosition = combatRendered->presentation.playerTorpedo->positionMeters;
                            }
                            DeepRun::Game::Combat::DrawTacticalSituationOverlay(
                                framing.band,
                                *camera,
                                ownship->positionMeters,
                                combatUiSnapshot->selectedTrackEstimatedPositionMeters,
                                combatUiSnapshot->selectedTrackId,
                                playerTorpedoPosition);
                        }
'''
text = replace_once(text, old, new, 'main strategic overlay wiring')
path.write_text(text, encoding='utf-8')

# 7) Update regression expectations for smooth composition and wider authored presentation profile.
path = ROOT / 'Tests/M5ScalableEnvironmentPresentationChecks.h'
text = path.read_text(encoding='utf-8')
old = '''    if (std::abs(localSky - 0.15F) > 0.0001F ||
        !(transitionSky > localSky && transitionSky < tacticalSky) ||
        std::abs(tacticalSky - 0.32F) > 0.0001F ||
        std::abs(operationalSky - 0.36F) > 0.0001F ||
        std::abs(strategicSky - 0.40F) > 0.0001F)
    {
        return false;
    }
'''
new = '''    const float beforeTacticalBoundary = AboveWaterFractionForPresentationSpanMeters(2'299.0F);
    const float afterTacticalBoundary = AboveWaterFractionForPresentationSpanMeters(2'301.0F);
    const float beforeOperationalBoundary = AboveWaterFractionForPresentationSpanMeters(8'999.0F);
    const float afterOperationalBoundary = AboveWaterFractionForPresentationSpanMeters(9'001.0F);
    const float beforeStrategicBoundary = AboveWaterFractionForPresentationSpanMeters(119'999.0F);
    const float afterStrategicBoundary = AboveWaterFractionForPresentationSpanMeters(120'001.0F);
    if (std::abs(localSky - 0.15F) > 0.0001F ||
        !(transitionSky > localSky && transitionSky < tacticalSky) ||
        !(tacticalSky > TacticalGameplayAboveWaterFraction && tacticalSky < OperationalGameplayAboveWaterFraction) ||
        !(operationalSky > OperationalGameplayAboveWaterFraction && operationalSky < StrategicGameplayAboveWaterFraction) ||
        std::abs(strategicSky - StrategicGameplayAboveWaterFraction) > 0.0001F ||
        std::abs(afterTacticalBoundary - beforeTacticalBoundary) > 0.001F ||
        std::abs(afterOperationalBoundary - beforeOperationalBoundary) > 0.001F ||
        std::abs(afterStrategicBoundary - beforeStrategicBoundary) > 0.001F)
    {
        return false;
    }
'''
text = replace_once(text, old, new, 'smooth composition regression')
text = replace_once(
    text,
    "        tacticalProfile.front().xMeters > -59'000.0F || tacticalProfile.back().xMeters < 59'000.0F ||",
    "        tacticalProfile.front().xMeters > -299'000.0F || tacticalProfile.back().xMeters < 299'000.0F ||",
    'profile extent regression')
path.write_text(text, encoding='utf-8')

print('M5 V1.6 strategic presentation repair applied')
