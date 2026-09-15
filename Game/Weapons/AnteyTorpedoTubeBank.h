#pragma once

#include "Game/Weapons/PlayerWeaponSelection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <string>

namespace DeepRun::Game::Armament
{
// Project 949A production/gameplay topology: four 533 mm bow tubes and two 650 mm bow tubes.
// Open sources confirm automated rapid loading but do not provide a trustworthy per-tube cycle time.
// The reload durations below are explicit GAME POLICY chosen to preserve the public "several minutes for the
// whole torpedo load" scale while creating a meaningful tactical window after the initially loaded tubes fire.
inline constexpr std::size_t Antey533MmTorpedoTubeCount = 4U;
inline constexpr std::size_t Antey650MmTorpedoTubeCount = 2U;
inline constexpr std::size_t AnteyTorpedoTubeCount = Antey533MmTorpedoTubeCount + Antey650MmTorpedoTubeCount;
inline constexpr double Antey533MmTorpedoTubeReloadSeconds = 45.0;
inline constexpr double Antey650MmTorpedoTubeReloadSeconds = 60.0;

enum class AnteyTorpedoTubeCalibre
{
    Mm533,
    Mm650,
};

struct AnteyTorpedoTubeRuntime final
{
    AnteyTorpedoTubeCalibre calibre = AnteyTorpedoTubeCalibre::Mm533;
    double nextReadyTimeSeconds = 0.0;
};

struct AnteyTorpedoTubeLaunch final
{
    std::size_t tubeIndex = 0U;
    AnteyTorpedoTubeCalibre calibre = AnteyTorpedoTubeCalibre::Mm533;
    double nextReadyTimeSeconds = 0.0;
};

[[nodiscard]] inline std::optional<AnteyTorpedoTubeCalibre> AnteyTorpedoTubeCalibreForWeapon(
    const PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case PlayerWeaponType::HeavyweightTorpedo: return AnteyTorpedoTubeCalibre::Mm533;
    case PlayerWeaponType::Type6576AFast:
    case PlayerWeaponType::Type6576AEconomy: return AnteyTorpedoTubeCalibre::Mm650;
    case PlayerWeaponType::P700Granit: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] inline double AnteyTorpedoTubeReloadSeconds(const AnteyTorpedoTubeCalibre calibre) noexcept
{
    return calibre == AnteyTorpedoTubeCalibre::Mm533
        ? Antey533MmTorpedoTubeReloadSeconds
        : Antey650MmTorpedoTubeReloadSeconds;
}

class AnteyTorpedoTubeBank final
{
public:
    AnteyTorpedoTubeBank() noexcept
    {
        for (std::size_t index = 0U; index < tubes_.size(); ++index)
        {
            tubes_[index].calibre = index < Antey533MmTorpedoTubeCount
                ? AnteyTorpedoTubeCalibre::Mm533
                : AnteyTorpedoTubeCalibre::Mm650;
        }
    }

    [[nodiscard]] const std::array<AnteyTorpedoTubeRuntime, AnteyTorpedoTubeCount>& Tubes() const noexcept
    {
        return tubes_;
    }

    [[nodiscard]] std::size_t ReadyTubeCount(
        const PlayerWeaponType weapon,
        const double simulationTimeSeconds) const noexcept
    {
        const auto calibre = AnteyTorpedoTubeCalibreForWeapon(weapon);
        if (!calibre || !std::isfinite(simulationTimeSeconds))
            return 0U;
        return static_cast<std::size_t>(std::count_if(tubes_.begin(), tubes_.end(), [&](const auto& tube) {
            return tube.calibre == *calibre && simulationTimeSeconds + 1.0e-9 >= tube.nextReadyTimeSeconds;
        }));
    }

    [[nodiscard]] std::optional<double> SecondsUntilNextReadyTube(
        const PlayerWeaponType weapon,
        const double simulationTimeSeconds) const noexcept
    {
        const auto calibre = AnteyTorpedoTubeCalibreForWeapon(weapon);
        if (!calibre || !std::isfinite(simulationTimeSeconds))
            return std::nullopt;
        double best = std::numeric_limits<double>::infinity();
        for (const auto& tube : tubes_)
        {
            if (tube.calibre != *calibre)
                continue;
            best = std::min(best, std::max(0.0, tube.nextReadyTimeSeconds - simulationTimeSeconds));
        }
        return std::isfinite(best) ? std::optional<double>{best} : std::nullopt;
    }

    [[nodiscard]] std::expected<AnteyTorpedoTubeLaunch, std::string> CommitLaunch(
        const PlayerWeaponType weapon,
        const double simulationTimeSeconds)
    {
        const auto calibre = AnteyTorpedoTubeCalibreForWeapon(weapon);
        if (!calibre || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
            return std::unexpected("torpedo tube launch request is invalid");

        for (std::size_t index = 0U; index < tubes_.size(); ++index)
        {
            auto& tube = tubes_[index];
            if (tube.calibre != *calibre || simulationTimeSeconds + 1.0e-9 < tube.nextReadyTimeSeconds)
                continue;
            tube.nextReadyTimeSeconds = simulationTimeSeconds + AnteyTorpedoTubeReloadSeconds(*calibre);
            return AnteyTorpedoTubeLaunch{
                .tubeIndex = index,
                .calibre = *calibre,
                .nextReadyTimeSeconds = tube.nextReadyTimeSeconds};
        }
        return std::unexpected(*calibre == AnteyTorpedoTubeCalibre::Mm533
            ? "all four 533 mm torpedo tubes are reloading"
            : "both 650 mm torpedo tubes are reloading");
    }

private:
    std::array<AnteyTorpedoTubeRuntime, AnteyTorpedoTubeCount> tubes_{};
};
} // namespace DeepRun::Game::Armament
