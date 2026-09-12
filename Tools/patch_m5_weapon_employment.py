from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


runtime_path = Path("Game/Combat/CombatPlaygroundRuntime.h")
runtime = runtime_path.read_text(encoding="utf-8")

runtime = replace_once(
    runtime,
    '#include "Simulation/Weapons/TorpedoSeeker.h"\n',
    '#include "Simulation/Weapons/TorpedoSeeker.h"\n#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"\n',
    "employment include",
)

runtime = replace_once(
    runtime,
    'const auto automated = AdvanceAutomatedPlayerCommander(simulationTimeSeconds);',
    'const auto automated = AdvanceAutomatedPlayerCommander(playerSnapshot, simulationTimeSeconds);',
    "automated commander call",
)

runtime = replace_once(
    runtime,
    '[[nodiscard]] std::expected<void, std::string> AdvanceAutomatedPlayerCommander(\n        const double simulationTimeSeconds)',
    '[[nodiscard]] std::expected<void, std::string> AdvanceAutomatedPlayerCommander(\n        const Submarine::AnteyAcousticSnapshot& playerSnapshot,\n        const double simulationTimeSeconds)',
    "automated commander signature",
)

manual_old = '''                if (playerTorpedo_.has_value())
                {
                    continue; // Preserve J2's post-launch weapon-command behavior; J4 decoy remains available.
                }
                const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
'''
manual_new = '''                if (playerTorpedo_.has_value())
                {
                    continue; // Preserve J2's post-launch weapon-command behavior; J4 decoy remains available.
                }
                if (command.type == PlayerCombatCommandType::FireWeapon)
                {
                    const auto employment = AssessPlayerUset80Employment(playerSnapshot);
                    if (!employment)
                    {
                        return std::unexpected("M5 weapon employment assessment failed: " + employment.error());
                    }
                    if (!employment->allowed)
                    {
                        lastCombatCommand_ = PlayerCombatCommandFeedback{
                            .command = PlayerCombatCommandType::FireWeapon,
                            .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = "USET-80 launch blocked: " + employment->reason};
                        continue;
                    }
                }
                const auto executed = playerCombat_.Execute(command, playerTracks_.Tracks(), simulationTimeSeconds);
'''
runtime = replace_once(runtime, manual_old, manual_new, "manual fire gate")

auto_fire_old = '''        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
        {
            const auto fired = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::FireWeapon}, tracks, simulationTimeSeconds);
'''
auto_fire_new = '''        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
        {
            const auto employment = AssessPlayerUset80Employment(playerSnapshot);
            if (!employment)
            {
                return std::unexpected("M5-H automated weapon employment assessment failed: " + employment.error());
            }
            if (!employment->allowed)
            {
                return {}; // Valid perceived target, but the carrier/geometry is outside the launch envelope.
            }
            const auto fired = playerCombat_.Execute(
                {.type = PlayerCombatCommandType::FireWeapon}, tracks, simulationTimeSeconds);
'''
runtime = replace_once(runtime, auto_fire_old, auto_fire_new, "automated fire gate")

presentation_old = '''        PlayerCombatPresentationSnapshot playerCombatPresentation =
            playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot);
        ApplyIncomingThreatPresentation(playerCombatPresentation);
'''
presentation_new = '''        PlayerCombatPresentationSnapshot playerCombatPresentation =
            playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot);
        if (playerCombatPresentation.canFireWeapon)
        {
            const auto employment = AssessPlayerUset80Employment(playerSnapshot);
            playerCombatPresentation.canFireWeapon = employment.has_value() && employment->allowed;
        }
        ApplyIncomingThreatPresentation(playerCombatPresentation);
'''
runtime = replace_once(runtime, presentation_old, presentation_new, "presentation fire eligibility")

