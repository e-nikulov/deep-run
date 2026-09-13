#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace DeepRun::Game::Armament
{
enum class PlayerWeaponType
{
    HeavyweightTorpedo,
    Type6576AFast,
    Type6576AEconomy,
    P700Granit,
};

inline constexpr std::array<PlayerWeaponType, 4> PlayerWeaponCycle{
    PlayerWeaponType::HeavyweightTorpedo,
    PlayerWeaponType::Type6576AFast,
    PlayerWeaponType::Type6576AEconomy,
    PlayerWeaponType::P700Granit};

[[nodiscard]] constexpr bool IsPlayerTorpedo(const PlayerWeaponType weapon) noexcept
{
    return weapon == PlayerWeaponType::HeavyweightTorpedo ||
           weapon == PlayerWeaponType::Type6576AFast ||
           weapon == PlayerWeaponType::Type6576AEconomy;
}

[[nodiscard]] constexpr std::string_view PlayerWeaponName(const PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return "USET-80";
    case PlayerWeaponType::Type6576AFast: return "65-76A FAST";
    case PlayerWeaponType::Type6576AEconomy: return "65-76A ECONOMY";
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
