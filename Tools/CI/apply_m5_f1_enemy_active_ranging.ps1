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
    if ($index -lt 0) { throw "Anchor not found: $Label" }
    if ($Content.IndexOf($oldLf, $index + $oldLf.Length, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Anchor is not unique: $Label"
    }
    return $Content.Substring(0, $index) + $newLf + $Content.Substring($index + $oldLf.Length)
}

$runtimePath = 'Game/Combat/CombatPlaygroundRuntime.h'
$runtime = Read-Lf $runtimePath

$runtime = Replace-Once $runtime @'
inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;
'@ @'
inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;
inline constexpr double M5CombatDestroyerActiveRangingIntervalSeconds = 6.0;
'@ 'destroyer active-ranging interval'

$oldDecision = @'
        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
'@
$newDecision = @'
        // M5-F.1: active ranging is initiated from perceived passive awareness only. The beam direction comes
        // from the destroyer's Track; player ground truth is visible solely to the acoustic simulator as a
        // reflector. The returned active echo is converted back into ordinary ranged perceived evidence before
        // the existing Track-only combat controller is allowed to make a fire-control decision.
        if (!destroyerActivePulse_ && destroyer_.weapon.phase != Weapons::WeaponPhase::Launched &&
            simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
        {
            const auto awarenessTrack = SelectBestSimpleDestroyerTrack(
                destroyerDefinition_.combat, destroyerTracks_.Tracks());
            if (awarenessTrack)
            {
                const float bearing = awarenessTrack->estimatedBearingRadians;
                destroyerActivePulse_ = Acoustics::ActiveAcousticPulse{
                    .originMeters = destroyerAcoustics->passiveReceiver.positionMeters,
                    .forwardUnitVector = {
                        .x = static_cast<float>(std::cos(static_cast<double>(bearing))),
                        .y = static_cast<float>(std::sin(static_cast<double>(bearing))),
                        .z = 0.0F},
                    .sourceLevelDb = {.levelDb = {228.0F, 232.0F, 234.0F, 230.0F}},
                    .beamHalfAngleRadians = 0.30F,
                    .emissionTimeSeconds = simulationTimeSeconds};
                destroyerActiveReflector_ = Acoustics::AcousticReflector{
                    .positionMeters = playerSnapshot.emitter.positionMeters,
                    .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            }
        }

        if (destroyerActivePulse_ && destroyerActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = destroyerAcoustics->passiveReceiver;
            activeReceiver.positionMeters = destroyerActivePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_,
                *destroyerActivePulse_,
                *destroyerActiveReflector_,
                activeReceiver,
                simulationTimeSeconds);
            if (!activeEcho)
            {
                return std::unexpected("M5-F.1 destroyer active echo failed: " + activeEcho.error());
            }
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, destroyerActivePulse_->originMeters);
                if (!perceived || !destroyerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-F.1 destroyer ranged evidence failed perception integration");
                }
                destroyerActivePulse_.reset();
                destroyerActiveReflector_.reset();
                nextDestroyerActivePulseTimeSeconds_ =
                    simulationTimeSeconds + M5CombatDestroyerActiveRangingIntervalSeconds;
            }
        }

        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
'@
$runtime = Replace-Once $runtime $oldDecision $newDecision 'destroyer active-ranging composition'

$runtime = Replace-Once $runtime @'
          decoyDefinition_(std::move(decoyDefinition)),
          nextActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds)
'@ @'
          decoyDefinition_(std::move(decoyDefinition)),
          nextActivePulseTimeSeconds_(simulationTimeSeconds),
          nextDestroyerActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds)
'@ 'destroyer active-ranging constructor init'

$runtime = Replace-Once $runtime @'
    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    double nextActivePulseTimeSeconds_ = 0.0;
'@ @'
    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    double nextActivePulseTimeSeconds_ = 0.0;
    std::optional<Acoustics::ActiveAcousticPulse> destroyerActivePulse_{};
    std::optional<Acoustics::AcousticReflector> destroyerActiveReflector_{};
    double nextDestroyerActivePulseTimeSeconds_ = 0.0;
'@ 'destroyer active-ranging members'
Write-Lf $runtimePath $runtime

