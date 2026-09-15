from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


# Generic Engine submission boundary for the already-existing generic procedural one-shot service.
replace_once(
    "Engine/Core/Engine.h",
    """namespace DeepRun::Assets
{
class AssetManager;
}
""",
    """namespace DeepRun::Audio
{
struct ProceduralNoiseOneShotRequest;
}

namespace DeepRun::Assets
{
class AssetManager;
}
""",
)
replace_once(
    "Engine/Core/Engine.h",
    """    [[nodiscard]] std::expected<void, std::string> SubmitHapticEffect(
        const Input::HapticEffectRequest& request);

    [[nodiscard]] int ExitCode() const noexcept;
""",
    """    [[nodiscard]] std::expected<void, std::string> SubmitHapticEffect(
        const Input::HapticEffectRequest& request);

    // Generic presentation-only audio submission. Game owns semantic event meaning and maps it into the
    // renderer/audio-neutral synthesis request before crossing this boundary.
    [[nodiscard]] std::expected<void, std::string> SubmitAudioOneShot(
        const Audio::ProceduralNoiseOneShotRequest& request);

    [[nodiscard]] int ExitCode() const noexcept;
""",
)
replace_once(
    "Engine/Core/Engine.cpp",
    """        timer.Tick();

        // Age only effects inherited from the previous application frame.""",
    """        timer.Tick();
        if (audio != nullptr)
            audio->Update();

        // Age only effects inherited from the previous application frame.""",
)
replace_once(
    "Engine/Core/Engine.cpp",
    """std::expected<void, std::string> Engine::SubmitHapticEffect(const Input::HapticEffectRequest& request)
{
    return impl_->hapticMixer.Submit(request);
}

int Engine::ExitCode() const noexcept
""",
    """std::expected<void, std::string> Engine::SubmitHapticEffect(const Input::HapticEffectRequest& request)
{
    return impl_->hapticMixer.Submit(request);
}

std::expected<void, std::string> Engine::SubmitAudioOneShot(const Audio::ProceduralNoiseOneShotRequest& request)
{
    if (impl_->audio == nullptr || !impl_->audioReady)
        return std::unexpected("audio playback device is unavailable");
    return impl_->audio->SubmitProceduralOneShot(request);
}

int Engine::ExitCode() const noexcept
""",
)

