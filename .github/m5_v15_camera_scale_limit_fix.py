from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# 1) Camera: runtime pixel-footprint cap + helpers.
path = ROOT / 'Game/Camera/MultiScaleTacticalCamera.h'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    "inline constexpr float MultiScaleMaximumHorizontalSpanMeters = 600'000.0F;\n",
    "// Absolute engineering ceiling. Normal player zoom is further capped at runtime so the production\n"
    "// ownship projects to at least one horizontal pixel in the current render target.\n"
    "inline constexpr float MultiScaleMaximumHorizontalSpanMeters = 600'000.0F;\n"
    "inline constexpr float MultiScaleMinimumOwnshipProjectedPixels = 1.0F;\n",
    'camera constants')
insert_after = '''[[nodiscard]] inline MultiScalePresentationTier PresentationTierForBand(\n    const MultiScaleCameraBand band) noexcept\n{\n    switch (band)\n    {\n    case MultiScaleCameraBand::Detail: return MultiScalePresentationTier::FullDetail;\n    case MultiScaleCameraBand::Local: return MultiScalePresentationTier::FullDetail;\n    case MultiScaleCameraBand::Tactical: return MultiScalePresentationTier::TacticalSimplified;\n    case MultiScaleCameraBand::Operational: return MultiScalePresentationTier::OperationalSymbols;\n    case MultiScaleCameraBand::Strategic: return MultiScalePresentationTier::StrategicSymbols;\n    }\n    return MultiScalePresentationTier::FullDetail;\n}\n'''
helpers = insert_after + '''\n[[nodiscard]] inline std::expected<float, std::string> MaximumHorizontalSpanForProjectedWidth(\n    const float worldWidthMeters,\n    const std::uint32_t viewportWidthPixels,\n    const float minimumProjectedPixels = MultiScaleMinimumOwnshipProjectedPixels)\n{\n    if (!std::isfinite(worldWidthMeters) || !(worldWidthMeters > 0.0F) || viewportWidthPixels == 0U ||\n        !std::isfinite(minimumProjectedPixels) || !(minimumProjectedPixels > 0.0F))\n    {\n        return std::unexpected("multi-scale camera pixel-footprint limit received invalid dimensions");\n    }\n    const float maximumSpanMeters =\n        worldWidthMeters * static_cast<float>(viewportWidthPixels) / minimumProjectedPixels;\n    if (!std::isfinite(maximumSpanMeters) || maximumSpanMeters < MultiScaleMinimumHorizontalSpanMeters)\n    {\n        return std::unexpected("multi-scale camera pixel-footprint limit is below the supported minimum span");\n    }\n    return std::clamp(\n        maximumSpanMeters, MultiScaleMinimumHorizontalSpanMeters, MultiScaleMaximumHorizontalSpanMeters);\n}\n\n[[nodiscard]] inline float ProjectedHorizontalPixels(\n    const float worldWidthMeters,\n    const float horizontalSpanMeters,\n    const std::uint32_t viewportWidthPixels) noexcept\n{\n    if (!std::isfinite(worldWidthMeters) || !(worldWidthMeters > 0.0F) ||\n        !std::isfinite(horizontalSpanMeters) || !(horizontalSpanMeters > 0.0F) || viewportWidthPixels == 0U)\n    {\n        return 0.0F;\n    }\n    return worldWidthMeters / horizontalSpanMeters * static_cast<float>(viewportWidthPixels);\n}\n'''
text = replace_once(text, insert_after, helpers, 'camera helpers')
text = replace_once(
    text,
    '''        if (zoomOctaves != 0.0F)\n        {\n            requestedSpanMeters_ = std::clamp(\n                requestedSpanMeters_ * static_cast<float>(std::exp2(static_cast<double>(zoomOctaves))),\n                MultiScaleMinimumHorizontalSpanMeters,\n                MultiScaleMaximumHorizontalSpanMeters);\n        }\n''',
    '''        if (zoomOctaves != 0.0F)\n        {\n            requestedSpanMeters_ = std::clamp(\n                requestedSpanMeters_ * static_cast<float>(std::exp2(static_cast<double>(zoomOctaves))),\n                MultiScaleMinimumHorizontalSpanMeters,\n                maximumHorizontalSpanMeters_);\n        }\n''',
    'camera zoom clamp')
