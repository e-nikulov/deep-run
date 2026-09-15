#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace DeepRun::Audio
{
struct ProceduralNoiseOneShotRequest final
{
    std::uint64_t seed = 0U;
    float gain = 1.0F;
    float durationSeconds = 1.0F;
    float attackSeconds = 0.01F;
    float releaseSeconds = 0.75F;
    float lowPassCutoffHz = 240.0F;
    float toneFrequencyHz = 48.0F;
    float toneMix = 0.20F;
    float transientMix = 0.20F;
};

struct ProceduralOneShotBuffer final
{
    std::uint32_t sampleRate = 0U;
    std::vector<float> monoSamples{};
    float peakAbsoluteAmplitude = 0.0F;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateProceduralNoiseOneShotRequest(
    const ProceduralNoiseOneShotRequest& request,
    const std::uint32_t sampleRate)
{
    const auto normalized = [](const float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (sampleRate < 8'000U || sampleRate > 192'000U)
        return std::unexpected("procedural one-shot sample rate must be in 8000..192000 Hz");
    if (!normalized(request.gain))
        return std::unexpected("procedural one-shot gain must be finite in [0,1]");
    if (!std::isfinite(request.durationSeconds) || request.durationSeconds < 0.05F ||
        request.durationSeconds > 10.0F)
        return std::unexpected("procedural one-shot duration must be finite in 0.05..10 seconds");
    if (!std::isfinite(request.attackSeconds) || request.attackSeconds < 0.0F ||
        request.attackSeconds > request.durationSeconds * 0.5F)
        return std::unexpected("procedural one-shot attack must be finite and at most half the duration");
    if (!std::isfinite(request.releaseSeconds) || request.releaseSeconds <= 0.0F ||
        request.releaseSeconds > request.durationSeconds)
        return std::unexpected("procedural one-shot release must be finite in (0,duration]");
    const float nyquist = static_cast<float>(sampleRate) * 0.5F;
    if (!std::isfinite(request.lowPassCutoffHz) || request.lowPassCutoffHz < 20.0F ||
        request.lowPassCutoffHz > nyquist * 0.90F)
        return std::unexpected("procedural one-shot low-pass cutoff is outside the supported band");
    if (!std::isfinite(request.toneFrequencyHz) || request.toneFrequencyHz < 0.0F ||
        request.toneFrequencyHz > nyquist * 0.45F)
        return std::unexpected("procedural one-shot tone frequency is outside the supported band");
    if (!normalized(request.toneMix) || !normalized(request.transientMix) ||
        request.toneMix + request.transientMix > 1.0F)
        return std::unexpected("procedural one-shot tone/transient mixes must be normalized and sum to <= 1");
    return {};
}

namespace ProceduralOneShotDetail
{
[[nodiscard]] inline std::uint64_t SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] inline float SignedNoise(std::uint64_t& state) noexcept
{
    state = SplitMix64(state);
    constexpr double Scale = 1.0 / static_cast<double>(0xFFFFFFULL);
    const double unit = static_cast<double>(state & 0xFFFFFFULL) * Scale;
    return static_cast<float>(unit * 2.0 - 1.0);
}

[[nodiscard]] inline float SmoothEnvelope(
    const float timeSeconds,
    const float durationSeconds,
    const float attackSeconds,
    const float releaseSeconds) noexcept
{
    const float attack = attackSeconds > 1.0e-6F
        ? std::clamp(timeSeconds / attackSeconds, 0.0F, 1.0F)
        : 1.0F;
    const float attackShape = attack * attack * (3.0F - 2.0F * attack);
    const float remainingSeconds = (std::max)(durationSeconds - timeSeconds, 0.0F);
    const float release = std::clamp(remainingSeconds / releaseSeconds, 0.0F, 1.0F);
    const float releaseShape = release * release * (3.0F - 2.0F * release);
    return attackShape * releaseShape;
}
}

[[nodiscard]] inline std::expected<ProceduralOneShotBuffer, std::string> GenerateProceduralNoiseOneShot(
    const ProceduralNoiseOneShotRequest& request,
    const std::uint32_t sampleRate)
{
    if (const auto valid = ValidateProceduralNoiseOneShotRequest(request, sampleRate); !valid)
        return std::unexpected(valid.error());

    const double exactFrameCount = static_cast<double>(request.durationSeconds) * static_cast<double>(sampleRate);
    if (!std::isfinite(exactFrameCount) || exactFrameCount < 1.0 ||
        exactFrameCount > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
        return std::unexpected("procedural one-shot derived frame count is invalid");
    const std::size_t frameCount = static_cast<std::size_t>(std::ceil(exactFrameCount));

    ProceduralOneShotBuffer result{};
    result.sampleRate = sampleRate;
    result.monoSamples.resize(frameCount);

    const float dt = 1.0F / static_cast<float>(sampleRate);
    const float lowPassAlpha = 1.0F - std::exp(
        -2.0F * std::numbers::pi_v<float> * request.lowPassCutoffHz * dt);
    const float noiseMix = 1.0F - request.toneMix - request.transientMix;
    std::uint64_t randomState = request.seed ^ 0x415544494F5F5031ULL;
    float lowPassedNoise = 0.0F;
    float previousNoise = 0.0F;
    float phase = static_cast<float>((request.seed & 0xFFFFU) / 65535.0) * 2.0F * std::numbers::pi_v<float>;
    const float phaseStep = 2.0F * std::numbers::pi_v<float> * request.toneFrequencyHz * dt;

    for (std::size_t frame = 0U; frame < frameCount; ++frame)
    {
        const float timeSeconds = static_cast<float>(frame) * dt;
        const float whiteNoise = ProceduralOneShotDetail::SignedNoise(randomState);
        lowPassedNoise += lowPassAlpha * (whiteNoise - lowPassedNoise);
        const float highFrequencyCrack = whiteNoise - 0.72F * previousNoise;
        previousNoise = whiteNoise;

        const float slowModulation = 0.72F + 0.28F * std::sin(
            2.0F * std::numbers::pi_v<float> * 1.7F * timeSeconds + phase * 0.37F);
        const float tone = request.toneFrequencyHz > 0.0F ? std::sin(phase) : 0.0F;
        phase += phaseStep;
        if (phase > 2.0F * std::numbers::pi_v<float>)
            phase -= 2.0F * std::numbers::pi_v<float>;

        const float transientEnvelope = std::exp(-timeSeconds * 15.0F);
        const float envelope = ProceduralOneShotDetail::SmoothEnvelope(
            timeSeconds,
            request.durationSeconds,
            request.attackSeconds,
            request.releaseSeconds);
        const float composite =
            noiseMix * lowPassedNoise * slowModulation +
            request.toneMix * tone * (0.55F + 0.45F * slowModulation) +
            request.transientMix * highFrequencyCrack * transientEnvelope;
        const float sample = std::clamp(composite * envelope * request.gain, -1.0F, 1.0F);
        if (!std::isfinite(sample))
            return std::unexpected("procedural one-shot generated a non-finite sample");
        result.monoSamples[frame] = sample;
        result.peakAbsoluteAmplitude = (std::max)(result.peakAbsoluteAmplitude, std::abs(sample));
    }

    return result;
}
} // namespace DeepRun::Audio
