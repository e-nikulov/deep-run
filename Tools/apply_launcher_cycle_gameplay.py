from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# P-700 lifecycle: explicit wet-launch flooding after hatch opening.
# -----------------------------------------------------------------------------
p = "Simulation/Weapons/P700Granit.h"
s = read(p)
s = replace_once(
    s,
    "    double launcherHatchOpeningSeconds = 0.75;\n    double waterExitTransitionSeconds = 0.50;",
    "    double launcherHatchOpeningSeconds = 0.75;\n"
    "    // GAME POLICY duration: open sources confirm a wet launch with the launcher flooded before ejection,\n"
    "    // but do not provide a dependable public cycle time for SM-225A. Keep the timing explicit/tunable.\n"
    "    double launcherFloodingSeconds = 2.0;\n"
    "    double waterExitTransitionSeconds = 0.50;",
    "P700 definition flooding duration")
s = replace_once(
    s,
    "    float hatchOpenProgress = 0.0F;\n    float postExitTransitionProgress = 0.0F;",
    "    float hatchOpenProgress = 0.0F;\n    float launcherFloodProgress = 0.0F;\n    float postExitTransitionProgress = 0.0F;",
    "P700 runtime flood progress")
s = replace_once(
    s,
    "        !std::isfinite(definition.launcherHatchOpeningSeconds) || definition.launcherHatchOpeningSeconds <= 0.0 ||\n        !std::isfinite(definition.waterExitTransitionSeconds) || definition.waterExitTransitionSeconds <= 0.0 ||",
    "        !std::isfinite(definition.launcherHatchOpeningSeconds) || definition.launcherHatchOpeningSeconds <= 0.0 ||\n"
    "        !std::isfinite(definition.launcherFloodingSeconds) || definition.launcherFloodingSeconds <= 0.0 ||\n"
    "        !std::isfinite(definition.waterExitTransitionSeconds) || definition.waterExitTransitionSeconds <= 0.0 ||",
    "P700 definition flood validation")
s = replace_once(
    s,
    "    state.hatchOpenProgress = 0.0F;\n    state.postExitTransitionProgress = 0.0F;",
    "    state.hatchOpenProgress = 0.0F;\n    state.launcherFloodProgress = 0.0F;\n    state.postExitTransitionProgress = 0.0F;",
    "P700 launch flood init")
s = replace_once(
    s,
    "        !std::isfinite(state.hatchOpenProgress) || state.hatchOpenProgress < 0.0F || state.hatchOpenProgress > 1.0F ||\n        !std::isfinite(state.postExitTransitionProgress) || state.postExitTransitionProgress < 0.0F ||",
    "        !std::isfinite(state.hatchOpenProgress) || state.hatchOpenProgress < 0.0F || state.hatchOpenProgress > 1.0F ||\n"
    "        !std::isfinite(state.launcherFloodProgress) || state.launcherFloodProgress < 0.0F ||\n"
    "        state.launcherFloodProgress > 1.0F ||\n"
    "        !std::isfinite(state.postExitTransitionProgress) || state.postExitTransitionProgress < 0.0F ||",
    "P700 runtime flood validation")
old_hatch = '''        if (state.phase == P700GranitPhase::HatchOpening)
        {
            if (state.speedMetersPerSecond != 0.0F || state.deploymentProgress != 0.0F ||
                state.postExitTransitionProgress != 0.0F || state.launchBoosterActive || state.mainEngineActive ||
                !state.launchBoosterAttached || !state.noseProtectionCapAttached)
            {
                return std::unexpected("P-700 hatch-opening component contract is invalid");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.launcherHatchOpeningSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            cursorTime += stepSeconds;
            remainingSeconds -= stepSeconds;
            state.lastUpdateTimeSeconds = cursorTime;
            state.hatchOpenProgress = std::clamp(
                static_cast<float>((cursorTime - state.phaseStartTimeSeconds) / definition.launcherHatchOpeningSeconds),
                0.0F, 1.0F);
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.hatchOpenProgress = 1.0F;
                state.launchBoosterActive = true;
                state.phase = state.positionMeters.y < state.surfaceLevelYMeters - 0.05F
                    ? P700GranitPhase::UnderwaterLaunch
                    : P700GranitPhase::WaterExit;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = state.phase == P700GranitPhase::UnderwaterLaunch
                    ? definition.underwaterExitSpeedMetersPerSecond
                    : definition.waterExitSpeedMetersPerSecond;
                continue;
            }
            break;
        }
'''
new_hatch = '''        if (state.phase == P700GranitPhase::HatchOpening)
        {
            if (state.speedMetersPerSecond != 0.0F || state.deploymentProgress != 0.0F ||
                state.postExitTransitionProgress != 0.0F || state.launchBoosterActive || state.mainEngineActive ||
                !state.launchBoosterAttached || !state.noseProtectionCapAttached)
            {
                return std::unexpected("P-700 hatch/flood preparation component contract is invalid");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double totalPreparationSeconds =
                definition.launcherHatchOpeningSeconds + definition.launcherFloodingSeconds;
            const double phaseRemaining = std::max(0.0, totalPreparationSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            cursorTime += stepSeconds;
            remainingSeconds -= stepSeconds;
            state.lastUpdateTimeSeconds = cursorTime;
            const double totalElapsed = cursorTime - state.phaseStartTimeSeconds;
            state.hatchOpenProgress = std::clamp(
                static_cast<float>(totalElapsed / definition.launcherHatchOpeningSeconds), 0.0F, 1.0F);
            const double floodElapsed = std::max(0.0, totalElapsed - definition.launcherHatchOpeningSeconds);
            state.launcherFloodProgress = std::clamp(
                static_cast<float>(floodElapsed / definition.launcherFloodingSeconds), 0.0F, 1.0F);
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.hatchOpenProgress = 1.0F;
                state.launcherFloodProgress = 1.0F;
                state.launchBoosterActive = true;
                state.phase = state.positionMeters.y < state.surfaceLevelYMeters - 0.05F
                    ? P700GranitPhase::UnderwaterLaunch
                    : P700GranitPhase::WaterExit;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = state.phase == P700GranitPhase::UnderwaterLaunch
                    ? definition.underwaterExitSpeedMetersPerSecond
                    : definition.waterExitSpeedMetersPerSecond;
                continue;
            }
            break;
        }
'''
s = replace_once(s, old_hatch, new_hatch, "P700 hatch/flood phase")
s = replace_once(
    s,
    "            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||\n                state.postExitTransitionProgress != 0.0F || state.launchForwardUnitVector.y <= 0.0F ||",
    "            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||\n"
    "                state.launcherFloodProgress != 1.0F || state.postExitTransitionProgress != 0.0F ||\n"
    "                state.launchForwardUnitVector.y <= 0.0F ||",
    "P700 underwater requires flooded launcher")
