$ErrorActionPreference = 'Stop'

function Read-Lf([string]$Path) {
    return (Get-Content -Raw $Path).Replace("`r`n", "`n")
}

function Write-Lf([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Replace-Once([string]$Content, [string]$Old, [string]$New, [string]$Label) {
    $oldLf = $Old.Replace("`r`n", "`n")
    $newLf = $New.Replace("`r`n", "`n")
    $index = $Content.IndexOf($oldLf, [System.StringComparison]::Ordinal)
    if ($index -lt 0) {
        throw "Anchor not found: $Label"
    }
    if ($Content.IndexOf($oldLf, $index + $oldLf.Length, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Anchor is not unique: $Label"
    }
    return $Content.Substring(0, $index) + $newLf + $Content.Substring($index + $oldLf.Length)
}

$runtimePath = 'Game/Combat/CombatPlaygroundRuntime.h'
$runtime = Read-Lf $runtimePath

$runtime = Replace-Once $runtime @'
#include "Simulation/Weapons/NavalMine.h"
'@ @'
#include "Simulation/Weapons/NavalMine.h"
#include "Simulation/Weapons/TorpedoSeeker.h"
'@ 'runtime seeker include'

$runtime = Replace-Once $runtime @'
inline constexpr float M5CombatPlayerMaximumIntegrity = 100.0F;
'@ @'
inline constexpr float M5CombatPlayerMaximumIntegrity = 100.0F;
inline constexpr double M5CombatTorpedoSeekerEmissionSampleIntervalSeconds = 0.10;
inline constexpr float M5CombatTorpedoSeekerAssociationGateRadians = 0.03F;
inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;
'@ 'runtime seeker constants'

$runtime = Replace-Once $runtime @'
        const auto destroyerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .observationsToConfirm = 2U,
            .coastAfterSeconds = 5.0,
            .lostAfterSeconds = 20.0,
            .confidenceDecayPerSecond = 0.02F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.01F,
            .positionUncertaintyGrowthMetersPerSecond = 5.0F,
            .maximumTracks = 8U});
        if (!playerTracks || !destroyerTracks)
        {
            return std::unexpected("M5-H perception manager creation failed");
        }
'@ @'
        const auto destroyerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .observationsToConfirm = 2U,
            .coastAfterSeconds = 5.0,
            .lostAfterSeconds = 20.0,
            .confidenceDecayPerSecond = 0.02F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.01F,
            .positionUncertaintyGrowthMetersPerSecond = 5.0F,
            .maximumTracks = 8U});
        const auto playerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.35,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.50F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 8U});
        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks)
        {
            return std::unexpected("M5-H/M5-E.1 perception manager creation failed");
        }
'@ 'runtime seeker track manager creation'

$runtime = Replace-Once $runtime @'
            *playerTracks,
            *destroyerTracks,
            destroyerDefinition,
'@ @'
            *playerTracks,
            *destroyerTracks,
            *playerTorpedoSeekerTracks,
            destroyerDefinition,
'@ 'runtime constructor call seeker tracks'

$runtime = Replace-Once $runtime @'
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
'@ @'
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& PlayerTorpedoSeekerState() const noexcept
    {
        return playerTorpedoSeekerState_;
    }
'@ 'runtime seeker accessor'

$runtime = Replace-Once $runtime @'
        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        SimpleDestroyerDefinition destroyerDefinition,
'@ @'
        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        Perception::TrackManager playerTorpedoSeekerTracks,
        SimpleDestroyerDefinition destroyerDefinition,
'@ 'runtime constructor parameter'

$runtime = Replace-Once $runtime @'
          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          destroyerDefinition_(std::move(destroyerDefinition)),
'@ @'
          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),
          destroyerDefinition_(std::move(destroyerDefinition)),
'@ 'runtime constructor initializer'

$oldCombatAdvance = @'
        std::optional<Weapons::ConventionalTorpedoImpact> impact{};
        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerTorpedo_->guidanceTrackId);
            const auto guidanceTrack = BuildPlayerTorpedoGuidanceTrack(perceivedTrack);
            const auto advanced = Weapons::AdvanceConventionalTorpedoWithCollision(
                playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-H torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                impact = **advanced;
                lastExplosion_ = impact->explosion;
                if (impact->physicsHit.body != destroyer_.body)
                {
                    return std::unexpected("M5-H torpedo struck an unexpected physical body");
                }
                const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                if (!damaged)
                {
                    return std::unexpected("M5-H destroyer damage application failed: " + damaged.error());
                }
            }
        }

        std::optional<Weapons::NavalMineDetonation> mineDetonation{};
