#pragma once

#include "Engine/Audio/ProceduralOneShot.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Audio
{
class AudioEngine final
{
public:
    explicit AudioEngine(Diagnostics::Logger& logger);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::string_view Status() const noexcept;

    // Generic non-spatialized presentation one-shot. Semantic meaning (thunder, UI, machinery, etc.) remains
    // above Engine/Audio. Failure is reported to the caller and must never participate in simulation rollback.
    [[nodiscard]] std::expected<void, std::string> SubmitProceduralOneShot(
        const ProceduralNoiseOneShotRequest& request);
    // Reclaims completed miniaudio voices. Safe to call every presentation frame; no simulation state is touched.
    void Update() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace DeepRun::Audio
