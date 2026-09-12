#pragma once

#include "Engine/Core/TimeCompression.h"
#include "Simulation/Weapons/P700Granit.h"

#include <optional>

namespace DeepRun::Game::Combat
{
struct CombatTimeCompressionSignals final
{
    bool incomingThreatDetected = false;
    bool playerDestroyed = false;
    bool importantImpactEvent = false;
    bool hasPerceivedContact = false;
    bool selectedTrackWeaponQualified = false;
    bool conventionalTorpedoInFlight = false;
    std::optional<Weapons::P700GranitPhase> p700Phase{};
};

[[nodiscard]] inline Core::TimeCompressionRate ResolveCombatTimeCompressionMaximum(
    const CombatTimeCompressionSignals& signals) noexcept
{
    using Core::TimeCompressionRate;
    using Weapons::P700GranitPhase;

    if (signals.incomingThreatDetected || signals.playerDestroyed || signals.importantImpactEvent)
    {
        return TimeCompressionRate::X1;
    }

    if (signals.p700Phase.has_value())
    {
        switch (*signals.p700Phase)
        {
        case P700GranitPhase::HatchOpening:
        case P700GranitPhase::UnderwaterLaunch:
        case P700GranitPhase::WaterExit:
        case P700GranitPhase::PostExitTransition:
        case P700GranitPhase::AirborneDeploying:
        case P700GranitPhase::Terminal:
        case P700GranitPhase::Defeated:
        case P700GranitPhase::Impact:
            return TimeCompressionRate::X1;
        case P700GranitPhase::Cruise:
            return TimeCompressionRate::X4;
        case P700GranitPhase::Stored:
        case P700GranitPhase::Spent:
            break;
        }
    }

    if (signals.conventionalTorpedoInFlight || signals.selectedTrackWeaponQualified)
    {
        return TimeCompressionRate::X2;
    }

    if (signals.hasPerceivedContact)
    {
        return TimeCompressionRate::X4;
    }

    return TimeCompressionRate::X8;
}
} // namespace DeepRun::Game::Combat
