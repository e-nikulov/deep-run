from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> tuple[Path, str]:
    p = ROOT / path
    return p, p.read_text(encoding="utf-8")


def write(p: Path, text: str) -> None:
    p.write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# PhysicalPlayground publishes the exact weather-derived sensor snapshot used by the current scenario.
p, text = read("Game/PhysicalPlayground.h")
anchor = "    // M5-I.2 live hazard bridge. The snapshot is a value copy of the already-authoritative production collision\n"
method = '''    // W1-E read-only adapter. The returned value is derived from the exact WeatherState that also generated\n    // this playground's production wave spectrum. Consumers receive values only and cannot mutate Environment.\n    [[nodiscard]] std::expected<WeatherSensorEnvironment, std::string> BuildWeatherSensorEnvironment() const\n    {\n        if (!weather_.has_value())\n            return std::unexpected("physical playground W1-E weather authority is unavailable");\n        const WeatherSensorEnvironment environment = EvaluateWeatherSensorEnvironment(*weather_);\n        if (!ValidWeatherSensorEnvironment(environment))\n            return std::unexpected("physical playground W1-E sensor environment is invalid");\n        return environment;\n    }\n\n'''
if method not in text:
    text = replace_once(text, anchor, method + anchor, "PhysicalPlayground weather snapshot method")
write(p, text)

# Electronic runtime consumes value modifiers only; sensor primitives remain independent of WeatherState.
p, text = read("Game/Combat/AnteyElectronicCombatRuntime.h")
text = replace_once(
    text,
    '#include "Game/Combat/SurfaceContactSensorTruth.h"\n',
    '#include "Game/Combat/SurfaceContactSensorTruth.h"\n#include "Game/Environment/WeatherSensorCoupling.h"\n',
    "electronics include")
radio_anchor = '''    [[nodiscard]] bool RadioTransmitting(const double simulationTimeSeconds) const noexcept\n    {\n        return std::isfinite(simulationTimeSeconds) && state_.radioTransmitUntilSeconds > simulationTimeSeconds;\n    }\n\n'''
setter = '''    [[nodiscard]] std::expected<void, std::string> SetWeatherSensorEnvironment(\n        const WeatherSensorEnvironment& environment)\n    {\n        if (!ValidWeatherSensorEnvironment(environment))\n            return std::unexpected("Antey electronic weather sensor environment is invalid");\n        weatherSensorEnvironment_ = environment;\n        return {};\n    }\n\n'''
if setter not in text:
    text = replace_once(text, radio_anchor, radio_anchor + setter, "electronics weather setter")
advanced_anchor = '''        if (!advanced)\n            return std::unexpected("Antey electronic suite advance failed: " + advanced.error());\n\n        AnteyElectronicCombatFrame frame{};\n'''
sensor_config = '''        if (!advanced)\n            return std::unexpected("Antey electronic suite advance failed: " + advanced.error());\n\n        AnteyElectronicSuiteConfig sensorConfig = config_;\n        sensorConfig.zonaConfidence = std::clamp(\n            config_.zonaConfidence * weatherSensorEnvironment_.rf.terrestrialConfidenceMultiplier, 0.0F, 1.0F);\n        sensorConfig.radianMaximumDetectionRangeMeters =\n            config_.radianMaximumDetectionRangeMeters * weatherSensorEnvironment_.surfaceRadar.detectionRangeMultiplier;\n        sensorConfig.radianMinimumRangeUncertaintyMeters =\n            config_.radianMinimumRangeUncertaintyMeters * weatherSensorEnvironment_.surfaceRadar.rangeUncertaintyMultiplier;\n        sensorConfig.radianFractionalRangeUncertainty =\n            config_.radianFractionalRangeUncertainty * weatherSensorEnvironment_.surfaceRadar.rangeUncertaintyMultiplier;\n        sensorConfig.radianConfidence = std::clamp(\n            config_.radianConfidence * weatherSensorEnvironment_.surfaceRadar.confidenceMultiplier, 0.0F, 1.0F);\n\n        AnteyElectronicCombatFrame frame{};\n'''
text = replace_once(text, advanced_anchor, sensor_config, "electronics effective sensor config")
text = replace_once(
    text,
    '''                const auto observed = ObserveZonaEmitter(\n                    config_, state_, navigationPosition, truth.emitter.positionMeters,\n''',
    '''                const auto observed = ObserveZonaEmitter(\n                    sensorConfig, state_, navigationPosition, truth.emitter.positionMeters,\n''',
    "ZONA weather config")
