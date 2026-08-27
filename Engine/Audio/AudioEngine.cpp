#include "Engine/Audio/AudioEngine.h"

#include "Engine/Diagnostics/Logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <memory>
#include <string>

namespace DeepRun::Audio
{
class AudioEngine::Impl final
{
public:
    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger)
    {
    }

    ~Impl()
    {
        if (initialized)
        {
            ma_engine_uninit(&engine);
            logger.Info(Diagnostics::LogCategory::Audio, "miniaudio shut down");
        }
    }

    Diagnostics::Logger& logger;
    ma_engine engine{};
    std::string status = "not initialized";
    bool initialized = false;
};

AudioEngine::AudioEngine(Diagnostics::Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::Initialize()
{
    if (impl_->initialized)
    {
        return true;
    }

    const ma_result result = ma_engine_init(nullptr, &impl_->engine);
    if (result != MA_SUCCESS)
    {
        impl_->status = ma_result_description(result);
        impl_->logger.Warning(
            Diagnostics::LogCategory::Audio,
            std::string("miniaudio device unavailable: ") + impl_->status);
        return false;
    }

    impl_->initialized = true;
    impl_->status = "default playback device ready";
    impl_->logger.Info(Diagnostics::LogCategory::Audio, "miniaudio initialized");
    return true;
}

bool AudioEngine::IsInitialized() const noexcept
{
    return impl_->initialized;
}

std::string_view AudioEngine::Status() const noexcept
{
    return impl_->status;
}
}
