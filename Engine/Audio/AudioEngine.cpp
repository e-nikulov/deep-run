#include "Engine/Audio/AudioEngine.h"

#include "Engine/Diagnostics/Logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Audio
{
class AudioEngine::Impl final
{
public:
    struct ActiveOneShot final
    {
        ~ActiveOneShot()
        {
            if (soundInitialized)
                ma_sound_uninit(&sound);
            if (bufferInitialized)
                ma_audio_buffer_uninit(&buffer);
        }

        ActiveOneShot() = default;
        ActiveOneShot(const ActiveOneShot&) = delete;
        ActiveOneShot& operator=(const ActiveOneShot&) = delete;

        ProceduralOneShotBuffer generated{};
        ma_audio_buffer buffer{};
        ma_sound sound{};
        bool bufferInitialized = false;
        bool soundInitialized = false;
    };

    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger)
    {
    }

    ~Impl()
    {
        // Voices/data sources must be detached while the engine graph is still alive.
        activeOneShots.clear();
        if (initialized)
        {
            ma_engine_uninit(&engine);
            logger.Info(Diagnostics::LogCategory::Audio, "miniaudio shut down");
        }
    }

    void ReclaimFinishedOneShots() noexcept
    {
        activeOneShots.erase(
            std::remove_if(
                activeOneShots.begin(), activeOneShots.end(),
                [](const std::unique_ptr<ActiveOneShot>& shot)
                {
                    return shot == nullptr || !shot->soundInitialized || ma_sound_at_end(&shot->sound) == MA_TRUE;
                }),
            activeOneShots.end());
    }

    Diagnostics::Logger& logger;
    ma_engine engine{};
    std::vector<std::unique_ptr<ActiveOneShot>> activeOneShots{};
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

std::expected<void, std::string> AudioEngine::SubmitProceduralOneShot(
    const ProceduralNoiseOneShotRequest& request)
{
    if (!impl_->initialized)
        return std::unexpected("audio playback device is unavailable");

    impl_->ReclaimFinishedOneShots();
    const std::uint32_t sampleRate = ma_engine_get_sample_rate(&impl_->engine);
    const auto generated = GenerateProceduralNoiseOneShot(request, sampleRate);
    if (!generated)
        return std::unexpected(generated.error());

    auto active = std::make_unique<Impl::ActiveOneShot>();
    active->generated = *generated;
    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
        ma_format_f32,
        1U,
        static_cast<ma_uint64>(active->generated.monoSamples.size()),
        active->generated.monoSamples.data(),
        nullptr);
    bufferConfig.sampleRate = sampleRate;
    const ma_result bufferResult = ma_audio_buffer_init(&bufferConfig, &active->buffer);
    if (bufferResult != MA_SUCCESS)
    {
        return std::unexpected(
            std::string("miniaudio procedural buffer initialization failed: ") +
            ma_result_description(bufferResult));
    }
    active->bufferInitialized = true;

    const ma_result soundResult = ma_sound_init_from_data_source(
        &impl_->engine,
        &active->buffer,
        MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        &active->sound);
    if (soundResult != MA_SUCCESS)
    {
        return std::unexpected(
            std::string("miniaudio procedural sound initialization failed: ") +
            ma_result_description(soundResult));
    }
    active->soundInitialized = true;

    const ma_result startResult = ma_sound_start(&active->sound);
    if (startResult != MA_SUCCESS)
    {
        return std::unexpected(
            std::string("miniaudio procedural sound start failed: ") + ma_result_description(startResult));
    }

    impl_->activeOneShots.push_back(std::move(active));
    return {};
}

void AudioEngine::Update() noexcept
{
    if (impl_->initialized)
        impl_->ReclaimFinishedOneShots();
}
} // namespace DeepRun::Audio