text = replace_once(
    text,
    '''    [[nodiscard]] std::expected<void, std::string> SetRequestedHorizontalSpanMeters(const float spanMeters)\n    {\n        if (!std::isfinite(spanMeters) || spanMeters < MultiScaleMinimumHorizontalSpanMeters ||\n            spanMeters > MultiScaleMaximumHorizontalSpanMeters)\n        {\n            return std::unexpected("multi-scale camera requested span is outside the supported 80 m..600 km range");\n        }\n        requestedSpanMeters_ = spanMeters;\n        return {};\n    }\n''',
    '''    [[nodiscard]] std::expected<void, std::string> SetRequestedHorizontalSpanMeters(const float spanMeters)\n    {\n        if (!std::isfinite(spanMeters) || spanMeters < MultiScaleMinimumHorizontalSpanMeters ||\n            spanMeters > maximumHorizontalSpanMeters_)\n        {\n            return std::unexpected("multi-scale camera requested span is outside the current visual zoom limit");\n        }\n        requestedSpanMeters_ = spanMeters;\n        return {};\n    }\n\n    [[nodiscard]] std::expected<void, std::string> SetMaximumHorizontalSpanMeters(const float spanMeters)\n    {\n        if (!std::isfinite(spanMeters) || spanMeters < MultiScaleMinimumHorizontalSpanMeters ||\n            spanMeters > MultiScaleMaximumHorizontalSpanMeters)\n        {\n            return std::unexpected("multi-scale camera maximum span is outside the supported engineering range");\n        }\n        maximumHorizontalSpanMeters_ = spanMeters;\n        requestedSpanMeters_ = (std::min)(requestedSpanMeters_, maximumHorizontalSpanMeters_);\n        spanMeters_ = (std::min)(spanMeters_, maximumHorizontalSpanMeters_);\n        ClampHorizontalPan();\n        UpdateBandWithHysteresis();\n        return {};\n    }\n\n    [[nodiscard]] float MaximumHorizontalSpanMeters() const noexcept\n    {\n        return maximumHorizontalSpanMeters_;\n    }\n''',
    'camera runtime maximum')
text = replace_once(
    text,
    '''    float spanMeters_ = MultiScaleInitialHorizontalSpanMeters;\n    float requestedSpanMeters_ = MultiScaleInitialHorizontalSpanMeters;\n    MultiScaleCameraBand band_ = MultiScaleCameraBand::Local;\n''',
    '''    float spanMeters_ = MultiScaleInitialHorizontalSpanMeters;\n    float requestedSpanMeters_ = MultiScaleInitialHorizontalSpanMeters;\n    float maximumHorizontalSpanMeters_ = MultiScaleMaximumHorizontalSpanMeters;\n    MultiScaleCameraBand band_ = MultiScaleCameraBand::Local;\n''',
    'camera member maximum')
# Need cstdint for viewport pixels.
text = replace_once(text, '#include <cmath>\n', '#include <cmath>\n#include <cstdint>\n', 'camera cstdint')
path.write_text(text, encoding='utf-8')