s = replace_once(
    s,
    "            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||\n                state.postExitTransitionProgress != 0.0F || !state.launchBoosterActive ||",
    "            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||\n"
    "                state.launcherFloodProgress != 1.0F || state.postExitTransitionProgress != 0.0F ||\n"
    "                !state.launchBoosterActive ||",
    "P700 water exit requires flooded launcher")
write(p, s)


# -----------------------------------------------------------------------------
# P-700 inventory: a single launch advances to the next complete paired hatch
# before revisiting the unused missile under a half-used hatch.
# -----------------------------------------------------------------------------
p = "Game/Weapons/P700LauncherInventory.h"
s = read(p)
old = '''        if (requestedCount == 1U)
        {
            const auto first = FirstLoadedSlotIndex();
            if (!first)
                return std::unexpected("P-700 launcher inventory is exhausted");
            return std::vector<std::size_t>{*first};
        }
'''
new = '''        if (requestedCount == 1U)
        {
            // A single shot uses one missile under a still-complete paired hatch first, then advances to the
            // next hatch. Only after every hatch has contributed one missile do we revisit half-used groups.
            // This keeps Single mode a true sequential hatch ripple instead of firing both cells under one lid.
            for (std::size_t first = 0U; first < slots_.size(); ++first)
            {
                if (slots_[first].state != P700LauncherSlotState::Loaded ||
                    slots_[first].anchor.hatchGroupSemanticId.empty())
                    continue;
                const bool completePair = std::ranges::any_of(
                    slots_.begin() + static_cast<std::ptrdiff_t>(first + 1U), slots_.end(),
                    [&](const auto& candidate) {
                        return candidate.state == P700LauncherSlotState::Loaded &&
                               candidate.anchor.hatchGroupSemanticId == slots_[first].anchor.hatchGroupSemanticId;
                    });
                if (completePair)
                    return std::vector<std::size_t>{first};
            }
            const auto first = FirstLoadedSlotIndex();
            if (!first)
                return std::unexpected("P-700 launcher inventory is exhausted");
            return std::vector<std::size_t>{*first};
        }
'''
s = replace_once(s, old, new, "P700 sequential single-hatch selection")
write(p, s)


# -----------------------------------------------------------------------------
# Player combat presentation: launcher/tube resource state.
# -----------------------------------------------------------------------------
p = "Game/Combat/PlayerCombatCommandRuntime.h"
s = read(p)
s = replace_once(
    s,
    "    std::size_t p700LoadedCount = 0U;\n    Weapons::P700SalvoMode p700SalvoMode = Weapons::P700SalvoMode::Single;",
    "    std::size_t p700LoadedCount = 0U;\n"
    "    double p700NextLaunchReadySeconds = 0.0;\n"
    "    std::optional<float> activeP700FloodProgress{};\n"
    "    std::size_t torpedoRoundsRemaining = 0U;\n"
    "    std::size_t torpedoReadyTubeCount = 0U;\n"
    "    std::size_t torpedoTubeCount = 0U;\n"
    "    std::optional<double> torpedoNextTubeReadySeconds{};\n"
    "    std::size_t playerTorpedoesInFlight = 0U;\n"
    "    std::size_t playerP700InFlight = 0U;\n"
    "    Weapons::P700SalvoMode p700SalvoMode = Weapons::P700SalvoMode::Single;",
    "combat snapshot launcher state")
write(p, s)


# -----------------------------------------------------------------------------
# Runtime: tube bank, multi-in-flight ordnance, resource gates and ripple cadence.
# -----------------------------------------------------------------------------
p = "Game/Combat/CombatPlaygroundRuntime.h"
s = read(p)
s = replace_once(
    s,
    '#include "Game/Weapons/AnteyTorpedoInventory.h"\n',
    '#include "Game/Weapons/AnteyTorpedoInventory.h"\n#include "Game/Weapons/AnteyTorpedoTubeBank.h"\n',
    "runtime tube bank include")
s = replace_once(
    s,
    "inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;\n",
    "inline constexpr double M5CombatActiveRangingIntervalSeconds = 3.0;\n"
    "// GAME POLICY: public descriptions support high-tempo sequential Granit salvos but not a dependable\n"
    "// SM-225A cycle number. Five seconds keeps the launcher sequence readable and prevents 24 simultaneous starts.\n"
    "inline constexpr double M5CombatP700MinimumInterSalvoSeconds = 5.0;\n",
    "P700 ripple interval constant")
# Public accessors for presentation/acceptance.
s = replace_once(
    s,
    "    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept\n    {\n        return playerTorpedoLaunchPosition_;\n    }",
    "    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept\n"
    "    {\n        return playerTorpedoLaunchPosition_;\n    }\n"
    "    [[nodiscard]] const std::vector<Weapons::ConventionalTorpedoRuntimeState>& AdditionalPlayerTorpedoes() const noexcept\n"
    "    {\n        return additionalPlayerTorpedoes_;\n    }",
    "additional torpedo accessor")
s = replace_once(
    s,
    "    [[nodiscard]] const std::vector<Weapons::P700GranitRuntimeState>& PlayerP700Wingmen() const noexcept\n    {\n        return playerP700Wingmen_;\n    }",
    "    [[nodiscard]] const std::vector<Weapons::P700GranitRuntimeState>& PlayerP700Wingmen() const noexcept\n"
    "    {\n        return playerP700Wingmen_;\n    }\n"
    "    [[nodiscard]] const std::vector<Weapons::P700GranitRuntimeState>& AdditionalPlayerP700Missiles() const noexcept\n"
    "    {\n        return additionalPlayerP700Missiles_;\n    }",
    "additional P700 accessor")
# Rearm fire-control at the next fixed step after a committed launch, independently of flight duration.
s = replace_once(
    s,
    "        const auto readiness = playerCombat_.Advance(simulationTimeSeconds);\n        if (!readiness)\n        {\n            return std::unexpected(\"M5-J2 player commander readiness failed: \" + readiness.error());\n        }",
    "        if (playerLaunchRearmPending_)\n"
    "        {\n"
    "            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n"
    "            if (!rearmed)\n"
    "                return std::unexpected(\"player fire-control re-arm after committed launch failed: \" + rearmed.error());\n"
    "            playerLaunchRearmPending_ = false;\n"
    "        }\n"
    "        const auto readiness = playerCombat_.Advance(simulationTimeSeconds);\n"
    "        if (!readiness)\n"
    "        {\n"
    "            return std::unexpected(\"M5-J2 player commander readiness failed: \" + readiness.error());\n"
    "        }",
    "launch-commit rearm")