text = replace_once(
    text,
    '''                const auto observed = ObserveRadianSurfaceTarget(\n                    config_, state_, navigationPosition, truth.emitter.positionMeters, simulationTimeSeconds);\n''',
    '''                const auto observed = ObserveRadianSurfaceTarget(\n                    sensorConfig, state_, navigationPosition, truth.emitter.positionMeters, simulationTimeSeconds);\n''',
    "RADIAN weather config")
report_old = '''            // The remote sensor truth is sampled only to manufacture the report. The player receives a stale,\n            // uncertain observation; classification/entity identity never crosses the perception boundary.\n            const ExternalTargetReport report = BuildExternalTargetReport(\n                config_, source, militaryTruth->emitter.positionMeters, simulationTimeSeconds);\n            const auto observed = ExternalTargetReportObservation(\n                config_, report, navigationPosition, simulationTimeSeconds);\n'''
report_new = '''            // The remote sensor truth is sampled only to manufacture the report. The player receives a stale,\n            // uncertain observation; classification/entity identity never crosses the perception boundary.\n            AnteyElectronicSuiteConfig reportConfig = config_;\n            if (source == ExternalTargetReportSource::MkrcSatellite)\n            {\n                reportConfig.selenaInitialPositionUncertaintyMeters *=\n                    weatherSensorEnvironment_.rf.satelliteReportUncertaintyMultiplier;\n                reportConfig.externalReportConfidence = std::clamp(\n                    config_.externalReportConfidence * weatherSensorEnvironment_.rf.satelliteReportConfidenceMultiplier,\n                    0.0F, 1.0F);\n            }\n            else\n            {\n                reportConfig.externalReportConfidence = std::clamp(\n                    config_.externalReportConfidence * weatherSensorEnvironment_.rf.terrestrialConfidenceMultiplier,\n                    0.0F, 1.0F);\n            }\n            const ExternalTargetReport report = BuildExternalTargetReport(\n                reportConfig, source, militaryTruth->emitter.positionMeters, simulationTimeSeconds);\n            const auto observed = ExternalTargetReportObservation(\n                reportConfig, report, navigationPosition, simulationTimeSeconds);\n'''
text = replace_once(text, report_old, report_new, "external report weather config")
text = replace_once(
    text,
    '                .confidence = 0.74F};\n',
    '                .confidence = 0.74F * weatherSensorEnvironment_.rf.hostileInterceptConfidenceMultiplier};\n',
    "hostile ESM weather confidence")
text = replace_once(
    text,
    '''private:\n    AnteyElectronicSuiteConfig config_{};\n    AnteyElectronicSuiteState state_{};\n''',
    '''private:\n    AnteyElectronicSuiteConfig config_{};\n    AnteyElectronicSuiteState state_{};\n    WeatherSensorEnvironment weatherSensorEnvironment_{};\n''',
    "electronics weather member")
write(p, text)

# Combat runtime applies weather to periscope optics, both sides' passive receivers and electronics.
p, text = read("Game/Combat/CombatPlaygroundRuntime.h")
text = replace_once(
    text,
    '#include "Game/Combat/SurfaceContactSensorTruth.h"\n',
    '#include "Game/Combat/SurfaceContactSensorTruth.h"\n#include "Game/Environment/WeatherSensorCoupling.h"\n',
    "combat runtime weather include")