$testPath = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$test = Read-Lf $testPath
$test = Replace-Once $test @'
    bool sawDestroyerAwareness = false;
    bool sawDestroyerPreparation = false;
'@ @'
    bool sawDestroyerAwareness = false;
    bool sawDestroyerBearingOnlyAwareness = false;
    bool sawDestroyerSpatialFireControlTrack = false;
    bool sawDestroyerPreparation = false;
    bool sawDestroyerLaunch = false;
'@ 'destroyer ranging test flags'

$oldTrackLoop = @'
        for (const auto& track : frame->destroyerTracks)
        {
            sawDestroyerAwareness = sawDestroyerAwareness ||
                track.lifecycle == Perception::TrackLifecycleState::Confirmed;
            // Passive destroyer awareness must remain bearing-only in this live composition.
            if (track.estimatedPositionMeters.has_value())
            {
                return false;
            }
        }
        sawDestroyerPreparation = sawDestroyerPreparation ||
            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;
'@
$newTrackLoop = @'
        for (const auto& track : frame->destroyerTracks)
        {
            if (track.lifecycle == Perception::TrackLifecycleState::Confirmed)
            {
                sawDestroyerAwareness = true;
                if (track.estimatedPositionMeters.has_value())
                {
                    if (!track.positionUncertaintyMeters.has_value() ||
                        !std::isfinite(*track.positionUncertaintyMeters) || *track.positionUncertaintyMeters <= 0.0F)
                    {
                        return false;
                    }
                    sawDestroyerSpatialFireControlTrack = true;
                }
                else
                {
                    sawDestroyerBearingOnlyAwareness = true;
                }
            }
        }
        sawDestroyerPreparation = sawDestroyerPreparation ||
            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;
        if (frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::LaunchWeapon)
        {
            const bool launchHadQualifiedPerceivedTrack = frame->destroyerDecision.perceivedTrackId.has_value() &&
                std::ranges::any_of(frame->destroyerTracks, [&runtime, &frame](const auto& track) {
                    return track.trackId == *frame->destroyerDecision.perceivedTrackId &&
                        Weapons::ValidateTrackForWeapon(runtime.DestroyerDefinition().weapon, track).has_value();
                });
            if (!launchHadQualifiedPerceivedTrack ||
                runtime.Destroyer().weapon.targetTrackId != frame->destroyerDecision.perceivedTrackId)
            {
                return false;
            }
            sawDestroyerLaunch = true;
        }
'@
$test = Replace-Once $test $oldTrackLoop $newTrackLoop 'destroyer ranged track and launch gate'

$test = Replace-Once $test @'
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerPreparation || !sawTorpedo ||
'@ @'
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerBearingOnlyAwareness ||
        !sawDestroyerSpatialFireControlTrack || !sawDestroyerPreparation || !sawDestroyerLaunch || !sawTorpedo ||
'@ 'require destroyer ranging sequence'

$test = Replace-Once $test @'
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase == Weapons::WeaponPhase::Launched ||
        runtime.Destroyer().integrity.destroyed ||
'@ @'
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase != Weapons::WeaponPhase::Launched ||
        !runtime.Destroyer().weapon.targetTrackId.has_value() ||
        runtime.Destroyer().integrity.destroyed ||
'@ 'require legitimate enemy launch final state'

$test = Replace-Once $test @'
    // The destroyer AI heard and reacted to the player but never received a ranged target solution, so it must
    // not launch merely because the scenario simulator knows where the player is.
    if (runtime.Destroyer().weapon.targetTrackId.has_value())
    {
        return false;
    }

    return physicsWorld.DestroyBody(runtime.Destroyer().body);
'@ @'
    // M5-F.1 closes the previous bearing-only boundary deliberately: launch is now legal only because a
    // monostatic active echo produced ranged perceived evidence. The controller still receives no player body,
    // Transform or scenario-ground-truth position.
    return physicsWorld.DestroyBody(runtime.Destroyer().body);
'@ 'replace old no-launch conclusion'
Write-Lf $testPath $test

Write-Host 'M5-F.1 enemy active-ranging patch applied.'
