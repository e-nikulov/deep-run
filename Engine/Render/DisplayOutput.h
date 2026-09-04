#pragma once

#include <cstdint>

namespace DeepRun::Render
{
// Presentation-only output selection. Game and Simulation never consume this state.
enum class DisplayOutputMode
{
    Sdr,
    HdrScRgb,
};

struct DisplayOutputCapabilities final
{
    bool output6Available = false;
    bool hdrActive = false;
    bool scRgbPresentSupported = false;
    std::uint32_t bitsPerColor = 0;
    float minLuminanceNits = 0.0F;
    float maxLuminanceNits = 0.0F;
    float maxFullFrameLuminanceNits = 0.0F;
};

enum class DisplayOutputFallbackReason
{
    None,
    HdrNotRequested,
    Output6Unavailable,
    HdrInactive,
    ScRgbPresentUnsupported,
};

struct DisplayOutputSelection final
{
    DisplayOutputMode mode = DisplayOutputMode::Sdr;
    DisplayOutputFallbackReason fallbackReason = DisplayOutputFallbackReason::HdrNotRequested;
};

[[nodiscard]] constexpr DisplayOutputSelection SelectDisplayOutputMode(
    const bool hdrRequested,
    const DisplayOutputCapabilities& capabilities) noexcept
{
    if (!hdrRequested)
    {
        return {.mode = DisplayOutputMode::Sdr, .fallbackReason = DisplayOutputFallbackReason::HdrNotRequested};
    }
    if (!capabilities.output6Available)
    {
        return {.mode = DisplayOutputMode::Sdr, .fallbackReason = DisplayOutputFallbackReason::Output6Unavailable};
    }
    if (!capabilities.hdrActive)
    {
        return {.mode = DisplayOutputMode::Sdr, .fallbackReason = DisplayOutputFallbackReason::HdrInactive};
    }
    if (!capabilities.scRgbPresentSupported)
    {
        return {.mode = DisplayOutputMode::Sdr,
                .fallbackReason = DisplayOutputFallbackReason::ScRgbPresentUnsupported};
    }
    return {.mode = DisplayOutputMode::HdrScRgb, .fallbackReason = DisplayOutputFallbackReason::None};
}
}
