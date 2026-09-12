from pathlib import Path

path = Path('Game/Combat/CombatPlaygroundRuntime.h')
text = path.read_text(encoding='utf-8')


def replace_one(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match, got {count}')
    text = text.replace(old, new, 1)


replace_one(
    'inline constexpr float M5CombatTorpedoSeekerAssociationGateRadians = 0.03F;\n',
    '''inline constexpr float M5CombatTorpedoSeekerAssociationGateRadians = 0.03F;
// Torpedo-local active seeker values are GAME POLICY only, not real weapon performance data. They exist to
// distinguish the much smaller local seeker from the carrier sonar while preserving the same acoustic simulator.
inline constexpr double M5CombatTorpedoActiveListenWindowSeconds = 4.0;
inline constexpr float M5CombatTorpedoActiveBeamHalfAngleRadians = 0.45F;
inline constexpr Acoustics::ActiveSonarConfig M5CombatTorpedoActiveSonarConfig{
    .bearingUncertaintyRadians = 0.06F,
    .minimumRangeUncertaintyMeters = 3.0F,
    .fractionalRangeUncertainty = 0.04F};
''',
    'constants')

player_manager = '''        const auto playerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.35,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.50F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 8U});
'''
replace_one(
    player_manager,
    player_manager + '''        const auto playerTorpedoActiveSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.50,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.35F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 2.0F,
            .maximumTracks = 8U});
''',
    'player active track manager')

destroyer_manager = '''        const auto destroyerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.35,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.50F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 0.0F,
            .maximumTracks = 8U});
'''
replace_one(
    destroyer_manager,
    destroyer_manager + '''        const auto destroyerTorpedoActiveSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{
            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,
            .observationsToConfirm = 1U,
            .coastAfterSeconds = 0.50,
            .lostAfterSeconds = 1.5,
            .confidenceDecayPerSecond = 0.35F,
            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
            .positionUncertaintyGrowthMetersPerSecond = 2.0F,
            .maximumTracks = 8U});
''',
    'destroyer active track manager')

replace_one(
    '''        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks ||
            !destroyerTorpedoSeekerTracks || !incomingThreatTracks)
''',
    '''        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks || !playerTorpedoActiveSeekerTracks ||
            !destroyerTorpedoSeekerTracks || !destroyerTorpedoActiveSeekerTracks || !incomingThreatTracks)
''',
    'manager validation')

replace_one(
    '''            *playerTorpedoSeekerTracks,
            *destroyerTorpedoSeekerTracks,
            *incomingThreatTracks,
''',
    '''            *playerTorpedoSeekerTracks,
            *playerTorpedoActiveSeekerTracks,
            *destroyerTorpedoSeekerTracks,
            *destroyerTorpedoActiveSeekerTracks,
            *incomingThreatTracks,
''',
    'constructor call')

replace_one(
    '''        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        Perception::TrackManager playerTorpedoSeekerTracks,
        Perception::TrackManager destroyerTorpedoSeekerTracks,
        Perception::TrackManager incomingThreatTracks,
''',
    '''        Perception::TrackManager playerTracks,
        Perception::TrackManager destroyerTracks,
        Perception::TrackManager playerTorpedoSeekerTracks,
        Perception::TrackManager playerTorpedoActiveSeekerTracks,
        Perception::TrackManager destroyerTorpedoSeekerTracks,
        Perception::TrackManager destroyerTorpedoActiveSeekerTracks,
        Perception::TrackManager incomingThreatTracks,
''',
    'constructor signature')

replace_one(
    '''          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),
          destroyerTorpedoSeekerTracks_(std::move(destroyerTorpedoSeekerTracks)),
          incomingThreatTracks_(std::move(incomingThreatTracks)),
''',
    '''          playerTracks_(std::move(playerTracks)),
          destroyerTracks_(std::move(destroyerTracks)),
          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),
          playerTorpedoActiveSeekerTracks_(std::move(playerTorpedoActiveSeekerTracks)),
          destroyerTorpedoSeekerTracks_(std::move(destroyerTorpedoSeekerTracks)),
          destroyerTorpedoActiveSeekerTracks_(std::move(destroyerTorpedoActiveSeekerTracks)),
          incomingThreatTracks_(std::move(incomingThreatTracks)),
''',
    'constructor initializer')

replace_one(
    '''            const auto seekerCue = AdvancePlayerTorpedoSeeker(*destroyerAcoustics, simulationTimeSeconds);
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
''',
    '''            const auto seekerDecision = AdvancePlayerTorpedoSeeker(*destroyerAcoustics, simulationTimeSeconds);
            if (!seekerDecision)
            {
                return std::unexpected("M5-E.1 live torpedo seeker failed: " + seekerDecision.error());
            }

            const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), playerTorpedo_->guidanceTrackId);
            const float forwardProgressMeters = playerTorpedoLaunchPosition_
                ? (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_
                : 0.0F;
            const bool seekerEnabled = forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
            // Before seeker enable the launch-platform fire-control Track may update the onboard aim point. Once
            // enabled, loss of local contact never falls back to live carrier Track updates: the weapon continues
            // from its last onboard solution until its own passive/active seeker reacquires something.
            const auto guidanceTrack = seekerEnabled
                ? std::optional<Perception::Track>{}
                : BuildPlayerTorpedoGuidanceTrack(perceivedTrack);

            const auto advanced = seekerDecision->guidanceCue
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    playerTorpedoDefinition_,
                    playerTorpedoSeekerConfig_,
                    *playerTorpedo_,
                    *seekerDecision->guidanceCue,
                    *physicsWorld_,
                    simulationTimeSeconds)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);
''',
    'player advance')

replace_one(
    '''            const auto seekerCue = AdvanceDestroyerTorpedoSeeker(playerSnapshot, simulationTimeSeconds);
            if (!seekerCue)
            {
                return std::unexpected("M5-J4 destroyer torpedo seeker failed: " + seekerCue.error());
            }
            const auto perceivedTrack = FindTrack(destroyerTracks_.Tracks(), destroyerTorpedo_->guidanceTrackId);
            const float forwardProgressMeters = destroyerTorpedoLaunchPosition_
                ? (destroyerTorpedo_->positionMeters.x - destroyerTorpedoLaunchPosition_->x) *
                    destroyerTorpedoForwardSign_
                : 0.0F;
            const bool localSeekerOwnsCourse =
                forwardProgressMeters >= M5CombatTorpedoStraightRunMeters && seekerCue->has_value();
            const auto advanced = localSeekerOwnsCourse
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    destroyerTorpedoDefinition_,
                    destroyerTorpedoSeekerConfig_,
                    *destroyerTorpedo_,
                    **seekerCue,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    destroyerTorpedoDefinition_,
                    *destroyerTorpedo_,
                    perceivedTrack,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body);
''',
    '''            const auto seekerDecision = AdvanceDestroyerTorpedoSeeker(playerSnapshot, simulationTimeSeconds);
            if (!seekerDecision)
            {
                return std::unexpected("M5-J4 destroyer torpedo seeker failed: " + seekerDecision.error());
            }
            const auto perceivedTrack = FindTrack(destroyerTracks_.Tracks(), destroyerTorpedo_->guidanceTrackId);
            const float forwardProgressMeters = destroyerTorpedoLaunchPosition_
                ? (destroyerTorpedo_->positionMeters.x - destroyerTorpedoLaunchPosition_->x) *
                    destroyerTorpedoForwardSign_
                : 0.0F;
            const bool seekerEnabled = forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
            const auto guidanceTrack = seekerEnabled ? std::optional<Perception::Track>{} : perceivedTrack;
            const auto advanced = seekerDecision->guidanceCue
                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(
                    destroyerTorpedoDefinition_,
                    destroyerTorpedoSeekerConfig_,
                    *destroyerTorpedo_,
                    *seekerDecision->guidanceCue,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body)
                : Weapons::AdvanceConventionalTorpedoWithCollision(
                    destroyerTorpedoDefinition_,
                    *destroyerTorpedo_,
                    guidanceTrack,
                    *physicsWorld_,
                    simulationTimeSeconds,
                    destroyer_.body);
''',
    'destroyer advance')

replace_one(
    '''        pendingPlayerTorpedoSeekerEmissions_.clear();
        nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;

        const Physics::PhysicsVector3 decoyPosition{
''',
    '''        pendingPlayerTorpedoSeekerEmissions_.clear();
        nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        playerTorpedoActivePulse_.reset();
        playerTorpedoActiveReflector_.reset();
        playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;

        const Physics::PhysicsVector3 decoyPosition{
''',
    'player launch reset')

replace_one(
    '''        pendingDestroyerTorpedoSeekerEmissions_.clear();
        nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        pendingIncomingThreatEmissions_.clear();
''',
    '''        pendingDestroyerTorpedoSeekerEmissions_.clear();
        nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;
        destroyerTorpedoActivePulse_.reset();
        destroyerTorpedoActiveReflector_.reset();
        destroyerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;
        pendingIncomingThreatEmissions_.clear();
''',
    'destroyer launch reset')

replace_one(
    '                pendingPlayerTorpedoSeekerEmissions_.clear();\n                if (impact->physicsHit.body != destroyer_.body)',
    '                pendingPlayerTorpedoSeekerEmissions_.clear();\n                playerTorpedoActivePulse_.reset();\n                playerTorpedoActiveReflector_.reset();\n                if (impact->physicsHit.body != destroyer_.body)',
    'player impact cleanup')
replace_one(
    '                pendingDestroyerTorpedoSeekerEmissions_.clear();\n                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)',
    '                pendingDestroyerTorpedoSeekerEmissions_.clear();\n                destroyerTorpedoActivePulse_.reset();\n                destroyerTorpedoActiveReflector_.reset();\n                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)',
    'destroyer impact cleanup')

player_start_marker = '    [[nodiscard]] std::expected<std::optional<Weapons::TorpedoSeekerCue>, std::string> AdvancePlayerTorpedoSeeker('
destroyer_start_marker = '    [[nodiscard]] std::expected<std::optional<Weapons::TorpedoSeekerCue>, std::string>\n    AdvanceDestroyerTorpedoSeeker('
player_start = text.index(player_start_marker)
player_end = text.index(destroyer_start_marker, player_start)
player_func = r'''    [[nodiscard]] std::expected<Weapons::TorpedoSeekerModeDecision, std::string> AdvancePlayerTorpedoSeeker(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const double simulationTimeSeconds)
    {
        if (!playerTorpedo_ || playerTorpedo_->movementDomain != Weapons::MovementDomain::Underwater ||
            !playerTorpedo_->positionMeters.IsFinite() || !destroyerAcoustics.emitter.positionMeters.IsFinite() ||
            !destroyerAcoustics.emitter.continuousSourceLevelDb.IsFinite() || !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-E.1 seeker source/runtime input is invalid");
        }

        const float forwardProgressMeters = playerTorpedoLaunchPosition_
            ? (playerTorpedo_->positionMeters.x - playerTorpedoLaunchPosition_->x) * playerTorpedoForwardSign_
            : 0.0F;
        const bool seekerEnabled = playerTorpedoLaunchPosition_.has_value() &&
            forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
        if (!seekerEnabled)
        {
            return Weapons::UpdateTorpedoSeekerMode(
                playerTorpedoSeekerConfig_, playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_,
                false, std::nullopt, std::nullopt, simulationTimeSeconds);
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
                    return std::unexpected("M5-E.1 decoy emission snapshot failed: " + decoyEmission.error());
                if (decoyEmission->has_value())
                    pendingPlayerTorpedoSeekerEmissions_.push_back(**decoyEmission);
            }
            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver passiveReceiver{
            .sensorId = "M5_PLAYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = playerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedPassiveObservation = false;
        auto emission = pendingPlayerTorpedoSeekerEmissions_.begin();
        while (emission != pendingPlayerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
                return std::unexpected("M5-E.1 seeker emission arrival time is non-finite");
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds);
            if (!observed)
                return std::unexpected("M5-E.1 seeker acoustic propagation failed: " + observed.error().message);
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !playerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5-E.1 seeker perception integration failed");
                integratedPassiveObservation = true;
            }
            emission = pendingPlayerTorpedoSeekerEmissions_.erase(emission);
        }
        if (!integratedPassiveObservation && !playerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5-E.1 passive seeker TrackManager failed to advance");

        bool integratedActiveObservation = false;
        if (playerTorpedoActivePulse_ && playerTorpedoActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = passiveReceiver;
            activeReceiver.sensorId = "M5_PLAYER_TORPEDO_ACTIVE_SEEKER";
            activeReceiver.positionMeters = playerTorpedoActivePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *playerTorpedoActivePulse_, *playerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, {}, {}, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)
                return std::unexpected("M5 torpedo active echo failed: " + activeEcho.error());
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, playerTorpedoActivePulse_->originMeters);
                if (!perceived || !playerTorpedoActiveSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5 torpedo active echo failed perception integration");
                integratedActiveObservation = true;
                playerTorpedoActivePulse_.reset();
                playerTorpedoActiveReflector_.reset();
            }
            else if (simulationTimeSeconds + 1.0e-9 >= playerTorpedoActivePulseDeadlineSeconds_)
            {
                playerTorpedoActivePulse_.reset();
                playerTorpedoActiveReflector_.reset();
            }
        }
        if (!integratedActiveObservation && !playerTorpedoActiveSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5 torpedo active seeker TrackManager failed to advance");

        const auto passiveCue = Weapons::SelectBestTorpedoSeekerCue(
            playerTorpedoSeekerConfig_, playerTorpedoSeekerTracks_.Tracks());
        const auto activeCue = Weapons::SelectBestTorpedoSeekerCue(
            playerTorpedoSeekerConfig_, playerTorpedoActiveSeekerTracks_.Tracks());
        if (!passiveCue || !activeCue)
            return std::unexpected("M5 torpedo local seeker cue selection failed");
        auto decision = Weapons::UpdateTorpedoSeekerMode(
            playerTorpedoSeekerConfig_, playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_,
            true, *passiveCue, *activeCue, simulationTimeSeconds);
        if (!decision)
            return decision;

        if (decision->requestActivePing && !playerTorpedoActivePulse_)
        {
            playerTorpedoActivePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = playerTorpedo_->positionMeters,
                .forwardUnitVector = {
                    .x = static_cast<float>(std::cos(static_cast<double>(playerTorpedo_->headingRadians))),
                    .y = static_cast<float>(std::sin(static_cast<double>(playerTorpedo_->headingRadians))),
                    .z = 0.0F},
                .sourceLevelDb = {.levelDb = {188.0F, 191.0F, 193.0F, 189.0F}},
                .beamHalfAngleRadians = M5CombatTorpedoActiveBeamHalfAngleRadians,
                .emissionTimeSeconds = simulationTimeSeconds};
            // Ground truth is confined to reflector input of the acoustic simulator and never enters guidance.
            playerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                .positionMeters = destroyerAcoustics.emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            playerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
            const auto notified = Weapons::NotifyTorpedoSeekerActivePingEmitted(
                playerTorpedoSeekerModeConfig_, playerTorpedoSeekerState_, simulationTimeSeconds);
            if (!notified)
                return std::unexpected(notified.error());
        }
        return decision;
    }

'''
text = text[:player_start] + player_func + text[player_end:]

destroyer_start = text.index(destroyer_start_marker, player_start + len(player_func))
destroyer_end_marker = '    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception('
destroyer_end = text.index(destroyer_end_marker, destroyer_start)
destroyer_func = r'''    [[nodiscard]] std::expected<Weapons::TorpedoSeekerModeDecision, std::string>
    AdvanceDestroyerTorpedoSeeker(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!destroyerTorpedo_ || destroyerTorpedo_->movementDomain != Weapons::MovementDomain::Underwater ||
            !destroyerTorpedo_->positionMeters.IsFinite() || !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.continuousSourceLevelDb.IsFinite() || !std::isfinite(simulationTimeSeconds))
        {
            return std::unexpected("M5-J4 hostile seeker source/runtime input is invalid");
        }

        const float forwardProgressMeters = destroyerTorpedoLaunchPosition_
            ? (destroyerTorpedo_->positionMeters.x - destroyerTorpedoLaunchPosition_->x) * destroyerTorpedoForwardSign_
            : 0.0F;
        const bool seekerEnabled = destroyerTorpedoLaunchPosition_.has_value() &&
            forwardProgressMeters >= M5CombatTorpedoStraightRunMeters;
        if (!seekerEnabled)
        {
            return Weapons::UpdateTorpedoSeekerMode(
                destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_,
                false, std::nullopt, std::nullopt, simulationTimeSeconds);
        }

        if (simulationTimeSeconds + 1.0e-9 >= nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_)
        {
            pendingDestroyerTorpedoSeekerEmissions_.push_back(Acoustics::AcousticEmission{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .sourceLevelDb = playerSnapshot.emitter.continuousSourceLevelDb,
                .emissionTimeSeconds = simulationTimeSeconds});
            if (playerDecoy_)
            {
                const auto decoyEmission = Weapons::SampleAcousticDecoyEmission(
                    playerDecoyDefinition_, *playerDecoy_, simulationTimeSeconds);
                if (!decoyEmission)
                    return std::unexpected("M5-J4 player decoy emission snapshot failed: " + decoyEmission.error());
                if (decoyEmission->has_value())
                    pendingDestroyerTorpedoSeekerEmissions_.push_back(**decoyEmission);
            }
            nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver passiveReceiver{
            .sensorId = "M5_DESTROYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = destroyerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedPassiveObservation = false;
        auto emission = pendingDestroyerTorpedoSeekerEmissions_.begin();
        while (emission != pendingDestroyerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, passiveReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
                return std::unexpected("M5-J4 hostile seeker emission arrival time is non-finite");
            if (simulationTimeSeconds + 1.0e-9 < arrivalTimeSeconds)
            {
                ++emission;
                continue;
            }
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds);
            if (!observed)
                return std::unexpected("M5-J4 hostile seeker acoustic propagation failed: " + observed.error().message);
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !destroyerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5-J4 hostile seeker perception integration failed");
                integratedPassiveObservation = true;
            }
            emission = pendingDestroyerTorpedoSeekerEmissions_.erase(emission);
        }
        if (!integratedPassiveObservation && !destroyerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5-J4 hostile passive seeker TrackManager failed to advance");

        bool integratedActiveObservation = false;
        if (destroyerTorpedoActivePulse_ && destroyerTorpedoActiveReflector_)
        {
            Acoustics::AcousticReceiver activeReceiver = passiveReceiver;
            activeReceiver.sensorId = "M5_DESTROYER_TORPEDO_ACTIVE_SEEKER";
            activeReceiver.positionMeters = destroyerTorpedoActivePulse_->originMeters;
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *destroyerTorpedoActivePulse_, *destroyerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, {}, {}, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)
                return std::unexpected("M5 hostile torpedo active echo failed: " + activeEcho.error());
            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, destroyerTorpedoActivePulse_->originMeters);
                if (!perceived || !destroyerTorpedoActiveSeekerTracks_.IntegrateObservation(*perceived))
                    return std::unexpected("M5 hostile torpedo active echo failed perception integration");
                integratedActiveObservation = true;
                destroyerTorpedoActivePulse_.reset();
                destroyerTorpedoActiveReflector_.reset();
            }
            else if (simulationTimeSeconds + 1.0e-9 >= destroyerTorpedoActivePulseDeadlineSeconds_)
            {
                destroyerTorpedoActivePulse_.reset();
                destroyerTorpedoActiveReflector_.reset();
            }
        }
        if (!integratedActiveObservation && !destroyerTorpedoActiveSeekerTracks_.AdvanceTo(simulationTimeSeconds))
            return std::unexpected("M5 hostile torpedo active seeker TrackManager failed to advance");

        const auto passiveCue = Weapons::SelectBestTorpedoSeekerCue(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerTracks_.Tracks());
        const auto activeCue = Weapons::SelectBestTorpedoSeekerCue(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoActiveSeekerTracks_.Tracks());
        if (!passiveCue || !activeCue)
            return std::unexpected("M5 hostile torpedo local seeker cue selection failed");
        auto decision = Weapons::UpdateTorpedoSeekerMode(
            destroyerTorpedoSeekerConfig_, destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_,
            true, *passiveCue, *activeCue, simulationTimeSeconds);
        if (!decision)
            return decision;

        if (decision->requestActivePing && !destroyerTorpedoActivePulse_)
        {
            destroyerTorpedoActivePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = destroyerTorpedo_->positionMeters,
                .forwardUnitVector = {
                    .x = static_cast<float>(std::cos(static_cast<double>(destroyerTorpedo_->headingRadians))),
                    .y = static_cast<float>(std::sin(static_cast<double>(destroyerTorpedo_->headingRadians))),
                    .z = 0.0F},
                .sourceLevelDb = {.levelDb = {188.0F, 191.0F, 193.0F, 189.0F}},
                .beamHalfAngleRadians = M5CombatTorpedoActiveBeamHalfAngleRadians,
                .emissionTimeSeconds = simulationTimeSeconds};
            destroyerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            destroyerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
            const auto notified = Weapons::NotifyTorpedoSeekerActivePingEmitted(
                destroyerTorpedoSeekerModeConfig_, destroyerTorpedoSeekerState_, simulationTimeSeconds);
            if (!notified)
                return std::unexpected(notified.error());
        }
        return decision;
    }

'''
text = text[:destroyer_start] + destroyer_func + text[destroyer_end:]

replace_one(
    '''    Perception::TrackManager playerTracks_;
    Perception::TrackManager destroyerTracks_;
    Perception::TrackManager playerTorpedoSeekerTracks_;
    Perception::TrackManager destroyerTorpedoSeekerTracks_;
    Perception::TrackManager incomingThreatTracks_;
''',
    '''    Perception::TrackManager playerTracks_;
    Perception::TrackManager destroyerTracks_;
    Perception::TrackManager playerTorpedoSeekerTracks_;
    Perception::TrackManager playerTorpedoActiveSeekerTracks_;
    Perception::TrackManager destroyerTorpedoSeekerTracks_;
    Perception::TrackManager destroyerTorpedoActiveSeekerTracks_;
    Perception::TrackManager incomingThreatTracks_;
''',
    'track manager members')

player_state = '''    Weapons::TorpedoSeekerConfig playerTorpedoSeekerConfig_{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    Weapons::TorpedoSeekerRuntimeState playerTorpedoSeekerState_{};
    std::vector<Acoustics::AcousticEmission> pendingPlayerTorpedoSeekerEmissions_{};
    double nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;
'''
replace_one(
    player_state,
    player_state + '''    Weapons::TorpedoSeekerModeConfig playerTorpedoSeekerModeConfig_{
        .passiveSearchBeforeActiveSeconds = 1.5,
        .activePingIntervalSeconds = 1.0,
        .lostContactBeforeActiveSeconds = 0.35,
        .maximumSearchWithoutContactSeconds = 30.0,
        .preferPassiveCue = true};
    std::optional<Acoustics::ActiveAcousticPulse> playerTorpedoActivePulse_{};
    std::optional<Acoustics::AcousticReflector> playerTorpedoActiveReflector_{};
    double playerTorpedoActivePulseDeadlineSeconds_ = 0.0;
''',
    'player seeker members')

destroyer_state = '''    Weapons::TorpedoSeekerConfig destroyerTorpedoSeekerConfig_{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    Weapons::TorpedoSeekerRuntimeState destroyerTorpedoSeekerState_{};
    std::vector<Acoustics::AcousticEmission> pendingDestroyerTorpedoSeekerEmissions_{};
    double nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;
'''
replace_one(
    destroyer_state,
    destroyer_state + '''    Weapons::TorpedoSeekerModeConfig destroyerTorpedoSeekerModeConfig_{
        .passiveSearchBeforeActiveSeconds = 1.5,
        .activePingIntervalSeconds = 1.0,
        .lostContactBeforeActiveSeconds = 0.35,
        .maximumSearchWithoutContactSeconds = 30.0,
        .preferPassiveCue = true};
    std::optional<Acoustics::ActiveAcousticPulse> destroyerTorpedoActivePulse_{};
    std::optional<Acoustics::AcousticReflector> destroyerTorpedoActiveReflector_{};
    double destroyerTorpedoActivePulseDeadlineSeconds_ = 0.0;
''',
    'destroyer seeker members')

path.write_text(text, encoding='utf-8')