# Resource gate replaces the old global one-projectile lock.
old_gate = '''                if (playerTorpedo_.has_value() || playerP700_.has_value())
                {
                    continue;
                }
                if (command.type == PlayerCombatCommandType::FireWeapon)
                {
                    const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit
                        ? AssessPlayerP700Employment(playerSnapshot)
                        : AssessPlayerTorpedoEmployment(playerSnapshot);
'''
new_gate = '''                if (command.type == PlayerCombatCommandType::FireWeapon)
                {
                    if (selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit)
                    {
                        if (simulationTimeSeconds + 1.0e-9 < nextP700LaunchAllowedTimeSeconds_)
                        {
                            lastCombatCommand_ = PlayerCombatCommandFeedback{
                                .command = PlayerCombatCommandType::FireWeapon,
                                .accepted = false,
                                .trackId = playerCombat_.SelectedTrackId(),
                                .message = "next P-700 hatch launch sequence available in " +
                                    std::to_string(nextP700LaunchAllowedTimeSeconds_ - simulationTimeSeconds) + " s"};
                            continue;
                        }
                    }
                    else
                    {
                        if (playerTorpedoInventory_.LoadedCount(selectedPlayerWeapon_) == 0U)
                        {
                            lastCombatCommand_ = PlayerCombatCommandFeedback{
                                .command = PlayerCombatCommandType::FireWeapon,
                                .accepted = false,
                                .trackId = playerCombat_.SelectedTrackId(),
                                .message = "selected torpedo ammunition pool is exhausted"};
                            continue;
                        }
                        if (playerTorpedoTubeBank_.ReadyTubeCount(selectedPlayerWeapon_, simulationTimeSeconds) == 0U)
                        {
                            const auto wait = playerTorpedoTubeBank_.SecondsUntilNextReadyTube(
                                selectedPlayerWeapon_, simulationTimeSeconds);
                            lastCombatCommand_ = PlayerCombatCommandFeedback{
                                .command = PlayerCombatCommandType::FireWeapon,
                                .accepted = false,
                                .trackId = playerCombat_.SelectedTrackId(),
                                .message = wait
                                    ? "all compatible torpedo tubes are reloading; next tube ready in " +
                                        std::to_string(*wait) + " s"
                                    : "no compatible torpedo tube is available"};
                            continue;
                        }
                    }
                    const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit
                        ? AssessPlayerP700Employment(playerSnapshot)
                        : AssessPlayerTorpedoEmployment(playerSnapshot);
'''
s = replace_once(s, old_gate, new_gate, "runtime resource fire gate")
# Materialize even when previous ordnance is in flight; mark fire-control rearm pending.
s = replace_once(
    s,
    "        if (!playerTorpedo_.has_value() && !playerP700_.has_value() &&\n            playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)",
    "        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Launched)",
    "remove active projectile materialization lock")
s = replace_once(
    s,
    "            if (automatedPlayer)\n            {\n                automatedPlayerLaunchCommitted_ = true;\n            }",
    "            playerLaunchRearmPending_ = true;\n"
    "            if (p700AcceptanceMode_ && selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit)\n"
    "                p700AcceptanceLaunchCommitted_ = true;\n"
    "            if (automatedPlayer)\n"
    "            {\n                automatedPlayerLaunchCommitted_ = true;\n            }",
    "mark committed launch rearm")
# Stop P700 acceptance from rippling all 24 during one visual smoke.
s = replace_once(
    s,
    "            else if (selected && selected->estimatedPositionMeters.has_value() &&\n                     playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)\n            {\n                commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};\n            }",
    "            else if (selected && selected->estimatedPositionMeters.has_value() &&\n"
    "                     playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready &&\n"
    "                     !p700AcceptanceLaunchCommitted_)\n"
    "            {\n                commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};\n            }",
    "P700 acceptance one launch")
# Stage tube bank transactionally with inventory and archive the previous primary torpedo only after new creation succeeds.
s = replace_once(
    s,
    "        auto nextTorpedoInventory = playerTorpedoInventory_;\n        const auto consumedRoundMass = nextTorpedoInventory.Consume(selectedPlayerWeapon_);\n        if (!consumedRoundMass)\n        {\n            return std::unexpected(\"M5-H torpedo inventory consumption failed: \" + consumedRoundMass.error());\n        }",
    "        auto nextTorpedoInventory = playerTorpedoInventory_;\n"
    "        auto nextTorpedoTubeBank = playerTorpedoTubeBank_;\n"
    "        const auto tubeLaunch = nextTorpedoTubeBank.CommitLaunch(selectedPlayerWeapon_, simulationTimeSeconds);\n"
    "        if (!tubeLaunch)\n"
    "            return std::unexpected(\"M5-H torpedo tube launch failed: \" + tubeLaunch.error());\n"
    "        const auto consumedRoundMass = nextTorpedoInventory.Consume(selectedPlayerWeapon_);\n"
    "        if (!consumedRoundMass)\n"
    "        {\n            return std::unexpected(\"M5-H torpedo inventory consumption failed: \" + consumedRoundMass.error());\n        }",
    "stage torpedo tube transaction")
s = replace_once(
    s,
    "        // Commit the staged inventory only after the weapon runtime is successfully materialized.\n        playerTorpedoInventory_ = nextTorpedoInventory;\n        playerTorpedoInFlightDefinition_ = playerTorpedoDefinition_;\n        playerTorpedo_ = *launched;",
    "        // Commit launcher/ammunition state only after the new weapon runtime is valid. Preserve an older\n"
    "        // in-flight torpedo as an independent background flight instead of blocking another loaded tube.\n"
    "        if (playerTorpedo_)\n"
    "        {\n"
    "            if (!playerTorpedoInFlightDefinition_ || !playerTorpedoLaunchPosition_)\n"
    "                return std::unexpected(\"existing player torpedo lacks launch-time state during ripple launch\");\n"
    "            additionalPlayerTorpedoes_.push_back(*playerTorpedo_);\n"
    "            additionalPlayerTorpedoDefinitions_.push_back(*playerTorpedoInFlightDefinition_);\n"
    "            additionalPlayerTorpedoLaunchPositions_.push_back(*playerTorpedoLaunchPosition_);\n"
    "            additionalPlayerTorpedoForwardSigns_.push_back(playerTorpedoForwardSign_);\n"
    "        }\n"
    "        playerTorpedoInventory_ = nextTorpedoInventory;\n"
    "        playerTorpedoTubeBank_ = nextTorpedoTubeBank;\n"
    "        playerTorpedoInFlightDefinition_ = playerTorpedoDefinition_;\n"
    "        playerTorpedo_ = *launched;",
    "archive previous torpedo flight")
