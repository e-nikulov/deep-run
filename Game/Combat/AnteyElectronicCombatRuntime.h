#pragma once

#include "Game/Combat/AnteyElectronicSuite.h"
#include "Game/Combat/SurfaceContactSensorTruth.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Perception/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Game::Combat
{
struct AnteyElectronicCombatFrame final
{
    bool integratedPlayerEvidence = false;
    bool hostileElectronicSupportEvidence = false;
};

// Gameplay orchestration around the sensor/resource primitives in AnteyElectronicSuite.h.
// This class deliberately owns no hostile entity identity and cannot mutate PhysicsWorld. Surface-contact truth
// enters only at the sensor-simulation boundary, exactly like reflector/emitter truth in the acoustic path; all
// information delivered to gameplay leaves this class as ordinary perceived SensorObservation/Track evidence.
class AnteyElectronicCombatRuntime final
{
public:
    explicit AnteyElectronicCombatRuntime(const double simulationTimeSeconds = 0.0) noexcept
    {
        state_.lastUpdateTimeSeconds = std::isfinite(simulationTimeSeconds) && simulationTimeSeconds >= 0.0
            ? simulationTimeSeconds
            : 0.0;
    }

    [[nodiscard]] const AnteyElectronicSuiteConfig& Config() const noexcept { return config_; }
    [[nodiscard]] const AnteyElectronicSuiteState& State() const noexcept { return state_; }
    [[nodiscard]] AnteyElectronicSystem SelectedSystem() const noexcept { return state_.selectedSystem; }

    void CycleSelectedSystem() noexcept
    {
        CycleAnteyElectronicSystem(state_);
    }

    [[nodiscard]] std::expected<std::string, std::string> OperateSelectedSystem(
        const float ownshipDepthMeters,
        const double simulationTimeSeconds)
    {
        // SIGNAL-3 has a gameplay optics profile, but the production SailDevice node is intentionally unresolved.
        // Do not create a virtual mast that can see while no physical presentation binding exists.
        if (state_.selectedSystem == AnteyElectronicSystem::Signal3NavigationPeriscope)
            return std::string("SIGNAL-3 unavailable: production SailDevice mapping is not confirmed yet");
        return OperateAnteyElectronicSystem(config_, state_, ownshipDepthMeters, simulationTimeSeconds);
    }

    [[nodiscard]] bool AnyAuxiliaryMastDeployed() const noexcept
    {
        return std::ranges::any_of(state_.deployed, [](const bool value) { return value; });
    }

    [[nodiscard]] bool RkpRequested() const noexcept
    {
        return AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::RkpCompressorIntake);
    }

    [[nodiscard]] bool RadioTransmitting(const double simulationTimeSeconds) const noexcept
    {
        return std::isfinite(simulationTimeSeconds) && state_.radioTransmitUntilSeconds > simulationTimeSeconds;
    }

    [[nodiscard]] std::expected<AnteyElectronicCombatFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const std::span<const SurfaceContactSensorTruth> surfaceTruths,
        Perception::TrackManager& playerTracks,
        Perception::TrackManager& hostileTracks,
        const Physics::PhysicsVector3& hostileElectronicSupportReceiverPositionMeters,
        const double simulationTimeSeconds)
    {
        if (!playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.velocityMetersPerSecond.IsFinite() ||
            !playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !hostileElectronicSupportReceiverPositionMeters.IsFinite() ||
            !std::isfinite(playerSnapshot.signedDepthMeters) || playerSnapshot.signedDepthMeters < 0.0F ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state_.lastUpdateTimeSeconds)
        {
            return std::unexpected("Antey electronic combat runtime input is invalid or time-reversing");
        }

        const auto& velocity = playerSnapshot.emitter.velocityMetersPerSecond;
        const float speedMetersPerSecond = static_cast<float>(std::sqrt(
            static_cast<double>(velocity.x) * velocity.x +
            static_cast<double>(velocity.y) * velocity.y +
            static_cast<double>(velocity.z) * velocity.z));
        const auto advanced = AdvanceAnteyElectronicSuite(
            config_, state_, playerSnapshot.signedDepthMeters, speedMetersPerSecond, simulationTimeSeconds);
        if (!advanced)
            return std::unexpected("Antey electronic suite advance failed: " + advanced.error());

        AnteyElectronicCombatFrame frame{};
        const Physics::PhysicsVector3 navigationPosition = AnteyNavigationEstimatedPosition(
            playerSnapshot.passiveReceiver.positionMeters, state_);

        // ZONA is passive RF ESM. In the current combat playground the military surface proxy carries an
        // always-on search/navigation RF emitter as GAME POLICY; civilian contacts do not. This is intentionally
        // distinct from acoustic active-sonar emissions.
        if (AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::ZonaRadioDirectionFinder))
        {
            for (const SurfaceContactSensorTruth& truth : surfaceTruths)
            {
                if (truth.kind != SurfaceContactTruthKind::MilitaryCombatant)
                    continue;
                const auto observed = ObserveZonaEmitter(
                    config_, state_, navigationPosition, truth.emitter.positionMeters,
                    true, simulationTimeSeconds, "ANTEY_ZONA_SURFACE_RF_ESM");
                if (!observed)
                    return std::unexpected("ZONA ESM simulation failed: " + observed.error());
                if (observed->has_value())
                {
                    const auto integrated = playerTracks.IntegrateObservation(**observed);
                    if (!integrated)
                        return std::unexpected("ZONA ESM evidence failed TrackManager integration: " + integrated.error());
                    frame.integratedPlayerEvidence = true;
                }
            }
        }

        if (AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::RadianSurfaceRadar) &&
            state_.radianTransmitting)
        {
            for (const SurfaceContactSensorTruth& truth : surfaceTruths)
            {
                const auto observed = ObserveRadianSurfaceTarget(
                    config_, state_, navigationPosition, truth.emitter.positionMeters, simulationTimeSeconds);
                if (!observed)
                    return std::unexpected("RADIAN surface-radar simulation failed: " + observed.error());
                if (observed->has_value())
                {
                    const auto integrated = playerTracks.IntegrateObservation(**observed);
                    if (!integrated)
                        return std::unexpected("RADIAN evidence failed TrackManager integration: " + integrated.error());
                    frame.integratedPlayerEvidence = true;
                }
            }
        }

        const auto militaryTruth = std::ranges::find_if(surfaceTruths, [](const SurfaceContactSensorTruth& truth) {
            return truth.kind == SurfaceContactTruthKind::MilitaryCombatant;
        });
        const auto receiveExternalReport = [&](
            const AnteyElectronicSystem receiverSystem,
            const ExternalTargetReportSource source,
            std::optional<double>& lastIssueTimeSeconds) -> std::expected<bool, std::string>
        {
            if (!AnteyElectronicSystemDeployed(state_, receiverSystem) || militaryTruth == surfaceTruths.end())
                return false;
            if (lastIssueTimeSeconds &&
                simulationTimeSeconds - *lastIssueTimeSeconds + 1.0e-9 < config_.externalReportIntervalSeconds)
            {
                return false;
            }

            // The remote sensor truth is sampled only to manufacture the report. The player receives a stale,
            // uncertain observation; classification/entity identity never crosses the perception boundary.
            const ExternalTargetReport report = BuildExternalTargetReport(
                config_, source, militaryTruth->emitter.positionMeters, simulationTimeSeconds);
            const auto observed = ExternalTargetReportObservation(
                config_, report, navigationPosition, simulationTimeSeconds);
            if (!observed)
                return std::unexpected("external target-designation report failed: " + observed.error());
            const auto integrated = playerTracks.IntegrateObservation(*observed);
            if (!integrated)
                return std::unexpected("external target-designation evidence failed TrackManager integration: " + integrated.error());

            lastIssueTimeSeconds = simulationTimeSeconds;
            state_.lastReceivedReportSource = source;
            state_.lastReceivedReportUncertaintyMeters = observed->rangeUncertaintyMeters;
            return true;
        };

        const auto mrsc2 = receiveExternalReport(
            AnteyElectronicSystem::Mrsc2TargetingReceiver,
            ExternalTargetReportSource::Tu95Rts,
            state_.lastMrsc2ReportIssueTimeSeconds);
        if (!mrsc2)
            return std::unexpected(mrsc2.error());
        frame.integratedPlayerEvidence = frame.integratedPlayerEvidence || *mrsc2;

        const auto selena = receiveExternalReport(
            AnteyElectronicSystem::SelenaSatelliteTargeting,
            ExternalTargetReportSource::MkrcSatellite,
            state_.lastSelenaReportIssueTimeSeconds);
        if (!selena)
            return std::unexpected(selena.error());
        frame.integratedPlayerEvidence = frame.integratedPlayerEvidence || *selena;

        // RADIAN and explicit radio transmissions are detectable by the hostile ESM path. This is bearing-only
        // perceived evidence: using an emitting sensor buys information but can improve the enemy's awareness.
        const bool radioTransmitting = RadioTransmitting(simulationTimeSeconds);
        if (state_.radianTransmitting || radioTransmitting)
        {
            const Perception::SensorObservation exposure{
                .modality = Perception::SensorModality::ElectronicSupport,
                .sensorId = state_.radianTransmitting ? "HOSTILE_ESM_ANTEY_RADIAN" : "HOSTILE_ESM_ANTEY_RADIO",
                .sensorPositionMeters = hostileElectronicSupportReceiverPositionMeters,
                .observationTimeSeconds = simulationTimeSeconds,
                .measuredBearingRadians = AnteyElectronicBearing2d(
                    hostileElectronicSupportReceiverPositionMeters, playerSnapshot.emitter.positionMeters),
                .bearingUncertaintyRadians = 3.0F * AnteyElectronicPi / 180.0F,
                .confidence = 0.74F};
            const auto integrated = hostileTracks.IntegrateObservation(exposure);
            if (!integrated)
                return std::unexpected("player RF emission failed hostile ESM Track integration: " + integrated.error());
            frame.hostileElectronicSupportEvidence = true;
        }

        return frame;
    }

private:
    AnteyElectronicSuiteConfig config_{};
    AnteyElectronicSuiteState state_{};
};
} // namespace DeepRun::Game::Combat
