from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# The previously validated local-light patch predates the restored audio regression call.
# Normalize the test temporarily, apply the patch, then restore the full regression chain explicitly.
test_path = "Tests/P700LaunchVfxTest.cpp"
test = read(test_path)
test = test.replace('#include "Game/Weapons/P700LaunchAudioPresentation.h"\n', '')
test = test.replace('!RunLifecycleChecks() || !RunAudioMappingChecks() ||\n        !RunSurfaceDisturbanceChecks()',
                    '!RunLifecycleChecks() || !RunSurfaceDisturbanceChecks()')
write(test_path, test)
exec(compile(read("Tools/p700_final_visual_patch.py"), "p700_final_visual_patch", "exec"))

test = read(test_path)
needle = '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Game/Weapons/P700LaunchLightingPresentation.h"\n'
replacement = '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Game/Weapons/P700LaunchAudioPresentation.h"\n#include "Game/Weapons/P700LaunchLightingPresentation.h"\n'
if needle not in test:
    raise SystemExit("post-lighting test include anchor missing")
test = test.replace(needle, replacement, 1)
test = test.replace('!RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||\n        !RunLaunchLightingChecks()',
                    '!RunLifecycleChecks() || !RunAudioMappingChecks() ||\n        !RunSurfaceDisturbanceChecks() || !RunLaunchLightingChecks()', 1)
write(test_path, test)

# A generic arbitrary orthographic look-at camera lets Game consume the already-authored showcase camera poses.
replace_once(
    "Engine/Render/Camera.h",
    "[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    float aspectRatio,\n    float horizontalSpan,\n    const Assets::ModelBounds& depthBounds,\n    float sideYawRadians = 0.0F);",
    "[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildFixedWorldSideViewCamera(\n    const Assets::ModelVector3& target,\n    float aspectRatio,\n    float horizontalSpan,\n    const Assets::ModelBounds& depthBounds,\n    float sideYawRadians = 0.0F);\n[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildFixedWorldOrthographicCamera(\n    const Assets::ModelVector3& position,\n    const Assets::ModelVector3& target,\n    const Assets::ModelVector3& up,\n    float aspectRatio,\n    float horizontalSpan,\n    const Assets::ModelBounds& depthBounds);"
)

