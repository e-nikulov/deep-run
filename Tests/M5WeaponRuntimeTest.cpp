#include "Game/Combat/SonarPresentation.h"
#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Weapons/WeaponRuntime.h"
#include "Tests/M5CombatImpactChecks.h"
#include "Tests/M5ConventionalTorpedoChecks.h"
#include "Tests/M5WeaponEmploymentChecks.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace
{
void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

DeepRun::Perception::Track MakeTrack()
{
    return DeepRun::Perception::Track{
        .trackId = 42U,
        .contactId = 7U,
        .lifecycle = DeepRun::Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = DeepRun::Physics::PhysicsVector3{.x = 1200.0F, .y = -140.0F, .z = 0.0F},
        .positionUncertaintyMeters = 50.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.25F,
        .bearingUncertaintyRadians = 0.05F,
        .confidence = 0.90F,
        .observationCount = 4U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 8.0};
}

DeepRun::Acoustics::AcousticSpectrum UniformSpectrum(const float value)
{
    return DeepRun::Acoustics::AcousticSpectrum{.levelDb = {value, value, value, value}};
}

DeepRun::Perception::Track BuildSpatialTrackFromActiveEcho()
{
    using namespace DeepRun;

    const auto world = Acoustics::AcousticWorld::Create({});
    Require(world.has_value(), "M5-B acoustic world must be created");

    Acoustics::ActiveAcousticPulse pulse{};
    pulse.originMeters = {100.0F, -120.0F, 2.0F};
    pulse.forwardUnitVector = {1.0F, 0.0F, 0.0F};
    pulse.sourceLevelDb = UniformSpectrum(230.0F);
    pulse.emissionTimeSeconds = 1.0;

    Acoustics::AcousticReflector reflector{};
    reflector.positionMeters = {3100.0F, -120.0F, 2.0F};
    reflector.reflectionLossDb = UniformSpectrum(8.0F);

    const Acoustics::AcousticReceiver receiver{
        .sensorId = "MGK540_BOW_ARRAY",
        .positionMeters = pulse.originMeters,
        .ambientNoiseLevelDb = UniformSpectrum(30.0F),
        .selfNoiseLevelDb = UniformSpectrum(30.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F};

    const auto echo = Acoustics::CollectMonostaticActiveEchoObservation(*world, pulse, reflector, receiver, 5.0);
    Require(echo.has_value() && echo->has_value(), "M5-B active echo must arrive at the expected SimulationTime");

    const auto rangedWithoutOwnPosition = Perception::FromAcousticObservation(**echo);
    Require(rangedWithoutOwnPosition.has_value(), "active echo must cross the ordinary perception boundary");
    auto noOwnPositionManager = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .observationsToConfirm = 1,
        .coastAfterSeconds = 100.0,
        .lostAfterSeconds = 200.0,
        .confidenceDecayPerSecond = 0.0F});
    Require(noOwnPositionManager.has_value(), "M5-B no-own-position TrackManager must be created");
    Require(noOwnPositionManager->IntegrateObservation(*rangedWithoutOwnPosition).has_value(),
            "ranged evidence without own sensor position remains valid evidence");
    const auto nonSpatialTracks = noOwnPositionManager->Tracks();
    Require(nonSpatialTracks.size() == 1U && !nonSpatialTracks.front().estimatedPositionMeters.has_value(),
            "range evidence must not be spatialized without known own sensor position");

    const auto perceived = Perception::FromAcousticObservation(**echo, receiver.positionMeters);
    Require(perceived.has_value() && perceived->sensorPositionMeters == receiver.positionMeters,
            "own sensor position must cross explicitly with ranged perceived evidence");

    const Physics::PhysicsVector3 invalidOwnPosition{
        .x = std::numeric_limits<float>::quiet_NaN(), .y = 0.0F, .z = 0.0F};
    Require(!Perception::FromAcousticObservation(**echo, invalidOwnPosition).has_value(),
            "non-finite own sensor position must be rejected before track integration");

    auto manager = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .observationsToConfirm = 1,
        .coastAfterSeconds = 100.0,
        .lostAfterSeconds = 200.0,
        .confidenceDecayPerSecond = 0.0F,
        .positionUncertaintyGrowthMetersPerSecond = 5.0F});
    Require(manager.has_value(), "M5-B spatial TrackManager must be created");
    Require(manager->IntegrateObservation(*perceived).has_value(), "ranged perceived evidence must integrate");

    const auto tracks = manager->Tracks();
    Require(tracks.size() == 1U, "ranged active evidence must create one track");
    const auto& track = tracks.front();
    Require(track.lifecycle == Perception::TrackLifecycleState::Confirmed,
            "single-observation M5-B test configuration must confirm the ranged track");
    Require(track.estimatedPositionMeters.has_value() && track.positionUncertaintyMeters.has_value(),
            "ranged track must carry spatial estimate and explicit uncertainty");
    Require(std::abs(track.estimatedPositionMeters->x - reflector.positionMeters.x) < 0.01F &&
            std::abs(track.estimatedPositionMeters->y - reflector.positionMeters.y) < 0.01F &&
            std::abs(track.estimatedPositionMeters->z - receiver.positionMeters.z) < 0.01F,
            "spatial track must derive from own sensor position plus measured bearing/range");

    const float expectedLateralUncertainty =
        *perceived->estimatedRangeMeters * perceived->bearingUncertaintyRadians;
    const float expectedPositionUncertainty = static_cast<float>(std::hypot(
        static_cast<double>(*perceived->rangeUncertaintyMeters),
        static_cast<double>(expectedLateralUncertainty)));
    Require(std::abs(*track.positionUncertaintyMeters - expectedPositionUncertainty) < 0.01F,
            "position uncertainty must combine range and angular evidence uncertainty");

    Require(manager->AdvanceTo(12.0).has_value(), "spatial track uncertainty must age on SimulationTime");
    const auto agedTracks = manager->Tracks();
    Require(agedTracks.size() == 1U && agedTracks.front().positionUncertaintyMeters.has_value(),
            "aged spatial track must retain explicit uncertainty");
    Require(std::abs(*agedTracks.front().positionUncertaintyMeters - (expectedPositionUncertainty + 35.0F)) < 0.01F,
            "position uncertainty age must be measured from the last ranged estimate");

    return track;
}
} // namespace