'@
$newCombatAdvance = @'
        // M5-E.1: the destroyer's deployed countermeasure advances before the local seeker samples it. The
        // seeker consumes timestamped AcousticEmission values through AcousticWorld/TrackManager; no decoy flag,
        // source entity, destroyer body handle or ground-truth target position crosses into seeker selection.
        if (decoy_)
        {
            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-E.1 decoy advance failed: " + advanced.error());
            }
        }

        std::optional<Weapons::ConventionalTorpedoImpact> impact{};
        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto seekerCue = AdvancePlayerTorpedoSeeker(*destroyerAcoustics, simulationTimeSeconds);
            if (!seekerCue)
            {
                return std::unexpected("M5-E.1 live torpedo seeker failed: " + seekerCue.error());
            }

            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerTorpedo_->guidanceTrackId);
            const auto guidanceTrack = BuildPlayerTorpedoGuidanceTrack(perceivedTrack);
            const float forwardProgressMeters = playerTorpedoLaunchPosition_
                ? (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_
                : 0.0F;
            const bool localSeekerOwnsCourse =
                forwardProgressMeters >= M5CombatTorpedoStraightRunMeters && seekerCue->has_value();

            const auto advanced = localSeekerOwnsCourse
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    playerTorpedoDefinition_,
                    playerTorpedoSeekerConfig_,
                    *playerTorpedo_,
                    **seekerCue,
                    *physicsWorld_,
                    simulationTimeSeconds)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-E.1 torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                impact = **advanced;
                lastExplosion_ = impact->explosion;
                pendingPlayerTorpedoSeekerEmissions_.clear();
                if (impact->physicsHit.body != destroyer_.body)
                {
                    return std::unexpected("M5-E.1 torpedo struck an unexpected physical body");
                }
                const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                if (!damaged)
                {
                    return std::unexpected("M5-E.1 destroyer damage application failed: " + damaged.error());
                }
            }
        }

        std::optional<Weapons::NavalMineDetonation> mineDetonation{};
'@
$runtime = Replace-Once $runtime $oldCombatAdvance $newCombatAdvance 'runtime live seeker advance'

$runtime = Replace-Once $runtime @'
        if (decoy_)
        {
            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M5-H decoy advance failed: " + advanced.error());
            }
        }

        lastUpdateTimeSeconds_ = simulationTimeSeconds;
'@ @'
        lastUpdateTimeSeconds_ = simulationTimeSeconds;
'@ 'runtime remove old decoy advance'

$runtime = Replace-Once $runtime @'
        playerTorpedo_ = *launched;
        playerTorpedoLaunchPosition_ = launchPosition;

        const Physics::PhysicsVector3 decoyPosition{
            .x = destroyerAcoustics.emitter.positionMeters.x - 20.0F,
            .y = destroyerAcoustics.emitter.positionMeters.y - 15.0F,
            .z = destroyerAcoustics.emitter.positionMeters.z};
'@ @'
        playerTorpedo_ = *launched;
        playerTorpedoLaunchPosition_ = launchPosition;
        playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{
            .selectedTrackId = std::nullopt,
            .lastUpdateTimeSeconds = simulationTimeSeconds};
        pendingPlayerTorpedoSeekerEmissions_.clear();
        nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;

        const Physics::PhysicsVector3 decoyPosition{
            .x = destroyerAcoustics.emitter.positionMeters.x - 20.0F,
            .y = destroyerAcoustics.emitter.positionMeters.y - M5CombatDecoyVerticalOffsetMeters,
            .z = destroyerAcoustics.emitter.positionMeters.z};
'@ 'runtime launch seeker reset and decoy separation'

$runtime = Replace-Once $runtime @'
    [[nodiscard]] static double Distance(
        const Physics::PhysicsVector3& first,
        const Physics::PhysicsVector3& second) noexcept
