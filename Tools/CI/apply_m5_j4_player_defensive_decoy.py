from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def write_new(path: str, content: str) -> None:
    p = ROOT / path
    if p.exists():
        raise RuntimeError(f"{path}: already exists")
    p.write_text(content, encoding="utf-8")


# Platform/input semantic action: B / F -> DeployDecoy.
replace_once(
    "Engine/Platform/Window.h",
    "    R,\n    Left,",
    "    R,\n    F,\n    Left,")
replace_once(
    "Engine/Platform/Windows/WinWindow.cpp",
    "    case 'R': return Key::R;\n    case VK_LEFT:",
    "    case 'R': return Key::R;\n    case 'F': return Key::F;\n    case VK_LEFT:")
replace_once(
    "Engine/Input/InputState.h",
    "    PrepareWeapon,\n    FireWeapon,\n    Count,",
    "    PrepareWeapon,\n    FireWeapon,\n    DeployDecoy,\n    Count,")
replace_once(
    "Engine/Input/InputSystem.h",
    "    bool prepareWeapon = false;\n    bool fireWeapon = false;",
    "    bool prepareWeapon = false;\n    bool fireWeapon = false;\n    bool deployDecoy = false;")
replace_once(
    "Engine/Input/InputSystem.h",
    "    bool prepareWeaponKeyDown_ = false;\n    bool fireWeaponKeyDown_ = false;",
    "    bool prepareWeaponKeyDown_ = false;\n    bool fireWeaponKeyDown_ = false;\n    bool deployDecoyKeyDown_ = false;")
replace_once(
    "Engine/Input/InputSystem.cpp",
    "    return ControllerSemanticActions{\n        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),\n        .prepareWeapon = HasGamepadButton(gamepad, GamepadButton::X),\n        .fireWeapon = HasGamepadButton(gamepad, GamepadButton::A)};",
    "    return ControllerSemanticActions{\n        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),\n        .prepareWeapon = HasGamepadButton(gamepad, GamepadButton::X),\n        .fireWeapon = HasGamepadButton(gamepad, GamepadButton::A),\n        .deployDecoy = HasGamepadButton(gamepad, GamepadButton::B)};")
replace_once(
    "Engine/Input/InputSystem.cpp",
    "            else if (event.key == Platform::Key::Space)\n            {\n                fireWeaponKeyDown_ = true;\n            }\n            else if (event.key == Platform::Key::A)",
    "            else if (event.key == Platform::Key::Space)\n            {\n                fireWeaponKeyDown_ = true;\n            }\n            else if (event.key == Platform::Key::F)\n            {\n                deployDecoyKeyDown_ = true;\n            }\n            else if (event.key == Platform::Key::A)")
replace_once(
    "Engine/Input/InputSystem.cpp",
    "            else if (event.key == Platform::Key::Space)\n            {\n                fireWeaponKeyDown_ = false;\n            }\n            else if (event.key == Platform::Key::A)",
    "            else if (event.key == Platform::Key::Space)\n            {\n                fireWeaponKeyDown_ = false;\n            }\n            else if (event.key == Platform::Key::F)\n            {\n                deployDecoyKeyDown_ = false;\n            }\n            else if (event.key == Platform::Key::A)")
replace_once(
    "Engine/Input/InputSystem.cpp",
    "    state_.SetActionDown(\n        InputAction::FireWeapon,\n        fireWeaponKeyDown_ ||\n            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||\n            controller.fireWeapon);",
    "    state_.SetActionDown(\n        InputAction::FireWeapon,\n        fireWeaponKeyDown_ ||\n            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||\n            controller.fireWeapon);\n    state_.SetActionDown(InputAction::DeployDecoy, deployDecoyKeyDown_ || controller.deployDecoy);")

# Commander semantic command/presentation state.
replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "    PrepareWeapon,\n    FireWeapon,\n};",
    "    PrepareWeapon,\n    FireWeapon,\n    DeployDecoy,\n};")
replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "    std::optional<float> incomingThreatConfidence{};\n    std::optional<PlayerCombatCommandFeedback> lastCommand{};",
    "    std::optional<float> incomingThreatConfidence{};\n    bool canDeployDecoy = false;\n    bool playerDecoyActive = false;\n    std::optional<PlayerCombatCommandFeedback> lastCommand{};")
replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "        case PlayerCombatCommandType::FireWeapon:\n            return Fire(tracks, simulationTimeSeconds);\n        }",
    "        case PlayerCombatCommandType::FireWeapon:\n            return Fire(tracks, simulationTimeSeconds);\n        case PlayerCombatCommandType::DeployDecoy:\n            return std::unexpected(\"M5-J4 DeployDecoy is owned by CombatPlaygroundRuntime, not weapon runtime\");\n        }")

