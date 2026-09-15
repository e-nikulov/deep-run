#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Game/Combat/PeriscopeObservationSystem.h"
#include "Simulation/Perception/SensorObservation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace DeepRun::Game::Combat
{
inline constexpr float AnteyElectronicPi = 3.14159265358979323846F;

enum class AnteyElectronicSystem : std::size_t
{
    SynthesisSatNav = 0,
    ZonaRadioDirectionFinder,
    AnisRadio,
    Mrsc2TargetingReceiver,
    RadianSurfaceRadar,
    KoraCommunications,
    RkpCompressorIntake,
    SelenaSatelliteTargeting,
    Signal3NavigationPeriscope,
    Pzns10AttackPeriscope,
    Count,
};

inline constexpr std::size_t AnteyElectronicSystemCount =
    static_cast<std::size_t>(AnteyElectronicSystem::Count);

[[nodiscard]] inline constexpr std::string_view AnteyElectronicSystemName(const AnteyElectronicSystem system) noexcept
{
    switch (system)
    {
    case AnteyElectronicSystem::SynthesisSatNav: return "SYNTHESIS SATNAV";
    case AnteyElectronicSystem::ZonaRadioDirectionFinder: return "ZONA RDF / ESM";
    case AnteyElectronicSystem::AnisRadio: return "ANIS RADIO";
    case AnteyElectronicSystem::Mrsc2TargetingReceiver: return "MRSC-2 TARGETING";
    case AnteyElectronicSystem::RadianSurfaceRadar: return "RADIAN RADAR";
    case AnteyElectronicSystem::KoraCommunications: return "KORA / MOLNIYA-M";
    case AnteyElectronicSystem::RkpCompressorIntake: return "RKP COMPRESSOR INTAKE";
    case AnteyElectronicSystem::SelenaSatelliteTargeting: return "SELENA / KORALL";
    case AnteyElectronicSystem::Signal3NavigationPeriscope: return "SIGNAL-3 NAV PERISCOPE";
    case AnteyElectronicSystem::Pzns10AttackPeriscope: return "PZNS-10S ATTACK PERISCOPE";
    case AnteyElectronicSystem::Count: break;
    }
    return "UNKNOWN";
}

enum class ExternalTargetReportSource
{
    Tu95Rts,
    MkrcSatellite,
};

[[nodiscard]] inline constexpr std::string_view ExternalTargetReportSourceName(
    const ExternalTargetReportSource source) noexcept
{
    return source == ExternalTargetReportSource::Tu95Rts ? "TU-95RTS / MRSC-2" : "MKRC / SELENA";
}

struct AnteyElectronicSuiteConfig final
{
    float maximumMastOperatingDepthMeters = 20.0F;

    // ESM/radar values are GAME POLICY chosen to create distinct information channels. They are not represented
    // as Project 949A classified or exact equipment performance.
    float zonaMaximumDetectionRangeMeters = 300'000.0F;
    float zonaBearingUncertaintyRadians = 2.0F * AnteyElectronicPi / 180.0F;
    float zonaConfidence = 0.72F;

    float radianMaximumDetectionRangeMeters = 100'000.0F;
    float radianBearingUncertaintyRadians = 0.35F * AnteyElectronicPi / 180.0F;
    float radianMinimumRangeUncertaintyMeters = 50.0F;
    float radianFractionalRangeUncertainty = 0.01F;
    float radianConfidence = 0.92F;

    // Inertial-navigation drift and satellite correction are deliberately readable gameplay rates.
    float navigationDriftMetersPerSecond = 0.08F;
    float navigationSpeedDriftMetersPerMeter = 0.00015F;
    float satelliteFixErrorMeters = 35.0F;
    float satelliteFixConvergenceMetersPerSecond = 350.0F;

    // External target reports remain stale/perceived data. Uncertainty continues growing after the report is
    // issued; raising a receiver does not grant live world truth.
    double externalReportIntervalSeconds = 45.0;
    float mrsc2InitialPositionUncertaintyMeters = 8'000.0F;
    float selenaInitialPositionUncertaintyMeters = 5'000.0F;
    float externalReportUncertaintyGrowthMetersPerSecond = 35.0F;
    float externalReportConfidence = 0.78F;

    double radioBurstSeconds = 3.0;
    float radioExposureMaximumRangeMeters = 300'000.0F;
};

struct ExternalTargetReport final
{
    ExternalTargetReportSource source = ExternalTargetReportSource::Tu95Rts;
    Physics::PhysicsVector3 reportedPositionMeters{};
    double issueTimeSeconds = 0.0;
    float initialPositionUncertaintyMeters = 0.0F;
};

struct AnteyElectronicSuiteState final
{
    AnteyElectronicSystem selectedSystem = AnteyElectronicSystem::ZonaRadioDirectionFinder;
    std::array<bool, AnteyElectronicSystemCount> deployed{};
    bool radianTransmitting = false;
    double radioTransmitUntilSeconds = 0.0;
    float navigationErrorMeters = 250.0F;
    double lastUpdateTimeSeconds = 0.0;
    std::optional<double> lastMrsc2ReportIssueTimeSeconds{};
    std::optional<double> lastSelenaReportIssueTimeSeconds{};
    std::optional<ExternalTargetReportSource> lastReceivedReportSource{};
    std::optional<float> lastReceivedReportUncertaintyMeters{};
    std::optional<float> signal3ViewBearingRadians{};
};

[[nodiscard]] inline constexpr std::size_t AnteyElectronicSystemIndex(const AnteyElectronicSystem system) noexcept
{
    return static_cast<std::size_t>(system);
}

[[nodiscard]] inline bool AnteyElectronicSystemDeployed(
    const AnteyElectronicSuiteState& state,
    const AnteyElectronicSystem system) noexcept
{
    const auto index = AnteyElectronicSystemIndex(system);
    return index < state.deployed.size() && state.deployed[index];
}

inline void SetAnteyElectronicSystemDeployed(
    AnteyElectronicSuiteState& state,
    const AnteyElectronicSystem system,
    const bool deployed) noexcept
{
    const auto index = AnteyElectronicSystemIndex(system);
    if (index < state.deployed.size())
        state.deployed[index] = deployed;
}

[[nodiscard]] inline bool AnteyElectronicMastsAvailable(
    const AnteyElectronicSuiteConfig& config,
    const float ownshipDepthMeters) noexcept
{
    return std::isfinite(ownshipDepthMeters) && ownshipDepthMeters >= 0.0F &&
           ownshipDepthMeters <= config.maximumMastOperatingDepthMeters;
}

inline void CycleAnteyElectronicSystem(AnteyElectronicSuiteState& state) noexcept
{
    const std::size_t current = AnteyElectronicSystemIndex(state.selectedSystem);
    state.selectedSystem = static_cast<AnteyElectronicSystem>((current + 1U) % AnteyElectronicSystemCount);
}

[[nodiscard]] inline std::expected<std::string, std::string> OperateAnteyElectronicSystem(
    const AnteyElectronicSuiteConfig& config,
    AnteyElectronicSuiteState& state,
    const float ownshipDepthMeters,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(ownshipDepthMeters) || ownshipDepthMeters < 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("electronic-suite operation input is invalid or time-reversing");
    }

    if (state.selectedSystem == AnteyElectronicSystem::Pzns10AttackPeriscope)
        return std::string("attack periscope uses dedicated P / D-pad Up control");

    if (!AnteyElectronicMastsAvailable(config, ownshipDepthMeters))
        return std::string("mast unavailable: ownship is below the 20 m mast operating zone");

    const auto system = state.selectedSystem;
    const bool deployed = AnteyElectronicSystemDeployed(state, system);

    if (system == AnteyElectronicSystem::RadianSurfaceRadar)
    {
        if (!deployed)
        {
            SetAnteyElectronicSystemDeployed(state, system, true);
            state.radianTransmitting = false;
            return std::string("RADIAN raised; radar remains EMCON / standby");
        }
        state.radianTransmitting = !state.radianTransmitting;
        return state.radianTransmitting
            ? std::string("RADIAN transmitting: precise surface ranging, high intercept risk")
            : std::string("RADIAN emission stopped; mast remains raised");
    }

    if (system == AnteyElectronicSystem::AnisRadio || system == AnteyElectronicSystem::KoraCommunications)
    {
        if (!deployed)
        {
            SetAnteyElectronicSystemDeployed(state, system, true);
            return std::string(AnteyElectronicSystemName(system)) + " raised / receive-only";
        }
        state.radioTransmitUntilSeconds = simulationTimeSeconds + config.radioBurstSeconds;
        return std::string(AnteyElectronicSystemName(system)) + " burst transmission started; ESM exposure risk";
    }

    SetAnteyElectronicSystemDeployed(state, system, !deployed);
    if (system == AnteyElectronicSystem::Signal3NavigationPeriscope && deployed)
        state.signal3ViewBearingRadians.reset();
    return std::string(AnteyElectronicSystemName(system)) + (!deployed ? " raised / active" : " stowed");
}