insert_anchor = "\nbool BoundsFitInCamera(\n"
generic_camera = r'''
std::expected<OrthographicCamera, std::string> BuildFixedWorldOrthographicCamera(
    const Assets::ModelVector3& position,
    const Assets::ModelVector3& target,
    const Assets::ModelVector3& up,
    const float aspectRatio,
    const float horizontalSpan,
    const Assets::ModelBounds& depthBounds)
{
    if (!IsFinite(position) || !IsFinite(target) || !IsFinite(up) || !IsFinite(depthBounds.minimum) ||
        !IsFinite(depthBounds.maximum) || !std::isfinite(aspectRatio) || !std::isfinite(horizontalSpan) ||
        aspectRatio <= 0.0F || horizontalSpan <= 0.0F)
        return std::unexpected("orthographic camera requires finite pose, bounds and positive aspect/span");

    const auto subtract = [](const Assets::ModelVector3& a, const Assets::ModelVector3& b) noexcept {
        return Assets::ModelVector3{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    const auto length = [](const Assets::ModelVector3& v) noexcept {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    };
    const auto normalized = [&length](const Assets::ModelVector3& v) -> std::optional<Assets::ModelVector3> {
        const float l = length(v);
        if (!std::isfinite(l) || l <= 1.0e-5F) return std::nullopt;
        return Assets::ModelVector3{v.x / l, v.y / l, v.z / l};
    };
    const auto cross = [](const Assets::ModelVector3& a, const Assets::ModelVector3& b) noexcept {
        return Assets::ModelVector3{a.y * b.z - a.z * b.y,
                                    a.z * b.x - a.x * b.z,
                                    a.x * b.y - a.y * b.x};
    };
    const auto dot = [](const Assets::ModelVector3& a, const Assets::ModelVector3& b) noexcept {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };

    const auto forward = normalized(subtract(target, position));
    const auto upNormalized = normalized(up);
    if (!forward || !upNormalized)
        return std::unexpected("orthographic camera pose has a degenerate direction");
    const Assets::ModelVector3 backward{-forward->x, -forward->y, -forward->z};
    const auto right = normalized(cross(*upNormalized, backward));
    if (!right)
        return std::unexpected("orthographic camera up vector is parallel to its view direction");
    const auto correctedUp = normalized(cross(backward, *right));
    if (!correctedUp)
        return std::unexpected("orthographic camera basis construction failed");

    OrthographicCamera camera;
    camera.position = position;
    camera.target = target;
    camera.up = *correctedUp;
    camera.viewDirection = *forward;
    camera.width = horizontalSpan;
    camera.height = horizontalSpan / aspectRatio;

    float farthestPositive = 0.0F;
    for (const float x : {depthBounds.minimum.x, depthBounds.maximum.x})
        for (const float y : {depthBounds.minimum.y, depthBounds.maximum.y})
            for (const float z : {depthBounds.minimum.z, depthBounds.maximum.z})
            {
                const Assets::ModelVector3 delta{x - position.x, y - position.y, z - position.z};
                farthestPositive = std::max(farthestPositive, dot(delta, *forward));
            }
    const float targetDistance = length(subtract(target, position));
    camera.nearPlane = 0.10F;
    camera.farPlane = std::max({targetDistance * 4.0F, farthestPositive + targetDistance, 250.0F});

    camera.view.values.fill(0.0F);
    SetElement(camera.view, 0, 0, right->x); SetElement(camera.view, 0, 1, right->y); SetElement(camera.view, 0, 2, right->z);
    SetElement(camera.view, 0, 3, -dot(*right, position));
    SetElement(camera.view, 1, 0, correctedUp->x); SetElement(camera.view, 1, 1, correctedUp->y); SetElement(camera.view, 1, 2, correctedUp->z);
    SetElement(camera.view, 1, 3, -dot(*correctedUp, position));
    SetElement(camera.view, 2, 0, backward.x); SetElement(camera.view, 2, 1, backward.y); SetElement(camera.view, 2, 2, backward.z);
    SetElement(camera.view, 2, 3, -dot(backward, position));
    SetElement(camera.view, 3, 3, 1.0F);

    camera.projection.values.fill(0.0F);
    SetElement(camera.projection, 0, 0, 2.0F / camera.width);
    SetElement(camera.projection, 1, 1, 2.0F / camera.height);
    SetElement(camera.projection, 2, 2, 1.0F / (camera.nearPlane - camera.farPlane));
    SetElement(camera.projection, 2, 3, camera.nearPlane / (camera.nearPlane - camera.farPlane));
    SetElement(camera.projection, 3, 3, 1.0F);
    camera.viewProjection = Multiply(camera.projection, camera.view);
    if (!IsFinite(camera.view) || !IsFinite(camera.projection) || !IsFinite(camera.viewProjection))
        return std::unexpected("orthographic camera produced invalid matrices");
    return camera;
}
'''
cam_cpp = read("Engine/Render/Camera.cpp")
if insert_anchor not in cam_cpp:
    raise SystemExit("Camera.cpp insertion anchor missing")
cam_cpp = cam_cpp.replace(insert_anchor, "\n" + generic_camera + insert_anchor, 1)
if "#include <optional>" not in cam_cpp:
    cam_cpp = cam_cpp.replace("#include <limits>\n", "#include <limits>\n#include <optional>\n", 1)
write("Engine/Render/Camera.cpp", cam_cpp)

# Game-owned launch/weather adapter from the exact authoritative WeatherStateConfig.
Path("Game/Weapons/P700LaunchWeatherPresentation.h").write_text(r'''#pragma once

#include "Game/Weapons/P700LaunchVfx.h"
#include "Simulation/Environment/WeatherSeaState.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Armament
{
[[nodiscard]] inline std::expected<P700LaunchVfxEnvironment, std::string>
BuildP700LaunchVfxEnvironmentFromWeather(
    const Environment::WeatherStateConfig& weather,
    const float daylightFraction = 1.0F) noexcept
{
    if (!std::isfinite(daylightFraction) || daylightFraction < 0.0F || daylightFraction > 1.0F ||
        !std::isfinite(weather.windSpeedMetersPerSecond) || weather.windSpeedMetersPerSecond < 0.0F ||
        !std::isfinite(weather.windDirectionDegrees) || !std::isfinite(weather.rainRateMillimetersPerHour) ||
        weather.rainRateMillimetersPerHour < 0.0F || !std::isfinite(weather.cloudCoverFraction) ||
        weather.cloudCoverFraction < 0.0F || weather.cloudCoverFraction > 1.0F)
        return std::unexpected("P-700 weather presentation input is invalid");

    constexpr float Pi = 3.14159265358979323846F;
    const float rainFraction = std::clamp(weather.rainRateMillimetersPerHour / 50.0F, 0.0F, 1.0F);
    const float humidity = std::clamp(0.52F + 0.33F * weather.cloudCoverFraction + 0.15F * rainFraction,
                                      0.45F, 0.99F);
    return P700LaunchVfxEnvironment{
        .significantWaveHeightMeters = std::hypot(
            weather.windSea.significantWaveHeightMeters, weather.swell.significantWaveHeightMeters),
        .windSpeedMetersPerSecond = weather.windSpeedMetersPerSecond,
        .windDirectionRadians = weather.windDirectionDegrees * Pi / 180.0F,
        .rainRateMillimetersPerHour = weather.rainRateMillimetersPerHour,
        .humidityFraction = humidity,
        .daylightFraction = daylightFraction,
        .lightningVisible = weather.lightningRatePerMinute > 0.0F};
}
} // namespace DeepRun::Game::Armament
''', encoding="utf-8")