# Runtime: second local seeker, one-shot player decoy, and command feedback projection.
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;\ninline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;",
    "inline constexpr float M5CombatDecoyVerticalOffsetMeters = 120.0F;\ninline constexpr float M5CombatPlayerDecoyVerticalOffsetMeters = 120.0F;\ninline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        const auto incomingThreatTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{",
    "        const auto destroyerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{\n            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,\n            .observationsToConfirm = 1U,\n            .coastAfterSeconds = 0.35,\n            .lostAfterSeconds = 1.5,\n            .confidenceDecayPerSecond = 0.50F,\n            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,\n            .positionUncertaintyGrowthMetersPerSecond = 0.0F,\n            .maximumTracks = 8U});\n        const auto incomingThreatTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks || !incomingThreatTracks)\n        {\n            return std::unexpected(\"M5-H/M5-E.1/M5-J3 perception manager creation failed\");\n        }",
    "        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks ||\n            !destroyerTorpedoSeekerTracks || !incomingThreatTracks)\n        {\n            return std::unexpected(\"M5-H/M5-E.1/M5-J3/M5-J4 perception manager creation failed\");\n        }")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        const Weapons::AcousticDecoyDefinition decoyDefinition{\n            .id = \"m5.live-acoustic-decoy\",\n            .continuousSourceLevelDb = {.levelDb = {158.0F, 154.0F, 149.0F, 143.0F}},\n            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},\n            .activeLifetimeSeconds = 10.0};",
    "        const Weapons::AcousticDecoyDefinition decoyDefinition{\n            .id = \"m5.live-acoustic-decoy\",\n            .continuousSourceLevelDb = {.levelDb = {158.0F, 154.0F, 149.0F, 143.0F}},\n            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},\n            .activeLifetimeSeconds = 10.0};\n        const Weapons::AcousticDecoyDefinition playerDecoyDefinition{\n            .id = \"m5.live-player-acoustic-decoy\",\n            .continuousSourceLevelDb = {.levelDb = {172.0F, 168.0F, 162.0F, 156.0F}},\n            .driftVelocityMetersPerSecond = {.x = -1.0F, .y = -0.25F, .z = 0.0F},\n            .activeLifetimeSeconds = 12.0};")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "            *playerTorpedoSeekerTracks,\n            *incomingThreatTracks,",
    "            *playerTorpedoSeekerTracks,\n            *destroyerTorpedoSeekerTracks,\n            *incomingThreatTracks,")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "            std::move(*playerCombat),\n            decoyDefinition,\n            simulationTimeSeconds);",
    "            std::move(*playerCombat),\n            decoyDefinition,\n            playerDecoyDefinition,\n            simulationTimeSeconds);")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }\n    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& PlayerTorpedoSeekerState() const noexcept",
    "    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }\n    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& PlayerDecoy() const noexcept\n    {\n        return playerDecoy_;\n    }\n    [[nodiscard]] bool PlayerDecoyAvailable() const noexcept { return playerDecoyAvailable_; }\n    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& DestroyerTorpedoSeekerState() const noexcept\n    {\n        return destroyerTorpedoSeekerState_;\n    }\n    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& PlayerTorpedoSeekerState() const noexcept")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        Perception::TrackManager playerTorpedoSeekerTracks,\n        Perception::TrackManager incomingThreatTracks,",
    "        Perception::TrackManager playerTorpedoSeekerTracks,\n        Perception::TrackManager destroyerTorpedoSeekerTracks,\n        Perception::TrackManager incomingThreatTracks,")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        PlayerCombatCommandRuntime playerCombat,\n        Weapons::AcousticDecoyDefinition decoyDefinition,\n        const double simulationTimeSeconds)",
    "        PlayerCombatCommandRuntime playerCombat,\n        Weapons::AcousticDecoyDefinition decoyDefinition,\n        Weapons::AcousticDecoyDefinition playerDecoyDefinition,\n        const double simulationTimeSeconds)")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),\n          incomingThreatTracks_(std::move(incomingThreatTracks)),",
    "          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),\n          destroyerTorpedoSeekerTracks_(std::move(destroyerTorpedoSeekerTracks)),\n          incomingThreatTracks_(std::move(incomingThreatTracks)),")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "          playerCombat_(std::move(playerCombat)),\n          decoyDefinition_(std::move(decoyDefinition)),",
    "          playerCombat_(std::move(playerCombat)),\n          decoyDefinition_(std::move(decoyDefinition)),\n          playerDecoyDefinition_(std::move(playerDecoyDefinition)),")

