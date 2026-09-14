#pragma once

#include "Game/Weapons/AnteyOrdnanceMass.h"

#include <cstddef>
#include <expected>
#include <string>

namespace DeepRun::Game::Armament
{
// Finite Project 949A torpedo-ammunition pools. RussianShips gives 18 rounds for the four 533 mm tubes and
// 10 rounds for the two 650 mm tubes. Deep Run currently maps USET-80 to the 533 mm pool and 65-76A
// FAST/ECONOMY to the shared 650 mm pool; additional historical weapon types can consume these same pools later.
class AnteyTorpedoInventory final
{
public:
    [[nodiscard]] std::size_t LoadedCount(const PlayerWeaponType weapon) const noexcept
    {
        switch (weapon)
        {
        case PlayerWeaponType::HeavyweightTorpedo: return calibre533Loaded_;
        case PlayerWeaponType::Type6576AFast:
        case PlayerWeaponType::Type6576AEconomy: return calibre650Loaded_;
        case PlayerWeaponType::P700Granit: return 0U;
        }
        return 0U;
    }

    [[nodiscard]] float ExpendedMassKg() const noexcept
    {
        return static_cast<float>(AnteyConfigured533MmRounds - calibre533Loaded_) * Uset80RoundMassKg +
               static_cast<float>(AnteyConfigured650MmRounds - calibre650Loaded_) * Type6576ARoundMassKg;
    }

    [[nodiscard]] std::expected<float, std::string> Consume(const PlayerWeaponType weapon)
    {
        switch (weapon)
        {
        case PlayerWeaponType::HeavyweightTorpedo:
            if (calibre533Loaded_ == 0U)
                return std::unexpected("533 mm Project 949A ammunition pool is exhausted");
            --calibre533Loaded_;
            return Uset80RoundMassKg;
        case PlayerWeaponType::Type6576AFast:
        case PlayerWeaponType::Type6576AEconomy:
            if (calibre650Loaded_ == 0U)
                return std::unexpected("650 mm Project 949A ammunition pool is exhausted");
            --calibre650Loaded_;
            return Type6576ARoundMassKg;
        case PlayerWeaponType::P700Granit:
            return std::unexpected("P-700 is not a torpedo-inventory item");
        }
        return std::unexpected("unknown player torpedo inventory item");
    }

private:
    std::size_t calibre533Loaded_ = AnteyConfigured533MmRounds;
    std::size_t calibre650Loaded_ = AnteyConfigured650MmRounds;
};
} // namespace DeepRun::Game::Armament