# 2) Physical playground: expose production ownship model length as read-only presentation metadata.
path = ROOT / 'Game/PhysicalPlayground.h'
text = path.read_text(encoding='utf-8')
needle = '''    [[nodiscard]] float PresentationCameraHorizontalSpanMeters() const noexcept\n    {\n        return M2GameplayCameraHorizontalSpanMeters;\n    }\n'''
replacement = needle + '''\n    [[nodiscard]] std::expected<float, std::string> ProductionSubmarinePresentationLengthMeters() const\n    {\n        if (!modelAsset_.IsValid())\n        {\n            return std::unexpected("physical playground production submarine model is unavailable");\n        }\n        const float lengthMeters = modelAsset_->bounds.maximum.x - modelAsset_->bounds.minimum.x;\n        if (!std::isfinite(lengthMeters) || !(lengthMeters > 0.0F))\n        {\n            return std::unexpected("physical playground production submarine length is invalid");\n        }\n        return lengthMeters;\n    }\n'''
text = replace_once(text, needle, replacement, 'ownship presentation length getter')
path.write_text(text, encoding='utf-8')

# 3) Camera HUD data/function in existing UI module (no new build target).
path = ROOT / 'Game/Combat/CombatCommandUi.h'
text = path.read_text(encoding='utf-8')
text = replace_once(text, '#include "Game/Combat/PlayerCombatCommandRuntime.h"\n',
                    '#include "Game/Camera/MultiScaleTacticalCamera.h"\n#include "Game/Combat/PlayerCombatCommandRuntime.h"\n\n#include <cstdint>\n',
                    'ui camera include')
text = replace_once(
    text,
    '''// J2-B shipping-playground presentation only. The UI receives a read-only perceived-world/weapon projection;\n// it cannot mutate TrackManager, weapon state, PhysicsWorld, or issue commands directly.\nvoid DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);\n''',
    '''// J2-B shipping-playground presentation only. The UI receives a read-only perceived-world/weapon projection;\n// it cannot mutate TrackManager, weapon state, PhysicsWorld, or issue commands directly.\nvoid DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);\n\nstruct CameraScaleHudSnapshot final\n{\n    Camera::MultiScaleCameraBand band = Camera::MultiScaleCameraBand::Local;\n    float horizontalSpanMeters = Camera::MultiScaleInitialHorizontalSpanMeters;\n    float maximumHorizontalSpanMeters = Camera::MultiScaleMaximumHorizontalSpanMeters;\n    float ownshipProjectedPixels = 0.0F;\n};\n\nvoid DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);\n''',
    'ui scale snapshot')
path.write_text(text, encoding='utf-8')