old_command_block = '''        if (!playerTorpedo_.has_value())
        {
            if (automatedPlayer)
            {
                const auto automated = AdvanceAutomatedPlayerCommander(simulationTimeSeconds);
                if (!automated)
                {
                    return std::unexpected(automated.error());
                }
            }
            else
            {
                for (const PlayerCombatCommand command : commands)
                {
                    const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
                    if (!executed)
                    {
                        return std::unexpected("M5-J2 player command failed: " + executed.error());
                    }
                }
            }

            if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)
            {
                const auto targetTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.Weapon().targetTrackId);
                if (!targetTrack)
                {
                    return std::unexpected("M5-J2 launched weapon lost its perceived launch track on the launch tick");
                }
                const auto launch = MaterializePlayerLaunch(
                    playerSnapshot, *destroyerAcoustics, *targetTrack, simulationTimeSeconds);
                if (!launch)
                {
                    return std::unexpected(launch.error());
                }
            }
        }
'''
new_command_block = '''        if (automatedPlayer)
        {
            if (!playerTorpedo_.has_value())
            {
                const auto automated = AdvanceAutomatedPlayerCommander(simulationTimeSeconds);
                if (!automated)
                {
                    return std::unexpected(automated.error());
                }
            }
        }
        else
        {
            for (const PlayerCombatCommand command : commands)
            {
                if (command.type == PlayerCombatCommandType::DeployDecoy)
                {
                    const auto feedback = ExecutePlayerDecoyCommand(playerSnapshot, simulationTimeSeconds);
                    if (!feedback)
                    {
                        return std::unexpected("M5-J4 player decoy command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (playerTorpedo_.has_value())
                {
                    continue; // Preserve J2's post-launch weapon-command behavior; J4 decoy remains available.
                }
                const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
                if (!executed)
                {
                    return std::unexpected("M5-J2 player command failed: " + executed.error());
                }
                lastCombatCommand_ = *executed;
            }
        }

        if (!playerTorpedo_.has_value() && playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)
        {
            const auto targetTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.Weapon().targetTrackId);
            if (!targetTrack)
            {
                return std::unexpected("M5-J2 launched weapon lost its perceived launch track on the launch tick");
            }
            const auto launch = MaterializePlayerLaunch(
                playerSnapshot, *destroyerAcoustics, *targetTrack, simulationTimeSeconds);
            if (!launch)
            {
                return std::unexpected(launch.error());
            }
        }
'''
replace_once("Game/Combat/CombatPlaygroundRuntime.h", old_command_block, new_command_block)
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        if (decoy_)\n        {\n            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);\n            if (!advanced)\n            {\n                return std::unexpected(\"M5-E.1 decoy advance failed: \" + advanced.error());\n            }\n        }",
    "        if (decoy_)\n        {\n            const auto advanced = Weapons::AdvanceAcousticDecoy(decoyDefinition_, *decoy_, simulationTimeSeconds);\n            if (!advanced)\n            {\n                return std::unexpected(\"M5-E.1 decoy advance failed: \" + advanced.error());\n            }\n        }\n        if (playerDecoy_)\n        {\n            const auto advanced = Weapons::AdvanceAcousticDecoy(\n                playerDecoyDefinition_, *playerDecoy_, simulationTimeSeconds);\n            if (!advanced)\n            {\n                return std::unexpected(\"M5-J4 player decoy advance failed: \" + advanced.error());\n            }\n        }")

old_hostile_advance = '''        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto perceivedTrack = FindTrack(destroyerTracks_.Tracks(), destroyerTorpedo_->guidanceTrackId);
            const auto advanced = Weapons::AdvanceConventionalTorpedoWithCollision(
                destroyerTorpedoDefinition_,
                *destroyerTorpedo_,
                perceivedTrack,
                *physicsWorld_,
                simulationTimeSeconds,
                destroyer_.body);
            if (!advanced)
            {
                return std::unexpected("M5-F.2 destroyer torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                destroyerImpact = **advanced;
                lastExplosion_ = destroyerImpact->explosion;
                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)
                {
                    const auto damaged = DeepRun::Combat::ApplyCombatDamage(*playerIntegrity_, destroyerImpact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-F.2 player torpedo-damage application failed: " + damaged.error());
                    }
                }
            }
        }
'''
new_hostile_advance = '''        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto seekerCue = AdvanceDestroyerTorpedoSeeker(playerSnapshot, simulationTimeSeconds);
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
            if (!advanced)
            {
                return std::unexpected("M5-F.2/J4 destroyer torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                destroyerImpact = **advanced;
                lastExplosion_ = destroyerImpact->explosion;
                pendingDestroyerTorpedoSeekerEmissions_.clear();
                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)
                {
                    const auto damaged = DeepRun::Combat::ApplyCombatDamage(*playerIntegrity_, destroyerImpact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-F.2 player torpedo-damage application failed: " + damaged.error());
                    }
                }
            }
        }
'''
replace_once("Game/Combat/CombatPlaygroundRuntime.h", old_hostile_advance, new_hostile_advance)
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        ApplyIncomingThreatPresentation(playerCombatPresentation);\n        return CombatPlaygroundFrame{",
    "        ApplyIncomingThreatPresentation(playerCombatPresentation);\n        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;\n        playerCombatPresentation.playerDecoyActive = playerDecoy_.has_value() && playerDecoy_->active;\n        if (lastCombatCommand_)\n        {\n            playerCombatPresentation.lastCommand = lastCombatCommand_;\n        }\n        return CombatPlaygroundFrame{")

# Insert J4 command handler immediately before destroyer launch materialization.
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    [[nodiscard]] std::expected<void, std::string> MaterializeDestroyerLaunch(\n",
    '''    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerDecoyCommand(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || !playerSnapshot.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-J4 player decoy command input is invalid");
        }
        if (!playerDecoyAvailable_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::DeployDecoy,
                .accepted = false,
                .trackId = std::nullopt,
                .message = "player acoustic decoy already expended"};
        }

        const Physics::PhysicsVector3 decoyPosition{
            .x = playerSnapshot.emitter.positionMeters.x - 20.0F,
            .y = playerSnapshot.emitter.positionMeters.y - M5CombatPlayerDecoyVerticalOffsetMeters,
            .z = playerSnapshot.emitter.positionMeters.z};
        const auto deployed = Weapons::DeployAcousticDecoy(
            playerDecoyDefinition_, decoyPosition, simulationTimeSeconds);
        if (!deployed)
        {
            return std::unexpected("M5-J4 player acoustic decoy deployment failed: " + deployed.error());
        }
        playerDecoy_ = *deployed;
        playerDecoyAvailable_ = false;
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::DeployDecoy,
            .accepted = true,
            .trackId = std::nullopt,
            .message = "player acoustic decoy deployed"};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializeDestroyerLaunch(
''')
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        destroyerTorpedo_ = *launched;\n        destroyerTorpedoLaunchPosition_ = launchPosition;\n        pendingIncomingThreatEmissions_.clear();",
    "        destroyerTorpedo_ = *launched;\n        destroyerTorpedoLaunchPosition_ = launchPosition;\n        destroyerTorpedoForwardSign_ = forwardSign;\n        destroyerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{\n            .selectedTrackId = std::nullopt,\n            .lastUpdateTimeSeconds = simulationTimeSeconds};\n        pendingDestroyerTorpedoSeekerEmissions_.clear();\n        nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;\n        pendingIncomingThreatEmissions_.clear();")

