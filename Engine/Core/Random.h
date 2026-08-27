#pragma once

#include <cstdint>
#include <random>

namespace DeepRun::Core
{
class Random final
{
public:
    explicit Random(std::uint32_t seed);
    [[nodiscard]] std::uint32_t NextUInt();

private:
    std::mt19937 generator_;
};
}