Path("Game/Weapons/P700LaunchAudioPresentation.h").write_text(
    r'''#pragma once

#include "Engine/Audio/ProceduralOneShot.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Armament
{
namespace P700LaunchAudioDetail
{
struct Profile final
{
    float gain = 0.5F;
    float durationSeconds = 1.0F;
    float attackSeconds = 0.01F;
    float releaseSeconds = 0.7F;
    float lowPassCutoffHz = 800.0F;
    float toneFrequencyHz = 60.0F;
    float toneMix = 0.2F;
    float transientMix = 0.2F;
    float referenceDistanceMeters = 150.0F;
    float maximumDistanceMeters = 2'000.0F;
};

[[nodiscard]] inline Profile ProfileFor(const P700LaunchAudioEvent event) noexcept
{
    switch (event)
    {
    case P700LaunchAudioEvent::LauncherOpen:
        return {.gain = 0.30F, .durationSeconds = 0.42F, .attackSeconds = 0.004F, .releaseSeconds = 0.30F,
                .lowPassCutoffHz = 1'200.0F, .toneFrequencyHz = 90.0F, .toneMix = 0.12F,
                .transientMix = 0.42F, .referenceDistanceMeters = 75.0F, .maximumDistanceMeters = 800.0F};
    case P700LaunchAudioEvent::UnderwaterIgnition:
        return {.gain = 0.82F, .durationSeconds = 1.80F, .attackSeconds = 0.008F, .releaseSeconds = 1.30F,
                .lowPassCutoffHz = 190.0F, .toneFrequencyHz = 42.0F, .toneMix = 0.35F,
                .transientMix = 0.16F, .referenceDistanceMeters = 180.0F, .maximumDistanceMeters = 1'800.0F};
    case P700LaunchAudioEvent::UnderwaterPass:
        return {.gain = 0.48F, .durationSeconds = 1.20F, .attackSeconds = 0.020F, .releaseSeconds = 0.90F,
                .lowPassCutoffHz = 150.0F, .toneFrequencyHz = 55.0F, .toneMix = 0.28F,
                .transientMix = 0.06F, .referenceDistanceMeters = 220.0F, .maximumDistanceMeters = 1'400.0F};
    case P700LaunchAudioEvent::SurfaceBreach:
        return {.gain = 0.92F, .durationSeconds = 1.10F, .attackSeconds = 0.003F, .releaseSeconds = 0.80F,
                .lowPassCutoffHz = 2'200.0F, .toneFrequencyHz = 58.0F, .toneMix = 0.08F,
                .transientMix = 0.58F, .referenceDistanceMeters = 220.0F, .maximumDistanceMeters = 2'600.0F};
    case P700LaunchAudioEvent::BoosterAir:
        return {.gain = 0.88F, .durationSeconds = 1.50F, .attackSeconds = 0.006F, .releaseSeconds = 1.10F,
                .lowPassCutoffHz = 900.0F, .toneFrequencyHz = 72.0F, .toneMix = 0.28F,
                .transientMix = 0.18F, .referenceDistanceMeters = 300.0F, .maximumDistanceMeters = 3'400.0F};
    case P700LaunchAudioEvent::WingDeploy:
        return {.gain = 0.35F, .durationSeconds = 0.35F, .attackSeconds = 0.002F, .releaseSeconds = 0.24F,
                .lowPassCutoffHz = 3'400.0F, .toneFrequencyHz = 180.0F, .toneMix = 0.10F,
                .transientMix = 0.55F, .referenceDistanceMeters = 120.0F, .maximumDistanceMeters = 1'200.0F};
    case P700LaunchAudioEvent::BoosterSeparate:
        return {.gain = 0.42F, .durationSeconds = 0.48F, .attackSeconds = 0.002F, .releaseSeconds = 0.32F,
                .lowPassCutoffHz = 2'400.0F, .toneFrequencyHz = 95.0F, .toneMix = 0.08F,
                .transientMix = 0.58F, .referenceDistanceMeters = 150.0F, .maximumDistanceMeters = 1'600.0F};
    case P700LaunchAudioEvent::TurbojetStart:
        return {.gain = 0.62F, .durationSeconds = 1.30F, .attackSeconds = 0.020F, .releaseSeconds = 1.00F,
                .lowPassCutoffHz = 820.0F, .toneFrequencyHz = 86.0F, .toneMix = 0.24F,
                .transientMix = 0.16F, .referenceDistanceMeters = 280.0F, .maximumDistanceMeters = 3'200.0F};
    case P700LaunchAudioEvent::Flyby:
        return {.gain = 0.70F, .durationSeconds = 0.85F, .attackSeconds = 0.010F, .releaseSeconds = 0.62F,
                .lowPassCutoffHz = 1'800.0F, .toneFrequencyHz = 110.0F, .toneMix = 0.07F,
                .transientMix = 0.40F, .referenceDistanceMeters = 420.0F, .maximumDistanceMeters = 4'500.0F};
    }
    return {};
}

[[nodiscard]] inline bool FinitePosition(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

[[nodiscard]] inline float DistanceMeters(
    const std::array<float, 3>& first, const std::array<float, 3>& second) noexcept
{
    const double dx = static_cast<double>(second[0]) - first[0];
    const double dy = static_cast<double>(second[1]) - first[1];
    const double dz = static_cast<double>(second[2]) - first[2];
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}
} // namespace P700LaunchAudioDetail

// Game-owned semantic mapping into the generic non-spatialized one-shot synthesizer. Distance is intentionally
// converted to gain here because the current Engine/Audio primitive has no world semantics. A later spatial
// backend can replace this adapter without changing weapon lifecycle authority.
[[nodiscard]] inline std::expected<std::optional<Audio::ProceduralNoiseOneShotRequest>, std::string>
BuildP700LaunchAudioOneShotRequest(
    const P700LaunchAudioHook& hook,
    const std::array<float, 3>& listenerWorldPositionMeters)
{
    if (!P700LaunchAudioDetail::FinitePosition(hook.worldPositionMeters) ||
        !P700LaunchAudioDetail::FinitePosition(listenerWorldPositionMeters))
        return std::unexpected("P-700 launch audio received a non-finite world position");

    const auto profile = P700LaunchAudioDetail::ProfileFor(hook.event);
    const float distanceMeters = P700LaunchAudioDetail::DistanceMeters(
        hook.worldPositionMeters, listenerWorldPositionMeters);
    if (!std::isfinite(distanceMeters))
        return std::unexpected("P-700 launch audio derived a non-finite listener distance");
    if (distanceMeters >= profile.maximumDistanceMeters)
        return std::optional<Audio::ProceduralNoiseOneShotRequest>{};

    const float normalizedDistance = distanceMeters / profile.referenceDistanceMeters;
    const float distanceGain = 1.0F / std::sqrt(1.0F + normalizedDistance * normalizedDistance);
    const float mediumGain = hook.underwater ? 0.88F : 1.0F;
    const float cutoffHz = hook.underwater
        ? (std::min)(profile.lowPassCutoffHz, 520.0F)
        : profile.lowPassCutoffHz;
    const std::uint64_t semanticSalt =
        (static_cast<std::uint64_t>(hook.event) + 1ULL) * 0x9E3779B97F4A7C15ULL;

    Audio::ProceduralNoiseOneShotRequest request{
        .seed = hook.launchSeed ^ semanticSalt,
        .gain = std::clamp(profile.gain * distanceGain * mediumGain, 0.0F, 1.0F),
        .durationSeconds = profile.durationSeconds,
        .attackSeconds = profile.attackSeconds,
        .releaseSeconds = profile.releaseSeconds,
        .lowPassCutoffHz = cutoffHz,
        .toneFrequencyHz = profile.toneFrequencyHz,
        .toneMix = profile.toneMix,
        .transientMix = profile.transientMix};
    if (const auto valid = Audio::ValidateProceduralNoiseOneShotRequest(request, 48'000U); !valid)
        return std::unexpected("P-700 launch audio mapping produced invalid synthesis controls: " + valid.error());
    return std::optional<Audio::ProceduralNoiseOneShotRequest>{request};
}
} // namespace DeepRun::Game::Armament
''',
    encoding="utf-8",
)

