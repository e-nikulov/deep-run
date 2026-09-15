from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one anchor, found {count}: {old[:100]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """    float atmosphereBoundaryViewportY = 0.0F;\n    float presentationTimeSeconds = 0.0F;\n    float cloudPatternOffset = 0.0F;\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 128U);""",
    """    float atmosphereBoundaryViewportY = 0.0F;\n    float presentationTimeSeconds = 0.0F;\n    float cloudPatternOffset = 0.0F;\n    float lightningFlashIntensity = 0.0F;\n    float lightningViewportX = 0.5F;\n    float lightningPatternOffset = 0.0F;\n    float padding2 = 0.0F;\n};\n\nstatic_assert(sizeof(ScenePresentationConstants) == 144U);""",
)

replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """            .atmosphereBoundaryViewportY = parameters.atmosphereBoundaryViewportY,\n            .presentationTimeSeconds = presentationTimeSeconds,\n            .cloudPatternOffset = parameters.cloudPatternOffset};""",
    """            .atmosphereBoundaryViewportY = parameters.atmosphereBoundaryViewportY,\n            .presentationTimeSeconds = presentationTimeSeconds,\n            .cloudPatternOffset = parameters.cloudPatternOffset,\n            .lightningFlashIntensity = parameters.lightningFlashIntensity,\n            .lightningViewportX = parameters.lightningViewportX,\n            .lightningPatternOffset = parameters.lightningPatternOffset};""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    '#include "Game/Environment/ScalableEnvironmentPresentation.h"\n#include "Game/Environment/WeatherPresentation.h"',
    '#include "Game/Environment/ScalableEnvironmentPresentation.h"\n#include "Game/Environment/ThunderstormPresentation.h"\n#include "Game/Environment/WeatherPresentation.h"',
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    WeatherPresentationParameters weatherPresentation{};\n    if (freePresentationCameraFraming_)\n    {\n        if (!weather_.has_value())\n            return std::unexpected(\"physical playground W1-F weather authority is unavailable\");\n        weatherPresentation = EvaluateWeatherPresentation(*weather_);\n        if (!ValidWeatherPresentationParameters(weatherPresentation))\n            return std::unexpected(\"physical playground W1-F weather presentation is invalid\");\n    }""",
    """    WeatherPresentationParameters weatherPresentation{};\n    ThunderstormPresentationSample thunderstormPresentation{};\n    if (freePresentationCameraFraming_)\n    {\n        if (!weather_.has_value())\n            return std::unexpected(\"physical playground W1 weather authority is unavailable\");\n        weatherPresentation = EvaluateWeatherPresentation(*weather_);\n        if (!ValidWeatherPresentationParameters(weatherPresentation))\n            return std::unexpected(\"physical playground W1-F weather presentation is invalid\");\n        thunderstormPresentation = EvaluateThunderstormPresentation(*weather_, presentationTimeSeconds);\n    }""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """        .atmosphereBoundaryViewportY = freePresentationCameraFraming_\n            ? std::clamp(*waterlineViewportY, 0.0F, 1.0F)\n            : 0.0F,\n        .cloudPatternOffset = weatherPresentation.cloudPatternOffset};""",
    """        .atmosphereBoundaryViewportY = freePresentationCameraFraming_\n            ? std::clamp(*waterlineViewportY, 0.0F, 1.0F)\n            : 0.0F,\n        .cloudPatternOffset = weatherPresentation.cloudPatternOffset,\n        .lightningFlashIntensity = thunderstormPresentation.flashIntensity,\n        .lightningViewportX = thunderstormPresentation.lightningViewportX,\n        .lightningPatternOffset = thunderstormPresentation.lightningPatternOffset};""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    // W1-F is presentation-only: legacy benchmark framing remains neutral; free gameplay reconstructs\n    // cloud/haze/precipitation from the same WeatherState already used by ocean and sensor composition.""",
    """    // W1-F/W1-G are presentation-only: legacy benchmark framing remains neutral; free gameplay reconstructs\n    // cloud/haze/precipitation/lightning from the same WeatherState already used by ocean and sensor composition.\n    // Thunder remains a semantic delayed cue in Game and is not owned by this render path.""",
)