# Low-frequency presentation-only impulse player. It never changes simulation or weapon state.
Path("Game/Combat/P700LaunchCameraPresentation.h").write_text(r'''#pragma once

#include "Game/Weapons/P700LaunchVfx.h"

#include <array>
#include <cmath>

namespace DeepRun::Game::Combat
{
class P700LaunchCameraImpulsePlayer final
{
public:
    void Trigger(const Armament::P700LaunchCameraImpulse& impulse, const double presentationTimeSeconds) noexcept
    {
        if (!std::isfinite(presentationTimeSeconds) || !std::isfinite(impulse.amplitude) ||
            !std::isfinite(impulse.frequencyHz) || !std::isfinite(impulse.durationSeconds) ||
            impulse.amplitude <= 0.0F || impulse.frequencyHz <= 0.0F || impulse.durationSeconds <= 0.0F)
            return;
        impulse_ = impulse;
        startTimeSeconds_ = presentationTimeSeconds;
        active_ = true;
    }

    [[nodiscard]] std::array<float, 2> Evaluate(const double presentationTimeSeconds) noexcept
    {
        if (!active_ || !std::isfinite(presentationTimeSeconds) || presentationTimeSeconds < startTimeSeconds_)
            return {};
        const double age = presentationTimeSeconds - startTimeSeconds_;
        if (age >= impulse_.durationSeconds)
        {
            active_ = false;
            return {};
        }
        const float normalized = static_cast<float>(age / impulse_.durationSeconds);
        const float envelope = (1.0F - normalized) * (1.0F - normalized);
        constexpr float Tau = 6.28318530718F;
        const float phase = Tau * impulse_.frequencyHz * static_cast<float>(age);
        const float vertical = impulse_.amplitude * envelope * std::sin(phase);
        const float horizontal = impulse_.amplitude * 0.36F * envelope * std::sin(phase * 0.73F + 1.1F);
        return {horizontal, vertical};
    }

private:
    Armament::P700LaunchCameraImpulse impulse_{};
    double startTimeSeconds_ = 0.0;
    bool active_ = false;
};
} // namespace DeepRun::Game::Combat
''', encoding="utf-8")

# PhysicalPlayground exposes a value-copy weather snapshot and a presentation-only cinematic camera/daylight override.
header = read("Game/PhysicalPlayground.h")
weather_marker = "    // M5-I.2 live hazard bridge."
weather_block = r'''    [[nodiscard]] std::expected<Environment::WeatherStateConfig, std::string> BuildWeatherStateConfigSnapshot() const
    {
        if (!weather_.has_value())
            return std::unexpected("physical playground weather authority is unavailable");
        return weather_->Config();
    }

    [[nodiscard]] std::expected<void, std::string> SetPresentationDaylightFraction(const float fraction)
    {
        if (!std::isfinite(fraction) || fraction < 0.0F || fraction > 1.0F)
            return std::unexpected("presentation daylight fraction must be finite in [0,1]");
        presentationDaylightFraction_ = fraction;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> SetPresentationCameraRelativePosition(
        const std::optional<Assets::ModelVector3>& relativePositionMeters)
    {
        if (relativePositionMeters.has_value() &&
            (!std::isfinite(relativePositionMeters->x) || !std::isfinite(relativePositionMeters->y) ||
             !std::isfinite(relativePositionMeters->z)))
            return std::unexpected("presentation camera relative position must be finite");
        presentationCameraRelativePositionMeters_ = relativePositionMeters;
        return {};
    }

'''
if weather_marker not in header:
    raise SystemExit("PhysicalPlayground weather insertion marker missing")
header = header.replace(weather_marker, weather_block + weather_marker, 1)
member_marker = "    std::optional<Environment::WeatherState> weather_;"
member_add = member_marker + "\n    float presentationDaylightFraction_ = 1.0F;\n    std::optional<Assets::ModelVector3> presentationCameraRelativePositionMeters_{};"
if header.count(member_marker) != 1:
    raise SystemExit("PhysicalPlayground weather member marker missing")
