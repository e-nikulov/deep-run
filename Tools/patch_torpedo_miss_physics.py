from pathlib import Path


def replace_one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match, got {count}')
    return text.replace(old, new, 1)


# 1) Bounded torpedo endurance / explicit terminal reason.
path = Path('Simulation/Weapons/ConventionalTorpedo.h')
text = path.read_text(encoding='utf-8')
text = replace_one(text,
'''enum class MovementDomain
{
    Attached,
    Underwater,
    Spent,
};
''',
'''enum class MovementDomain
{
    Attached,
    Underwater,
    Spent,
};

enum class ConventionalTorpedoTerminalReason
{
    None,
    Impact,
    EnduranceExpired,
};
''', 'terminal reason enum')
text = replace_one(text,
'''    float underwaterSpeedMetersPerSecond = 20.0F;
    float maximumTurnRateRadiansPerSecond = 0.25F;

    // Gameplay-authored 2.5D depth-course limit.''',
'''    float underwaterSpeedMetersPerSecond = 20.0F;
    float maximumTurnRateRadiansPerSecond = 0.25F;
    // GAME POLICY only: finite propulsion/energy endurance prevents a missed weapon from pursuing forever.
    // This is not an exact endurance/range claim for any real torpedo.
    double maximumRunTimeSeconds = 300.0;

    // Gameplay-authored 2.5D depth-course limit.''', 'definition endurance')
text = replace_one(text,
'''    float speedMetersPerSecond = 0.0F;
    double lastUpdateTimeSeconds = 0.0;
    std::optional<std::uint64_t> guidanceTrackId{};''',
'''    float speedMetersPerSecond = 0.0F;
    double launchTimeSeconds = 0.0;
    double lastUpdateTimeSeconds = 0.0;
    ConventionalTorpedoTerminalReason terminalReason = ConventionalTorpedoTerminalReason::None;
    std::optional<std::uint64_t> guidanceTrackId{};''', 'runtime endurance state')
text = replace_one(text,
'''        !std::isfinite(definition.maximumTurnRateRadiansPerSecond) ||
        definition.maximumTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumTurnRateRadiansPerSecond > 3.1415927F ||
        !std::isfinite(definition.maximumVerticalCourseAngleRadians) ||''',
'''        !std::isfinite(definition.maximumTurnRateRadiansPerSecond) ||
        definition.maximumTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumTurnRateRadiansPerSecond > 3.1415927F ||
        !std::isfinite(definition.maximumRunTimeSeconds) || definition.maximumRunTimeSeconds <= 0.0 ||
        !std::isfinite(definition.maximumVerticalCourseAngleRadians) ||''', 'validate endurance')
text = replace_one(text,
'''        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .guidanceTrackId = targetTrack.trackId,''',
'''        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,
        .launchTimeSeconds = simulationTimeSeconds,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .terminalReason = ConventionalTorpedoTerminalReason::None,
        .guidanceTrackId = targetTrack.trackId,''', 'launch endurance state')

insert_anchor = '''[[nodiscard]] inline std::expected<void, std::string> UpdateConventionalTorpedoGuidance(
'''
helper = '''[[nodiscard]] inline std::expected<bool, std::string> ExpireConventionalTorpedoEnduranceIfNeeded(
    const ConventionalTorpedoDefinition& definition,
    ConventionalTorpedoRuntimeState& state,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || !std::isfinite(state.launchTimeSeconds) ||
        simulationTimeSeconds < state.lastUpdateTimeSeconds || simulationTimeSeconds < state.launchTimeSeconds)
    {
        return std::unexpected("conventional torpedo endurance time is invalid or time-reversing");
    }
    if (state.movementDomain != MovementDomain::Underwater || state.impactedBody.has_value())
    {
        return false;
    }
    const double elapsedSeconds = simulationTimeSeconds - state.launchTimeSeconds;
    if (elapsedSeconds + 1.0e-9 < definition.maximumRunTimeSeconds)
    {
        return false;
    }
    state.speedMetersPerSecond = 0.0F;
    state.movementDomain = MovementDomain::Spent;
    state.terminalReason = ConventionalTorpedoTerminalReason::EnduranceExpired;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return true;
}

'''
text = replace_one(text, insert_anchor, helper + insert_anchor, 'endurance helper')
text = replace_one(text,
'''    const auto definitionValid = ValidateConventionalTorpedoDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (state.weapon.definitionId != definition.weapon.id || state.weapon.phase != WeaponPhase::Launched ||''',
'''    const auto definitionValid = ValidateConventionalTorpedoDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    const auto expired = ExpireConventionalTorpedoEnduranceIfNeeded(definition, state, simulationTimeSeconds);
    if (!expired)
    {
        return std::unexpected(expired.error());
    }
    if (*expired)
    {
        return {};
    }
    if (state.weapon.definitionId != definition.weapon.id || state.weapon.phase != WeaponPhase::Launched ||''', 'guidance expiry')