# Insert one shared perceived-world employment helper immediately before the automated commander helper.
helper_anchor = '''    [[nodiscard]] std::expected<void, std::string> AdvanceAutomatedPlayerCommander(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
'''
helper = '''    [[nodiscard]] std::expected<Weapons::WeaponEmploymentAssessment, std::string> AssessPlayerUset80Employment(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot) const
    {
        if (!currentPlayerPhysicalProxy_.has_value() || !currentPlayerPhysicalProxy_->orientation.IsFinite() ||
            !playerSnapshot.emitter.positionMeters.IsFinite() ||
            !playerSnapshot.emitter.velocityMetersPerSecond.IsFinite() || !std::isfinite(playerSnapshot.signedDepthMeters))
        {
            return std::unexpected("live ownship state is unavailable for weapon employment");
        }

        const auto selected = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
        if (!selected.has_value() || !selected->estimatedPositionMeters.has_value())
        {
            return Weapons::WeaponEmploymentAssessment{
                .allowed = false,
                .reason = "selected perceived track has no spatial estimate"};
        }

        const auto& orientation = currentPlayerPhysicalProxy_->orientation;
        const float launcherHeadingRadians = static_cast<float>(std::atan2(
            2.0 * (static_cast<double>(orientation.w) * orientation.z +
                   static_cast<double>(orientation.x) * orientation.y),
            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                         static_cast<double>(orientation.z) * orientation.z)));
        const auto& velocity = playerSnapshot.emitter.velocityMetersPerSecond;
        const float carrierSpeedMetersPerSecond = static_cast<float>(std::sqrt(
            static_cast<double>(velocity.x) * velocity.x +
            static_cast<double>(velocity.y) * velocity.y +
            static_cast<double>(velocity.z) * velocity.z));
        const float surfaceLevelMeters = playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
        const float perceivedTargetDepthMeters = (std::max)(
            0.0F, surfaceLevelMeters - selected->estimatedPositionMeters->y);

        return Weapons::EvaluateWeaponEmployment(
            Weapons::Uset80EmploymentEnvelope,
            Weapons::WeaponEmploymentContext{
                .launchPositionMeters = playerSnapshot.emitter.positionMeters,
                .perceivedTargetPositionMeters = *selected->estimatedPositionMeters,
                .launchDepthMeters = playerSnapshot.signedDepthMeters,
                .perceivedTargetDepthMeters = perceivedTargetDepthMeters,
                .carrierSpeedMetersPerSecond = carrierSpeedMetersPerSecond,
                .launcherHeadingRadians = launcherHeadingRadians});
    }

'''
if runtime.count(helper_anchor) != 1:
    raise RuntimeError(f"employment helper anchor: expected one match, found {runtime.count(helper_anchor)}")
runtime = runtime.replace(helper_anchor, helper + helper_anchor, 1)

runtime_path.write_text(runtime, encoding="utf-8")


test_path = Path("Tests/M5WeaponRuntimeTest.cpp")
test = test_path.read_text(encoding="utf-8")
test = replace_once(
    test,
    '#include "Tests/M5ConventionalTorpedoChecks.h"\n',
    '#include "Tests/M5ConventionalTorpedoChecks.h"\n#include "Tests/M5WeaponEmploymentChecks.h"\n',
    "employment test include",
)
test = replace_once(
    test,
    '''    Require(DeepRun::Tests::RunM5CombatImpactChecks(),
            "M5-D swept collision, impact, explosion and bounded combat damage checks must pass");

    std::cout << "M5 weapon/perception/torpedo/combat runtime checks passed\\n";
''',
    '''    Require(DeepRun::Tests::RunM5CombatImpactChecks(),
            "M5-D swept collision, impact, explosion and bounded combat damage checks must pass");
    Require(DeepRun::Tests::RunM5WeaponEmploymentChecks(),
            "weapon employment depth/range/sector/speed boundaries must pass");

    std::cout << "M5 weapon/perception/torpedo/combat/employment runtime checks passed\\n";
''',
    "employment test call",
)
test_path.write_text(test, encoding="utf-8")

print("M5 weapon employment runtime patch: PASS")
