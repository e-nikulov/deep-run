#pragma once

#include <cstdint>
#include <limits>

namespace DeepRun::Scene
{
struct Entity final
{
    static constexpr std::uint32_t InvalidValue = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t value = InvalidValue;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return value != InvalidValue;
    }

    [[nodiscard]] constexpr bool operator==(const Entity&) const noexcept = default;
};
}