text = replace_one(text,
'''    candidate.movementDomain = MovementDomain::Spent;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;''',
'''    candidate.movementDomain = MovementDomain::Spent;
    candidate.terminalReason = ConventionalTorpedoTerminalReason::Impact;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;''', 'impact reason')
path.write_text(text, encoding='utf-8')

# 2) Apply the same endurance contract to local-seeker steering.
path = Path('Simulation/Weapons/TorpedoSeeker.h')
text = path.read_text(encoding='utf-8')
text = replace_one(text,
'''    if (!seekerValid)
    {
        return std::unexpected(seekerValid.error());
    }
    if (torpedo.weapon.definitionId != definition.weapon.id || torpedo.weapon.phase != WeaponPhase::Launched ||''',
'''    if (!seekerValid)
    {
        return std::unexpected(seekerValid.error());
    }
    const auto expired = ExpireConventionalTorpedoEnduranceIfNeeded(definition, torpedo, simulationTimeSeconds);
    if (!expired)
    {
        return std::unexpected(expired.error());
    }
    if (*expired)
    {
        return {};
    }
    if (torpedo.weapon.definitionId != definition.weapon.id || torpedo.weapon.phase != WeaponPhase::Launched ||''', 'seeker guidance expiry')
text = replace_one(text,
'''    candidate.movementDomain = MovementDomain::Spent;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;''',
'''    candidate.movementDomain = MovementDomain::Spent;
    candidate.terminalReason = ConventionalTorpedoTerminalReason::Impact;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;''', 'seeker impact reason')
path.write_text(text, encoding='utf-8')

# 3) Let local passive/active seeker paths consume the existing thermocline attenuation model.
path = Path('Game/Combat/CombatPlaygroundRuntime.h')
text = path.read_text(encoding='utf-8')
text = replace_one(text,
'''#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/SensorObservation.h"''',
'''#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Acoustics/AcousticEnvironment.h"
#include "Simulation/Perception/SensorObservation.h"''', 'acoustic environment include')

# Player passive path: compute environment per emission because target/decoy depths may differ.
text = replace_one(text,
'''            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds);
            if (!observed)
                return std::unexpected("M5-E.1 seeker acoustic propagation failed: " + observed.error().message);''',
'''            const float referenceSurfaceYMeters = destroyerAcoustics.emitter.positionMeters.y +
                destroyerDefinition_.bodyCenterBelowSurfaceMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters, passiveReceiver.positionMeters, referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 torpedo passive environment path failed: " + environment.error());
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
                return std::unexpected("M5-E.1 seeker acoustic propagation failed: " + observed.error().message);''', 'player passive environment')
text = replace_one(text,
'''            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *playerTorpedoActivePulse_, *playerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, {}, {}, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)''',
'''            const float referenceSurfaceYMeters = destroyerAcoustics.emitter.positionMeters.y +
                destroyerDefinition_.bodyCenterBelowSurfaceMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                playerTorpedoActivePulse_->originMeters, playerTorpedoActiveReflector_->positionMeters,
                referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 torpedo active environment path failed: " + environment.error());
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *playerTorpedoActivePulse_, *playerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, *environment, *environment, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)''', 'player active environment')