[[nodiscard]] inline std::expected<void, std::string> AdvanceAnteyElectronicSuite(
    const AnteyElectronicSuiteConfig& config,
    AnteyElectronicSuiteState& state,
    const float ownshipDepthMeters,
    const float ownshipSpeedMetersPerSecond,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(config.maximumMastOperatingDepthMeters) || config.maximumMastOperatingDepthMeters < 0.0F ||
        !std::isfinite(config.navigationDriftMetersPerSecond) || config.navigationDriftMetersPerSecond < 0.0F ||
        !std::isfinite(config.navigationSpeedDriftMetersPerMeter) || config.navigationSpeedDriftMetersPerMeter < 0.0F ||
        !std::isfinite(config.satelliteFixErrorMeters) || config.satelliteFixErrorMeters < 0.0F ||
        !std::isfinite(config.satelliteFixConvergenceMetersPerSecond) || config.satelliteFixConvergenceMetersPerSecond <= 0.0F ||
        !std::isfinite(ownshipDepthMeters) || ownshipDepthMeters < 0.0F ||
        !std::isfinite(ownshipSpeedMetersPerSecond) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("electronic-suite advance input/configuration is invalid");
    }

    const double dt = simulationTimeSeconds - state.lastUpdateTimeSeconds;
    if (dt <= 0.0)
        return {};

    if (!AnteyElectronicMastsAvailable(config, ownshipDepthMeters))
    {
        state.deployed.fill(false);
        state.radianTransmitting = false;
        state.signal3ViewBearingRadians.reset();
    }

    const float dtSeconds = static_cast<float>(dt);
    const float speed = std::abs(ownshipSpeedMetersPerSecond);
    const float drift = (config.navigationDriftMetersPerSecond +
                         config.navigationSpeedDriftMetersPerMeter * speed) * dtSeconds;
    state.navigationErrorMeters = std::max(0.0F, state.navigationErrorMeters + drift);

    if (AnteyElectronicSystemDeployed(state, AnteyElectronicSystem::SynthesisSatNav) &&
        AnteyElectronicMastsAvailable(config, ownshipDepthMeters))
    {
        const float correction = config.satelliteFixConvergenceMetersPerSecond * dtSeconds;
        state.navigationErrorMeters = std::max(
            config.satelliteFixErrorMeters,
            state.navigationErrorMeters - correction);
    }

    if (state.radioTransmitUntilSeconds < simulationTimeSeconds)
        state.radioTransmitUntilSeconds = 0.0;

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}

