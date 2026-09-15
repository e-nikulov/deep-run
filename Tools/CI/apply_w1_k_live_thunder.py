from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# Engine owns the generic audio device/voice lifetime; Game owns all thunder semantics.
replace_once(
    "Engine/Core/Engine.cpp",
    "        timer.Tick();\n\n        // Age only effects inherited from the previous application frame.",
    "        timer.Tick();\n\n"
    "        // Reclaim completed generic procedural one-shot voices once per presentation frame. Audio remains\n"
    "        // presentation-only and cannot affect fixed SimulationTime or physics.\n"
    "        if (audioReady && audio)\n"
    "        {\n"
    "            audio->Update();\n"
    "        }\n\n"
    "        // Age only effects inherited from the previous application frame.",
)

replace_once(
    "Engine/Core/Engine.cpp",
    "std::expected<void, std::string> Engine::SubmitHapticEffect(const Input::HapticEffectRequest& request)\n"
    "{\n"
    "    return impl_->hapticMixer.Submit(request);\n"
    "}\n\n"
    "int Engine::ExitCode() const noexcept\n",
    "std::expected<void, std::string> Engine::SubmitHapticEffect(const Input::HapticEffectRequest& request)\n"
    "{\n"
    "    return impl_->hapticMixer.Submit(request);\n"
    "}\n\n"
    "std::expected<void, std::string> Engine::SubmitProceduralAudioOneShot(\n"
    "    const Audio::ProceduralNoiseOneShotRequest& request)\n"
    "{\n"
    "    if (!impl_->audio || !impl_->audioReady)\n"
    "    {\n"
    "        return std::unexpected(\"procedural audio submission requires an initialized windowed audio engine\");\n"
    "    }\n"
    "    return impl_->audio->SubmitProceduralOneShot(request);\n"
    "}\n\n"
    "int Engine::ExitCode() const noexcept\n",
)

# PhysicalPlayground already owns the exact WeatherState used by waves/sensors. Let Game advance the existing
# deterministic strike schedule from presentation time without exposing WeatherState to Main or Engine.
replace_once(
    "Game/PhysicalPlayground.h",
    '#include "Game/Environment/VerticalOceanGameplayContract.h"\n#include "Game/Environment/WeatherSensorCoupling.h"\n',
    '#include "Game/Environment/VerticalOceanGameplayContract.h"\n'
    '#include "Game/Environment/WeatherSensorCoupling.h"\n'
    '#include "Game/Environment/ThunderstormPresentation.h"\n',
)

sensor_method_end = '''        return environment;
    }

    // M5-I.2 live hazard bridge.'''
replace_once(
    "Game/PhysicalPlayground.h",
    sensor_method_end,
    '''        return environment;
    }

    // W1-H presentation-only live bridge. Strike scheduling uses PresentationTime and the exact authoritative
    // WeatherState; returned semantic cues contain no backend/miniaudio type and cannot alter simulation.
    [[nodiscard]] std::vector<ThunderAudioCue> AdvanceThunderAudioCues(const double presentationTimeSeconds)
    {
        if (!weather_.has_value())
            return {};
        return thunderstormCueTracker_.Advance(*weather_, presentationTimeSeconds);
    }

    // M5-I.2 live hazard bridge.''',
)

replace_once(
    "Game/PhysicalPlayground.h",
    "    std::optional<Environment::WeatherState> weather_;\n"
    "    // Explicit Game-owned M2 Antey playground tuning.",
    "    std::optional<Environment::WeatherState> weather_;\n"
    "    ThunderstormCueTracker thunderstormCueTracker_;\n"
    "    // Explicit Game-owned M2 Antey playground tuning.",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    "    water_ = *water;\n    weather_ = *weather;\n    buoyancy_ = std::move(buoyancy);\n",
    "    water_ = *water;\n    weather_ = *weather;\n    thunderstormCueTracker_.Reset();\n    buoyancy_ = std::move(buoyancy);\n",
)

# Main maps semantic thunder cues into generic procedural synthesis controls and submits them to Engine.
replace_once(
    "DeepRun/Main.cpp",
    '#include "Game/Haptics/HapticFeedbackSystem.h"\n#include "Game/PhysicalPlayground.h"\n',
    '#include "Game/Haptics/HapticFeedbackSystem.h"\n'
    '#include "Game/Environment/ThunderAudioPresentation.h"\n'
    '#include "Game/PhysicalPlayground.h"\n',
)

replace_once(
    "DeepRun/Main.cpp",
    "                const auto rendered = playground.Render(renderer, simulationTimeSeconds, presentationTimeSeconds);\n",
    "                // W1-H: optical lightning and delayed thunder share one deterministic strike schedule.\n"
    "                // Audio submission is presentation-only: an unavailable output device never fails gameplay.\n"
    "                for (const auto& thunderCue : playground.AdvanceThunderAudioCues(presentationTimeSeconds))\n"
    "                {\n"
    "                    const auto request = DeepRun::Game::BuildThunderAudioOneShotRequest(thunderCue);\n"
    "                    if (!request)\n"
    "                    {\n"
    "                        std::cerr << \"[Game][WARN] \" << request.error() << '\\n';\n"
    "                        continue;\n"
    "                    }\n"
    "                    static_cast<void>(engineServices->SubmitProceduralAudioOneShot(*request));\n"
    "                }\n\n"
    "                const auto rendered = playground.Render(renderer, simulationTimeSeconds, presentationTimeSeconds);\n",
)

print("W1-K live thunder/audio patch applied")