# Keep post-breach surface relaxation anchored at the actual crossing point instead of following the missile.
replace_once(
    "Game/Weapons/P700SurfacePresentation.h",
    """        if (!missile->positionMeters.IsFinite() || !std::isfinite(missile->surfaceLevelYMeters) ||
            !std::isfinite(missile->phaseStartTimeSeconds) || simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 surface presentation observed invalid missile state");
""",
    """        if (!missile->positionMeters.IsFinite() || !missile->launchForwardUnitVector.IsFinite() ||
            !std::isfinite(missile->surfaceLevelYMeters) || !std::isfinite(missile->speedMetersPerSecond) ||
            missile->speedMetersPerSecond < 0.0F || !std::isfinite(missile->phaseStartTimeSeconds) ||
            simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 surface presentation observed invalid missile state");
""",
)
replace_once(
    "Game/Weapons/P700SurfacePresentation.h",
    """                const float normalizedAge = std::clamp(age / tuning.breachSurfaceRelaxationSeconds, 0.0F, 1.0F);
                const float retainedBulge = tuning.preBreachBulgeMaximumMeters * (1.0F - normalizedAge);
                const float collapsingShoulder = tuning.breachSurfaceCollapseMeters * std::sin(Pi * normalizedAge);
                const float amplitude = retainedBulge - collapsingShoulder;
                if (std::abs(amplitude) > 0.01F)
                {
                    candidates.push_back({
                        .centerX = missile->positionMeters.x,
""",
    """                const float normalizedAge = std::clamp(age / tuning.breachSurfaceRelaxationSeconds, 0.0F, 1.0F);
                const float retainedBulge = tuning.preBreachBulgeMaximumMeters * (1.0F - normalizedAge);
                const float collapsingShoulder = tuning.breachSurfaceCollapseMeters * std::sin(Pi * normalizedAge);
                const float amplitude = retainedBulge - collapsingShoulder;
                const float breachCenterX = missile->positionMeters.x -
                    missile->launchForwardUnitVector.x * missile->speedMetersPerSecond * age;
                if (std::abs(amplitude) > 0.01F)
                {
                    candidates.push_back({
                        .centerX = breachCenterX,
""",
)

