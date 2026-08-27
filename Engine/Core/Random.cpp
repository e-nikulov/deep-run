#include "Engine/Core/Random.h"

namespace DeepRun::Core
{
Random::Random(const std::uint32_t seed)
    : generator_(seed)
{
}

std::uint32_t Random::NextUInt()
{
    return generator_();
}
}