old_setter = '''    [[nodiscard]] std::expected<void, std::string> SetPeriscopeOpticalConditions(\n        const PeriscopeOpticalConditions& conditions)\n    {\n        if (!ValidPeriscopeOpticalConditions(conditions))\n            return std::unexpected("invalid periscope optical conditions");\n        periscopeOpticalConditions_ = conditions;\n        return {};\n    }\n'''
new_setter = '''    [[nodiscard]] std::expected<void, std::string> SetWeatherSensorEnvironment(\n        const WeatherSensorEnvironment& environment)\n    {\n        if (!ValidWeatherSensorEnvironment(environment))\n            return std::unexpected("combat playground weather sensor environment is invalid");\n        const PeriscopeOpticalConditions opticalConditions{\n            .meteorologicalVisibilityMeters = environment.optical.meteorologicalVisibilityMeters,\n            .ambientLightFraction = environment.optical.ambientLightFraction,\n            .glareFraction = periscopeOpticalConditions_.glareFraction,\n            .seaStateObscurationFraction = environment.optical.seaStateObscurationFraction};\n        if (!ValidPeriscopeOpticalConditions(opticalConditions))\n            return std::unexpected("combat playground weather-derived optical conditions are invalid");\n        const auto electronicConfigured = electronics_.SetWeatherSensorEnvironment(environment);\n        if (!electronicConfigured)\n            return std::unexpected(electronicConfigured.error());\n        weatherSensorEnvironment_ = environment;\n        periscopeOpticalConditions_ = opticalConditions;\n        return {};\n    }\n\n    [[nodiscard]] std::expected<void, std::string> SetPeriscopeOpticalConditions(\n        const PeriscopeOpticalConditions& conditions)\n    {\n        if (!ValidPeriscopeOpticalConditions(conditions))\n            return std::unexpected("invalid periscope optical conditions");\n        periscopeOpticalConditions_ = conditions;\n        return {};\n    }\n'''
text = replace_once(text, old_setter, new_setter, "combat runtime weather setter")
destroyer_old = '''        const auto destroyerAcoustics = SampleSimpleDestroyerAcoustics(\n            destroyerDefinition_, destroyer_, *physicsWorld_);\n        if (!destroyerAcoustics)\n        {\n            return std::unexpected("M5-H destroyer acoustic snapshot failed: " + destroyerAcoustics.error());\n        }\n'''
destroyer_new = '''        auto destroyerAcoustics = SampleSimpleDestroyerAcoustics(\n            destroyerDefinition_, destroyer_, *physicsWorld_);\n        if (!destroyerAcoustics)\n        {\n            return std::unexpected("M5-H destroyer acoustic snapshot failed: " + destroyerAcoustics.error());\n        }\n        const auto destroyerWeatherAmbient = ApplyWeatherPassiveAmbientNoise(\n            weatherSensorEnvironment_.passiveAcoustic,\n            destroyerDefinition_.bodyCenterBelowSurfaceMeters,\n            destroyerAcoustics->passiveReceiver.ambientNoiseLevelDb);\n        if (!destroyerWeatherAmbient)\n        {\n            return std::unexpected("W1-E destroyer passive weather coupling failed: " +\n                                   destroyerWeatherAmbient.error());\n        }\n        destroyerAcoustics->passiveReceiver.ambientNoiseLevelDb = destroyerWeatherAmbient->ambientNoiseLevelDb;\n'''
text = replace_once(text, destroyer_old, destroyer_new, "destroyer passive weather")
text = replace_once(
    text,
    '''    PeriscopeState periscopeState_{};\n    PeriscopeOpticalConditions periscopeOpticalConditions_{};\n    AnteyElectronicCombatRuntime electronics_{};\n''',
    '''    PeriscopeState periscopeState_{};\n    PeriscopeOpticalConditions periscopeOpticalConditions_{};\n    WeatherSensorEnvironment weatherSensorEnvironment_{};\n    AnteyElectronicCombatRuntime electronics_{};\n''',
    "combat runtime weather member")
write(p, text)

# Windowed composition stores the snapshot before lazy runtime creation and forwards future updates.
p, text = read("Game/Combat/CombatPlaygroundWindowedComposition.h")
text = replace_once(
    text,
    '#include "Game/Combat/GameplayPacingMetrics.h"\n',
    '#include "Game/Combat/GameplayPacingMetrics.h"\n#include "Game/Environment/WeatherSensorCoupling.h"\n',
    "windowed composition weather include")