# Primary torpedo no longer owns fire-control rearm at terminal resolution.
s = replace_once(
    s,
    "            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n            if (!rearmed)\n                return std::unexpected(\"player torpedo resolution re-arm failed: \" + rearmed.error());\n            playerTorpedo_.reset();",
    "            playerTorpedo_.reset();",
    "remove terminal torpedo rearm")
# Advance archived torpedoes with launch-track guidance/collision and remove them after terminal state.
marker = "\n        std::optional<Weapons::P700GranitImpact> p700Impact{};\n"
if marker not in s:
    raise RuntimeError("additional torpedo advance insertion marker missing")
additional_torpedo_block = r'''
        for (std::size_t index = 0U; index < additionalPlayerTorpedoes_.size();)
        {
            auto& torpedo = additionalPlayerTorpedoes_[index];
            auto& definition = additionalPlayerTorpedoDefinitions_[index];
            bool keepImpactFrame = false;
            if (torpedo.movementDomain == Weapons::MovementDomain::Underwater)
            {
                auto perceivedTrack = FindTrack(playerTracks_.Tracks(), torpedo.guidanceTrackId);
                if (perceivedTrack && perceivedTrack->estimatedPositionMeters)
                {
                    const float forwardProgress =
                        (torpedo.positionMeters.x - additionalPlayerTorpedoLaunchPositions_[index].x) *
                        additionalPlayerTorpedoForwardSigns_[index];
                    if (forwardProgress < M5CombatTorpedoStraightRunMeters)
                        perceivedTrack->estimatedPositionMeters->y = additionalPlayerTorpedoLaunchPositions_[index].y;
                    else
                        perceivedTrack->estimatedPositionMeters->y -= M5CombatTorpedoAttackPointBelowPerceivedTargetMeters;
                }
                const auto advanced = Weapons::AdvanceConventionalTorpedoWithCollision(
                    definition, torpedo, perceivedTrack, *physicsWorld_, simulationTimeSeconds, playerBody_);
                if (!advanced)
                    return std::unexpected("background player torpedo advance failed: " + advanced.error());
                if (advanced->has_value())
                {
                    const auto& hit = **advanced;
                    if (!impact) impact = hit;
                    lastExplosion_ = hit.explosion;
                    keepImpactFrame = true;
                    if (hit.physicsHit.body == destroyer_.body && !destroyer_.integrity.destroyed)
                    {
                        const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, hit.damage);
                        if (!damaged) return std::unexpected("background torpedo destroyer damage failed: " + damaged.error());
                    }
                    else if (civilian_ && hit.physicsHit.body == civilian_->body && !civilian_->integrity.destroyed)
                    {
                        const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, hit.damage);
                        if (!damaged) return std::unexpected("background torpedo civilian damage failed: " + damaged.error());
                    }
                }
            }
            if (torpedo.movementDomain == Weapons::MovementDomain::Spent && !keepImpactFrame)
            {
                additionalPlayerTorpedoes_.erase(additionalPlayerTorpedoes_.begin() + static_cast<std::ptrdiff_t>(index));
                additionalPlayerTorpedoDefinitions_.erase(additionalPlayerTorpedoDefinitions_.begin() + static_cast<std::ptrdiff_t>(index));
                additionalPlayerTorpedoLaunchPositions_.erase(additionalPlayerTorpedoLaunchPositions_.begin() + static_cast<std::ptrdiff_t>(index));
                additionalPlayerTorpedoForwardSigns_.erase(additionalPlayerTorpedoForwardSigns_.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
'''
s = s.replace(marker, additional_torpedo_block + marker, 1)
# Archive existing P700 flight before replacing primary with the next hatch salvo.
s = replace_once(
    s,
    "        playerP700LaunchSlotIndex_ = committed->launcherSlotIndices.front();\n        playerP700LaunchSlotIndices_ = committed->launcherSlotIndices;",
    "        if (playerP700_)\n"
    "        {\n"
    "            const float priorRange = playerP700LaunchRangeMeters_.value_or(\n"
    "                Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters);\n"
    "            if (playerP700_->phase != Weapons::P700GranitPhase::Spent)\n"
    "            {\n"
    "                additionalPlayerP700Missiles_.push_back(*playerP700_);\n"
    "                additionalPlayerP700LaunchRanges_.push_back(priorRange);\n"
    "            }\n"
    "            for (const auto& wingman : playerP700Wingmen_)\n"
    "            {\n"
    "                if (wingman.phase == Weapons::P700GranitPhase::Spent) continue;\n"
    "                additionalPlayerP700Missiles_.push_back(wingman);\n"
    "                additionalPlayerP700LaunchRanges_.push_back(priorRange);\n"
    "            }\n"
    "        }\n"
    "        playerP700LaunchSlotIndex_ = committed->launcherSlotIndices.front();\n"
    "        playerP700LaunchSlotIndices_ = committed->launcherSlotIndices;",
    "archive previous P700 salvo")
s = replace_once(
    s,
    "        for (std::size_t index = 1U; index < committed->runtime.missiles.size(); ++index)\n            playerP700Wingmen_.push_back(std::move(committed->runtime.missiles[index]));\n\n        const auto exposure = ObserveExposureEvent(",
    "        for (std::size_t index = 1U; index < committed->runtime.missiles.size(); ++index)\n"
    "            playerP700Wingmen_.push_back(std::move(committed->runtime.missiles[index]));\n"
    "        nextP700LaunchAllowedTimeSeconds_ = simulationTimeSeconds + M5CombatP700MinimumInterSalvoSeconds;\n\n"
    "        const auto exposure = ObserveExposureEvent(",
    "P700 ripple cooldown commit")
# Primary P700 terminal resolution no longer re-arms fire-control.
s = replace_once(
    s,
    "                const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n                if (!rearmed) return std::unexpected(\"P-700 commander re-arm failed: \" + rearmed.error());\n                playerP700_.reset();",
    "                playerP700_.reset();",
    "remove terminal P700 rearm")
# Advance archived P700 missiles after current salvo logic.
marker = "\n        const auto threatPerception = AdvanceIncomingThreatPerception(playerSnapshot, simulationTimeSeconds);\n"
if marker not in s:
    raise RuntimeError("additional P700 advance insertion marker missing")
