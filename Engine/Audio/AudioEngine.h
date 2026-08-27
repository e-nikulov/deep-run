#pragma once

#include <memory>
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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