int main()
{
    using DeepRun::Perception::TrackLifecycleState;
    using DeepRun::Weapons::AdvanceWeaponReadiness;
    using DeepRun::Weapons::AssignWeaponTarget;
    using DeepRun::Weapons::CreateWeaponRuntime;
    using DeepRun::Weapons::LaunchWeapon;
    using DeepRun::Weapons::PrepareWeapon;
    using DeepRun::Weapons::ValidateTrackForWeapon;
    using DeepRun::Weapons::WeaponDefinition;
    using DeepRun::Weapons::WeaponPhase;
    using DeepRun::Weapons::WeaponTargetingRequirements;

    const WeaponDefinition torpedo{
        .id = "m5.conventional-heavyweight",
        .preparationSeconds = 5.0,
        .targeting = WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};

    auto runtimeResult = CreateWeaponRuntime(torpedo, 10.0);
    Require(runtimeResult.has_value(), "valid weapon runtime must be created");
    auto runtime = *runtimeResult;
    Require(runtime.phase == WeaponPhase::Stored, "weapon must start Stored");
    Require(!runtime.targetTrackId.has_value(), "weapon must start without a target");

    Require(!LaunchWeapon(torpedo, runtime, 10.0).has_value(), "Stored weapon must not launch");
    Require(PrepareWeapon(torpedo, runtime, 10.0).has_value(), "Stored weapon must enter preparation");
    Require(runtime.phase == WeaponPhase::Preparing, "weapon must be Preparing before preparation time elapses");

    auto bearingOnlyTrack = MakeTrack();
    bearingOnlyTrack.estimatedPositionMeters = std::nullopt;
    bearingOnlyTrack.positionUncertaintyMeters = std::nullopt;
    Require(!AssignWeaponTarget(torpedo, runtime, bearingOnlyTrack, 10.0).has_value(),
            "M4-style bearing-only track must not satisfy position-requiring torpedo targeting");
    Require(!runtime.targetTrackId.has_value(), "rejected track must not leak into weapon target state");

    auto missingSpatialUncertainty = MakeTrack();
    missingSpatialUncertainty.positionUncertaintyMeters = std::nullopt;
    Require(!ValidateTrackForWeapon(torpedo, missingSpatialUncertainty).has_value(),
            "position-requiring weapon must reject a spatial estimate with unknown uncertainty");

    auto excessiveSpatialUncertainty = MakeTrack();
    excessiveSpatialUncertainty.positionUncertaintyMeters = 151.0F;
    Require(!ValidateTrackForWeapon(torpedo, excessiveSpatialUncertainty).has_value(),
            "position-requiring weapon must reject a track above its spatial uncertainty budget");

    auto weakTrack = MakeTrack();
    weakTrack.confidence = 0.50F;
    Require(!AssignWeaponTarget(torpedo, runtime, weakTrack, 10.0).has_value(), "low-confidence track must be rejected");

    auto uncertainTrack = MakeTrack();
    uncertainTrack.bearingUncertaintyRadians = 0.20F;
    Require(!AssignWeaponTarget(torpedo, runtime, uncertainTrack, 10.0).has_value(),
            "track above weapon bearing-uncertainty budget must be rejected");

    auto coastingTrack = MakeTrack();
    coastingTrack.lifecycle = TrackLifecycleState::Coasting;
    Require(!AssignWeaponTarget(torpedo, runtime, coastingTrack, 10.0).has_value(),
            "coasting track must be rejected unless the definition explicitly allows it");

    const auto goodTrack = MakeTrack();
    Require(!AssignWeaponTarget(torpedo, runtime, goodTrack, 9.0).has_value(),
            "target assignment must reject SimulationTime reversal");
    Require(AssignWeaponTarget(torpedo, runtime, goodTrack, 10.5).has_value(),
            "qualified perceived-world track must be accepted");
    Require(runtime.targetTrackId == goodTrack.trackId, "weapon runtime must retain only perceived-world track identity");
    Require(runtime.lastUpdateTimeSeconds == 10.5, "target assignment must advance authoritative weapon time");

    Require(AdvanceWeaponReadiness(torpedo, runtime, 14.99).has_value(), "readiness must advance monotonically");
    Require(runtime.phase == WeaponPhase::Preparing, "weapon must remain Preparing before preparation time elapses");
    Require(!LaunchWeapon(torpedo, runtime, 14.99).has_value(), "Preparing weapon must not launch");

    Require(AdvanceWeaponReadiness(torpedo, runtime, 15.0).has_value(), "weapon must reach authored readiness time");
    Require(runtime.phase == WeaponPhase::Ready, "weapon must transition to Ready at authored preparation time");
    Require(!AdvanceWeaponReadiness(torpedo, runtime, 14.0).has_value(), "weapon runtime must reject SimulationTime reversal");

    Require(LaunchWeapon(torpedo, runtime, 15.0).has_value(), "Ready weapon with accepted track must launch");
    Require(runtime.phase == WeaponPhase::Launched, "launch must become authoritative weapon phase");
    Require(runtime.targetTrackId == goodTrack.trackId, "launch must preserve track identity without gaining ground truth");

    const WeaponDefinition badDefinition{.id = "", .preparationSeconds = -1.0};
    Require(!CreateWeaponRuntime(badDefinition, 0.0).has_value(), "invalid weapon definitions must fail deterministically");

    auto untargetedResult = CreateWeaponRuntime(torpedo, 20.0);
    Require(untargetedResult.has_value(), "second weapon runtime must be created");
    auto untargeted = *untargetedResult;
    Require(PrepareWeapon(torpedo, untargeted, 20.0).has_value(), "second weapon must prepare");
    Require(AdvanceWeaponReadiness(torpedo, untargeted, 25.0).has_value(), "second weapon must become Ready");
    Require(!LaunchWeapon(torpedo, untargeted, 25.0).has_value(), "Ready weapon without a perceived target must not launch");

    auto coastingDefinition = torpedo;
    coastingDefinition.id = "m5.coasting-test";
    coastingDefinition.targeting.allowCoastingTrack = true;
    auto coastingRuntimeResult = CreateWeaponRuntime(coastingDefinition, 30.0);
    Require(coastingRuntimeResult.has_value(), "coasting-capable definition must create runtime");
    auto coastingRuntime = *coastingRuntimeResult;
    Require(PrepareWeapon(coastingDefinition, coastingRuntime, 30.0).has_value(), "coasting-capable weapon must prepare");
    Require(AssignWeaponTarget(coastingDefinition, coastingRuntime, coastingTrack, 30.0).has_value(),
            "coasting track must be accepted only when explicitly authored");

    const auto activeSpatialTrack = BuildSpatialTrackFromActiveEcho();
    Require(ValidateTrackForWeapon(torpedo, activeSpatialTrack).has_value(),
            "active ranged perception must produce a track that satisfies the first torpedo spatial quality gate");

    auto agedSpatialTrack = activeSpatialTrack;
    agedSpatialTrack.positionUncertaintyMeters = 160.0F;
    Require(!ValidateTrackForWeapon(torpedo, agedSpatialTrack).has_value(),
            "weapon must reject spatial evidence after its uncertainty exceeds the authored budget");

    {
        auto bearingOnlySonarTrack = MakeTrack();
        bearingOnlySonarTrack.trackId = 51U;
        bearingOnlySonarTrack.estimatedBearingRadians = 0.50F;
        bearingOnlySonarTrack.estimatedPositionMeters.reset();
        bearingOnlySonarTrack.positionUncertaintyMeters.reset();

        auto rangedSonarTrack = MakeTrack();
        rangedSonarTrack.trackId = 52U;
        rangedSonarTrack.estimatedBearingRadians = 0.25F;
        rangedSonarTrack.estimatedPositionMeters = DeepRun::Physics::PhysicsVector3{.x = 1100.0F, .y = -100.0F, .z = 0.0F};
        rangedSonarTrack.positionUncertaintyMeters = 75.0F;

        const std::array sonarTracks{bearingOnlySonarTrack, rangedSonarTrack};
        const DeepRun::Physics::PhysicsVector3 ownship{.x = 100.0F, .y = -100.0F, .z = 0.0F};
        const DeepRun::Acoustics::ActiveAcousticPulse pulse{
            .originMeters = ownship,
            .forwardUnitVector = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            .beamHalfAngleRadians = 0.25F,
            .emissionTimeSeconds = 40.0};
        const DeepRun::Acoustics::AcousticObservation echo{
            .kind = DeepRun::Acoustics::AcousticObservationKind::ActiveEcho,
            .sensorId = "MGK540_BOW_ARRAY",
            .observationTimeSeconds = 41.0,
            .arrivalTimeSeconds = 41.0,
            .measuredBearingRadians = 0.25F,
            .bearingUncertaintyRadians = 0.03F,
            .estimatedRangeMeters = 1000.0F,
            .rangeUncertaintyMeters = 20.0F,
            .confidence = 0.85F};

        constexpr float testSoundSpeedMetersPerSecond = 1475.0F;
        const auto sonar = DeepRun::Game::Combat::BuildSonarPresentation(
            sonarTracks, 51U, ownship, 0.25F, pulse, echo, 42.0, testSoundSpeedMetersPerSecond);
        Require(sonar.has_value(), "M5 sonar presentation must accept perceived tracks and own active evidence");
        Require(sonar->tracks.size() == 2U, "M5 sonar presentation must retain all non-lost perceived tracks");
        Require(sonar->tracks[0].selected && !sonar->tracks[0].estimatedRangeMeters.has_value(),
                "bearing-only sonar contact must remain bearing-only and selectable");
        Require(std::abs(sonar->tracks[0].relativeBearingRadians - 0.25F) < 0.001F,
                "sonar bearing must be relative to current ownship facing");
        Require(sonar->tracks[1].estimatedRangeMeters.has_value() &&
                    std::abs(*sonar->tracks[1].estimatedRangeMeters - 1000.0F) < 0.01F,
                "ranged sonar contact must derive range only from the perceived spatial estimate");
        Require(sonar->activePulse.has_value() && sonar->recentEcho.has_value(),
                "own ping and measured echo evidence must be visible to sonar presentation");
        Require(std::abs(sonar->recentEcho->estimatedRangeMeters - 1000.0F) < 0.01F,
                "sonar echo range must come from measured AcousticObservation evidence");
        Require(std::abs(sonar->effectiveSoundSpeedMetersPerSecond - testSoundSpeedMetersPerSecond) < 0.001F,
                "sonar presentation must retain the acoustic-world effective sound speed");
        Require(std::abs(DeepRun::Game::Combat::SonarOutgoingWaveRangeMeters(*sonar) - 2950.0F) < 0.01F,
                "sonar HUD outgoing wave must advance from SimulationTime using supplied acoustic sound speed");

        const auto invalidSoundSpeed = DeepRun::Game::Combat::BuildSonarPresentation(
            sonarTracks, 51U, ownship, 0.25F, pulse, echo, 42.0, 0.0F);
        Require(!invalidSoundSpeed.has_value(),
                "sonar presentation must reject a non-positive acoustic sound speed");

        const auto staleEcho = DeepRun::Game::Combat::BuildSonarPresentation(
            sonarTracks, 51U, ownship, 0.25F, std::nullopt, echo, 46.0);
        Require(staleEcho.has_value() && !staleEcho->recentEcho.has_value(),
                "old active echo evidence must age out of the presentation without mutating Tracks");

        auto lost = bearingOnlySonarTrack;
        lost.lifecycle = TrackLifecycleState::Lost;
        const std::array lostOnly{lost};
        const auto lostPresentation = DeepRun::Game::Combat::BuildSonarPresentation(
            lostOnly, 51U, ownship, 0.25F, std::nullopt, std::nullopt, 42.0);
        Require(lostPresentation.has_value() && lostPresentation->tracks.empty(),
                "Lost tracks must not remain on the player sonar scope");
    }

    Require(DeepRun::Tests::RunM5ConventionalTorpedoChecks(),
            "M5-C conventional torpedo runtime must remain track-bound and SimulationTime deterministic");
    Require(DeepRun::Tests::RunM5CombatImpactChecks(),
            "M5-D swept collision, impact, explosion and bounded combat damage checks must pass");
    Require(DeepRun::Tests::RunM5WeaponEmploymentChecks(),
            "weapon employment depth/range/sector/speed boundaries must pass");

    std::cout << "M5 weapon/perception/torpedo/combat/employment runtime checks passed\n";
    return EXIT_SUCCESS;
}