additional_p700_block = r'''
        for (std::size_t index = 0U; index < additionalPlayerP700Missiles_.size();)
        {
            auto& missile = additionalPlayerP700Missiles_[index];
            bool keepImpactFrame = false;
            if (missile.phase != Weapons::P700GranitPhase::Spent)
            {
                const auto perceivedTrack = FindTrack(playerTracks_.Tracks(), missile.guidanceTrackId);
                std::optional<Weapons::P700TerminalDefenseProfile> targetDefense{};
                if (!p700AcceptanceMode_)
                {
                    Weapons::P700TerminalDefenseProfile defense{};
                    const float launchRange = additionalPlayerP700LaunchRanges_[index];
                    if (launchRange < Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters)
                    {
                        const float minimumRange = Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters;
                        const float shortfall = std::clamp((minimumRange - launchRange) / minimumRange, 0.0F, 1.0F);
                        defense.hardKillProbability = std::clamp(defense.hardKillProbability + 0.30F * shortfall, 0.0F, 1.0F);
                        defense.maneuverDefeatProbability = std::clamp(defense.maneuverDefeatProbability + 0.10F * shortfall, 0.0F, 1.0F);
                        defense.seekerFailureProbability = std::clamp(defense.seekerFailureProbability + 0.05F * shortfall, 0.0F, 1.0F);
                    }
                    targetDefense = defense;
                }
                const auto advanced = Weapons::AdvanceP700GranitWithCollision(
                    playerP700Definition_, missile, perceivedTrack, *physicsWorld_, simulationTimeSeconds,
                    playerBody_, targetDefense);
                if (!advanced)
                    return std::unexpected("background P-700 advance failed: " + advanced.error());
                if (advanced->has_value())
                {
                    const auto& hit = **advanced;
                    if (!p700Impact) p700Impact = hit;
                    lastExplosion_ = hit.explosion;
                    keepImpactFrame = true;
                    if (hit.physicsHit.body == destroyer_.body && !destroyer_.integrity.destroyed)
                    {
                        const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, hit.damage);
                        if (!damaged) return std::unexpected("background P-700 destroyer damage failed: " + damaged.error());
                    }
                    else if (civilian_ && hit.physicsHit.body == civilian_->body && !civilian_->integrity.destroyed)
                    {
                        const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, hit.damage);
                        if (!damaged) return std::unexpected("background P-700 civilian damage failed: " + damaged.error());
                    }
                }
            }
            if (missile.phase == Weapons::P700GranitPhase::Spent && !keepImpactFrame)
            {
                additionalPlayerP700Missiles_.erase(additionalPlayerP700Missiles_.begin() + static_cast<std::ptrdiff_t>(index));
                additionalPlayerP700LaunchRanges_.erase(additionalPlayerP700LaunchRanges_.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
'''
s = s.replace(marker, additional_p700_block + marker, 1)
# Presentation/resource fields and can-fire gate.
s = replace_once(
    s,
    "        playerCombatPresentation.selectedWeapon = selectedPlayerWeapon_;\n        playerCombatPresentation.p700LoadedCount = p700LauncherInventory_ ? p700LauncherInventory_->LoadedCount() : 0U;",
    "        playerCombatPresentation.selectedWeapon = selectedPlayerWeapon_;\n"
    "        playerCombatPresentation.p700LoadedCount = p700LauncherInventory_ ? p700LauncherInventory_->LoadedCount() : 0U;\n"
    "        playerCombatPresentation.p700NextLaunchReadySeconds = std::max(\n"
    "            0.0, nextP700LaunchAllowedTimeSeconds_ - simulationTimeSeconds);\n"
    "        playerCombatPresentation.activeP700FloodProgress = playerP700_\n"
    "            ? std::optional<float>{playerP700_->launcherFloodProgress} : std::nullopt;\n"
    "        playerCombatPresentation.torpedoRoundsRemaining = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoInventory_.LoadedCount(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedoReadyTubeCount = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoTubeBank_.ReadyTubeCount(selectedPlayerWeapon_, simulationTimeSeconds);\n"
    "        const auto selectedTubeCalibre = Armament::AnteyTorpedoTubeCalibreForWeapon(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedoTubeCount = !selectedTubeCalibre ? 0U :\n"
    "            (*selectedTubeCalibre == Armament::AnteyTorpedoTubeCalibre::Mm533\n"
    "                ? Armament::Antey533MmTorpedoTubeCount : Armament::Antey650MmTorpedoTubeCount);\n"
    "        if (selectedTubeCalibre && playerCombatPresentation.torpedoReadyTubeCount == 0U &&\n"
    "            playerCombatPresentation.torpedoRoundsRemaining > 0U)\n"
    "            playerCombatPresentation.torpedoNextTubeReadySeconds =\n"
    "                playerTorpedoTubeBank_.SecondsUntilNextReadyTube(selectedPlayerWeapon_, simulationTimeSeconds);\n"
    "        playerCombatPresentation.playerTorpedoesInFlight = additionalPlayerTorpedoes_.size() +\n"
    "            (playerTorpedo_ && playerTorpedo_->movementDomain != Weapons::MovementDomain::Spent ? 1U : 0U);\n"
    "        playerCombatPresentation.playerP700InFlight = additionalPlayerP700Missiles_.size() +\n"
    "            (playerP700_ && playerP700_->phase != Weapons::P700GranitPhase::Spent ? 1U : 0U) +\n"
    "            static_cast<std::size_t>(std::count_if(playerP700Wingmen_.begin(), playerP700Wingmen_.end(), [](const auto& missile) {\n"
    "                return missile.phase != Weapons::P700GranitPhase::Spent;\n"
    "            }));",
    "runtime presentation resource fields")
# Insert resource can-fire restrictions before employment check.
s = replace_once(
    s,
    "        if (playerCombatPresentation.canFireWeapon)\n        {\n            const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit",
    "        if (playerCombatPresentation.canFireWeapon)\n"
    "        {\n"
    "            if (selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit)\n"
    "                playerCombatPresentation.canFireWeapon =\n"
    "                    simulationTimeSeconds + 1.0e-9 >= nextP700LaunchAllowedTimeSeconds_;\n"
    "            else\n"
    "                playerCombatPresentation.canFireWeapon =\n"
    "                    playerCombatPresentation.torpedoRoundsRemaining > 0U &&\n"
    "                    playerCombatPresentation.torpedoReadyTubeCount > 0U;\n"
    "        }\n"
    "        if (playerCombatPresentation.canFireWeapon)\n"
    "        {\n            const auto employment = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit",
    "resource can-fire presentation gate")
# Fields.
s = replace_once(
    s,
    "    Armament::AnteyTorpedoInventory playerTorpedoInventory_{};\n    std::optional<Armament::P700CarrierLaunchContract> p700CarrierLaunchContract_{};",
    "    Armament::AnteyTorpedoInventory playerTorpedoInventory_{};\n"
    "    Armament::AnteyTorpedoTubeBank playerTorpedoTubeBank_{};\n"
    "    std::optional<Armament::P700CarrierLaunchContract> p700CarrierLaunchContract_{};",
    "runtime tube bank field")