header = header.replace(member_marker, member_add, 1)
write("Game/PhysicalPlayground.h", header)

# Use the cinematic camera override consistently in both the read-only camera bridge and actual scene render.
for path in ["Game/PhysicalPlayground.h", "Game/PhysicalPlayground.cpp"]:
    text = read(path)
    old = "return Render::BuildFixedWorldSideViewCamera(\n            target, renderer.AspectRatio(), M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds,\n            M5ProductionCameraDepthCantRadians);"
    if path.endswith(".h"):
        if old not in text:
            raise SystemExit("BuildPresentationCamera side-view return anchor missing")
        new = "if (presentationCameraRelativePositionMeters_.has_value())\n        {\n            const Assets::ModelVector3 position{\n                target.x + presentationCameraRelativePositionMeters_->x,\n                target.y + presentationCameraRelativePositionMeters_->y,\n                target.z + presentationCameraRelativePositionMeters_->z};\n            return Render::BuildFixedWorldOrthographicCamera(\n                position, target, {0.0F, 1.0F, 0.0F}, renderer.AspectRatio(),\n                M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds);\n        }\n        " + old
        text = text.replace(old, new, 1)
    else:
        old2 = "const auto camera = Render::BuildFixedWorldSideViewCamera(\n        target,\n        renderer.AspectRatio(),\n        M2GameplayCameraHorizontalSpanMeters,\n        cameraDepthBounds,\n        M5ProductionCameraDepthCantRadians);"
        if old2 not in text:
            raise SystemExit("PhysicalPlayground::Render camera anchor missing")
        new2 = "const auto camera = presentationCameraRelativePositionMeters_.has_value()\n        ? Render::BuildFixedWorldOrthographicCamera(\n            Assets::ModelVector3{target.x + presentationCameraRelativePositionMeters_->x,\n                                 target.y + presentationCameraRelativePositionMeters_->y,\n                                 target.z + presentationCameraRelativePositionMeters_->z},\n            target, {0.0F, 1.0F, 0.0F}, renderer.AspectRatio(), M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds)\n        : Render::BuildFixedWorldSideViewCamera(\n            target, renderer.AspectRatio(), M2GameplayCameraHorizontalSpanMeters, cameraDepthBounds,\n            M5ProductionCameraDepthCantRadians);"
        text = text.replace(old2, new2, 1)
    write(path, text)

# Daylight affects the existing W1 sky/atmosphere presentation only; gameplay weather remains untouched.
cpp = read("Game/PhysicalPlayground.cpp")
old_weather = "        weatherPresentation = EvaluateWeatherPresentation(*weather_);\n        if (!ValidWeatherPresentationParameters(weatherPresentation))"
new_weather = "        weatherPresentation = EvaluateWeatherPresentation(*weather_);\n        weatherPresentation.sunTransmittance *= presentationDaylightFraction_;\n        weatherPresentation.skyLuminanceMultiplier *= std::max(0.035F, presentationDaylightFraction_);\n        for (float& channel : weatherPresentation.atmosphereFogColorRgb)\n            channel *= 0.08F + 0.92F * presentationDaylightFraction_;\n        if (!ValidWeatherPresentationParameters(weatherPresentation))"
if old_weather not in cpp:
    raise SystemExit("weather presentation daylight anchor missing")
cpp = cpp.replace(old_weather, new_weather, 1)
# Modulate the clear-based sky/sun used before tone mapping.
old_band = "const auto cleared = renderer.ClearViewportRect(band.viewport, band.color);"
new_band = "auto skyColor = band.color;\n            for (std::size_t channel = 0U; channel < 3U; ++channel)\n                skyColor[channel] *= 0.035F + 0.965F * presentationDaylightFraction_;\n            const auto cleared = renderer.ClearViewportRect(band.viewport, skyColor);"
if old_band not in cpp:
    raise SystemExit("sky band clear anchor missing")
cpp = cpp.replace(old_band, new_band, 1)
old_sun = "const auto cleared = renderer.ClearViewportRect(strip.viewport, strip.color);"
new_sun = "auto sunColor = strip.color;\n            for (std::size_t channel = 0U; channel < 3U; ++channel)\n                sunColor[channel] *= presentationDaylightFraction_;\n            const auto cleared = renderer.ClearViewportRect(strip.viewport, sunColor);"
if old_sun not in cpp:
    raise SystemExit("sun strip clear anchor missing")
cpp = cpp.replace(old_sun, new_sun, 1)
write("Game/PhysicalPlayground.cpp", cpp)