# Wire edge-triggered semantic hooks returned by CombatPlaygroundView into Engine's generic audio service.
replace_once(
    "DeepRun/Main.cpp",
    '#include "Game/Haptics/HapticFeedbackSystem.h"\n',
    '#include "Game/Haptics/HapticFeedbackSystem.h"\n#include "Game/Weapons/P700LaunchAudioPresentation.h"\n',
)
replace_once(
    "DeepRun/Main.cpp",
    """        bool loggedCombatRuntime = false;
        bool loggedCombatImpact = false;
        DeepRun::Core::Engine* engineServices = nullptr;
""",
    """        bool loggedCombatRuntime = false;
        bool loggedCombatImpact = false;
        bool loggedP700AudioSubmissionFailure = false;
        DeepRun::Core::Engine* engineServices = nullptr;
""",
)
replace_once(
    "DeepRun/Main.cpp",
    """             &inputState, &frameCapture, &captureEnabled, &options,
             &renderFrames, &engineServices](DeepRun::Render::D3D12Renderer& renderer)
""",
    """             &inputState, &frameCapture, &captureEnabled, &options,
             &renderFrames, &engineServices, &loggedP700AudioSubmissionFailure](DeepRun::Render::D3D12Renderer& renderer)
""",
)
replace_once(
    "DeepRun/Main.cpp",
    """                    if (!combatRendered)
                    {
                        std::cerr << "[Game][ERROR] " << combatRendered.error() << '\\n';
                        return false;
                    }
                    // CombatPlaygroundView owns asset-specific draw validation.""",
    """                    if (!combatRendered)
                    {
                        std::cerr << "[Game][ERROR] " << combatRendered.error() << '\\n';
                        return false;
                    }
                    const std::array<float, 3> audioListenerPosition{
                        camera->target.x, camera->target.y, camera->target.z};
                    for (const auto& hook : combatRendered->p700Vfx.audioHooks)
                    {
                        const auto request = DeepRun::Game::Armament::BuildP700LaunchAudioOneShotRequest(
                            hook, audioListenerPosition);
                        if (!request)
                        {
                            std::cerr << "[Game][ERROR] P-700 launch audio mapping failed: " << request.error() << '\\n';
                            return false;
                        }
                        if (!request->has_value())
                            continue;
                        const auto submitted = engineServices->SubmitAudioOneShot(**request);
                        if (!submitted && !loggedP700AudioSubmissionFailure)
                        {
                            loggedP700AudioSubmissionFailure = true;
                            std::cerr << "[Game][WARN] P-700 launch audio suppressed: " << submitted.error() << '\\n';
                        }
                    }
                    // CombatPlaygroundView owns asset-specific draw validation.""",
)