s = replace_once(
    s,
    "    std::vector<Weapons::P700GranitRuntimeState> playerP700Wingmen_{};\n    std::optional<float> playerP700LaunchRangeMeters_{};",
    "    std::vector<Weapons::P700GranitRuntimeState> playerP700Wingmen_{};\n"
    "    std::vector<Weapons::P700GranitRuntimeState> additionalPlayerP700Missiles_{};\n"
    "    std::vector<float> additionalPlayerP700LaunchRanges_{};\n"
    "    std::optional<float> playerP700LaunchRangeMeters_{};",
    "additional P700 fields")
s = replace_once(
    s,
    "    std::uint64_t nextP700SalvoId_ = 1U;\n    PlayerCombatCommandRuntime playerCombat_;",
    "    std::uint64_t nextP700SalvoId_ = 1U;\n"
    "    double nextP700LaunchAllowedTimeSeconds_ = 0.0;\n"
    "    PlayerCombatCommandRuntime playerCombat_;",
    "P700 cadence field")
s = replace_once(
    s,
    "    std::optional<Weapons::ConventionalTorpedoRuntimeState> playerTorpedo_{};\n    std::optional<Physics::PhysicsVector3> playerTorpedoLaunchPosition_{};\n    float playerTorpedoForwardSign_ = 1.0F;",
    "    std::optional<Weapons::ConventionalTorpedoRuntimeState> playerTorpedo_{};\n"
    "    std::optional<Physics::PhysicsVector3> playerTorpedoLaunchPosition_{};\n"
    "    float playerTorpedoForwardSign_ = 1.0F;\n"
    "    std::vector<Weapons::ConventionalTorpedoRuntimeState> additionalPlayerTorpedoes_{};\n"
    "    std::vector<Weapons::ConventionalTorpedoDefinition> additionalPlayerTorpedoDefinitions_{};\n"
    "    std::vector<Physics::PhysicsVector3> additionalPlayerTorpedoLaunchPositions_{};\n"
    "    std::vector<float> additionalPlayerTorpedoForwardSigns_{};",
    "additional torpedo fields")
s = replace_once(
    s,
    "    bool p700AcceptanceMode_ = false;\n    bool automatedPlayerLaunchCommitted_ = false;",
    "    bool p700AcceptanceMode_ = false;\n"
    "    bool p700AcceptanceLaunchCommitted_ = false;\n"
    "    bool playerLaunchRearmPending_ = false;\n"
    "    bool automatedPlayerLaunchCommitted_ = false;",
    "launch lifecycle flags")
write(p, s)


# -----------------------------------------------------------------------------
# Presentation draws: render additional in-flight torpedoes and P-700 missiles.
# -----------------------------------------------------------------------------
p = "Game/Combat/CombatPlaygroundPresentation.h"
s = read(p)
s = replace_once(
    s,
    "    float hatchOpenProgress = 0.0F;\n    float postExitTransitionProgress = 0.0F;",
    "    float hatchOpenProgress = 0.0F;\n    float launcherFloodProgress = 0.0F;\n    float postExitTransitionProgress = 0.0F;",
    "P700 presentation flood field")
s = replace_once(
    s,
    "    std::optional<CombatPlaygroundTorpedoPresentation> playerTorpedo{};\n    std::optional<CombatPlaygroundTorpedoPresentation> destroyerTorpedo{};",
    "    std::optional<CombatPlaygroundTorpedoPresentation> playerTorpedo{};\n"
    "    std::vector<CombatPlaygroundTorpedoPresentation> additionalPlayerTorpedoes{};\n"
    "    std::optional<CombatPlaygroundTorpedoPresentation> destroyerTorpedo{};",
    "additional torpedo presentation vector")
# Add runtime additional torpedoes after primary snapshot.
primary_torpedo_end = '''        snapshot.playerTorpedo = CombatPlaygroundTorpedoPresentation{
            .positionMeters = torpedo->positionMeters,
            .headingRadians = torpedo->headingRadians,
            .movementDomain = torpedo->movementDomain};
    }

    if (const auto& torpedo = runtime.DestroyerTorpedo(); torpedo.has_value())
'''
replacement = '''        snapshot.playerTorpedo = CombatPlaygroundTorpedoPresentation{
            .positionMeters = torpedo->positionMeters,
            .headingRadians = torpedo->headingRadians,
            .movementDomain = torpedo->movementDomain};
    }
    for (const auto& torpedo : runtime.AdditionalPlayerTorpedoes())
    {
        if (!torpedo.positionMeters.IsFinite() || !std::isfinite(torpedo.headingRadians))
            return std::unexpected("additional player torpedo presentation state is invalid");
        snapshot.additionalPlayerTorpedoes.push_back(CombatPlaygroundTorpedoPresentation{
            .positionMeters = torpedo.positionMeters,
            .headingRadians = torpedo.headingRadians,
            .movementDomain = torpedo.movementDomain});
    }

    if (const auto& torpedo = runtime.DestroyerTorpedo(); torpedo.has_value())
'''
s = replace_once(s, primary_torpedo_end, replacement, "additional torpedo snapshot")
s = replace_once(
    s,
    "            !std::isfinite(p700->hatchOpenProgress) || p700->hatchOpenProgress < 0.0F || p700->hatchOpenProgress > 1.0F ||\n            !std::isfinite(p700->postExitTransitionProgress) ||",
    "            !std::isfinite(p700->hatchOpenProgress) || p700->hatchOpenProgress < 0.0F || p700->hatchOpenProgress > 1.0F ||\n"
    "            !std::isfinite(p700->launcherFloodProgress) || p700->launcherFloodProgress < 0.0F ||\n"
    "            p700->launcherFloodProgress > 1.0F || !std::isfinite(p700->postExitTransitionProgress) ||",
    "primary P700 flood presentation validation")
s = replace_once(
    s,
    "            .hatchOpenProgress = p700->hatchOpenProgress,\n            .postExitTransitionProgress = p700->postExitTransitionProgress,",
    "            .hatchOpenProgress = p700->hatchOpenProgress,\n"
    "            .launcherFloodProgress = p700->launcherFloodProgress,\n"
    "            .postExitTransitionProgress = p700->postExitTransitionProgress,",
    "primary P700 flood copy")
s = replace_once(
    s,
    "            .hatchOpenProgress = p700.hatchOpenProgress,\n            .postExitTransitionProgress = p700.postExitTransitionProgress,",
    "            .hatchOpenProgress = p700.hatchOpenProgress,\n"
    "            .launcherFloodProgress = p700.launcherFloodProgress,\n"
    "            .postExitTransitionProgress = p700.postExitTransitionProgress,",
    "P700 wingman flood copy")