# Make the existing showcase presets executable rather than documentation-only.
show = read("Game/Combat/P700VfxShowcase.h")
show = show.replace('#include "Game/Weapons/P700LaunchVfx.h"\n',
                    '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Simulation/Weapons/P700Granit.h"\n', 1)
controller = r'''
struct P700VfxShowcaseCameraSelection final
{
    const P700VfxShowcaseCameraPreset* preset = nullptr;
    std::array<float, 3> relativePositionMeters{};
    std::array<float, 3> targetOffsetMeters{};
    float horizontalSpanMeters = 220.0F;
};

[[nodiscard]] inline P700VfxShowcaseCameraSelection EvaluateP700VfxShowcaseCamera(
    const Weapons::P700GranitRuntimeState& missile,
    const double simulationTimeSeconds) noexcept
{
    const float age = std::isfinite(simulationTimeSeconds) && simulationTimeSeconds >= missile.phaseStartTimeSeconds
        ? static_cast<float>(simulationTimeSeconds - missile.phaseStartTimeSeconds) : 0.0F;
    std::size_t index = 0U;
    switch (missile.phase)
    {
    case Weapons::P700GranitPhase::HatchOpening: index = 0U; break;
    case Weapons::P700GranitPhase::UnderwaterLaunch: index = age < 0.55F ? 0U : 1U; break;
    case Weapons::P700GranitPhase::WaterExit: index = age < 0.32F ? 2U : 3U; break;
    case Weapons::P700GranitPhase::PostExitTransition: index = 3U; break;
    case Weapons::P700GranitPhase::AirborneDeploying: index = 4U; break;
    case Weapons::P700GranitPhase::Cruise:
    case Weapons::P700GranitPhase::Terminal: index = 5U; break;
    default: index = 0U; break;
    }
    const auto& preset = P700VfxShowcaseCameras[index];
    return P700VfxShowcaseCameraSelection{
        .preset = &preset,
        .relativePositionMeters = preset.relativePositionMeters,
        .targetOffsetMeters = preset.targetOffsetMeters,
        .horizontalSpanMeters = preset.horizontalSpanMeters};
}
'''
show_anchor = "// Visual-only replay guidance."
if show_anchor not in show:
    raise SystemExit("showcase controller insertion anchor missing")
show = show.replace(show_anchor, controller + "\n" + show_anchor, 1)
if "#include <cmath>" not in show:
    show = show.replace("#include <array>\n", "#include <array>\n#include <cmath>\n", 1)
write("Game/Combat/P700VfxShowcase.h", show)

# Forward P-700 presentation environment through the existing combat composition.
comp = read("Game/Combat/CombatPlaygroundWindowedComposition.h")
marker = "    [[nodiscard]] const std::optional<CombatPlaygroundRuntime>& Runtime() const noexcept\n"
forwarder = "    void SetP700VfxEnvironment(const Armament::P700LaunchVfxEnvironment& environment) noexcept\n    {\n        view_.SetP700VfxEnvironment(environment);\n    }\n\n"
if marker not in comp:
    raise SystemExit("combat composition P700 environment insertion marker missing")
comp = comp.replace(marker, forwarder + marker, 1)
write("Game/Combat/CombatPlaygroundWindowedComposition.h", comp)

# Main runtime wiring: authoritative weather, selectable showcase profile, actual showcase camera and impulse.
main = read("DeepRun/Main.cpp")
main = main.replace('#include "Game/Combat/P700VisualAcceptance.h"\n',
                    '#include "Game/Combat/P700VisualAcceptance.h"\n#include "Game/Combat/P700LaunchCameraPresentation.h"\n#include "Game/Combat/P700VfxShowcase.h"\n', 1)
main = main.replace('#include "Game/Weapons/P700LaunchAudioPresentation.h"\n',
                    '#include "Game/Weapons/P700LaunchAudioPresentation.h"\n#include "Game/Weapons/P700LaunchWeatherPresentation.h"\n', 1)

# Resolve developer-only showcase profile from an environment variable without extending gameplay options.
namespace_end = "\n} // namespace\n\nint main(const int argumentCount, char** argumentValues)\n"
profile_helper = r'''
DeepRun::Game::Combat::P700VfxShowcaseEnvironment P700ShowcaseEnvironmentFromProcess() noexcept
{
    char* raw = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&raw, &size, "DR_P700_SHOWCASE") != 0 || raw == nullptr)
        return DeepRun::Game::Combat::P700VfxShowcaseEnvironment::DayCalm;
    const std::string_view value(raw);
    DeepRun::Game::Combat::P700VfxShowcaseEnvironment result =
        DeepRun::Game::Combat::P700VfxShowcaseEnvironment::DayCalm;
    if (value == "SUNSET") result = DeepRun::Game::Combat::P700VfxShowcaseEnvironment::Sunset;
    else if (value == "NIGHT") result = DeepRun::Game::Combat::P700VfxShowcaseEnvironment::Night;
    else if (value == "STORM") result = DeepRun::Game::Combat::P700VfxShowcaseEnvironment::Storm;
    std::free(raw);
    return result;
}
'''
if namespace_end not in main:
    raise SystemExit("Main namespace end anchor missing")