'@ @'
    [[nodiscard]] std::expected<std::optional<Weapons::TorpedoSeekerCue>, std::string> AdvancePlayerTorpedoSeeker(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const double simulationTimeSeconds)
    {
        if (!playerTorpedo_ || playerTorpedo_->movementDomain != Weapons::MovementDomain::Underwater ||
            !playerTorpedo_->positionMeters.IsFinite() || !destroyerAcoustics.emitter.positionMeters.IsFinite() ||
            !destroyerAcoustics.emitter.continuousSourceLevelDb.IsFinite() ||
            !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-E.1 seeker source/runtime input is invalid");
        }

        if (simulationTimeSeconds + 1.0e-9 >= nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_)
        {
            pendingPlayerTorpedoSeekerEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = destroyerAcoustics.emitter.positionMeters,
                .sourceLevelDb = destroyerAcoustics.emitter.continuousSourceLevelDb,
                .emissionTimeSeconds = simulationTimeSeconds});

            if (decoy_)
            {
                const auto decoyEmission = Weapons::SampleAcousticDecoyEmission(
                    decoyDefinition_, *decoy_, simulationTimeSeconds);
                if (!decoyEmission)
                {
                    return std::unexpected("M5-E.1 decoy emission snapshot failed: " + decoyEmission.error());
                }
                if (decoyEmission->has_value())
                {
                    pendingPlayerTorpedoSeekerEmissions_.push_back(**decoyEmission);
                }
            }
            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver seekerReceiver{
            .sensorId = "M5_PLAYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = playerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedObservation = false;
        auto emission = pendingPlayerTorpedoSeekerEmissions_.begin();
        while (emission != pendingPlayerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, seekerReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
            {
                return std::unexpected("M5-E.1 seeker emission arrival time is non-finite");
            }
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }

            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, seekerReceiver, simulationTimeSeconds);
            if (!observed)
            {
                return std::unexpected("M5-E.1 seeker acoustic propagation failed: " + observed.error().message);
            }
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !playerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-E.1 seeker perception integration failed");
                }
                integratedObservation = true;
            }
            emission = pendingPlayerTorpedoSeekerEmissions_.erase(emission);
        }

        if (!integratedObservation && !playerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-E.1 seeker TrackManager failed to advance");
        }

        return Weapons::SelectTorpedoSeekerCue(
            playerTorpedoSeekerConfig_,
            playerTorpedoSeekerState_,
            playerTorpedoSeekerTracks_.Tracks(),
            simulationTimeSeconds);
    }

    [[nodiscard]] static double Distance(
        const Physics::PhysicsVector3& first,
        const Physics::PhysicsVector3& second) noexcept
'@ 'runtime seeker helper'

$runtime = Replace-Once $runtime @'
    Perception::TrackManager playerTracks_;
    Perception::TrackManager destroyerTracks_;
    SimpleDestroyerDefinition destroyerDefinition_;
'@ @'
    Perception::TrackManager playerTracks_;
    Perception::TrackManager destroyerTracks_;
    Perception::TrackManager playerTorpedoSeekerTracks_;
    Weapons::TorpedoSeekerConfig playerTorpedoSeekerConfig_{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    Weapons::TorpedoSeekerRuntimeState playerTorpedoSeekerState_{};
    std::vector<Acoustics::AcousticEmission> pendingPlayerTorpedoSeekerEmissions_{};
    double nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;
    SimpleDestroyerDefinition destroyerDefinition_;
'@ 'runtime seeker members'

Write-Lf $runtimePath $runtime

$seekerPath = 'Simulation/Weapons/TorpedoSeeker.h'
$seeker = Read-Lf $seekerPath
$seeker = Replace-Once $seeker @'
    torpedo.headingRadians = WrapWeaponHeading(
        torpedo.headingRadians + std::clamp(headingDelta, -maximumTurn, maximumTurn));

    const float distanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);
'@ @'
    torpedo.headingRadians = WrapWeaponHeading(
        torpedo.headingRadians + std::clamp(headingDelta, -maximumTurn, maximumTurn));
    torpedo.headingRadians = ClampConventionalTorpedoVerticalCourse(
        torpedo.headingRadians, definition.maximumVerticalCourseAngleRadians);

    const float distanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);
'@ 'seeker vertical course clamp'

$seeker = Replace-Once $seeker @'
    torpedo.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}
} // namespace DeepRun::Weapons
'@ @'
    torpedo.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}

