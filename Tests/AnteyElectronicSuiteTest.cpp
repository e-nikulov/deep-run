#include "Game/Combat/AnteyElectronicSuite.h"
#include "Game/Submarine/AnteyHighPressureAir.h"
#include "Simulation/Perception/TrackManager.h"
#include "Engine/Input/InputSystem.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
void Require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "[AnteyElectronicSuiteTest][FAIL] " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace DeepRun;
    using namespace DeepRun::Game::Combat;
    using namespace DeepRun::Game::Submarine;

    AnteyElectronicSuiteConfig config{};
    AnteyElectronicSuiteState suite{};
    const Physics::PhysicsVector3 ownship{.x = 0.0F, .y = -10.0F, .z = 0.0F};
    const Physics::PhysicsVector3 target{.x = 40'000.0F, .y = 0.0F, .z = 0.0F};

    // ZONA is passive: bearing evidence only and no range shortcut.
    suite.selectedSystem = AnteyElectronicSystem::ZonaRadioDirectionFinder;
    const auto zonaRaised = OperateAnteyElectronicSystem(config, suite, 10.0F, 0.0);
    Require(zonaRaised.has_value() && AnteyElectronicSystemDeployed(suite, AnteyElectronicSystem::ZonaRadioDirectionFinder),
            "ZONA mast should raise inside the mast operating zone");
    const auto esm = ObserveZonaEmitter(config, suite, ownship, target, true, 0.0);
    Require(esm.has_value() && esm->has_value(), "ZONA should detect an active emitter");
    Require((**esm).modality == Perception::SensorModality::ElectronicSupport,
            "ZONA evidence must cross the perception boundary as ESM");
    Require(!(**esm).estimatedRangeMeters.has_value(), "ESM must remain bearing-only");
    Require(!(**esm).classificationEvidence.has_value(), "ESM must not reveal target classification");

    // RADIAN provides range only while the player deliberately transmits.
    suite.selectedSystem = AnteyElectronicSystem::RadianSurfaceRadar;
    const auto radianRaised = OperateAnteyElectronicSystem(config, suite, 10.0F, 0.0);
    Require(radianRaised.has_value() && !suite.radianTransmitting, "first RADIAN operation should raise in standby");
    const auto noRadar = ObserveRadianSurfaceTarget(config, suite, ownship, target, 0.0);
    Require(noRadar.has_value() && !noRadar->has_value(), "RADIAN standby must not create radar observations");
    const auto radianOn = OperateAnteyElectronicSystem(config, suite, 10.0F, 0.0);
    Require(radianOn.has_value() && suite.radianTransmitting, "second RADIAN operation should begin transmission");
    const auto radar = ObserveRadianSurfaceTarget(config, suite, ownship, target, 0.0);
    Require(radar.has_value() && radar->has_value() && (**radar).estimatedRangeMeters.has_value(),
            "transmitting RADIAN should produce a ranged observation");
    Require((**radar).modality == Perception::SensorModality::SurfaceRadar,
            "RADIAN observation modality mismatch");

    // External reports carry stale uncertainty, not live target identity.
    const ExternalTargetReport report = BuildExternalTargetReport(config, ExternalTargetReportSource::MkrcSatellite, target, 10.0);
    const auto freshReport = ExternalTargetReportObservation(config, report, ownship, 10.0);
    const auto staleReport = ExternalTargetReportObservation(config, report, ownship, 130.0);
    Require(freshReport.has_value() && staleReport.has_value(), "external target reports should be valid");
    Require(staleReport->rangeUncertaintyMeters.value_or(0.0F) > freshReport->rangeUncertaintyMeters.value_or(0.0F),
            "external report uncertainty must grow with data age");
    Require(!staleReport->classificationEvidence.has_value(), "external report must not identify military/civilian truth");
    Require(staleReport->sourceAgeSeconds.value_or(0.0F) >= 120.0F, "external report age should be explicit");

    auto tracksResult = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .observationsToConfirm = 1U,
        .coastAfterSeconds = 200.0,
        .lostAfterSeconds = 600.0,
        .positionUncertaintyGrowthMetersPerSecond = 0.0F});
    Require(tracksResult.has_value(), "TrackManager creation failed");
    auto tracks = *tracksResult;
    const auto trackId = tracks.IntegrateObservation(*freshReport);
    Require(trackId.has_value(), "external report should integrate through ordinary TrackManager");
    const auto fusedTracks = tracks.Tracks();
    Require(fusedTracks.size() == 1U && fusedTracks.front().hasExternalReportEvidence,
            "Track should retain external-report provenance");
    Require(fusedTracks.front().classification == Perception::ContactClassification::Unknown,
            "external target designation must leave classification unknown");

    // Navigation drift is real gameplay state and SATNAV converges it back to a finite fix error.
    suite = {};
    const auto drifted = AdvanceAnteyElectronicSuite(config, suite, 100.0F, 10.0F, 100.0);
    Require(drifted.has_value() && suite.navigationErrorMeters > 250.0F,
            "submerged inertial navigation should accumulate error");
    suite.selectedSystem = AnteyElectronicSystem::SynthesisSatNav;
    const auto synthesisRaised = OperateAnteyElectronicSystem(config, suite, 10.0F, 100.0);
    Require(synthesisRaised.has_value(), "SYNTHESIS should raise at periscope depth");
    const auto corrected = AdvanceAnteyElectronicSuite(config, suite, 10.0F, 0.0F, 102.0);
    Require(corrected.has_value() && suite.navigationErrorMeters <= config.satelliteFixErrorMeters + 0.01F,
            "SYNTHESIS should converge navigation error to the policy fix floor");

    // Radio transmit is an explicit finite emission interval rather than permanent magical exposure.
    suite.selectedSystem = AnteyElectronicSystem::KoraCommunications;
    Require(OperateAnteyElectronicSystem(config, suite, 10.0F, 102.0).has_value(), "KORA should raise");
    Require(OperateAnteyElectronicSystem(config, suite, 10.0F, 102.0).has_value(), "KORA should transmit on second operation");
    Require(suite.radioTransmitUntilSeconds > 102.0, "radio burst should have finite emission duration");
    Require(AdvanceAnteyElectronicSuite(config, suite, 10.0F, 0.0F, 110.0).has_value(), "suite advance failed");
    Require(suite.radioTransmitUntilSeconds == 0.0, "radio burst should end automatically");

    // RKP/HP-air model: blowing consumes air, shallow compressor intake restores it, and low pressure limits blow authority.
    AnteyHighPressureAirConfig airConfig{};
    AnteyHighPressureAirState air{};
    const auto afterBlow = AdvanceAnteyHighPressureAir(airConfig, air, false, 2.0F, -0.025F, 20.0F);
    Require(afterBlow.has_value() && afterBlow->pressureFraction < 1.0F,
            "main-ballast blowing should consume HP air");
    const auto afterRecharge = AdvanceAnteyHighPressureAir(airConfig, *afterBlow, true, 10.0F, 0.0F, 60.0F);
    Require(afterRecharge.has_value() && afterRecharge->pressureFraction > afterBlow->pressureFraction &&
            afterRecharge->rkpCompressorRunning,
            "RKP compressor should replenish HP air while shallow");
    const AnteyHighPressureAirState depleted{.pressureFraction = 0.01F};
    Require(AnteyMainBallastBlowAuthorityFraction(airConfig, depleted) == 0.0F,
            "depleted HP air should remove main-ballast blow authority");

    const auto navScope = Signal3NavigationPeriscopeConfig();
    Require(navScope.viewHalfAngleRadians > PeriscopeObservationConfig{}.viewHalfAngleRadians &&
            navScope.maximumTypeRecognitionRangeMeters < PeriscopeObservationConfig{}.maximumTypeRecognitionRangeMeters,
            "SIGNAL-3 gameplay optic should be wider but weaker than the attack optic");

    Input::GamepadState controls{};
    controls.connected = true;
    controls.buttons = static_cast<std::uint16_t>(Input::GamepadButton::LeftShoulder) |
                       static_cast<std::uint16_t>(Input::GamepadButton::B);
    const auto semanticActions = Input::SemanticActionsForGamepad(controls);
    Require(semanticActions.cycleElectronicSuite && semanticActions.operateElectronicSuite,
            "controller must expose electronic-suite select/operate actions");
    Require(AnteyElectronicSystemProductionRoleId(AnteyElectronicSystem::RadianSurfaceRadar) ==
                std::string_view{"RADIAN_SURFACE_RADAR"},
            "electronic system semantic role contract mismatch");

    std::cout << "[AnteyElectronicSuiteTest][PASS] ESM radar external-CU navigation radio RKP dual-optics and controls\n";
    return 0;
}
