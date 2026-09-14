#pragma once

#include "Game/Weapons/PlayerWeaponSelection.h"

#include <cstddef>

namespace DeepRun::Game::Armament
{
// Project 949A source-backed inventory geometry/counts:
//   24 x P-700 launchers, 18 x 533 mm ammunition, 10 x 650 mm ammunition (RussianShips).
// The public source lists a MIX of weapon types inside each torpedo-calibre pool (533 mm includes USET-80,
// VA-111 and URPK-6; 650 mm includes 65-73, 65-76A and URPK-7). Deep Run currently exposes USET-80 and
// 65-76A as the playable representatives, so their per-round masses are GAME mass-policy approximations,
// not a claim that every historical load consisted solely of those two torpedo models.
inline constexpr float P700GranitRoundMassKg = 7'000.0F;
inline constexpr float Uset80RoundMassKg = 2'000.0F;
inline constexpr float Type6576ARoundMassKg = 4'500.0F;

inline constexpr std::size_t AnteyConfiguredP700Rounds = 24U;
inline constexpr std::size_t AnteyConfigured533MmRounds = 18U;
inline constexpr std::size_t AnteyConfigured650MmRounds = 10U;

// Compatibility aliases for the currently playable representatives of each physical calibre pool.
inline constexpr std::size_t AnteyConfiguredUset80Rounds = AnteyConfigured533MmRounds;
inline constexpr std::size_t AnteyConfiguredType6576ARounds = AnteyConfigured650MmRounds;

inline constexpr float AnteyConfiguredCombatOrdnanceMassKg =
    P700GranitRoundMassKg * static_cast<float>(AnteyConfiguredP700Rounds) +
    Uset80RoundMassKg * static_cast<float>(AnteyConfigured533MmRounds) +
    Type6576ARoundMassKg * static_cast<float>(AnteyConfigured650MmRounds);

[[nodiscard]] constexpr float PlayerTorpedoRoundMassKg(const PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return Uset80RoundMassKg;
    case PlayerWeaponType::Type6576AFast:
    case PlayerWeaponType::Type6576AEconomy: return Type6576ARoundMassKg;
    case PlayerWeaponType::P700Granit: return 0.0F;
    }
    return 0.0F;
}

static_assert(AnteyConfiguredP700Rounds == 24U);
static_assert(AnteyConfigured533MmRounds == 18U);
static_assert(AnteyConfigured650MmRounds == 10U);
static_assert(AnteyConfiguredCombatOrdnanceMassKg == 249'000.0F);
} // namespace DeepRun::Game::Armament