# Insert symmetric hostile local-seeker path before J3 warning perception.
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(\n",
    '''    [[nodiscard]] std::expected<std::optional<Weapons::TorpedoSeekerCue>, std::string>
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
                {
                    return std::unexpected("M5-J4 player decoy emission snapshot failed: " + decoyEmission.error());
                }
                if (decoyEmission->has_value())
                {
                    pendingDestroyerTorpedoSeekerEmissions_.push_back(**decoyEmission);
                }
            }
            nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoSeekerEmissionSampleIntervalSeconds;
        }

        const Acoustics::AcousticReceiver seekerReceiver{
            .sensorId = "M5_DESTROYER_TORPEDO_PASSIVE_SEEKER",
            .positionMeters = destroyerTorpedo_->positionMeters,
            .ambientNoiseLevelDb = {.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}},
            .selfNoiseLevelDb = {.levelDb = {64.0F, 64.0F, 64.0F, 64.0F}},
            .sensitivityDb = {.levelDb = {0.0F, 0.0F, 0.0F, 0.0F}},
            .minimumPeakSnrDb = 3.0F};

        bool integratedObservation = false;
        auto emission = pendingDestroyerTorpedoSeekerEmissions_.begin();
        while (emission != pendingDestroyerTorpedoSeekerEmissions_.end())
        {
            const double distanceMeters = Distance(emission->positionMeters, seekerReceiver.positionMeters);
            const double arrivalTimeSeconds = emission->emissionTimeSeconds +
                distanceMeters / static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
            if (!std::isfinite(arrivalTimeSeconds))
            {
                return std::unexpected("M5-J4 hostile seeker emission arrival time is non-finite");
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
                return std::unexpected("M5-J4 hostile seeker acoustic propagation failed: " +
                                       observed.error().message);
            }
            if (observed->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(**observed);
                if (!perceived || !destroyerTorpedoSeekerTracks_.IntegrateObservation(*perceived))
                {
                    return std::unexpected("M5-J4 hostile seeker perception integration failed");
                }
                integratedObservation = true;
            }
            emission = pendingDestroyerTorpedoSeekerEmissions_.erase(emission);
        }

        if (!integratedObservation && !destroyerTorpedoSeekerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-J4 hostile seeker TrackManager failed to advance");
        }
        return Weapons::SelectTorpedoSeekerCue(
            destroyerTorpedoSeekerConfig_,
            destroyerTorpedoSeekerState_,
            destroyerTorpedoSeekerTracks_.Tracks(),
            simulationTimeSeconds);
    }

    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(
''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    Perception::TrackManager playerTorpedoSeekerTracks_;\n    Perception::TrackManager incomingThreatTracks_;",
    "    Perception::TrackManager playerTorpedoSeekerTracks_;\n    Perception::TrackManager destroyerTorpedoSeekerTracks_;\n    Perception::TrackManager incomingThreatTracks_;")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    double nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;\n    SimpleDestroyerDefinition destroyerDefinition_;",
    "    double nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;\n    Weapons::TorpedoSeekerConfig destroyerTorpedoSeekerConfig_{\n        .minimumTrackConfidence = 0.35F,\n        .maximumBearingUncertaintyRadians = 0.20F,\n        .allowCoastingTrack = false};\n    Weapons::TorpedoSeekerRuntimeState destroyerTorpedoSeekerState_{};\n    std::vector<Acoustics::AcousticEmission> pendingDestroyerTorpedoSeekerEmissions_{};\n    double nextDestroyerTorpedoSeekerEmissionSampleTimeSeconds_ = 0.0;\n    SimpleDestroyerDefinition destroyerDefinition_;")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    std::optional<Physics::PhysicsVector3> destroyerTorpedoLaunchPosition_{};\n    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;",
    "    std::optional<Physics::PhysicsVector3> destroyerTorpedoLaunchPosition_{};\n    float destroyerTorpedoForwardSign_ = -1.0F;\n    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;")
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "    Weapons::AcousticDecoyDefinition decoyDefinition_;\n    std::optional<Weapons::AcousticDecoyRuntimeState> decoy_{};",
    "    Weapons::AcousticDecoyDefinition decoyDefinition_;\n    std::optional<Weapons::AcousticDecoyRuntimeState> decoy_{};\n    Weapons::AcousticDecoyDefinition playerDecoyDefinition_;\n    std::optional<Weapons::AcousticDecoyRuntimeState> playerDecoy_{};\n    bool playerDecoyAvailable_ = true;\n    std::optional<PlayerCombatCommandFeedback> lastCombatCommand_{};")

# Renderer-facing projection supports both sides' ordinary decoy emitters.
replace_once(
    "Game/Combat/CombatPlaygroundPresentation.h",
    "    std::optional<CombatPlaygroundDecoyPresentation> decoy{};\n    std::optional<CombatPlaygroundMinePresentation> navalMine{};",
    "    std::optional<CombatPlaygroundDecoyPresentation> decoy{};\n    std::optional<CombatPlaygroundDecoyPresentation> playerDecoy{};\n    std::optional<CombatPlaygroundMinePresentation> navalMine{};")
replace_once(
    "Game/Combat/CombatPlaygroundPresentation.h",
    "    AcousticDecoy,\n    NavalMine,",
    "    AcousticDecoy,\n    PlayerAcousticDecoy,\n    NavalMine,")
replace_once(
    "Game/Combat/CombatPlaygroundPresentation.h",
    "    if (const auto& mine = runtime.Mine(); mine.has_value() && mine->armed && !mine->detonated)",
    '''    if (const auto& playerDecoy = runtime.PlayerDecoy(); playerDecoy.has_value())
    {
        if (!playerDecoy->emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-J4 player decoy presentation state is invalid");
        }
        snapshot.playerDecoy = CombatPlaygroundDecoyPresentation{
            .positionMeters = playerDecoy->emitter.positionMeters,
            .active = playerDecoy->active};
    }

    if (const auto& mine = runtime.Mine(); mine.has_value() && mine->armed && !mine->detonated)''')
replace_once(
    "Game/Combat/CombatPlaygroundPresentation.h",
    "    draws.reserve(7U);",
    "    draws.reserve(8U);")
replace_once(
    "Game/Combat/CombatPlaygroundPresentation.h",
    "    if (snapshot.navalMine)\n    {",
    '''    if (snapshot.playerDecoy && snapshot.playerDecoy->active)
    {
        const auto transform = PoseScaleTransform(
            snapshot.playerDecoy->positionMeters,
            {},
            {.x = 2.0F, .y = 2.0F, .z = 2.0F});
        if (!transform)
        {
            return std::unexpected(transform.error());
        }
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::PlayerAcousticDecoy,
            *transform,
            Material("M5PlayerAcousticDecoy", {0.82F, 0.62F, 0.16F, 1.0F}, 0.06F, 0.40F));
        if (!draw)
        {
            return std::unexpected(draw.error());
        }
        draws.push_back(std::move(*draw));
    }

    if (snapshot.navalMine)
    {''')

# UI and windowed command bridge.
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    "    case PlayerCombatCommandType::FireWeapon: return \"FIRE WEAPON\";\n    }",
    "    case PlayerCombatCommandType::FireWeapon: return \"FIRE WEAPON\";\n    case PlayerCombatCommandType::DeployDecoy: return \"DEPLOY DECOY\";\n    }")
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    "    ImGui::Text(\"Prepare available: %s\", snapshot.canPrepareWeapon ? \"YES\" : \"NO\");\n    ImGui::Text(\"Fire available: %s\", snapshot.canFireWeapon ? \"YES\" : \"NO\");",
    "    ImGui::Text(\"Prepare available: %s\", snapshot.canPrepareWeapon ? \"YES\" : \"NO\");\n    ImGui::Text(\"Fire available: %s\", snapshot.canFireWeapon ? \"YES\" : \"NO\");\n    ImGui::Text(\"Decoy available: %s\", snapshot.canDeployDecoy ? \"YES\" : \"NO\");\n    ImGui::Text(\"Player decoy active: %s\", snapshot.playerDecoyActive ? \"YES\" : \"NO\");")
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    "    ImGui::TextUnformatted(\"A / Space / LMB  Fire weapon\");",
    "    ImGui::TextUnformatted(\"A / Space / LMB  Fire weapon\");\n    ImGui::TextUnformatted(\"B / F            Deploy decoy\");")
replace_once(
    "DeepRun/Main.cpp",
    "        std::uint64_t consumedFireWeaponSequence = 0;",
    "        std::uint64_t consumedFireWeaponSequence = 0;\n        std::uint64_t consumedDeployDecoySequence = 0;")
replace_once(
    "DeepRun/Main.cpp",
    "             &consumedSelectContactSequence, &consumedPrepareWeaponSequence, &consumedFireWeaponSequence,",
    "             &consumedSelectContactSequence, &consumedPrepareWeaponSequence, &consumedFireWeaponSequence,\n             &consumedDeployDecoySequence,")
replace_once(
    "DeepRun/Main.cpp",
    "                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 3> playerCommands{};",
    "                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 4> playerCommands{};")
replace_once(
    "DeepRun/Main.cpp",
    "                        consume(*inputState, DeepRun::Input::InputAction::FireWeapon,\n                                DeepRun::Game::Combat::PlayerCombatCommandType::FireWeapon,\n                                consumedFireWeaponSequence);",
    "                        consume(*inputState, DeepRun::Input::InputAction::FireWeapon,\n                                DeepRun::Game::Combat::PlayerCombatCommandType::FireWeapon,\n                                consumedFireWeaponSequence);\n                        consume(*inputState, DeepRun::Input::InputAction::DeployDecoy,\n                                DeepRun::Game::Combat::PlayerCombatCommandType::DeployDecoy,\n                                consumedDeployDecoySequence);")
replace_once(
    "DeepRun/Main.cpp",
    "                    if (combatRendered->drawCalls < 2U || combatRendered->drawCalls > 5U ||",
    "                    if (combatRendered->drawCalls < 2U || combatRendered->drawCalls > 8U ||")

# Input regression expands J2 semantics with J4 controller/keyboard parity.
replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    "        static_cast<std::uint16_t>(GamepadButton::X) |\n        static_cast<std::uint16_t>(GamepadButton::A);",
    "        static_cast<std::uint16_t>(GamepadButton::X) |\n        static_cast<std::uint16_t>(GamepadButton::A) |\n        static_cast<std::uint16_t>(GamepadButton::B);")
replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    "    if (disconnected.selectContact || disconnected.prepareWeapon || disconnected.fireWeapon)",
    "    if (disconnected.selectContact || disconnected.prepareWeapon || disconnected.fireWeapon ||\n        disconnected.deployDecoy)")
replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    "    if (!controller.selectContact || !controller.prepareWeapon || !controller.fireWeapon)",
    "    if (!controller.selectContact || !controller.prepareWeapon || !controller.fireWeapon ||\n        !controller.deployDecoy)")
replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    "    if (triggerActions.selectContact || triggerActions.prepareWeapon || triggerActions.fireWeapon ||\n        std::abs(triggerAxes.cameraZoom - 0.60F) > 0.001F)",
    "    if (triggerActions.selectContact || triggerActions.prepareWeapon || triggerActions.fireWeapon ||\n        triggerActions.deployDecoy || std::abs(triggerAxes.cameraZoom - 0.60F) > 0.001F)")
replace_once(
    "Tests/M5PlayerCombatInputChecks.h",
    "    const std::array fireKeyUp{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Space}};\n    input.ProcessEvents(fireKeyUp);",
    '''    const std::array fireKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Space}};
    input.ProcessEvents(fireKeyUp);

    input.BeginFrame();
    const std::array decoyKeyDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::F}};
    input.ProcessEvents(decoyKeyDown);
    if (!input.State().WasPressed(InputAction::DeployDecoy) ||
        !input.State().IsDown(InputAction::DeployDecoy) || input.State().IsDown(InputAction::FireWeapon) ||
        input.State().IsDown(InputAction::PrepareWeapon) ||
        input.State().PressSequence(InputAction::DeployDecoy) == 0U)
    {
        return false;
    }
    const std::array decoyKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::F}};
    input.ProcessEvents(decoyKeyUp);
    if (!input.State().WasReleased(InputAction::DeployDecoy) || input.State().IsDown(InputAction::DeployDecoy))
    {
        return false;
    }''')

# Dedicated J4 live defensive-countermeasure regression.
write_new(
    "Tests/M5PlayerDefensiveDecoyChecks.h",
    r'''#pragma once