# Regression coverage for semantic audio mapping and fixed breach anchoring.
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    '#include "Game/Weapons/P700LaunchVfx.h"\n',
    '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Game/Weapons/P700LaunchAudioPresentation.h"\n',
)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    """[[nodiscard]] bool RunSurfaceDisturbanceChecks()
{""",
    """[[nodiscard]] bool RunAudioMappingChecks()
{
    constexpr std::array<P700LaunchAudioEvent, 9> Events{
        P700LaunchAudioEvent::LauncherOpen,
        P700LaunchAudioEvent::UnderwaterIgnition,
        P700LaunchAudioEvent::UnderwaterPass,
        P700LaunchAudioEvent::SurfaceBreach,
        P700LaunchAudioEvent::BoosterAir,
        P700LaunchAudioEvent::WingDeploy,
        P700LaunchAudioEvent::BoosterSeparate,
        P700LaunchAudioEvent::TurbojetStart,
        P700LaunchAudioEvent::Flyby};
    const std::array<float, 3> listener{0.0F, -30.0F, 0.0F};
    float underwaterCutoff = 0.0F;
    float breachCutoff = 0.0F;
    for (const auto event : Events)
    {
        const bool underwater = event == P700LaunchAudioEvent::LauncherOpen ||
            event == P700LaunchAudioEvent::UnderwaterIgnition || event == P700LaunchAudioEvent::UnderwaterPass;
        const DeepRun::Game::Armament::P700LaunchAudioHook hook{
            .event = event,
            .launchSeed = 0x700700ULL,
            .worldPositionMeters = {15.0F, underwater ? -25.0F : 0.0F, 0.0F},
            .underwater = underwater};
        const auto request = DeepRun::Game::Armament::BuildP700LaunchAudioOneShotRequest(hook, listener);
        if (!request || !request->has_value() ||
            !DeepRun::Audio::ValidateProceduralNoiseOneShotRequest(**request, 48'000U))
        {
            std::cerr << "P-700 launch audio event did not map to valid generic synthesis controls\\n";
            return false;
        }
        if (event == P700LaunchAudioEvent::UnderwaterIgnition) underwaterCutoff = (**request).lowPassCutoffHz;
        if (event == P700LaunchAudioEvent::SurfaceBreach) breachCutoff = (**request).lowPassCutoffHz;
    }
    if (!(underwaterCutoff > 0.0F && breachCutoff > underwaterCutoff))
    {
        std::cerr << "P-700 underwater launch audio is not spectrally restrained relative to breach\\n";
        return false;
    }
    const DeepRun::Game::Armament::P700LaunchAudioHook distant{
        .event = P700LaunchAudioEvent::Flyby,
        .launchSeed = 99U,
        .worldPositionMeters = {10'000.0F, 0.0F, 0.0F},
        .underwater = false};
    const auto culled = DeepRun::Game::Armament::BuildP700LaunchAudioOneShotRequest(distant, listener);
    if (!culled || culled->has_value())
    {
        std::cerr << "P-700 launch audio did not cull an inaudible distant one-shot\\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunSurfaceDisturbanceChecks()
{""",
)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    """    missile.phase = P700GranitPhase::WaterExit;
    missile.positionMeters.y = 0.2F;
    missile.phaseStartTimeSeconds = 1.20;
    const auto relaxation = DeepRun::Game::Armament::BuildP700SurfaceDisturbances(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), tuning, 1.70);
    if (!relaxation || relaxation->empty() ||
        relaxation->front().radiusMeters <= preBreach->front().radiusMeters)
""",
    """    const float breachCenterX = missile.positionMeters.x;
    missile.phase = P700GranitPhase::WaterExit;
    missile.positionMeters.y = 0.2F;
    missile.phaseStartTimeSeconds = 1.20;
    missile.speedMetersPerSecond = 100.0F;
    const float waterExitAgeSeconds = 0.50F;
    missile.positionMeters.x = breachCenterX +
        missile.launchForwardUnitVector.x * missile.speedMetersPerSecond * waterExitAgeSeconds;
    const auto relaxation = DeepRun::Game::Armament::BuildP700SurfaceDisturbances(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), tuning, 1.70);
    if (!relaxation || relaxation->empty() ||
        std::abs(relaxation->front().centerX - breachCenterX) > 1.0e-3F ||
        relaxation->front().radiusMeters <= preBreach->front().radiusMeters)
""",
)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    """    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||
""",
    """    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunAudioMappingChecks() ||
        !RunSurfaceDisturbanceChecks() ||
""",
)