main = main.replace(namespace_end, "\n" + profile_helper + namespace_end, 1)

var_anchor = "        DeepRun::Game::Camera::MultiScaleTacticalCamera multiScaleCamera;\n"
var_block = var_anchor + "        DeepRun::Game::Combat::P700LaunchCameraImpulsePlayer p700CameraImpulse;\n        const auto p700ShowcaseProfile = DeepRun::Game::Combat::P700VfxShowcaseProfileFor(\n            P700ShowcaseEnvironmentFromProcess());\n"
if var_anchor not in main:
    raise SystemExit("Main P700 camera variable anchor missing")
main = main.replace(var_anchor, var_block, 1)

# Weather propagation uses the authoritative state config, with showcase environment only in dedicated P700 smoke.
weather_anchor = "                if (combatPlayground.has_value())\n                {\n                    const auto weatherConfigured = combatPlayground->SetWeatherSensorEnvironment(*weatherSensors);"
if weather_anchor not in main:
    raise SystemExit("Main weather propagation anchor missing")
weather_repl = "                if (combatPlayground.has_value())\n                {\n                    const auto weatherConfigured = combatPlayground->SetWeatherSensorEnvironment(*weatherSensors);"
# append after the weather sensor configuration block by locating its known close and next combat block
weather_close = "                    if (!weatherConfigured)\n                    {\n                        std::cerr << \"[Game][ERROR] W1-E combat weather propagation failed: \"\n                                  << weatherConfigured.error() << '\\n';\n                        return false;\n                    }\n                }\n\n                if (combatPlayground.has_value())"
weather_new = "                    if (!weatherConfigured)\n                    {\n                        std::cerr << \"[Game][ERROR] W1-E combat weather propagation failed: \"\n                                  << weatherConfigured.error() << '\\n';\n                        return false;\n                    }\n                    const auto weatherStateConfig = playground.BuildWeatherStateConfigSnapshot();\n                    if (!weatherStateConfig)\n                    {\n                        std::cerr << \"[Game][ERROR] P-700 authoritative weather snapshot failed: \"\n                                  << weatherStateConfig.error() << '\\n';\n                        return false;\n                    }\n                    auto p700Environment = DeepRun::Game::Armament::BuildP700LaunchVfxEnvironmentFromWeather(\n                        *weatherStateConfig, 1.0F);\n                    if (!p700Environment)\n                    {\n                        std::cerr << \"[Game][ERROR] P-700 weather presentation mapping failed: \"\n                                  << p700Environment.error() << '\\n';\n                        return false;\n                    }\n                    if (options.p700SmokeTest)\n                        *p700Environment = p700ShowcaseProfile.environment;\n                    combatPlayground->SetP700VfxEnvironment(*p700Environment);\n                    const auto daylightConfigured = playground.SetPresentationDaylightFraction(\n                        options.p700SmokeTest ? p700Environment->daylightFraction : 1.0F);\n                    if (!daylightConfigured)\n                    {\n                        std::cerr << \"[Game][ERROR] P-700 showcase daylight configuration failed: \"\n                                  << daylightConfigured.error() << '\\n';\n                        return false;\n                    }\n                }\n\n                if (combatPlayground.has_value())"
if weather_close not in main:
    raise SystemExit("Main weather close anchor missing")
main = main.replace(weather_close, weather_new, 1)

# Capture the impulse player in render lambda.
old_capture = "             &renderFrames, &engineServices, &loggedP700AudioSubmissionFailure](DeepRun::Render::D3D12Renderer& renderer)"
new_capture = "             &renderFrames, &engineServices, &loggedP700AudioSubmissionFailure, &p700CameraImpulse,\n             &p700ShowcaseProfile](DeepRun::Render::D3D12Renderer& renderer)"
if old_capture not in main:
    raise SystemExit("Main render lambda capture anchor missing")
main = main.replace(old_capture, new_capture, 1)

# Evaluate visual-only impulse before scene camera setup.
time_anchor = "                const double presentationTimeSeconds = frameState.elapsedSeconds;\n\n                if (combatPlayground.has_value()"
if time_anchor not in main:
    raise SystemExit("Main presentation time anchor missing")
