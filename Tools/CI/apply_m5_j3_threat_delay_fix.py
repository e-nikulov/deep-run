from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text or text.count(old) != 1:
        raise RuntimeError(f"anchor mismatch in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

runtime = "Game/Combat/CombatPlaygroundRuntime.h"
replace_once(
    runtime,
    "inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;",
    "inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;\n"
    "inline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;"
)

start = '''    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat receiver/time input is invalid");
        }

        bool integratedObservation = false;
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater &&
            destroyerTorpedo_->positionMeters.IsFinite())
        {
            const double distanceMeters = Distance(
                destroyerTorpedo_->positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const double travelSeconds = distanceMeters /
                static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(travelSeconds))
            {
                return std::unexpected("M5-J3 incoming-threat acoustic travel time is invalid");
            }
            const double emissionTimeSeconds = simulationTimeSeconds > travelSeconds
                ? std::max(0.0, simulationTimeSeconds - travelSeconds - 1.0e-6)
                : 0.0;
            const Acoustics::AcousticEmission emission{
                .positionMeters = destroyerTorpedo_->positionMeters,
                // Gameplay-authored coarse machinery/propulsor signature for the M5 warning slice.
                .sourceLevelDb = {.levelDb = {176.0F, 172.0F, 164.0F, 156.0F}},
                .emissionTimeSeconds = emissionTimeSeconds};
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                emission, playerSnapshot.passiveReceiver, simulationTimeSeconds);
            if (!observed)
            {
                return std::unexpected("M5-J3 incoming-threat acoustic propagation failed: " +
                                       observed.error().message);
            }
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !incomingThreatTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-J3 incoming-threat evidence failed perception integration");
                }
                integratedObservation = true;
            }
        }

        if (!integratedObservation && !incomingThreatTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat TrackManager failed to advance");
        }
        return {};
    }
'''
replacement = '''    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat receiver/time input is invalid");
        }

        // Sample the hostile weapon only while it physically exists underwater. Samples keep the source position
        // and emission SimulationTime at which sound was actually emitted; they are never backdated to make a newly
        // launched threat visible immediately.
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater &&
            destroyerTorpedo_->positionMeters.IsFinite() &&
            simulationTimeSeconds + 1.0e-9 >= nextIncomingThreatEmissionSampleTimeSeconds_)
        {
            pendingIncomingThreatEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = destroyerTorpedo_->positionMeters,
                // Gameplay-authored coarse machinery/propulsor signature for the M5 warning slice.
                .sourceLevelDb = {.levelDb = {176.0F, 172.0F, 164.0F, 156.0F}},
                .emissionTimeSeconds = simulationTimeSeconds});
            nextIncomingThreatEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatIncomingThreatEmissionSampleIntervalSeconds;
        }

        bool integratedObservation = false;
        auto emission = pendingIncomingThreatEmissions_.begin();
        while (emission != pendingIncomingThreatEmissions_.end())
        {
            const double distanceMeters = Distance(
                emission->positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds + distanceMeters /
                static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
            {
                return std::unexpected("M5-J3 incoming-threat acoustic arrival time is invalid");
            }
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }

            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, playerSnapshot.passiveReceiver, simulationTimeSeconds);
            if (!observed)
            {
                return std::unexpected("M5-J3 incoming-threat acoustic propagation failed: " +
                                       observed.error().message);
            }
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !incomingThreatTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-J3 incoming-threat evidence failed perception integration");
                }
                integratedObservation = true;
            }
            emission = pendingIncomingThreatEmissions_.erase(emission);
        }

        if (!integratedObservation && !incomingThreatTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-J3 incoming-threat TrackManager failed to advance");
        }
        return {};
    }
'''
replace_once(runtime, start, replacement)
replace_once(
    runtime,
    "        destroyerTorpedo_ = *launched;\n        destroyerTorpedoLaunchPosition_ = launchPosition;\n        return {};",
    "        destroyerTorpedo_ = *launched;\n"
    "        destroyerTorpedoLaunchPosition_ = launchPosition;\n"
    "        pendingIncomingThreatEmissions_.clear();\n"
    "        nextIncomingThreatEmissionSampleTimeSeconds_ = simulationTimeSeconds;\n"
    "        return {};"
)
replace_once(
    runtime,
    "    Perception::TrackManager incomingThreatTracks_;\n    Weapons::TorpedoSeekerConfig playerTorpedoSeekerConfig_{",
    "    Perception::TrackManager incomingThreatTracks_;\n"
    "    std::vector<Acoustics::AcousticEmission> pendingIncomingThreatEmissions_{};\n"
    "    double nextIncomingThreatEmissionSampleTimeSeconds_ = 0.0;\n"
    "    Weapons::TorpedoSeekerConfig playerTorpedoSeekerConfig_{"
)

# Regression: warning must not appear essentially on the launch tick; acoustic travel must happen first.
test = "Tests/M5CombatPlaygroundRuntimeChecks.h"
replace_once(
    test,
    "    bool sawIncomingThreat = false;\n    bool sawIncomingThreatClearedAfterImpact = false;",
    "    bool sawIncomingThreat = false;\n"
    "    bool sawIncomingThreatPropagationDelay = false;\n"
    "    bool sawIncomingThreatClearedAfterImpact = false;\n"
    "    std::optional<double> destroyerLaunchTimeSeconds{};"
)
replace_once(
    test,
    "            sawDestroyerLaunch = true;\n            sawDestroyerTorpedoMaterialized = true;",
    "            sawDestroyerLaunch = true;\n"
    "            sawDestroyerTorpedoMaterialized = true;\n"
    "            if (!destroyerLaunchTimeSeconds)\n"
    "            {\n"
    "                destroyerLaunchTimeSeconds = simulationTimeSeconds;\n"
    "            }"
)
replace_once(
    test,
    "            sawIncomingThreat = true;",
    "            if (!sawIncomingThreat)\n"
    "            {\n"
    "                if (!destroyerLaunchTimeSeconds ||\n"
    "                    simulationTimeSeconds - *destroyerLaunchTimeSeconds <= 0.25)\n"
    "                {\n"
    "                    return false;\n"
    "                }\n"
    "                sawIncomingThreatPropagationDelay = true;\n"
    "            }\n"
    "            sawIncomingThreat = true;"
)
replace_once(
    test,
    "        !sawIncomingThreat || !sawIncomingThreatClearedAfterImpact || !sawTorpedo ||",
    "        !sawIncomingThreat || !sawIncomingThreatPropagationDelay ||\n"
    "        !sawIncomingThreatClearedAfterImpact || !sawTorpedo ||"
)

replace_once(
    "docs/development/m5-combat-playground.md",
    "passive-acoustic perceived-world path samples the simulated hostile weapon only as an AcousticEmission, feeds\n"
    "the production player passive receiver through AcousticWorld, converts observations through SensorObservation\n"
    "and TrackManager, and projects only lifecycle, bearing, bearing uncertainty and confidence.",
    "passive-acoustic perceived-world path samples the simulated hostile weapon only as timestamped AcousticEmission\n"
    "values, preserves propagation delay before they can reach the production player passive receiver through\n"
    "AcousticWorld, converts observations through SensorObservation and TrackManager, and projects only lifecycle,\n"
    "bearing, bearing uncertainty and confidence."
)

print("M5-J3 physical acoustic delay fix applied")
