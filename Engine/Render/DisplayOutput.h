#pragma once

#include <cstdint>

namespace DeepRun::Render
{
// Presentation-only output selection. Game and Simulation never consume this state.
enum class DisplayOutputMode
{
    Sdr,
    Sdr10Bit,
    HdrScRgb,
};

struct DisplayOutputCapabilities final
{
    bool output6Available = false;
    bool hdrActive = false;
    bool scRgbPresentSupported = false;
    bool sdr10BitPresentSupported = false;
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

[[nodiscard]] constexpr bool IsSdrOutputMode(const DisplayOutputMode mode) noexcept
{
    return mode == DisplayOutputMode::Sdr || mode == DisplayOutputMode::Sdr10Bit;
}

[[nodiscard]] constexpr DisplayOutputSelection SelectDisplayOutputMode(
    const bool hdrRequested,
    const DisplayOutputCapabilities& capabilities) noexcept
{
    const DisplayOutputMode sdrMode = capabilities.sdr10BitPresentSupported
        ? DisplayOutputMode::Sdr10Bit
        : DisplayOutputMode::Sdr;

    if (!hdrRequested)
    {
        return {.mode = sdrMode, .fallbackReason = DisplayOutputFallbackReason::HdrNotRequested};
    }
    if (!capabilities.output6Available)
    {
        return {.mode = sdrMode, .fallbackReason = DisplayOutputFallbackReason::Output6Unavailable};
    }
    if (!capabilities.hdrActive)
    {
        return {.mode = sdrMode, .fallbackReason = DisplayOutputFallbackReason::HdrInactive};
    }
    if (!capabilities.scRgbPresentSupported)
    {
        return {.mode = sdrMode,
                .fallbackReason = DisplayOutputFallbackReason::ScRgbPresentUnsupported};
    }
    return {.mode = DisplayOutputMode::HdrScRgb, .fallbackReason = DisplayOutputFallbackReason::None};
}
}