# Append archived P700s to the same presentation vector used for wingmen.
wingman_loop_end = '''            .terminalOutcome = p700.terminalOutcome, .phase = p700.phase});
    }

    if (const auto& decoy = runtime.Decoy(); decoy.has_value())
'''
wingman_replacement = '''            .terminalOutcome = p700.terminalOutcome, .phase = p700.phase});
    }
    for (const auto& p700 : runtime.AdditionalPlayerP700Missiles())
    {
        if (p700.phase == Weapons::P700GranitPhase::Stored || p700.phase == Weapons::P700GranitPhase::Spent)
            continue;
        if (!p700.positionMeters.IsFinite() || !std::isfinite(p700.headingRadians) ||
            !std::isfinite(p700.hatchOpenProgress) || !std::isfinite(p700.launcherFloodProgress) ||
            !std::isfinite(p700.postExitTransitionProgress) || !std::isfinite(p700.deploymentProgress))
            return std::unexpected("ripple P-700 presentation state is invalid");
        snapshot.playerP700Wingmen.push_back(CombatPlaygroundP700Presentation{
            .positionMeters = p700.positionMeters, .headingRadians = p700.headingRadians,
            .hatchOpenProgress = p700.hatchOpenProgress, .launcherFloodProgress = p700.launcherFloodProgress,
            .postExitTransitionProgress = p700.postExitTransitionProgress,
            .deploymentProgress = p700.deploymentProgress,
            .launchBoosterActive = p700.launchBoosterActive, .launchBoosterAttached = p700.launchBoosterAttached,
            .noseProtectionCapAttached = p700.noseProtectionCapAttached, .mainEngineActive = p700.mainEngineActive,
            .terminalOutcome = p700.terminalOutcome, .phase = p700.phase});
    }

    if (const auto& decoy = runtime.Decoy(); decoy.has_value())
'''
s = replace_once(s, wingman_loop_end, wingman_replacement, "additional P700 presentation")
# Render additional torpedoes using the same visual contract.
needle = '''    if (snapshot.destroyerTorpedo && snapshot.destroyerTorpedo->movementDomain == Weapons::MovementDomain::Underwater)
'''
if needle not in s:
    raise RuntimeError("additional torpedo draw insertion marker missing")
add_draws = r'''    for (const auto& torpedo : snapshot.additionalPlayerTorpedoes)
    {
        if (torpedo.movementDomain != Weapons::MovementDomain::Underwater)
            continue;
        const auto transform = PoseScaleTransform(
            torpedo.positionMeters, Weapons::WeaponHeadingQuaternion(torpedo.headingRadians),
            {.x = 1.0F, .y = 1.0F, .z = 1.0F});
        if (!transform) return std::unexpected(transform.error());
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::PlayerTorpedo, *transform,
            Material("M5PlayerTorpedoRipple", {1.0F, 0.96F, 0.56F, 1.0F}, 0.08F, 0.24F));
        if (!draw) return std::unexpected(draw.error());
        draws.push_back(std::move(*draw));
    }

'''
s = s.replace(needle, add_draws + needle, 1)
write(p, s)


# -----------------------------------------------------------------------------
# Combat HUD: surface the resource pressure instead of hiding cooldowns.
# -----------------------------------------------------------------------------
p = "Game/Combat/CombatCommandUi.cpp"
s = read(p)
old = '''    ImGui::Text("P-700 loaded: %zu / %zu", snapshot.p700LoadedCount, Armament::AnteyP700LauncherSlotCount);
    ImGui::Text("P-700 salvo: %s (G / D-pad Down)",
        snapshot.p700SalvoMode == Weapons::P700SalvoMode::Pair ? "PAIR x2 / cooperative" : "SINGLE x1 / economical");
'''
new = '''    ImGui::Text("P-700 loaded: %zu / %zu", snapshot.p700LoadedCount, Armament::AnteyP700LauncherSlotCount);
    ImGui::Text("P-700 salvo: %s (G / D-pad Down)",
        snapshot.p700SalvoMode == Weapons::P700SalvoMode::Pair ? "PAIR x2 / cooperative" : "SINGLE x1 / economical");
    if (snapshot.p700NextLaunchReadySeconds > 0.0)
        ImGui::Text("Next P-700 hatch sequence: %.1f s", snapshot.p700NextLaunchReadySeconds);
    if (snapshot.activeP700FloodProgress && *snapshot.activeP700FloodProgress < 1.0F)
        ImGui::Text("P-700 launcher flooding: %.0f%%", *snapshot.activeP700FloodProgress * 100.0F);
    if (snapshot.selectedWeapon != Armament::PlayerWeaponType::P700Granit)
    {
        ImGui::Text("Torpedo ammo: %zu | ready tubes: %zu / %zu",
                    snapshot.torpedoRoundsRemaining, snapshot.torpedoReadyTubeCount, snapshot.torpedoTubeCount);
        if (snapshot.torpedoNextTubeReadySeconds)
            ImGui::Text("Next compatible tube reload: %.1f s", *snapshot.torpedoNextTubeReadySeconds);
    }
    ImGui::Text("In flight: torpedoes %zu | P-700 %zu",
                snapshot.playerTorpedoesInFlight, snapshot.playerP700InFlight);
'''
s = replace_once(s, old, new, "HUD launcher status")
write(p, s)


# -----------------------------------------------------------------------------
# Tests: flooding lifecycle, tube bank and paired-hatch single-ripple policy.
# -----------------------------------------------------------------------------
p = "Tests/M5P700Checks.h"
s = read(p)
s = replace_once(
    s,
    "        .launcherHatchOpeningSeconds = 0.75,\n        .waterExitTransitionSeconds = 0.50,",
    "        .launcherHatchOpeningSeconds = 0.75,\n        .launcherFloodingSeconds = 2.0,\n        .waterExitTransitionSeconds = 0.50,",
    "P700 test definition flooding")
s = replace_once(
    s,
    "        runtime.deploymentProgress != 0.0F || runtime.guidanceTrackId != targetTrack.trackId ||\n        runtime.launchBoosterActive || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)",
    "        runtime.deploymentProgress != 0.0F || runtime.launcherFloodProgress != 0.0F ||\n"
    "        runtime.guidanceTrackId != targetTrack.trackId || runtime.launchBoosterActive ||\n"
    "        !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)",
    "P700 initial flood test")