#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline float M5J4AngleError(const float first, const float second) noexcept
{
    return std::abs(std::remainder(first - second, 6.2831853F));
}

[[nodiscard]] inline bool RunM5PlayerDefensiveDecoyChecks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace Game::Combat;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }
    auto runtimeResult = CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = std::move(*runtimeResult);

    const auto playerSnapshotResult = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = {.x = 0.0F, .y = -100.0F, .z = 0.0F},
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 35.0F,
            .signedDepthMeters = 100.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    if (!playerSnapshotResult)
    {
        return false;
    }
    const auto playerSnapshot = *playerSnapshotResult;

    const Physics::PhysicsVector3 playerHalfExtentsMeters{.x = 75.0F, .y = 8.0F, .z = 8.0F};
    const auto playerBody = physicsWorld.CreateDynamicBoxBody(Physics::DynamicBoxBodyCreateInfo{
        .halfExtents = playerHalfExtentsMeters,
        .mass = 12'000'000.0F,
        .position = playerSnapshot.emitter.positionMeters,
        .orientation = {},
        .gravityEnabled = false,
        .linearDamping = 0.0F,
        .angularDamping = 0.0F,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {}});
    if (!playerBody.IsValid())
    {
        return false;
    }
    const auto bound = runtime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = playerBody,
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .orientation = {},
            .halfExtentsMeters = playerHalfExtentsMeters},
        playerSnapshot,
        0.0);
    if (!bound || !runtime.PlayerDecoyAvailable() || runtime.PlayerDecoy().has_value())
    {
        return false;
    }

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    double simulationTimeSeconds = 0.0;
    bool threatDetected = false;
    for (int tick = 0; tick < 2400; ++tick)
    {
        simulationTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (frame->playerCombat.incomingThreatDetected)
        {
            if (!runtime.DestroyerTorpedo() || !frame->playerCombat.canDeployDecoy ||
                frame->playerCombat.playerDecoyActive)
            {
                return false;
            }
            threatDetected = true;
            break;
        }
    }
    if (!threatDetected)
    {
        return false;
    }

    simulationTimeSeconds += fixedDeltaSeconds;
    const std::array deploy{
        PlayerCombatCommand{.type = PlayerCombatCommandType::DeployDecoy}};
    const auto deployed = runtime.AdvancePlayerControlled(playerSnapshot, deploy, simulationTimeSeconds);
    if (!deployed || !deployed->playerCombat.lastCommand || !deployed->playerCombat.lastCommand->accepted ||
        deployed->playerCombat.lastCommand->command != PlayerCombatCommandType::DeployDecoy ||
        deployed->playerCombat.canDeployDecoy || !deployed->playerCombat.playerDecoyActive ||
        runtime.PlayerDecoyAvailable() || !runtime.PlayerDecoy() || !runtime.PlayerDecoy()->active)
    {
        return false;
    }

    const auto presentation = BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, simulationTimeSeconds);
    if (!presentation || !presentation->playerDecoy || !presentation->playerDecoy->active ||
        presentation->playerDecoy->positionMeters != runtime.PlayerDecoy()->emitter.positionMeters)
    {
        return false;
    }
    const auto draws = BuildCombatPlaygroundPresentationDraws(*presentation);
    if (!draws || !std::ranges::any_of(*draws, [](const auto& draw) {
            return draw.element == CombatPlaygroundPresentationElement::PlayerAcousticDecoy;
        }))
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const double originalDeploymentTime = runtime.PlayerDecoy()->deploymentTimeSeconds;
    simulationTimeSeconds += fixedDeltaSeconds;
    const auto repeated = runtime.AdvancePlayerControlled(playerSnapshot, deploy, simulationTimeSeconds);
    if (!repeated || !repeated->playerCombat.lastCommand || repeated->playerCombat.lastCommand->accepted ||
        repeated->playerCombat.lastCommand->command != PlayerCombatCommandType::DeployDecoy ||
        !runtime.PlayerDecoy() || runtime.PlayerDecoy()->deploymentTimeSeconds != originalDeploymentTime)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawSeekerSelection = false;
    bool sawDiversionTowardDecoy = false;
    bool sawExpiredDecoy = false;
    for (int tick = 0; tick < 1200; ++tick)
    {
        simulationTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }
        if (runtime.DestroyerTorpedo() &&
            runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater &&
            runtime.PlayerDecoy() && runtime.PlayerDecoy()->active &&
            runtime.DestroyerTorpedoSeekerState().selectedTrackId)
        {
            sawSeekerSelection = true;
            const auto& torpedo = *runtime.DestroyerTorpedo();
            const auto& decoy = *runtime.PlayerDecoy();
            const float playerBearing = static_cast<float>(std::atan2(
                static_cast<double>(playerSnapshot.emitter.positionMeters.y - torpedo.positionMeters.y),
                static_cast<double>(playerSnapshot.emitter.positionMeters.x - torpedo.positionMeters.x)));
            const float decoyBearing = static_cast<float>(std::atan2(
                static_cast<double>(decoy.emitter.positionMeters.y - torpedo.positionMeters.y),
                static_cast<double>(decoy.emitter.positionMeters.x - torpedo.positionMeters.x)));
            if (M5J4AngleError(torpedo.headingRadians, decoyBearing) + 0.02F <
                M5J4AngleError(torpedo.headingRadians, playerBearing))
            {
                sawDiversionTowardDecoy = true;
            }
        }
        if (runtime.PlayerDecoy() && !runtime.PlayerDecoy()->active)
        {
            if (frame->playerCombat.canDeployDecoy || frame->playerCombat.playerDecoyActive)
            {
                return false;
            }
            sawExpiredDecoy = true;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawDiversionTowardDecoy && sawExpiredDecoy)
        {
            break;
        }
    }

    const auto mineBody = runtime.Mine() ? runtime.Mine()->body : Physics::PhysicsBodyHandle{};
    const bool destroyedMine = !mineBody.IsValid() || physicsWorld.DestroyBody(mineBody);
    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);
    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);
    return sawSeekerSelection && sawDiversionTowardDecoy && sawExpiredDecoy &&
        destroyedMine && destroyedPlayer && destroyedDestroyer;
}
} // namespace DeepRun::Tests
''')
replace_once(
    "Tests/M5CombatImpactChecks.h",
    '#include "Tests/M5PlayerCombatInputChecks.h"\n',
    '#include "Tests/M5PlayerCombatInputChecks.h"\n#include "Tests/M5PlayerDefensiveDecoyChecks.h"\n')
replace_once(
    "Tests/M5CombatImpactChecks.h",
    "    if (!RunM5PlayerControlledCombatChecks(physicsWorld))\n    {\n        return fail(\"M5-J2-B normal-play commander gating and explicit prepare-ready-fire sequence\");\n    }",
    "    if (!RunM5PlayerControlledCombatChecks(physicsWorld))\n    {\n        return fail(\"M5-J2-B normal-play commander gating and explicit prepare-ready-fire sequence\");\n    }\n    if (!RunM5PlayerDefensiveDecoyChecks(physicsWorld))\n    {\n        return fail(\"M5-J4 player defensive decoy command and hostile local-seeker diversion\");\n    }")
replace_once(
    "Tests/M5CombatPlaygroundRuntimeChecks.h",
    "presentationDraws->size() > 7U",
    "presentationDraws->size() > 8U")

# Documentation: close J3 evidence and record J4 boundary.
replace_once(
    "docs/development/m5-combat-playground.md",
    "`1d90159ac236cc3cc2df0a1d07b6fbf4c4d5fdc1` passed post-merge CI run `34520094788` in both configurations.",
    "`1d90159ac236cc3cc2df0a1d07b6fbf4c4d5fdc1` passed post-merge CI run `34520094788` in both configurations.\nJ3 is additionally accepted at feature commit `3a6fef335878046808e9caa08d3b4df8ceb1bcaf`; post-merge CI\nrun `34523462484` passed Debug and Release configure, build, CTest, windowed smoke and visual artifacts.")
replace_once(
    "docs/development/m5-combat-playground.md",
    "Status: ACCEPTED when this documented tree passes the normal Debug/Release CI gate.",
    "Status: ACCEPTED.")
replace_once(
    "docs/development/m5-combat-playground.md",
    "coasts/clears after the physical threat is consumed. No tactical pause or generic command queue is introduced.\n\n## M5-H.1-B",
    '''coasts/clears after the physical threat is consumed. No tactical pause or generic command queue is introduced.

