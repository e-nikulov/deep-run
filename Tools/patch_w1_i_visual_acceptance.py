from pathlib import Path

BRANCH_FILES = {
    "Game/PhysicalPlayground.h": None,
    "Game/PhysicalPlayground.cpp": None,
    "DeepRun/Main.cpp": None,
}


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one occurrence, found {count}")
    return text.replace(old, new, 1)


# PhysicalPlayground.h: optional Beaufort override for bounded visual acceptance/scenario composition.
path = Path("Game/PhysicalPlayground.h")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "        bool verifyDistinctUploads,\n        float initialSubmarineDepthMeters = 100.0F);",
    "        bool verifyDistinctUploads,\n        float initialSubmarineDepthMeters = 100.0F,\n        std::optional<std::uint8_t> weatherBeaufortForce = std::nullopt);",
    "PhysicalPlayground.h Initialize signature",
)
path.write_text(text, encoding="utf-8", newline="\n")

# PhysicalPlayground.cpp: use the existing production spectrum, including valid flat-water Beaufort 0.
path = Path("Game/PhysicalPlayground.cpp")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "    const bool verifyDistinctUploads,\n    const float initialSubmarineDepthMeters)\n{",
    "    const bool verifyDistinctUploads,\n    const float initialSubmarineDepthMeters,\n    const std::optional<std::uint8_t> weatherBeaufortForce)\n{",
    "PhysicalPlayground.cpp Initialize signature",
)
old_weather = '''    // W1-B scenario composition: WeatherState is the environment input authority and Marine derives one
    // deterministic seven-component spectrum from it. The normal gameplay seed intentionally combines a
    // moderate local wind sea with a longer oblique swell so the surface has natural beats instead of a
    // repeating three-sine silhouette. Render still receives only a copied WaterBody snapshot.
    const auto weather = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 5U,
        .windSpeedMetersPerSecond = 10.0F,
        .windGustSpeedMetersPerSecond = 13.0F,
        .windDirectionDegrees = 18.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 2.0F,
            .probableMaximumWaveHeightMeters = 2.5F,
            .peakPeriodSeconds = 4.8F,
            .meanDirectionDegrees = 18.0F,
            .directionalSpreadDegrees = 42.0F},
        .swell = Environment::SwellState{
            .significantWaveHeightMeters = 1.2F,
            .peakPeriodSeconds = 9.5F,
            .meanDirectionDegrees = 342.0F,
            .directionalSpreadDegrees = 10.0F},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 100000.0F,
        .cloudCoverFraction = 0.25F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 0x4452554E5F573142ULL});
    if (!weather)
    {
        return std::unexpected("physical playground W1 weather creation failed: " + weather.error().message);
    }
    const auto productionWaves = Marine::BuildProductionOceanWaveField(*weather);
    if (!productionWaves || !productionWaves->has_value())
    {
        return std::unexpected("physical playground W1 spectral ocean creation failed" +
                               (productionWaves ? std::string{} : ": " + productionWaves.error()));
    }
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = M2SeaSurfaceLevelMeters,
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter,
         .waves = productionWaves->value()});
'''
new_weather = '''    // W1-B scenario composition: WeatherState is the environment input authority and Marine derives the
    // production spectrum from it. Normal gameplay keeps the accepted mixed wind-sea+swell seed. A bounded
    // Beaufort override is used by visual acceptance/mission composition and still flows through the same
    // WeatherState -> ProductionOceanSpectrum -> WaterBody -> Render path; it is not a second wave system.
    const auto weather = weatherBeaufortForce.has_value()
        ? Environment::WeatherState::FullyDevelopedBeaufort(
              *weatherBeaufortForce, 18.0F, 0x5731495F56495355ULL)
        : Environment::WeatherState::Create(Environment::WeatherStateConfig{
              .beaufortForce = 5U,
              .windSpeedMetersPerSecond = 10.0F,
              .windGustSpeedMetersPerSecond = 13.0F,
              .windDirectionDegrees = 18.0F,
              .windSea = Environment::WindSeaState{
                  .significantWaveHeightMeters = 2.0F,
                  .probableMaximumWaveHeightMeters = 2.5F,
                  .peakPeriodSeconds = 4.8F,
                  .meanDirectionDegrees = 18.0F,
                  .directionalSpreadDegrees = 42.0F},
              .swell = Environment::SwellState{
                  .significantWaveHeightMeters = 1.2F,
                  .peakPeriodSeconds = 9.5F,
                  .meanDirectionDegrees = 342.0F,
                  .directionalSpreadDegrees = 10.0F},
              .rainRateMillimetersPerHour = 0.0F,
              .meteorologicalVisibilityMeters = 100000.0F,
              .cloudCoverFraction = 0.25F,
              .lightningRatePerMinute = 0.0F,
              .weatherSeed = 0x4452554E5F573142ULL});
    if (!weather)
    {
        return std::unexpected("physical playground W1 weather creation failed: " + weather.error().message);
    }
    const auto productionWaves = Marine::BuildProductionOceanWaveField(*weather);
    if (!productionWaves)
    {
        return std::unexpected("physical playground W1 spectral ocean creation failed: " + productionWaves.error());
    }
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = M2SeaSurfaceLevelMeters,
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter,
         .waves = *productionWaves});
'''
text = replace_once(text, old_weather, new_weather, "PhysicalPlayground.cpp weather composition")
path.write_text(text, encoding="utf-8", newline="\n")