s = replace_once(
    s,
    "    lifecycleTimeSeconds = 1.00;\n    const auto underwater = AdvanceP700GranitWithCollision(\n        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);\n    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||\n        runtime.hatchOpenProgress != 1.0F || !runtime.launchBoosterActive ||\n        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= runtime.surfaceLevelYMeters)\n    {\n        return fail(\"underwater exit must use the attached launch booster and stay folded\");\n    }",
    "    lifecycleTimeSeconds = 1.00;\n"
    "    const auto flooding = AdvanceP700GranitWithCollision(\n"
    "        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);\n"
    "    if (!flooding || *flooding || runtime.phase != P700GranitPhase::HatchOpening ||\n"
    "        runtime.hatchOpenProgress != 1.0F || runtime.launcherFloodProgress <= 0.0F ||\n"
    "        runtime.launcherFloodProgress >= 1.0F || runtime.launchBoosterActive || runtime.positionMeters.y != -30.0F)\n"
    "    {\n        return fail(\"opened P-700 launcher must flood before booster ignition or missile motion\");\n    }\n\n"
    "    lifecycleTimeSeconds = definition.launcherHatchOpeningSeconds + definition.launcherFloodingSeconds + 0.10;\n"
    "    const auto underwater = AdvanceP700GranitWithCollision(\n"
    "        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);\n"
    "    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||\n"
    "        runtime.hatchOpenProgress != 1.0F || runtime.launcherFloodProgress != 1.0F ||\n"
    "        !runtime.launchBoosterActive || runtime.deploymentProgress != 0.0F ||\n"
    "        runtime.positionMeters.y >= runtime.surfaceLevelYMeters)\n"
    "    {\n        return fail(\"underwater exit must begin only after the launcher is fully flooded\");\n    }",
    "P700 flooding lifecycle test")
s = s.replace("advanceUntilPhase(P700GranitPhase::WaterExit, 3.0)",
              "advanceUntilPhase(P700GranitPhase::WaterExit, 5.0)", 1)
write(p, s)

p = "Tests/PeriscopeBallastGameplayChecks.h"
s = read(p)
s = replace_once(
    s,
    '#include "Game/Weapons/AnteyTorpedoInventory.h"\n',
    '#include "Game/Weapons/AnteyTorpedoInventory.h"\n#include "Game/Weapons/AnteyTorpedoTubeBank.h"\n',
    "tube bank test include")
anchor = '''    if (!torpedoes.Consume(Game::Armament::PlayerWeaponType::HeavyweightTorpedo) ||
        !torpedoes.Consume(Game::Armament::PlayerWeaponType::Type6576AEconomy) ||
        torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo) != 17U ||
        torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::Type6576AFast) != 9U ||
        std::abs(torpedoes.ExpendedMassKg() - 6'500.0F) > 1.0F)
        return false;
'''
addition = anchor + '''
    Game::Armament::AnteyTorpedoTubeBank tubeBank{};
    if (tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 0.0) != 4U ||
        tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::Type6576AFast, 0.0) != 2U)
        return false;
    for (std::size_t shot = 0U; shot < 4U; ++shot)
    {
        if (!tubeBank.CommitLaunch(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, static_cast<double>(shot)))
            return false;
    }
    if (tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 3.0) != 0U ||
        tubeBank.CommitLaunch(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 3.1) ||
        !tubeBank.SecondsUntilNextReadyTube(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 3.1) ||
        *tubeBank.SecondsUntilNextReadyTube(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 3.1) < 41.8 ||
        tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo, 45.0) != 1U)
        return false;
    if (!tubeBank.CommitLaunch(Game::Armament::PlayerWeaponType::Type6576AFast, 5.0) ||
        !tubeBank.CommitLaunch(Game::Armament::PlayerWeaponType::Type6576AEconomy, 6.0) ||
        tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::Type6576AFast, 6.0) != 0U ||
        tubeBank.CommitLaunch(Game::Armament::PlayerWeaponType::Type6576AFast, 7.0) ||
        tubeBank.ReadyTubeCount(Game::Armament::PlayerWeaponType::Type6576AFast, 65.0) != 1U)
        return false;
'''
s = replace_once(s, anchor, addition, "tube bank gameplay tests")
write(p, s)

p = "Tests/P700LauncherInventoryTest.cpp"
s = read(p)
insert_after = '''    const std::array<std::size_t, 2> duplicatePair{0U, 0U};
    if (inventory.ConsumeMany(duplicatePair) || inventory.LoadedCount() != AnteyP700LauncherSlotCount)
    {
        std::cerr << "P-700 transactional pair consumption mutated inventory after invalid request\\n";
        return false;
    }
'''
addition = insert_after + '''
    auto rippleInventoryResult = P700LauncherInventory::Create(anchors);
    if (!rippleInventoryResult)
        return false;
    auto rippleInventory = std::move(*rippleInventoryResult);
    for (std::size_t hatch = 0U; hatch < AnteyP700LauncherSlotCount / 2U; ++hatch)
    {
        const auto single = rippleInventory.LoadedSlotIndices(1U);
        const std::size_t expected = hatch * 2U;
        if (!single || single->size() != 1U || single->front() != expected ||
            !rippleInventory.Consume(single->front()))
        {
            std::cerr << "P-700 single ripple did not advance across paired hatch groups\\n";
            return false;
        }
    }
    const auto secondPassSingle = rippleInventory.LoadedSlotIndices(1U);
    if (!secondPassSingle || secondPassSingle->front() != 1U)
    {
        std::cerr << "P-700 single ripple did not return to half-used hatch groups on its second pass\\n";
        return false;
    }
'''
s = replace_once(s, insert_after, addition, "P700 single ripple inventory tests")
write(p, s)


# -----------------------------------------------------------------------------
# Documentation: public facts vs explicit gameplay timing policy.
# -----------------------------------------------------------------------------
p = "docs/development/m5-weapon-ux-soft-envelopes.md"
s = read(p)
s += '''

## Launcher-cycle resource contract

Project 949A gameplay now separates ammunition from launcher readiness. The accepted topology is four 533 mm bow torpedo tubes and two 650 mm bow torpedo tubes, with the existing 18-round 533 mm and 10-round 650 mm ammunition pools. All six tubes begin loaded. Firing consumes a round and makes only that physical tube unavailable while it reloads; the other loaded tubes may continue firing. Open references describe automated rapid loading and the ability to expend the torpedo load within several minutes but do not expose a dependable per-tube cycle time, so Deep Run uses explicit GAME POLICY reloads of 45 s for 533 mm and 60 s for 650 mm tubes.

The 24 P-700 missiles remain dedicated one-shot launcher inventory rather than a reloadable magazine. Single mode advances across the 12 paired hatch groups before revisiting the second missile under a half-used hatch; Pair mode commits both loaded missiles under one paired hatch. A launch sequence is hatch opening -> launcher flooding -> booster ejection. Flooding is explicit because the real system is publicly described as a wet launch; its 2.0 s duration and the 5.0 s minimum interval between player salvo commits are GAME POLICY pacing values, not claimed classified SM-225A timings. Already launched torpedoes and Granits keep independent flight runtimes, so launcher readiness rather than flight duration controls follow-on shots.
'''
write(p, s)

print("launcher-cycle gameplay patch applied")