[[nodiscard]] inline Physics::PhysicsVector3 AnteyNavigationEstimatedPosition(
    const Physics::PhysicsVector3& authoritativePositionMeters,
    const AnteyElectronicSuiteState& state) noexcept
{
    Physics::PhysicsVector3 result = authoritativePositionMeters;
    // Deterministic bias avoids adding a second random authority. Only the navigation solution is biased;
    // Jolt/body/sensor truth remains untouched.
    result.x += std::max(0.0F, state.navigationErrorMeters);
    return result;
}

[[nodiscard]] inline float AnteyElectronicDistance2d(
    const Physics::PhysicsVector3& first,
    const Physics::PhysicsVector3& second) noexcept
{
    return std::hypot(second.x - first.x, second.y - first.y);
}

[[nodiscard]] inline float AnteyElectronicBearing2d(
    const Physics::PhysicsVector3& from,
    const Physics::PhysicsVector3& to) noexcept
{
    return std::atan2(to.y - from.y, to.x - from.x);
}

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string> ObserveZonaEmitter(
    const AnteyElectronicSuiteConfig& config,
    const AnteyElectronicSuiteState& state,
    const Physics::PhysicsVector3& receiverPositionMeters,
    const Physics::PhysicsVector3& emitterPositionMeters,
    const bool emitterActive,
    const double simulationTimeSeconds,
    const std::string_view sensorId = "ANTEY_ZONA_ESM")
{
    if (!receiverPositionMeters.IsFinite() || !emitterPositionMeters.IsFinite() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        !std::isfinite(config.zonaMaximumDetectionRangeMeters) || config.zonaMaximumDetectionRangeMeters <= 0.0F ||
        !std::isfinite(config.zonaBearingUncertaintyRadians) || config.zonaBearingUncertaintyRadians < 0.0F ||
        !std::isfinite(config.zonaConfidence) || config.zonaConfidence < 0.0F || config.zonaConfidence > 1.0F)
    {
        return std::unexpected("invalid ZONA ESM observation input");
    }
    if (!emitterActive || !AnteyElectronicSystemDeployed(state, AnteyElectronicSystem::ZonaRadioDirectionFinder))
        return std::optional<Perception::SensorObservation>{};

    const float distance = AnteyElectronicDistance2d(receiverPositionMeters, emitterPositionMeters);
    if (!std::isfinite(distance) || distance > config.zonaMaximumDetectionRangeMeters)
        return std::optional<Perception::SensorObservation>{};

    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::ElectronicSupport,
        .sensorId = std::string(sensorId),
        .sensorPositionMeters = receiverPositionMeters,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = AnteyElectronicBearing2d(receiverPositionMeters, emitterPositionMeters),
        .bearingUncertaintyRadians = config.zonaBearingUncertaintyRadians,
        .confidence = config.zonaConfidence}};
}

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string> ObserveRadianSurfaceTarget(
    const AnteyElectronicSuiteConfig& config,
    const AnteyElectronicSuiteState& state,
    const Physics::PhysicsVector3& navigationSensorPositionMeters,
    const Physics::PhysicsVector3& targetPositionMeters,
    const double simulationTimeSeconds)
{
    if (!navigationSensorPositionMeters.IsFinite() || !targetPositionMeters.IsFinite() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        !std::isfinite(config.radianMaximumDetectionRangeMeters) || config.radianMaximumDetectionRangeMeters <= 0.0F ||
        !std::isfinite(config.radianBearingUncertaintyRadians) || config.radianBearingUncertaintyRadians < 0.0F ||
        !std::isfinite(config.radianMinimumRangeUncertaintyMeters) || config.radianMinimumRangeUncertaintyMeters < 0.0F ||
        !std::isfinite(config.radianFractionalRangeUncertainty) || config.radianFractionalRangeUncertainty < 0.0F ||
        !std::isfinite(config.radianConfidence) || config.radianConfidence < 0.0F || config.radianConfidence > 1.0F)
    {
        return std::unexpected("invalid RADIAN observation input");
    }
    if (!AnteyElectronicSystemDeployed(state, AnteyElectronicSystem::RadianSurfaceRadar) || !state.radianTransmitting)
        return std::optional<Perception::SensorObservation>{};

    const float distance = AnteyElectronicDistance2d(navigationSensorPositionMeters, targetPositionMeters);
    if (!std::isfinite(distance) || distance > config.radianMaximumDetectionRangeMeters)
        return std::optional<Perception::SensorObservation>{};

    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::SurfaceRadar,
        .sensorId = "ANTEY_RADIAN_SURFACE_RADAR",
        .sensorPositionMeters = navigationSensorPositionMeters,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = AnteyElectronicBearing2d(navigationSensorPositionMeters, targetPositionMeters),
        .bearingUncertaintyRadians = config.radianBearingUncertaintyRadians,
        .estimatedRangeMeters = distance,
        .rangeUncertaintyMeters = std::max(
            config.radianMinimumRangeUncertaintyMeters,
            distance * config.radianFractionalRangeUncertainty),
        .confidence = config.radianConfidence}};
}

