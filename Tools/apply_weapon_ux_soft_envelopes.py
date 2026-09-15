#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def write(rel: str, text: str) -> None:
    (ROOT / rel).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, repl: str, label: str, flags: int = re.S) -> str:
    out, count = re.subn(pattern, repl, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, found {count}")
    return out


# -----------------------------------------------------------------------------
# Player commander: selection is always legal; selection starts preparation.
# A projectile already in flight owns its own runtime, so resolving that old
# projectile must not reset a newly selected/preparing weapon.
# -----------------------------------------------------------------------------
rel = "Game/Combat/PlayerCombatCommandRuntime.h"
text = read(rel)
old = '''    // Weapon Selector changes the actual qualification/preparation profile, not merely a UI label. Switching is
    // legal only from Stored so readiness/target state from one weapon type can never bleed into another type.
    [[nodiscard]] std::expected<void, std::string> ReconfigureStoredWeapon(
        Weapons::WeaponDefinition definition,
        const double simulationTimeSeconds)
    {
        if (const auto advanced = Advance(simulationTimeSeconds); !advanced)
        {
            return std::unexpected(advanced.error());
        }
        if (weapon_.phase != Weapons::WeaponPhase::Stored)
        {
            return std::unexpected("M5 weapon selection can only change while the current weapon is Stored");
        }
        auto replacement = Weapons::CreateWeaponRuntime(definition, simulationTimeSeconds);
        if (!replacement)
        {
            return std::unexpected("M5 weapon selection profile creation failed: " + replacement.error());
        }
        definition_ = std::move(definition);
        weapon_ = std::move(*replacement);
        return {};
    }
'''
new = '''    // Player weapon preparation is automatic. The player chooses the weapon; the crew/fire-control runtime
    // begins preparation immediately and readiness remains SimulationTime-authoritative.
    [[nodiscard]] std::expected<void, std::string> BeginAutomaticPreparation(const double simulationTimeSeconds)
    {
        if (const auto advanced = Advance(simulationTimeSeconds); !advanced)
        {
            return std::unexpected(advanced.error());
        }
        if (weapon_.phase != Weapons::WeaponPhase::Stored)
        {
            return {};
        }
        const auto prepared = Weapons::PrepareWeapon(definition_, weapon_, simulationTimeSeconds);
        if (!prepared)
        {
            return std::unexpected("automatic player weapon preparation failed: " + prepared.error());
        }
        return {};
    }

    // Weapon selection is an intent change, not a lock imposed by the previously selected weapon. Any pending
    // preparation/readiness state is discarded and the newly selected profile starts its own preparation timer.
    // An already materialized projectile is independent and continues its flight in CombatPlaygroundRuntime.
    [[nodiscard]] std::expected<void, std::string> ReconfigureStoredWeapon(
        Weapons::WeaponDefinition definition,
        const double simulationTimeSeconds)
    {
        if (const auto advanced = Advance(simulationTimeSeconds); !advanced)
        {
            return std::unexpected(advanced.error());
        }
        auto replacement = Weapons::CreateWeaponRuntime(definition, simulationTimeSeconds);
        if (!replacement)
        {
            return std::unexpected("M5 weapon selection profile creation failed: " + replacement.error());
        }
        definition_ = std::move(definition);
        weapon_ = std::move(*replacement);
        return BeginAutomaticPreparation(simulationTimeSeconds);
    }
'''
text = replace_once(text, old, new, "commander selector/autoprep")
old = '''    [[nodiscard]] std::expected<void, std::string> CompleteResolvedLaunch(const double simulationTimeSeconds)
    {
        return Weapons::ResetWeaponAfterResolvedLaunch(definition_, weapon_, simulationTimeSeconds);
    }
'''
new = '''    [[nodiscard]] std::expected<void, std::string> CompleteResolvedLaunch(const double simulationTimeSeconds)
    {
        // The player may have switched weapons while this projectile was in flight. In that case the selected
        // weapon runtime is already Preparing/Ready and must not be reset by resolution of the old projectile.
        if (weapon_.phase != Weapons::WeaponPhase::Launched)
        {
            return {};
        }
        const auto reset = Weapons::ResetWeaponAfterResolvedLaunch(definition_, weapon_, simulationTimeSeconds);
        if (!reset)
        {
            return reset;
        }
        return BeginAutomaticPreparation(simulationTimeSeconds);
    }
'''
text = replace_once(text, old, new, "commander rearm/autoprep")
write(rel, text)


# -----------------------------------------------------------------------------
# Employment envelope: target range is advisory/optimal, not a trigger lock.
# Physical launch constraints remain hard. P-700 is submerged-only in Deep Run.
# -----------------------------------------------------------------------------
rel = "Simulation/Weapons/WeaponEmploymentEnvelope.h"
text = read(rel)
text = replace_once(
    text,
    '''struct WeaponEmploymentAssessment final
{
    bool allowed = false;
    float targetRangeMeters = 0.0F;
''',
    '''struct WeaponEmploymentAssessment final
{
    bool allowed = false;
    // Range limits describe the weapon's effective/nominal employment band. A player may deliberately fire
    // outside it; rangeOptimal lets gameplay/UI model the resulting risk without turning the band into a trigger lock.
    bool rangeOptimal = true;
    float targetRangeMeters = 0.0F;
''',
    "employment assessment rangeOptimal")
text = replace_once(
    text,
    '''// P-700/Project 949A public descriptions allow launch from the surface as well as submerged launch, with
// underwater depth commonly described around 30-50 m and a 50 m maximum. Deep Run therefore treats 0..50 m
// as one carrier-depth envelope; sea state/hatch/water-exit constraints belong to the future IG2 launcher
// lifecycle. A 20 km minimum is reported by a secondary technical compilation. The +/-90 degree horizontal
// target sector and shallow surface-target band are Game policy.
inline constexpr WeaponEmploymentEnvelope P700GranitEmploymentEnvelope{
    .id = "P-700-Granit",
    .minimumLaunchDepthMeters = 0.0F,
''',
    '''// Deep Run gameplay contract: P-700 is launched only while the Antey is submerged. Public descriptions put
// submerged launch in the roughly 30-50 m region; the 10 m lower bound is conservative GAME POLICY that prevents
// a visually surfaced boat from launching while preserving the 50 m maximum. The 20 km minimum is retained as an
// optimal/effective-range threshold, not a trigger lock. The +/-90 degree sector and shallow target band are policy.
inline constexpr WeaponEmploymentEnvelope P700GranitEmploymentEnvelope{
    .id = "P-700-Granit",
    .minimumLaunchDepthMeters = 10.0F,
''',
    "P700 submerged-only envelope")
old = '''    if (context.launchDepthMeters < envelope.minimumLaunchDepthMeters)
    {
        return rejected("launch depth is shallower than the weapon envelope");
    }
    if (context.launchDepthMeters > envelope.maximumLaunchDepthMeters)
    {
        return rejected("launch depth exceeds the weapon envelope");
    }
    if (rangeMeters < envelope.minimumTargetRangeMeters)
    {
        return rejected("target is inside the weapon minimum range");
    }
    if (rangeMeters > envelope.maximumTargetRangeMeters)
    {
        return rejected("target is beyond the weapon maximum range");
    }
    if (offBoresightRadians > envelope.maximumOffBoresightRadians)
'''
new = '''    if (context.launchDepthMeters < envelope.minimumLaunchDepthMeters)
    {
        return rejected("launch depth is shallower than the weapon envelope");
    }
    if (context.launchDepthMeters > envelope.maximumLaunchDepthMeters)
    {
        return rejected("launch depth exceeds the weapon envelope");
    }

    // Range is deliberately evaluated as effectiveness, not launch authority. Keep checking the true physical
    // constraints below so a soft range warning can never bypass depth/sector/speed/target-domain restrictions.
    const bool insideOptimalRange = rangeMeters >= envelope.minimumTargetRangeMeters &&
                                    rangeMeters <= envelope.maximumTargetRangeMeters;
    std::string rangeReason{};
    if (rangeMeters < envelope.minimumTargetRangeMeters)
    {
        rangeReason = "target is inside the optimal weapon range; launch allowed with reduced effectiveness";
    }
    else if (rangeMeters > envelope.maximumTargetRangeMeters)
    {
        rangeReason = "target is beyond nominal weapon endurance; launch allowed but intercept is not guaranteed";
    }

    if (offBoresightRadians > envelope.maximumOffBoresightRadians)
'''
text = replace_once(text, old, new, "soft range evaluation")
old = '''    return {.allowed = true,
            .targetRangeMeters = rangeMeters,
            .offBoresightRadians = offBoresightRadians,
            .reason = "weapon employment envelope satisfied"};
'''
new = '''    return {.allowed = true,
            .rangeOptimal = insideOptimalRange,
            .targetRangeMeters = rangeMeters,
            .offBoresightRadians = offBoresightRadians,
            .reason = insideOptimalRange ? "weapon employment envelope satisfied" : std::move(rangeReason)};
'''
text = replace_once(text, old, new, "soft range success result")
text = replace_once(
    text,
    "static_assert(P700GranitEmploymentEnvelope.minimumLaunchDepthMeters == 0.0F);",
    "static_assert(P700GranitEmploymentEnvelope.minimumLaunchDepthMeters == 10.0F);",
    "P700 minimum-depth assert")
write(rel, text)


# -----------------------------------------------------------------------------
# Combat runtime: initial/selected weapons auto-prepare, switching stays legal,
# and short-range P-700 shots face a stronger ship-defense opportunity.
# -----------------------------------------------------------------------------
rel = "Game/Combat/CombatPlaygroundRuntime.h"
text = read(rel)
old = '''        auto playerCombat = PlayerCombatCommandRuntime::Create(
            playerTorpedoDefinition.weapon, simulationTimeSeconds);
        if (!playerCombat)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5-H player commander runtime creation failed: " + playerCombat.error());
        }
        const Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition{
'''
new = '''        auto playerCombat = PlayerCombatCommandRuntime::Create(
            playerTorpedoDefinition.weapon, simulationTimeSeconds);
        if (!playerCombat)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("M5-H player commander runtime creation failed: " + playerCombat.error());
        }
        const auto initialPreparation = playerCombat->BeginAutomaticPreparation(simulationTimeSeconds);
        if (!initialPreparation)
        {
            (void)physicsWorld.DestroyBody(destroyer->body);
            return std::unexpected("player automatic weapon preparation failed: " + initialPreparation.error());
        }
        const Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition{
'''
text = replace_once(text, old, new, "initial automatic preparation")
old = '''        if (playerCombat_.Weapon().phase != Weapons::WeaponPhase::Stored || playerTorpedo_ || playerP700_)
        {
            return PlayerCombatCommandFeedback{
                .command = command.type,
                .accepted = false,
                .trackId = playerCombat_.SelectedTrackId(),
                .message = "weapon selection is available only while the current weapon is Stored"};
        }
'''
text = replace_once(text, old, "", "remove selector Stored/projectile lock")
text = replace_once(
    text,
    '''            .trackId = playerCombat_.SelectedTrackId(),
            .message = "selected " + std::string(Armament::PlayerWeaponName(selectedPlayerWeapon_))};
''',
    '''            .trackId = playerCombat_.SelectedTrackId(),
            .message = "selected " + std::string(Armament::PlayerWeaponName(selectedPlayerWeapon_)) +
                       "; automatic preparation started"};
''',
    "selector feedback")
old = '''            const std::optional<Weapons::P700TerminalDefenseProfile> targetDefense =
                p700AcceptanceMode_ || !actualTargetHasTerminalDefense
                    ? std::nullopt
                    : std::optional<Weapons::P700TerminalDefenseProfile>{Weapons::P700TerminalDefenseProfile{}};
'''
new = '''            std::optional<Weapons::P700TerminalDefenseProfile> targetDefense{};
            if (!p700AcceptanceMode_ && actualTargetHasTerminalDefense)
            {
                Weapons::P700TerminalDefenseProfile defense{};
                if (playerP700LaunchRangeMeters_ &&
                    *playerP700LaunchRangeMeters_ < Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters)
                {
                    // GAME POLICY: a deliberately too-close Granit shot gets less time/distance to establish its
                    // preferred flight profile, giving a defended combatant a better terminal interception window.
                    const float minimumRange = Weapons::P700GranitEmploymentEnvelope.minimumTargetRangeMeters;
                    const float shortfall = std::clamp(
                        (minimumRange - *playerP700LaunchRangeMeters_) / minimumRange, 0.0F, 1.0F);
                    defense.hardKillProbability = std::clamp(
                        defense.hardKillProbability + 0.30F * shortfall, 0.0F, 1.0F);
                    defense.maneuverDefeatProbability = std::clamp(
                        defense.maneuverDefeatProbability + 0.10F * shortfall, 0.0F, 1.0F);
                    defense.seekerFailureProbability = std::clamp(
                        defense.seekerFailureProbability + 0.05F * shortfall, 0.0F, 1.0F);
                }
                targetDefense = defense;
            }
'''
text = replace_once(text, old, new, "P700 short-range defense penalty")
old = '''        playerP700LaunchSlotIndex_ = committed->launcherSlotIndices.front();
        playerP700LaunchSlotIndices_ = committed->launcherSlotIndices;
        playerP700_ = std::move(committed->runtime.missiles.front());
'''
new = '''        playerP700LaunchSlotIndex_ = committed->launcherSlotIndices.front();
        playerP700LaunchSlotIndices_ = committed->launcherSlotIndices;
        if (!targetTrack.estimatedPositionMeters)
            return std::unexpected("D2 P-700 launch lost its perceived spatial estimate");
        playerP700LaunchRangeMeters_ = Weapons::P700DistanceMeters(
            committed->runtime.missiles.front().positionMeters, *targetTrack.estimatedPositionMeters);
        playerP700_ = std::move(committed->runtime.missiles.front());
'''
text = replace_once(text, old, new, "capture P700 launch range")
old = '''                playerP700_.reset();
                playerP700Wingmen_.clear();
                playerP700LaunchSlotIndex_.reset();
                playerP700LaunchSlotIndices_.clear();
'''
new = '''                playerP700_.reset();
                playerP700Wingmen_.clear();
                playerP700LaunchRangeMeters_.reset();
                playerP700LaunchSlotIndex_.reset();
                playerP700LaunchSlotIndices_.clear();
'''
text = replace_once(text, old, new, "reset P700 launch range")
old = '''    std::optional<Armament::P700LauncherInventory> p700LauncherInventory_{};
    std::optional<Weapons::P700GranitRuntimeState> playerP700_{};
    std::vector<Weapons::P700GranitRuntimeState> playerP700Wingmen_{};
'''
new = '''    std::optional<Armament::P700LauncherInventory> p700LauncherInventory_{};
    std::optional<Weapons::P700GranitRuntimeState> playerP700_{};
    std::vector<Weapons::P700GranitRuntimeState> playerP700Wingmen_{};
    std::optional<float> playerP700LaunchRangeMeters_{};
'''
text = replace_once(text, old, new, "P700 launch-range member")
write(rel, text)


# -----------------------------------------------------------------------------
# HUD: no manual Prepare affordance; range bands are explicit soft warnings.
# -----------------------------------------------------------------------------
rel = "Game/Combat/CombatCommandUi.cpp"
text = read(rel)
old = '''            else if (*selectedRangeMeters < envelope->minimumTargetRangeMeters)
            {
                ImGui::Text("Engagement range: %.1f km - INSIDE %.1f km MIN",
                            *selectedRangeMeters / 1000.0F,
                            envelope->minimumTargetRangeMeters / 1000.0F);
            }
            else if (*selectedRangeMeters > envelope->maximumTargetRangeMeters)
            {
                ImGui::Text("Engagement range: %.1f km - OUTSIDE %.1f km MAX",
                            *selectedRangeMeters / 1000.0F,
                            envelope->maximumTargetRangeMeters / 1000.0F);
            }
'''
new = '''            else if (*selectedRangeMeters < envelope->minimumTargetRangeMeters)
            {
                ImGui::TextColored(
                    ImVec4(1.0F, 0.72F, 0.20F, 1.0F),
                    "Engagement range: %.1f km - NON-OPTIMAL / TOO CLOSE (%.1f km)",
                    *selectedRangeMeters / 1000.0F,
                    envelope->minimumTargetRangeMeters / 1000.0F);
                if (snapshot.selectedWeapon == Armament::PlayerWeaponType::P700Granit)
                    ImGui::TextWrapped("FIRE ALLOWED: short flight profile gives ship defenses a better intercept opportunity.");
                else
                    ImGui::TextWrapped("FIRE ALLOWED: compressed straight-run/seeker geometry increases acquisition or overshoot risk.");
            }
            else if (*selectedRangeMeters > envelope->maximumTargetRangeMeters)
            {
                ImGui::TextColored(
                    ImVec4(1.0F, 0.72F, 0.20F, 1.0F),
                    "Engagement range: %.1f km - NON-OPTIMAL / BEYOND %.1f km NOMINAL",
                    *selectedRangeMeters / 1000.0F,
                    envelope->maximumTargetRangeMeters / 1000.0F);
                ImGui::TextWrapped("FIRE ALLOWED: weapon may exhaust its travel/endurance budget before intercept and be lost.");
            }
'''
text = replace_once(text, old, new, "HUD soft range warnings")
text = replace_once(text, '    ImGui::Text("Prepare available: %s", snapshot.canPrepareWeapon ? "YES" : "NO");\n', "", "remove prepare availability HUD")
text = replace_once(text, '    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");\n', "", "remove prepare control hint")
write(rel, text)


# -----------------------------------------------------------------------------
# Input: the player no longer has a Prepare Weapon command. Keep the enum/runtime
# path for internal automation/tests, but remove the gameplay binding/sequence.
# -----------------------------------------------------------------------------
rel = "DeepRun/Main.cpp"
text = read(rel)
text = replace_once(text, "        std::uint64_t consumedPrepareWeaponSequence = 0;\n", "", "remove prepare sequence variable")
text = replace_once(
    text,
    "             &consumedPrepareWeaponSequence, &consumedFireWeaponSequence,\n",
    "             &consumedFireWeaponSequence,\n",
    "remove prepare lambda capture")
text = regex_once(
    text,
    r'''\s*consume\(\*inputState, DeepRun::Input::InputAction::PrepareWeapon,\n\s*DeepRun::Game::Combat::PlayerCombatCommandType::PrepareWeapon,\n\s*consumedPrepareWeaponSequence\);''',
    "",
    "remove prepare input consumption",
    flags=0)
write(rel, text)


# -----------------------------------------------------------------------------
# P-700 contract tests: target range is soft, launch depth is submerged-only.
# -----------------------------------------------------------------------------
rel = "Tests/M5P700Checks.h"
text = read(rel)
old = '''    const auto tooFarLaunch = LaunchP700Granit(
        definition, *tooFarRuntime, tooFarTrack, SubmergedCarrier(), 0.0);
    if (!tooFarLaunch || tooFarLaunch->allowed ||
        tooFarLaunch->reason.find("maximum range") == std::string::npos ||
        tooFarRuntime->phase != P700GranitPhase::Stored)
    {
        return fail("550 km maximum-range employment gate");
    }
'''
new = '''    const auto tooFarLaunch = LaunchP700Granit(
        definition, *tooFarRuntime, tooFarTrack, SubmergedCarrier(), 0.0);
    if (!tooFarLaunch || !tooFarLaunch->allowed || tooFarLaunch->rangeOptimal ||
        tooFarLaunch->reason.find("nominal weapon endurance") == std::string::npos ||
        tooFarRuntime->phase != P700GranitPhase::HatchOpening)
    {
        return fail("beyond-550-km launch must be allowed but flagged non-optimal");
    }
'''
text = replace_once(text, old, new, "P700 too-far soft test")
old = '''    const auto tooCloseLaunch = LaunchP700Granit(
        definition, *tooCloseRuntime, tooCloseTrack, SubmergedCarrier(), 0.0);
    if (!tooCloseLaunch || tooCloseLaunch->allowed ||
        tooCloseLaunch->reason.find("minimum range") == std::string::npos ||
        tooCloseRuntime->phase != P700GranitPhase::Stored)
    {
        return fail("20 km minimum-range employment gate");
    }
'''
new = '''    const auto tooCloseLaunch = LaunchP700Granit(
        definition, *tooCloseRuntime, tooCloseTrack, SubmergedCarrier(), 0.0);
    if (!tooCloseLaunch || !tooCloseLaunch->allowed || tooCloseLaunch->rangeOptimal ||
        tooCloseLaunch->reason.find("optimal weapon range") == std::string::npos ||
        tooCloseRuntime->phase != P700GranitPhase::HatchOpening)
    {
        return fail("inside-20-km launch must be allowed but flagged non-optimal");
    }
'''
text = replace_once(text, old, new, "P700 too-close soft test")
old = '''    const auto surfaceLaunch = LaunchP700Granit(
        definition, *surfaceRuntime, targetTrack, surfaceCarrier, 0.0);
    if (!surfaceLaunch || !surfaceLaunch->allowed || surfaceRuntime->phase != P700GranitPhase::HatchOpening ||
        surfaceRuntime->deploymentProgress != 0.0F || surfaceRuntime->hatchOpenProgress != 0.0F)
    {
        return fail("surface launch must still open the selected launcher hatch before booster ignition");
    }
'''
new = '''    const auto surfaceLaunch = LaunchP700Granit(
        definition, *surfaceRuntime, targetTrack, surfaceCarrier, 0.0);
    if (!surfaceLaunch || surfaceLaunch->allowed ||
        surfaceLaunch->reason.find("shallower") == std::string::npos ||
        surfaceRuntime->phase != P700GranitPhase::Stored)
    {
        return fail("P-700 surface launch must be rejected by the submerged-only carrier contract");
    }
'''
text = replace_once(text, old, new, "P700 surface-launch hard test")
write(rel, text)

print("weapon UX soft-envelope patch applied")