main = main.replace(time_anchor,
                    "                const double presentationTimeSeconds = frameState.elapsedSeconds;\n                const auto p700CameraOffset = p700CameraImpulse.Evaluate(presentationTimeSeconds);\n\n                if (combatPlayground.has_value()", 1)

# Replace the p700 smoke framing block created by the local-light patch with the real showcase preset consumer.
pattern = re.compile(r'''                    if \(options\.p700SmokeTest\)\n                    \{.*?                    \}\n                    else if \(options\.smokeTest\)''', re.S)
match = pattern.search(main)
if not match:
    raise SystemExit("P700 smoke framing block not found after local-light patch")
showcase_block = r'''                    if (options.p700SmokeTest)
                    {
                        float targetOffsetXMeters = p700CameraOffset[0];
                        float targetOffsetYMeters = p700CameraOffset[1];
                        float spanMeters = 360.0F;
                        std::optional<DeepRun::Assets::ModelVector3> cameraRelativePosition{};
                        if (combatPlayground->Runtime()->PlayerP700().has_value() &&
                            initialOwnshipNavigationPositionMeters.has_value())
                        {
                            const auto& missile = *combatPlayground->Runtime()->PlayerP700();
                            const auto selection = DeepRun::Game::Combat::EvaluateP700VfxShowcaseCamera(
                                missile, simulationTimeSeconds);
                            targetOffsetXMeters += missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x +
                                selection.targetOffsetMeters[0];
                            targetOffsetYMeters += missile.positionMeters.y - initialOwnshipNavigationPositionMeters->y +
                                selection.targetOffsetMeters[1];
                            spanMeters = selection.horizontalSpanMeters;
                            cameraRelativePosition = DeepRun::Assets::ModelVector3{
                                selection.relativePositionMeters[0], selection.relativePositionMeters[1],
                                selection.relativePositionMeters[2]};
                        }
                        else if (combatPlayground->Runtime()->LastExplosion().has_value() &&
                                 initialOwnshipNavigationPositionMeters.has_value())
                        {
                            const auto& explosion = *combatPlayground->Runtime()->LastExplosion();
                            targetOffsetXMeters += explosion.positionMeters.x - initialOwnshipNavigationPositionMeters->x;
                            targetOffsetYMeters += explosion.positionMeters.y - initialOwnshipNavigationPositionMeters->y;
                            spanMeters = 480.0F;
                        }
                        const auto poseApplied = playground.SetPresentationCameraRelativePosition(cameraRelativePosition);
                        if (!poseApplied)
                        {
                            std::cerr << "[Game][ERROR] P-700 showcase camera pose failed: " << poseApplied.error() << '\n';
                            return false;
                        }
                        const auto appliedFraming = playground.SetPresentationCameraFraming(
                            targetOffsetXMeters, targetOffsetYMeters, spanMeters, renderer.AspectRatio());
                        if (!appliedFraming)
                        {
                            std::cerr << "[Game][ERROR] P-700 acceptance camera failed: " << appliedFraming.error() << '\n';
                            return false;
                        }
                    }
                    else if (options.smokeTest)'''
main = main[:match.start()] + showcase_block + main[match.end():]

# Normal/smoke camera paths must explicitly clear cinematic pose and apply the bounded launch impulse.
main = main.replace("                    else if (options.smokeTest)\n                    {\n                        const auto cameraFraming",
                    "                    else if (options.smokeTest)\n                    {\n                        if (const auto cleared = playground.SetPresentationCameraRelativePosition(std::nullopt); !cleared)\n                            return false;\n                        const auto cameraFraming", 1)
main = main.replace("                            cameraFraming->targetOffsetXMeters, 0.0F, cameraFraming->horizontalSpanMeters, renderer.AspectRatio());",
                    "                            cameraFraming->targetOffsetXMeters + p700CameraOffset[0], p700CameraOffset[1],\n                            cameraFraming->horizontalSpanMeters, renderer.AspectRatio());", 1)
normal_else = "                    else\n                    {\n                        const auto ownshipLengthMeters"
if normal_else not in main:
    raise SystemExit("normal camera branch anchor missing")
main = main.replace(normal_else,
                    "                    else\n                    {\n                        if (const auto cleared = playground.SetPresentationCameraRelativePosition(std::nullopt); !cleared)\n                            return false;\n                        const auto ownshipLengthMeters", 1)