### M5-J4 — player defensive acoustic countermeasure

Status: ACCEPTED after the normal Debug/Release candidate gate used for promotion to the stable M5 branch.

J4 wires the already-canonical `DeployDecoy` semantic action into normal combat play (`B` on the reference
controller and `F` on keyboard). The command is a one-shot M5 playground resource only; it does not introduce
M6 inventory, compartment, crew or launcher-system simulation. Deployment creates an ordinary moving
`AcousticEmitter` through the existing `AcousticDecoy` contract and projects availability/active state plus
accepted/rejected command feedback to the combat UI.

The reciprocal F.2 torpedo now owns a local passive seeker with its own TrackManager. It samples timestamped
emissions from Antey and the player decoy through `AcousticWorld`, waits for acoustic propagation, converts only
observations into perceived tracks, and chooses a bearing-only cue through the same `TorpedoSeeker` policy used
by the player's weapon. The decoy therefore diverts the torpedo by winning perceived-track selection; no decoy
flag, player body handle, authoritative target Transform, or source identity crosses into seeker guidance.
The accepted automated smoke path never deploys the player decoy, preserving the deterministic F.2 baseline.

## M5-H.1-B''')
replace_once(
    "docs/roadmap/milestones.md",
    "- J3 passive-acoustic incoming-threat warning projected to combat UI without hostile position/body truth",
    "- J3 passive-acoustic incoming-threat warning projected to combat UI without hostile position/body truth\n- J4 controller/keyboard player defensive decoy command with reciprocal torpedo local-seeker diversion through perceived acoustics")

print("M5-J4 patch applied")