# Main.cpp: bounded environment-driven acceptance harness reusing the existing WindowFrameCapture.
path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "#include <array>\n#include <cstdint>",
    "#include <array>\n#include <charconv>\n#include <cstdint>",
    "Main.cpp charconv include",
)
text = replace_once(
    text,
    "#include <span>\n#include <string_view>",
    "#include <span>\n#include <string>\n#include <string_view>",
    "Main.cpp string include",
)
helper_anchor = '''constexpr double P700AcceptanceTimeoutSeconds = 110.0;

struct FindContext final
'''
helper = '''constexpr double P700AcceptanceTimeoutSeconds = 110.0;

[[nodiscard]] std::optional<std::uint8_t> WeatherVisualBeaufortFromEnvironment()
{
    char* rawValue = nullptr;
    std::size_t rawSize = 0U;
    if (_dupenv_s(&rawValue, &rawSize, "DR_WEATHER_VISUAL_BEAUFORT") != 0 || rawValue == nullptr)
    {
        std::free(rawValue);
        return std::nullopt;
    }
    const std::string valueText(rawValue);
    std::free(rawValue);

    unsigned int parsed = 0U;
    const auto [end, error] = std::from_chars(
        valueText.data(), valueText.data() + valueText.size(), parsed);
    if (error != std::errc{} || end != valueText.data() + valueText.size() || parsed > 12U)
        throw std::runtime_error("DR_WEATHER_VISUAL_BEAUFORT must be an integer in the inclusive range 0..12");
    return static_cast<std::uint8_t>(parsed);
}

struct FindContext final
'''
text = replace_once(text, helper_anchor, helper, "Main.cpp weather env helper")
text = replace_once(
    text,
    "        const DeepRun::Core::ApplicationOptions options = DeepRun::Core::ApplicationOptions::Parse(arguments);\n        DeepRun::Game::PhysicalPlayground playground;",
    "        const DeepRun::Core::ApplicationOptions options = DeepRun::Core::ApplicationOptions::Parse(arguments);\n        const std::optional<std::uint8_t> weatherVisualBeaufortForce = WeatherVisualBeaufortFromEnvironment();\n        DeepRun::Game::PhysicalPlayground playground;",
    "Main.cpp weather option local",
)
text = replace_once(
    text,
    "        std::uint64_t renderFrames = 0;\n        std::uint64_t consumedSelectContactSequence = 0;",
    "        std::uint64_t renderFrames = 0;\n        bool weatherVisualCaptured = false;\n        std::uint64_t consumedSelectContactSequence = 0;",
    "Main.cpp weather capture state",
)
text = replace_once(
    text,
    "            [&options, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,\n             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,\n             &inputState, &engineServices](DeepRun::Core::Engine& engine)",
    "            [&options, &weatherVisualBeaufortForce, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,\n             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,\n             &inputState, &engineServices](DeepRun::Core::Engine& engine)",
    "Main.cpp startup captures",
)
old_depth = '''                const float initialDepthMeters = options.p700SmokeTest
                    ? 30.0F
                    : options.smokeTest || options.benchmarkM3
                        ? 100.0F
                        : NormalGameplayInitialDepthMeters;
                const auto initialized = playground.Initialize(
                    engine.Assets(), *physics, *renderer, options.smokeTest || options.p700SmokeTest,
                    initialDepthMeters);
'''
new_depth = '''                const float initialDepthMeters = weatherVisualBeaufortForce.has_value()
                    ? 3.0F
                    : options.p700SmokeTest
                        ? 30.0F
                        : options.smokeTest || options.benchmarkM3
                            ? 100.0F
                            : NormalGameplayInitialDepthMeters;
                const auto initialized = playground.Initialize(
                    engine.Assets(), *physics, *renderer, options.smokeTest || options.p700SmokeTest,
                    initialDepthMeters, weatherVisualBeaufortForce);
'''
text = replace_once(text, old_depth, new_depth, "Main.cpp Initialize weather override")
text = replace_once(
    text,
    "                    if (options.smokeTest)\n                    {\n                        const auto localFraming = DeepRun::Game::Combat::CombatPlaygroundCameraDirector::LocalFraming();",
    "                    if (options.smokeTest || weatherVisualBeaufortForce.has_value())\n                    {\n                        const auto localFraming = DeepRun::Game::Combat::CombatPlaygroundCameraDirector::LocalFraming();",
    "Main.cpp weather local framing",
)
old_combat = '''                    const float destroyerInitialXMeters = options.p700SmokeTest
                        ? 20'100.0F
                        : options.smokeTest
                            ? DeepRun::Game::Combat::M5CombatDestroyerInitialXMeters
                            : NormalGameplayLongRangeCombatTargetMeters;
                    const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(
                        *renderer,
                        engine.Assets(),
                        destroyerInitialXMeters,
                        options.p700SmokeTest,
                        options.p700SmokeTest
                            ? 0.0F
                            : DeepRun::Game::Combat::M5CombatDestroyerCruiseVelocityXMetersPerSecond);
                    if (!combat)
                    {
                        std::cerr << "[Game][ERROR] " << combat.error() << '\\n';
                        return false;
                    }
                    combatPlayground = *combat;
'''
new_combat = '''                    if (!weatherVisualBeaufortForce.has_value())
                    {
                        const float destroyerInitialXMeters = options.p700SmokeTest
                            ? 20'100.0F
                            : options.smokeTest
                                ? DeepRun::Game::Combat::M5CombatDestroyerInitialXMeters
                                : NormalGameplayLongRangeCombatTargetMeters;
                        const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(
                            *renderer,
                            engine.Assets(),
                            destroyerInitialXMeters,
                            options.p700SmokeTest,
                            options.p700SmokeTest
                                ? 0.0F
                                : DeepRun::Game::Combat::M5CombatDestroyerCruiseVelocityXMetersPerSecond);
                        if (!combat)
                        {
                            std::cerr << "[Game][ERROR] " << combat.error() << '\\n';
                            return false;
                        }
                        combatPlayground = *combat;
                    }
'''
text = replace_once(text, old_combat, new_combat, "Main.cpp skip combat in weather visual mode")
text = replace_once(
    text,
    "                const auto command = (options.smokeTest || options.p700SmokeTest || options.benchmarkM3)",
    "                const auto command = (options.smokeTest || options.p700SmokeTest || options.benchmarkM3 ||\n                                      weatherVisualBeaufortForce.has_value())",
    "Main.cpp neutral command for weather visual",
)
text = replace_once(
    text,
    "            [&options, &playground, &hapticFeedback, &acousticPlaygroundRuntime, &combatPlayground,",
    "            [&options, &weatherVisualBeaufortForce, &playground, &hapticFeedback, &acousticPlaygroundRuntime, &combatPlayground,",
    "Main.cpp fixed-update captures",
)
text = replace_once(
    text,
    "             &inputState, &frameCapture, &captureEnabled, &options,\n             &renderFrames, &engineServices](DeepRun::Render::D3D12Renderer& renderer)",
    "             &inputState, &frameCapture, &captureEnabled, &options, &weatherVisualBeaufortForce,\n             &renderFrames, &weatherVisualCaptured, &engineServices](DeepRun::Render::D3D12Renderer& renderer)",
    "Main.cpp render captures",
)
render_anchor = '''                const auto rendered = playground.Render(renderer, simulationTimeSeconds, presentationTimeSeconds);
                if (!rendered)
                {
                    std::cerr << "[Game][ERROR] " << rendered.error() << '\\n';
                    return false;
                }

                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())
'''
render_replacement = '''                const auto rendered = playground.Render(renderer, simulationTimeSeconds, presentationTimeSeconds);
                if (!rendered)
                {
                    std::cerr << "[Game][ERROR] " << rendered.error() << '\\n';
                    return false;
                }

                if (weatherVisualBeaufortForce.has_value() && !weatherVisualCaptured &&
                    simulationTimeSeconds >= 2.0 && renderFrames >= 30U)
                {
                    if (!captureEnabled)
                    {
                        std::cerr << "[Game][ERROR] W1 sea-state visual acceptance requires frame capture\\n";
                        return false;
                    }
                    std::vector<std::byte> pixels;
                    std::uint32_t width = 0U;
                    std::uint32_t height = 0U;
                    const std::filesystem::path imagePath =
                        "w1-sea-beaufort-" + std::to_string(*weatherVisualBeaufortForce) + ".bmp";
                    if (!frameCapture.Capture(pixels, width, height) ||
                        !WriteBmp(imagePath, pixels, width, height))
                    {
                        std::cerr << "[Game][ERROR] W1 sea-state visual frame capture failed for Beaufort "
                                  << static_cast<unsigned int>(*weatherVisualBeaufortForce) << '\\n';
                        return false;
                    }
                    weatherVisualCaptured = true;
                    std::cout << "[Game][W1] Captured Beaufort "
                              << static_cast<unsigned int>(*weatherVisualBeaufortForce)
                              << " production sea state to " << imagePath.string()
                              << " at simulation_time_s=" << simulationTimeSeconds
                              << " resolution=" << width << 'x' << height << '\\n';
                    engineServices->RequestShutdown();
                }

                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())
'''
text = replace_once(text, render_anchor, render_replacement, "Main.cpp weather visual capture")
text = replace_once(
    text,
    "        return applicationExitCode;\n    }\n    catch (const std::exception& exception)",
    "        if (applicationExitCode == 0 && weatherVisualBeaufortForce.has_value() && !weatherVisualCaptured)\n        {\n            std::cerr << \"[Game][ERROR] W1 sea-state visual acceptance exited without a captured frame\\n\";\n            return 15;\n        }\n        return applicationExitCode;\n    }\n    catch (const std::exception& exception)",
    "Main.cpp weather visual completion gate",
)
path.write_text(text, encoding="utf-8", newline="\n")

print("W1-I visual acceptance patch applied")