// M5-E.1 live composition variant: seeker-local bearing guidance still uses the same authoritative swept
// collision contract as ordinary torpedo guidance. The seeker never receives a target body; a PhysicsWorld hit
// is the first point where body identity can enter terminal weapon state.
[[nodiscard]] inline std::expected<std::optional<ConventionalTorpedoImpact>, std::string>
AdvanceConventionalTorpedoWithSeekerCueAndCollision(
    const ConventionalTorpedoDefinition& definition,
    const TorpedoSeekerConfig& seekerConfig,
    ConventionalTorpedoRuntimeState& torpedo,
    const TorpedoSeekerCue& cue,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredBody = {})
{
    if (torpedo.movementDomain != MovementDomain::Underwater || torpedo.impactedBody.has_value())
    {
        return std::unexpected("spent/non-underwater conventional torpedo cannot advance with seeker collision");
    }

    const Physics::PhysicsVector3 startPosition = torpedo.positionMeters;
    ConventionalTorpedoRuntimeState candidate = torpedo;
    const auto movement = AdvanceConventionalTorpedoWithSeekerCue(
        definition, seekerConfig, candidate, cue, simulationTimeSeconds);
    if (!movement)
    {
        return std::unexpected(movement.error());
    }

    const Physics::PhysicsVector3 displacement{
        .x = candidate.positionMeters.x - startPosition.x,
        .y = candidate.positionMeters.y - startPosition.y,
        .z = candidate.positionMeters.z - startPosition.z};
    if (displacement.x == 0.0F && displacement.y == 0.0F && displacement.z == 0.0F)
    {
        torpedo = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const auto sweep = physicsWorld.SweepBoxClosest(Physics::PhysicsBoxSweepQuery{
        .halfExtentsMeters = definition.collisionHalfExtentsMeters,
        .startPositionMeters = startPosition,
        .orientation = WeaponHeadingQuaternion(candidate.headingRadians),
        .displacementMeters = displacement,
        .ignoredBody = ignoredBody});
    if (!sweep)
    {
        return std::unexpected("torpedo seeker physics sweep failed: " + sweep.error().message);
    }
    if (!*sweep)
    {
        torpedo = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const Physics::PhysicsSweepHit hit = **sweep;
    candidate.positionMeters = hit.positionMeters;
    candidate.speedMetersPerSecond = 0.0F;
    candidate.movementDomain = MovementDomain::Spent;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;
    candidate.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    torpedo = candidate;

    return std::optional<ConventionalTorpedoImpact>{ConventionalTorpedoImpact{
        .physicsHit = hit,
        .damage = Combat::CombatDamageEvent{
            .targetBody = hit.body,
            .positionMeters = hit.positionMeters,
            .damage = definition.directImpactDamage,
            .simulationTimeSeconds = simulationTimeSeconds},
        .explosion = Combat::CombatExplosionEvent{
            .positionMeters = hit.positionMeters,
            .nominalDamage = definition.directImpactDamage,
            .radiusMeters = definition.explosionRadiusMeters,
            .simulationTimeSeconds = simulationTimeSeconds}}};
}
} // namespace DeepRun::Weapons
'@ 'seeker swept collision helper'
Write-Lf $seekerPath $seeker

$testPath = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$test = Read-Lf $testPath
$test = Replace-Once $test @'
    bool sawDecoy = false;
    bool sawImpact = false;
'@ @'
    bool sawDecoy = false;
    bool sawLiveSeekerSelection = false;
    bool sawDecoyDiversion = false;
    bool sawPostDecoyRecovery = false;
    bool sawImpact = false;
'@ 'test seeker flags'

$test = Replace-Once $test @'
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
        sawDecoy = sawDecoy || (runtime.Decoy().has_value() && runtime.Decoy()->active);

        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater)
'@ @'
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
        sawDecoy = sawDecoy || (runtime.Decoy().has_value() && runtime.Decoy()->active);
        sawLiveSeekerSelection = sawLiveSeekerSelection || runtime.PlayerTorpedoSeekerState().selectedTrackId.has_value();

        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater)
'@ 'test observe seeker selection'

$test = Replace-Once $test @'
            const float forwardProgress = torpedo.positionMeters.x - launchPosition->x;
            if (forwardProgress >= 120.0F &&
'@ @'
            const float forwardProgress = torpedo.positionMeters.x - launchPosition->x;
            if (forwardProgress > Game::Combat::M5CombatTorpedoStraightRunMeters + 20.0F &&
                runtime.Decoy() && runtime.Decoy()->active &&
                runtime.PlayerTorpedoSeekerState().selectedTrackId.has_value() && torpedo.headingRadians < -0.002F)
            {
                sawDecoyDiversion = true;
            }
            if (sawDecoyDiversion && runtime.Decoy() && !runtime.Decoy()->active && torpedo.headingRadians > 0.01F)
            {
                sawPostDecoyRecovery = true;
            }

            if (forwardProgress >= 120.0F &&
'@ 'test seeker diversion/recovery'

$test = Replace-Once $test @'
        !sawDecoy || !sawImpact || !sawPresentationTorpedo || !sawPresentationDecoy ||
        !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
'@ @'
        !sawDecoy || !sawLiveSeekerSelection || !sawDecoyDiversion || !sawPostDecoyRecovery ||
        !sawImpact || !sawPresentationTorpedo || !sawPresentationDecoy ||
        !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
'@ 'test require live seeker interaction'
Write-Lf $testPath $test

Write-Host 'M5-E.1 live decoy seeker patch applied.'
