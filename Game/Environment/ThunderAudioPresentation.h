#pragma once

#include "Engine/Audio/ProceduralOneShot.h"
#include "Game/Environment/ThunderstormPresentation.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game
{
[[nodiscard]] inline std::expected<Audio::ProceduralNoiseOneShotRequest, std::string>
BuildThunderAudioOneShotRequest(const ThunderAudioCue& cue)
{
    if (!std::isfinite(cue.gain) || cue.gain < 0.0F || cue.gain > 1.0F ||
        !std::isfinite(cue.distanceMeters) || cue.distanceMeters < 0.0F ||
        !std::isfinite(cue.rumbleSeconds) || cue.rumbleSeconds <= 0.0F)
    {
        return std::unexpected("thunder audio cue is invalid");
    }

    const float distanceFraction = std::clamp((cue.distanceMeters - 1200.0F) / 7800.0F, 0.0F, 1.0F);
    // Game owns the semantic mapping. Distant thunder loses high-frequency crack, attacks more slowly and
    // moves toward a lower spectral center. Engine/Audio receives only generic synthesis controls.
    Audio::ProceduralNoiseOneShotRequest request{
        .seed = cue.strikeIndex ^ 0x5448554E44455231ULL,
        .gain = cue.gain,
        .durationSeconds = std::clamp(cue.rumbleSeconds, 0.5F, 7.5F),
        .attackSeconds = 0.012F + 0.090F * distanceFraction,
        .releaseSeconds = std::clamp(cue.rumbleSeconds * 0.72F, 0.45F, cue.rumbleSeconds),
        .lowPassCutoffHz = 330.0F - 220.0F * distanceFraction,
        .toneFrequencyHz = 52.0F - 18.0F * distanceFraction,
        .toneMix = 0.18F + 0.12F * distanceFraction,
        .transientMix = 0.38F - 0.30F * distanceFraction};
    if (const auto valid = Audio::ValidateProceduralNoiseOneShotRequest(request, 48'000U); !valid)
        return std::unexpected("thunder audio mapping produced invalid generic synthesis controls: " + valid.error());
    return request;
}
} // namespace DeepRun::Game