# Add impulse to normal final camera offsets.
main = main.replace("                            *ownshipFollowOffset,\n                            cameraFraming->targetOffsetYMeters,",
                    "                            *ownshipFollowOffset + p700CameraOffset[0],\n                            cameraFraming->targetOffsetYMeters + p700CameraOffset[1],", 1)

# Trigger the real camera impulse from the P700 VFX event after this frame; it affects the next presentation frame.
audio_anchor = "                    const std::array<float, 3> audioListenerPosition{\n                        camera->target.x, camera->target.y, camera->target.z};"
if audio_anchor not in main:
    raise SystemExit("Main P700 impulse trigger anchor missing")
main = main.replace(audio_anchor,
                    "                    if (combatRendered->p700Vfx.cameraImpulse.has_value())\n                        p700CameraImpulse.Trigger(*combatRendered->p700Vfx.cameraImpulse, presentationTimeSeconds);\n                    const std::array<float, 3> audioListenerPosition{\n                        camera->target.x, camera->target.y, camera->target.z};", 1)
write("DeepRun/Main.cpp", main)

# Tests: executable showcase camera, weather mapping, camera builder, and camera impulse.
test = read(test_path)
test = test.replace('#include "Game/Combat/P700VfxShowcase.h"\n',
                    '#include "Game/Combat/P700VfxShowcase.h"\n#include "Game/Combat/P700LaunchCameraPresentation.h"\n#include "Game/Weapons/P700LaunchWeatherPresentation.h"\n', 1)
showcase_old = "        P700VfxShowcaseCameras.size() != 6U || P700VfxShowcaseSpeedRamp.front().presentationRate != 1.0F ||\n        P700VfxShowcaseSpeedRamp.back().presentationRate != 1.0F)"
showcase_new = "        P700VfxShowcaseCameras.size() != 6U || P700VfxShowcaseSpeedRamp.front().presentationRate != 1.0F ||\n        P700VfxShowcaseSpeedRamp.back().presentationRate != 1.0F)"
if showcase_old not in test:
    raise SystemExit("showcase test base anchor missing")
# Add deeper checks before return true.
return_anchor = "    return true;\n}\n} // namespace\n\nint main()"
extra = r'''    auto missile = MakeMissile(901U, P700GranitPhase::WaterExit, 0.0F, 0.2F, 3.0);
    const auto selection = EvaluateP700VfxShowcaseCamera(missile, 3.1);
    if (selection.preset == nullptr || selection.preset->camera != P700VfxShowcaseCamera::WaterlineMoneyShot ||
        selection.horizontalSpanMeters <= 0.0F)
    {
        std::cerr << "P700VfxShowcase runtime camera selection is invalid\n";
        return false;
    }
    const auto weather = DeepRun::Environment::WeatherState::FullyDevelopedBeaufort(8U, 45.0F, 0x700ULL);
    if (!weather)
        return false;
    const auto mappedWeather = DeepRun::Game::Armament::BuildP700LaunchVfxEnvironmentFromWeather(weather->Config(), 0.25F);
    if (!mappedWeather || mappedWeather->significantWaveHeightMeters <= 0.0F ||
        mappedWeather->windSpeedMetersPerSecond <= 0.0F || mappedWeather->daylightFraction != 0.25F)
    {
        std::cerr << "P700 authoritative weather mapping is invalid\n";
        return false;
    }
    DeepRun::Game::Combat::P700LaunchCameraImpulsePlayer impulse;
    impulse.Trigger({.amplitude = 0.08F, .frequencyHz = 5.0F, .durationSeconds = 0.4F}, 1.0);
    const auto offset = impulse.Evaluate(1.05);
    if (!std::isfinite(offset[0]) || !std::isfinite(offset[1]) ||
        (std::abs(offset[0]) < 1.0e-6F && std::abs(offset[1]) < 1.0e-6F))
    {
        std::cerr << "P700 launch camera impulse is not applied as a presentation offset\n";
        return false;
    }
    DeepRun::Assets::ModelBounds bounds{{-10.0F, -10.0F, -10.0F}, {10.0F, 10.0F, 10.0F}};
    const auto camera = DeepRun::Render::BuildFixedWorldOrthographicCamera(
        {30.0F, 8.0F, 24.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 16.0F / 9.0F, 80.0F, bounds);
    if (!camera || camera->width != 80.0F || !DeepRun::Render::IsFinite(camera->viewProjection))
    {
        std::cerr << "P700 showcase arbitrary orthographic camera is invalid\n";
        return false;
    }
    return true;
}
} // namespace

int main()'''
if return_anchor not in test:
    raise SystemExit("showcase test return anchor missing")
test = test.replace(return_anchor, extra, 1)
write(test_path, test)

print("P-700 stage1 completion patch applied")
