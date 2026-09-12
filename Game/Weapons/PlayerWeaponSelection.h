#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace DeepRun::Game::Armament
{
enum class PlayerWeaponType
{
    HeavyweightTorpedo,
    P700Granit,
};

inline constexpr std::array<PlayerWeaponType, 2> PlayerWeaponCycle{
    PlayerWeaponType::HeavyweightTorpedo,
    PlayerWeaponType::P700Granit};

[[nodiscard]] constexpr std::string_view PlayerWeaponName(const PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return "USET-80";
    case PlayerWeaponType::P700Granit: return "P-700 GRANIT";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr PlayerWeaponType CyclePlayerWeapon(
    const PlayerWeaponType current,
    const int direction) noexcept
{
    std::size_t currentIndex = 0U;
    for (std::size_t index = 0; index < PlayerWeaponCycle.size(); ++index)
    {
        if (PlayerWeaponCycle[index] == current)
        {
            currentIndex = index;
            break;
        }
    }
    if (direction < 0)
    {
        return PlayerWeaponCycle[(currentIndex + PlayerWeaponCycle.size() - 1U) % PlayerWeaponCycle.size()];
    }
    return PlayerWeaponCycle[(currentIndex + 1U) % PlayerWeaponCycle.size()];
}
} // namespace DeepRun::Game::Armament