text = replace_one(text,
'''            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds);
            if (!observed)
                return std::unexpected("M5-J4 hostile seeker acoustic propagation failed: " + observed.error().message);''',
'''            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters, passiveReceiver.positionMeters, referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 hostile torpedo passive environment path failed: " + environment.error());
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
                return std::unexpected("M5-J4 hostile seeker acoustic propagation failed: " + observed.error().message);''', 'destroyer passive environment')
text = replace_one(text,
'''            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *destroyerTorpedoActivePulse_, *destroyerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, {}, {}, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)''',
'''            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                destroyerTorpedoActivePulse_->originMeters, destroyerTorpedoActiveReflector_->positionMeters,
                referenceSurfaceYMeters, 0.0F);
            if (!environment)
                return std::unexpected("M5 hostile torpedo active environment path failed: " + environment.error());
            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
                acousticWorld_, *destroyerTorpedoActivePulse_, *destroyerTorpedoActiveReflector_, activeReceiver,
                simulationTimeSeconds, *environment, *environment, M5CombatTorpedoActiveSonarConfig);
            if (!activeEcho)''', 'destroyer active environment')
path.write_text(text, encoding='utf-8')

# 4) Regression coverage for finite endurance.
path = Path('Tests/M5ConventionalTorpedoChecks.h')
text = path.read_text(encoding='utf-8')
text = replace_one(text,
'''    invalidDefinition = definition;
    invalidDefinition.maximumVerticalCourseAngleRadians = 0.0F;''',
'''    invalidDefinition = definition;
    invalidDefinition.maximumRunTimeSeconds = 0.0;
    if (ValidateConventionalTorpedoDefinition(invalidDefinition))
    {
        return false;
    }
    invalidDefinition = definition;
    invalidDefinition.maximumVerticalCourseAngleRadians = 0.0F;''', 'invalid endurance test')
text = replace_one(text,
'''    if (!constrainedTorpedo ||
        !UpdateConventionalTorpedoGuidance(constrainedDefinition, *constrainedTorpedo, elevatedTrack, 1.0) ||
        std::abs(constrainedTorpedo->headingRadians - 0.20F) > 0.001F ||
        constrainedTorpedo->positionMeters.y <= -100.0F)
    {
        return false;
    }

    return true;''',
'''    if (!constrainedTorpedo ||
        !UpdateConventionalTorpedoGuidance(constrainedDefinition, *constrainedTorpedo, elevatedTrack, 1.0) ||
        std::abs(constrainedTorpedo->headingRadians - 0.20F) > 0.001F ||
        constrainedTorpedo->positionMeters.y <= -100.0F)
    {
        return false;
    }

    // A missed weapon cannot pursue forever. Endurance expiry is a clean terminal miss state with no physical
    // impact body; values are authored gameplay policy rather than claimed real torpedo endurance.
    auto shortEnduranceDefinition = definition;
    shortEnduranceDefinition.maximumRunTimeSeconds = 1.0;
    auto shortWeaponResult = CreateWeaponRuntime(shortEnduranceDefinition.weapon, 0.0);
    if (!shortWeaponResult)
    {
        return false;
    }
    auto shortWeapon = *shortWeaponResult;
    if (!PrepareWeapon(shortEnduranceDefinition.weapon, shortWeapon, 0.0) ||
        !AssignWeaponTarget(shortEnduranceDefinition.weapon, shortWeapon, initialTrack, 0.0) ||
        !LaunchWeapon(shortEnduranceDefinition.weapon, shortWeapon, 0.0))
    {
        return false;
    }
    auto shortTorpedo = CreateLaunchedConventionalTorpedo(
        shortEnduranceDefinition, shortWeapon, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.0F, initialTrack, 0.0);
    if (!shortTorpedo || !UpdateConventionalTorpedoGuidance(
            shortEnduranceDefinition, *shortTorpedo, std::nullopt, 1.1) ||
        shortTorpedo->movementDomain != MovementDomain::Spent || shortTorpedo->speedMetersPerSecond != 0.0F ||
        shortTorpedo->terminalReason != ConventionalTorpedoTerminalReason::EnduranceExpired ||
        shortTorpedo->impactedBody.has_value())
    {
        return false;
    }

    return true;''', 'endurance regression test')
path.write_text(text, encoding='utf-8')