runtime_anchor = '''    [[nodiscard]] const std::optional<CombatPlaygroundRuntime>& Runtime() const noexcept\n    {\n        return runtime_;\n    }\n\n'''
composition_setter = '''    [[nodiscard]] std::expected<void, std::string> SetWeatherSensorEnvironment(\n        const WeatherSensorEnvironment& environment)\n    {\n        if (!ValidWeatherSensorEnvironment(environment))\n            return std::unexpected("windowed combat weather sensor environment is invalid");\n        if (runtime_.has_value())\n        {\n            const auto configured = runtime_->SetWeatherSensorEnvironment(environment);\n            if (!configured)\n                return std::unexpected("windowed combat weather propagation failed: " + configured.error());\n        }\n        weatherSensorEnvironment_ = environment;\n        return {};\n    }\n\n'''
if composition_setter not in text:
    text = replace_once(text, runtime_anchor, composition_setter + runtime_anchor, "windowed composition setter")
bind_anchor = '''        const auto boundPlayer = runtime->BindPlayerPhysicalProxy(\n            playerCollisionProxy, playerSnapshot, simulationTimeSeconds);\n'''
weather_before_bind = '''        if (weatherSensorEnvironment_.has_value())\n        {\n            const auto configured = runtime->SetWeatherSensorEnvironment(*weatherSensorEnvironment_);\n            if (!configured)\n            {\n                return std::unexpected("M5-H.1 windowed combat weather configuration failed: " + configured.error());\n            }\n        }\n        const auto boundPlayer = runtime->BindPlayerPhysicalProxy(\n            playerCollisionProxy, playerSnapshot, simulationTimeSeconds);\n'''
text = replace_once(text, bind_anchor, weather_before_bind, "windowed lazy runtime weather")
text = replace_once(
    text,
    '''    float destroyerCruiseVelocityXMetersPerSecond_ = M5CombatDestroyerCruiseVelocityXMetersPerSecond;\n    std::optional<CombatPlaygroundRuntime> runtime_{};\n''',
    '''    float destroyerCruiseVelocityXMetersPerSecond_ = M5CombatDestroyerCruiseVelocityXMetersPerSecond;\n    std::optional<WeatherSensorEnvironment> weatherSensorEnvironment_{};\n    std::optional<CombatPlaygroundRuntime> runtime_{};\n''',
    "windowed weather member")
write(p, text)

# Normal runtime publishes the weather sensor snapshot to combat immediately before that fixed-frame advance.
p, text = read("DeepRun/Main.cpp")
acoustic_marker = '''                const auto acousticSnapshot = playground.BuildAcousticSnapshot(\n                    DeepRun::Game::AcousticPlaygroundRuntime::AmbientNoiseLevelDb());\n'''
start = text.find(acoustic_marker)
if start < 0:
    raise RuntimeError("Main acoustic snapshot marker not found")
combat_marker = "                if (combatPlayground.has_value())\n                {\n"
pos = text.find(combat_marker, start)
if pos < 0:
    raise RuntimeError("Main combat marker after acoustic snapshot not found")
main_weather = '''                const auto weatherSensors = playground.BuildWeatherSensorEnvironment();\n                if (!weatherSensors)\n                {\n                    std::cerr << "[Game][ERROR] W1-E weather sensor environment failed: "\n                              << weatherSensors.error() << '\\n';\n                    return false;\n                }\n                if (combatPlayground.has_value())\n                {\n                    const auto weatherConfigured = combatPlayground->SetWeatherSensorEnvironment(*weatherSensors);\n                    if (!weatherConfigured)\n                    {\n                        std::cerr << "[Game][ERROR] W1-E combat weather propagation failed: "\n                                  << weatherConfigured.error() << '\\n';\n                        return false;\n                    }\n                }\n\n'''
if main_weather not in text:
    text = text[:pos] + main_weather + text[pos:]
write(p, text)

print("W1-E full sensor runtime patch: PASS")