path = ROOT / 'Game/Combat/CombatCommandUi.cpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '''const char* CommandName(const PlayerCombatCommandType command) noexcept\n{\n''',
    '''const char* CameraBandName(const Camera::MultiScaleCameraBand band) noexcept\n{\n    switch (band)\n    {\n    case Camera::MultiScaleCameraBand::Detail: return "DETAIL";\n    case Camera::MultiScaleCameraBand::Local: return "LOCAL";\n    case Camera::MultiScaleCameraBand::Tactical: return "TACTICAL";\n    case Camera::MultiScaleCameraBand::Operational: return "OPERATIONAL";\n    case Camera::MultiScaleCameraBand::Strategic: return "STRATEGIC";\n    }\n    return "UNKNOWN";\n}\n\nconst char* CommandName(const PlayerCombatCommandType command) noexcept\n{\n''',
    'ui camera band name')
append = '''\nvoid DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot)\n{\n    const ImGuiViewport* viewport = ImGui::GetMainViewport();\n    if (viewport != nullptr)\n    {\n        constexpr float margin = 16.0F;\n        ImGui::SetNextWindowPos(\n            ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + viewport->WorkSize.y - margin),\n            ImGuiCond_Always,\n            ImVec2(0.0F, 1.0F));\n    }\n    ImGui::SetNextWindowBgAlpha(0.78F);\n    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |\n                                       ImGuiWindowFlags_NoDecoration |\n                                       ImGuiWindowFlags_NoSavedSettings |\n                                       ImGuiWindowFlags_NoNavInputs |\n                                       ImGuiWindowFlags_NoInputs;\n    if (!ImGui::Begin("##CAMERA_SCALE", nullptr, flags))\n    {\n        ImGui::End();\n        return;\n    }\n\n    ImGui::Text("VIEW  %s", CameraBandName(snapshot.band));\n    ImGui::Text("Scale: %.2f km", snapshot.horizontalSpanMeters / 1000.0F);\n    ImGui::Text("Ownship: %.1f px", snapshot.ownshipProjectedPixels);\n    ImGui::Text("Limit: %.2f km", snapshot.maximumHorizontalSpanMeters / 1000.0F);\n    ImGui::End();\n}\n'''
text = replace_once(text, '} // namespace DeepRun::Game::Combat\n', append + '} // namespace DeepRun::Game::Combat\n', 'ui scale function')
path.write_text(text, encoding='utf-8')

# 4) Main: calculate resize-aware one-pixel cap before each player camera update; draw scale HUD.
path = ROOT / 'DeepRun/Main.cpp'
text = path.read_text(encoding='utf-8')
old = '''                    else\n                    {\n                        DeepRun::Game::Camera::MultiScaleCameraInput cameraInput =\n                            inputState != nullptr\n                                ? DeepRun::Game::Camera::MultiScaleCameraInputFromState(*inputState)\n                                : DeepRun::Game::Camera::MultiScaleCameraInput{};\n'''
new = '''                    else\n                    {\n                        const auto ownshipLengthMeters = playground.ProductionSubmarinePresentationLengthMeters();\n                        const std::uint32_t viewportWidthPixels = renderer.MemoryDiagnostics().width;\n                        const auto maximumCameraSpan = ownshipLengthMeters\n                            ? DeepRun::Game::Camera::MaximumHorizontalSpanForProjectedWidth(\n                                  *ownshipLengthMeters, viewportWidthPixels)\n                            : std::expected<float, std::string>{std::unexpected(ownshipLengthMeters.error())};\n                        if (!maximumCameraSpan)\n                        {\n                            std::cerr << "[Game][ERROR] M5 visual zoom limit failed: "\n                                      << maximumCameraSpan.error() << '\\n';\n                            return false;\n                        }\n                        const auto maximumApplied = multiScaleCamera.SetMaximumHorizontalSpanMeters(*maximumCameraSpan);\n                        if (!maximumApplied)\n                        {\n                            std::cerr << "[Game][ERROR] M5 visual zoom cap application failed: "\n                                      << maximumApplied.error() << '\\n';\n                            return false;\n                        }\n\n                        DeepRun::Game::Camera::MultiScaleCameraInput cameraInput =\n                            inputState != nullptr\n                                ? DeepRun::Game::Camera::MultiScaleCameraInputFromState(*inputState)\n                                : DeepRun::Game::Camera::MultiScaleCameraInput{};\n'''
text = replace_once(text, old, new, 'main zoom cap')
old = '''                    if (!options.smokeTest && combatUiSnapshot.has_value())\n                    {\n                        DeepRun::Game::Combat::DrawCombatCommandUi(*combatUiSnapshot);\n                    }\n'''
new = '''                    if (!options.smokeTest && combatUiSnapshot.has_value())\n                    {\n                        DeepRun::Game::Combat::DrawCombatCommandUi(*combatUiSnapshot);\n                        const auto ownshipLengthMeters = playground.ProductionSubmarinePresentationLengthMeters();\n                        const auto framing = multiScaleCamera.Framing();\n                        const std::uint32_t viewportWidthPixels = renderer.MemoryDiagnostics().width;\n                        if (ownshipLengthMeters)\n                        {\n                            DeepRun::Game::Combat::DrawCameraScaleHud({\n                                .band = framing.band,\n                                .horizontalSpanMeters = framing.horizontalSpanMeters,\n                                .maximumHorizontalSpanMeters = multiScaleCamera.MaximumHorizontalSpanMeters(),\n                                .ownshipProjectedPixels = DeepRun::Game::Camera::ProjectedHorizontalPixels(\n                                    *ownshipLengthMeters, framing.horizontalSpanMeters, viewportWidthPixels)});\n                        }\n                    }\n'''
text = replace_once(text, old, new, 'main camera hud')
path.write_text(text, encoding='utf-8')

# 5) Tests: prove one-pixel contract and dynamic resize clamp.
path = ROOT / 'Tests/M5MultiScaleCameraChecks.h'
text = path.read_text(encoding='utf-8')
needle = '''    MultiScaleTacticalCamera camera;\n    const auto initial = camera.Framing();\n'''
replacement = '''    // Visual zoom is resolution-aware: at the maximum span a 154 m production Antey must still occupy\n    // at least one horizontal pixel. Smaller render targets therefore reduce the allowed world span.\n    const auto maxAt1280 = MaximumHorizontalSpanForProjectedWidth(154.0F, 1280U);\n    const auto maxAt640 = MaximumHorizontalSpanForProjectedWidth(154.0F, 640U);\n    if (!maxAt1280 || !maxAt640 || std::abs(*maxAt1280 - 197'120.0F) > 0.01F ||\n        std::abs(*maxAt640 - 98'560.0F) > 0.01F ||\n        std::abs(ProjectedHorizontalPixels(154.0F, *maxAt1280, 1280U) - 1.0F) > 0.0001F ||\n        MaximumHorizontalSpanForProjectedWidth(0.0F, 1280U) ||\n        MaximumHorizontalSpanForProjectedWidth(154.0F, 0U))\n    {\n        return false;\n    }\n\n    MultiScaleTacticalCamera camera;\n    if (!camera.SetMaximumHorizontalSpanMeters(*maxAt1280) ||\n        camera.SetRequestedHorizontalSpanMeters(*maxAt1280 + 1.0F) ||\n        !camera.SetRequestedHorizontalSpanMeters(*maxAt1280))\n    {\n        return false;\n    }\n    for (int frame = 0; frame < 240; ++frame)\n    {\n        const auto capped = camera.Update({}, 1.0 / 60.0);\n        if (!capped)\n        {\n            return false;\n        }\n    }\n    if (camera.Framing().horizontalSpanMeters > *maxAt1280 + 0.01F ||\n        ProjectedHorizontalPixels(154.0F, camera.Framing().horizontalSpanMeters, 1280U) < 0.9999F ||\n        !camera.SetMaximumHorizontalSpanMeters(*maxAt640) ||\n        camera.Framing().horizontalSpanMeters > *maxAt640 + 0.01F ||\n        camera.Framing().requestedHorizontalSpanMeters > *maxAt640 + 0.01F)\n    {\n        return false;\n    }\n\n    // Reset the test camera to the normal initial state after exercising the runtime cap.\n    camera = MultiScaleTacticalCamera{};\n    const auto initial = camera.Framing();\n'''
text = replace_once(text, needle, replacement, 'camera pixel-limit tests')
path.write_text(text, encoding='utf-8')

# 6) Document the player-facing contract.
path = ROOT / 'docs/design/vertical-ocean-gameplay.md'
text = path.read_text(encoding='utf-8')
needle = '''This contract does not start P-700, M6, procedural-world generation, aircraft AI or new simulation scope.\n'''
replacement = '''This contract does not start P-700, M6, procedural-world generation, aircraft AI or new simulation scope.\n\n## Player zoom readability limit\n\nNormal player zoom-out is bounded by presentation readability rather than the old fixed 600 km engineering\nceiling. The production ownship must remain at least one horizontal render-target pixel wide. For an ownship\nworld length `L` and current render-target width `Wpx`, the player-visible maximum horizontal span is:\n\n```text\nmax_span_m = min(engineering_ceiling_m, L_m * Wpx / 1 px)\n```\n\nThe limit is recalculated after resize. It is presentation-only: it does not change world size, simulation,\nsensors or weapon range. The camera HUD exposes current scale in kilometres, the ownship projected pixel\nfootprint and the current resize-aware zoom limit.\n'''
text = replace_once(text, needle, replacement, 'zoom readability docs')
path.write_text(text, encoding='utf-8')
