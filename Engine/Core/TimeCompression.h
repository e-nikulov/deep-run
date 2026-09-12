#pragma once

#include <cstdint>
#include <string_view>

namespace DeepRun::Core
{
enum class TimeCompressionRate : std::uint8_t
{
    X1 = 0,
    X2,
    X4,
    X8,
};

[[nodiscard]] constexpr bool IsValidTimeCompressionRate(const TimeCompressionRate rate) noexcept
{
    switch (rate)
    {
    case TimeCompressionRate::X1:
    case TimeCompressionRate::X2:
    case TimeCompressionRate::X4:
    case TimeCompressionRate::X8:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr double TimeCompressionMultiplier(const TimeCompressionRate rate) noexcept
{
    switch (rate)
    {
    case TimeCompressionRate::X1: return 1.0;
    case TimeCompressionRate::X2: return 2.0;
    case TimeCompressionRate::X4: return 4.0;
    case TimeCompressionRate::X8: return 8.0;
    }
    return 1.0;
}

[[nodiscard]] constexpr std::string_view TimeCompressionLabel(const TimeCompressionRate rate) noexcept
{
    switch (rate)
    {
    case TimeCompressionRate::X1: return "1x";
    case TimeCompressionRate::X2: return "2x";
    case TimeCompressionRate::X4: return "4x";
    case TimeCompressionRate::X8: return "8x";
    }
    return "1x";
}

[[nodiscard]] constexpr TimeCompressionRate IncreaseTimeCompressionRate(const TimeCompressionRate rate) noexcept
{
    switch (rate)
    {
    case TimeCompressionRate::X1: return TimeCompressionRate::X2;
    case TimeCompressionRate::X2: return TimeCompressionRate::X4;
    case TimeCompressionRate::X4: return TimeCompressionRate::X8;
    case TimeCompressionRate::X8: return TimeCompressionRate::X8;
    }
    return TimeCompressionRate::X1;
}

[[nodiscard]] constexpr TimeCompressionRate DecreaseTimeCompressionRate(const TimeCompressionRate rate) noexcept
{
    switch (rate)
    {
    case TimeCompressionRate::X1: return TimeCompressionRate::X1;
    case TimeCompressionRate::X2: return TimeCompressionRate::X1;
    case TimeCompressionRate::X4: return TimeCompressionRate::X2;
    case TimeCompressionRate::X8: return TimeCompressionRate::X4;
    }
    return TimeCompressionRate::X1;
}

class TimeCompressionController final
{
public:
    void Reset() noexcept
    {
        requestedRate_ = TimeCompressionRate::X1;
        maximumRate_ = TimeCompressionRate::X8;
    }

    [[nodiscard]] bool SetRequestedRate(const TimeCompressionRate rate) noexcept
    {
        if (!IsValidTimeCompressionRate(rate))
        {
            return false;
        }
        requestedRate_ = rate;
        return true;
    }

    [[nodiscard]] bool SetMaximumRate(const TimeCompressionRate rate) noexcept
    {
        if (!IsValidTimeCompressionRate(rate))
        {
            return false;
        }
        maximumRate_ = rate;
        return true;
    }

    void IncreaseRequestedRate() noexcept
    {
        requestedRate_ = IncreaseTimeCompressionRate(requestedRate_);
    }

    void DecreaseRequestedRate() noexcept
    {
        requestedRate_ = DecreaseTimeCompressionRate(requestedRate_);
    }

    void BreakToRealtime() noexcept
    {
        requestedRate_ = TimeCompressionRate::X1;
    }

    [[nodiscard]] TimeCompressionRate RequestedRate() const noexcept
    {
        return requestedRate_;
    }

    [[nodiscard]] TimeCompressionRate MaximumRate() const noexcept
    {
        return maximumRate_;
    }

    [[nodiscard]] TimeCompressionRate EffectiveRate() const noexcept
    {
        return static_cast<std::uint8_t>(requestedRate_) <= static_cast<std::uint8_t>(maximumRate_)
                   ? requestedRate_
                   : maximumRate_;
    }

    [[nodiscard]] double EffectiveMultiplier() const noexcept
    {
        return TimeCompressionMultiplier(EffectiveRate());
    }

private:
    TimeCompressionRate requestedRate_ = TimeCompressionRate::X1;
    TimeCompressionRate maximumRate_ = TimeCompressionRate::X8;
};

static_assert(TimeCompressionMultiplier(TimeCompressionRate::X1) == 1.0);
static_assert(TimeCompressionMultiplier(TimeCompressionRate::X8) == 8.0);
static_assert(IncreaseTimeCompressionRate(TimeCompressionRate::X8) == TimeCompressionRate::X8);
static_assert(DecreaseTimeCompressionRate(TimeCompressionRate::X1) == TimeCompressionRate::X1);
}