[[nodiscard]] inline ExternalTargetReport BuildExternalTargetReport(
    const AnteyElectronicSuiteConfig& config,
    const ExternalTargetReportSource source,
    const Physics::PhysicsVector3& targetTruthPositionMeters,
    const double simulationTimeSeconds) noexcept
{
    return ExternalTargetReport{
        .source = source,
        .reportedPositionMeters = targetTruthPositionMeters,
        .issueTimeSeconds = simulationTimeSeconds,
        .initialPositionUncertaintyMeters = source == ExternalTargetReportSource::Tu95Rts
            ? config.mrsc2InitialPositionUncertaintyMeters
            : config.selenaInitialPositionUncertaintyMeters};
}

[[nodiscard]] inline std::expected<Perception::SensorObservation, std::string> ExternalTargetReportObservation(
    const AnteyElectronicSuiteConfig& config,
    const ExternalTargetReport& report,
    const Physics::PhysicsVector3& navigationOwnshipPositionMeters,
    const double receiveTimeSeconds)
{
    if (!report.reportedPositionMeters.IsFinite() || !navigationOwnshipPositionMeters.IsFinite() ||
        !std::isfinite(report.issueTimeSeconds) || report.issueTimeSeconds < 0.0 ||
        !std::isfinite(receiveTimeSeconds) || receiveTimeSeconds < report.issueTimeSeconds ||
        !std::isfinite(report.initialPositionUncertaintyMeters) || report.initialPositionUncertaintyMeters < 0.0F ||
        !std::isfinite(config.externalReportUncertaintyGrowthMetersPerSecond) ||
        config.externalReportUncertaintyGrowthMetersPerSecond < 0.0F ||
        !std::isfinite(config.externalReportConfidence) || config.externalReportConfidence < 0.0F ||
        config.externalReportConfidence > 1.0F)
    {
        return std::unexpected("invalid external target report");
    }

    const float ageSeconds = static_cast<float>(receiveTimeSeconds - report.issueTimeSeconds);
    const float uncertainty = report.initialPositionUncertaintyMeters +
        ageSeconds * config.externalReportUncertaintyGrowthMetersPerSecond;
    const float distance = AnteyElectronicDistance2d(navigationOwnshipPositionMeters, report.reportedPositionMeters);
    const float bearingUncertainty = distance > 1.0F
        ? std::clamp(std::atan2(uncertainty, distance), 0.005F, 0.60F)
        : 0.60F;

    return Perception::SensorObservation{
        .modality = Perception::SensorModality::ExternalReport,
        .sensorId = report.source == ExternalTargetReportSource::Tu95Rts
            ? "ANTEY_MRSC2_TU95RTS_REPORT"
            : "ANTEY_SELENA_MKRC_REPORT",
        .sensorPositionMeters = navigationOwnshipPositionMeters,
        .observationTimeSeconds = receiveTimeSeconds,
        .measuredBearingRadians = AnteyElectronicBearing2d(navigationOwnshipPositionMeters, report.reportedPositionMeters),
        .bearingUncertaintyRadians = bearingUncertainty,
        .estimatedRangeMeters = distance,
        .rangeUncertaintyMeters = uncertainty,
        .confidence = config.externalReportConfidence,
        .sourceAgeSeconds = ageSeconds};
}

[[nodiscard]] inline PeriscopeObservationConfig Signal3NavigationPeriscopeConfig() noexcept
{
    PeriscopeObservationConfig config{};
    // GAME POLICY: make the navigation scope a wider/faster search optic than the attack scope, but weaker for
    // fine identification. The dedicated PZNS-10S/attack-periscope path remains the best fire-control optic.
    config.viewHalfAngleRadians = 12.0F * AnteyElectronicPi / 180.0F;
    config.bearingUncertaintyRadians = 0.50F * AnteyElectronicPi / 180.0F;
    config.maximumTypeRecognitionRangeMeters = 6'000.0F;
    config.maximumFlagRecognitionRangeMeters = 1'500.0F;
    return config;
}
} // namespace DeepRun::Game::Combat
