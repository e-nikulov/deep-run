from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


p = Path("Simulation/Weapons/P700Granit.h")
s = p.read_text(encoding="utf-8")

s = replace_once(s, '''enum class P700GranitPhase
{
    Stored,
    UnderwaterLaunch,
    WaterExit,
    AirborneDeploying,
    Cruise,
    Terminal,
    Impact,
    Spent,
};''', '''enum class P700GranitPhase
{
    Stored,
    HatchOpening,
    UnderwaterLaunch,
    WaterExit,
    PostExitTransition,
    AirborneDeploying,
    Cruise,
    Terminal,
    Defeated,
    Impact,
    Spent,
};

enum class P700TerminalEngagementOutcome
{
    Unresolved,
    HitPath,
    SeekerLost,
    SoftKill,
    HardKill,
    ManeuverMiss,
};

// Explicit GAME POLICY for terminal effectiveness. These probabilities model independent terminal failure
// opportunities after a legal Track-based launch; they are not claims about classified P-700 seeker/EW/PВО data.
struct P700TerminalDefenseProfile final
{
    float seekerFailureProbability = 0.04F;
    float softKillProbability = 0.10F;
    float hardKillProbability = 0.16F;
    float maneuverDefeatProbability = 0.04F;
};''', "phase/defense enums")

s = replace_once(s, '''    float underwaterExitSpeedMetersPerSecond = 50.0F;
    float waterExitSpeedMetersPerSecond = 100.0F;
    float deploymentFlightSpeedMetersPerSecond = 180.0F;
    float cruiseSpeedMetersPerSecond = 500.0F;
    float terminalSpeedMetersPerSecond = 500.0F;
    float maximumAirborneTurnRateRadiansPerSecond = 0.35F;
    double waterExitTransitionSeconds = 0.50;
    double deploymentSeconds = 1.50;''', '''    float underwaterExitSpeedMetersPerSecond = 50.0F;
    float waterExitSpeedMetersPerSecond = 100.0F;
    float deploymentFlightSpeedMetersPerSecond = 180.0F;
    // M5 gameplay flight policy: ~Mach 2 class cruise/terminal pacing without claiming a historical exact profile.
    float cruiseSpeedMetersPerSecond = 680.0F;
    float terminalSpeedMetersPerSecond = 750.0F;
    float maximumAirborneTurnRateRadiansPerSecond = 0.35F;
    double launcherHatchOpeningSeconds = 0.75;
    double waterExitTransitionSeconds = 0.50;
    double postExitTransitionSeconds = 0.60;
    double deploymentSeconds = 1.50;''', "definition timing/speed")

s = replace_once(s, '''    float speedMetersPerSecond = 0.0F;
    float deploymentProgress = 0.0F;
    double phaseStartTimeSeconds = 0.0;''', '''    float speedMetersPerSecond = 0.0F;
    float hatchOpenProgress = 0.0F;
    float postExitTransitionProgress = 0.0F;
    float deploymentProgress = 0.0F;
    bool launchBoosterActive = false;
    bool launchBoosterAttached = true;
    bool noseProtectionCapAttached = true;
    bool mainEngineActive = false;
    P700TerminalEngagementOutcome terminalOutcome = P700TerminalEngagementOutcome::Unresolved;
    std::uint64_t terminalRandomSeed = 0U;
    double phaseStartTimeSeconds = 0.0;''', "runtime presentation state")

s = replace_once(s, '''        !std::isfinite(definition.maximumAirborneTurnRateRadiansPerSecond) ||
        definition.maximumAirborneTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumAirborneTurnRateRadiansPerSecond > 3.14159265358979323846F ||
        !std::isfinite(definition.waterExitTransitionSeconds) || definition.waterExitTransitionSeconds <= 0.0 ||
        !std::isfinite(definition.deploymentSeconds) || definition.deploymentSeconds <= 0.0 ||''', '''        !std::isfinite(definition.maximumAirborneTurnRateRadiansPerSecond) ||
        definition.maximumAirborneTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumAirborneTurnRateRadiansPerSecond > 3.14159265358979323846F ||
        !std::isfinite(definition.launcherHatchOpeningSeconds) || definition.launcherHatchOpeningSeconds <= 0.0 ||
        !std::isfinite(definition.waterExitTransitionSeconds) || definition.waterExitTransitionSeconds <= 0.0 ||
        !std::isfinite(definition.postExitTransitionSeconds) || definition.postExitTransitionSeconds <= 0.0 ||
        !std::isfinite(definition.deploymentSeconds) || definition.deploymentSeconds <= 0.0 ||''', "definition validation timings")

insert_after = '''[[nodiscard]] inline Physics::PhysicsQuaternion P700HeadingQuaternion(const float headingRadians) noexcept
{
    const float half = 0.5F * headingRadians;
    return Physics::PhysicsQuaternion{
        .x = 0.0F,
        .y = 0.0F,
        .z = static_cast<float>(std::sin(static_cast<double>(half))),
        .w = static_cast<float>(std::cos(static_cast<double>(half)))};
}
'''
addition = '''
[[nodiscard]] inline bool ValidateP700TerminalDefenseProfile(const P700TerminalDefenseProfile& profile) noexcept
{
    const auto validProbability = [](const float value) noexcept {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    return validProbability(profile.seekerFailureProbability) && validProbability(profile.softKillProbability) &&
           validProbability(profile.hardKillProbability) && validProbability(profile.maneuverDefeatProbability);
}

[[nodiscard]] inline std::uint64_t P700SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] inline float P700UnitRandom(const std::uint64_t seed, const std::uint64_t stream) noexcept
{
    const std::uint64_t bits = P700SplitMix64(seed ^ (stream * 0xD1B54A32D192ED03ULL));
    return static_cast<float>((bits >> 40U) * (1.0 / 16777216.0));
}

[[nodiscard]] inline P700TerminalEngagementOutcome ResolveP700TerminalEngagement(
    const P700TerminalDefenseProfile& defense,
    const std::uint64_t seed,
    const float perceivedPositionUncertaintyMeters) noexcept
{
    if (!ValidateP700TerminalDefenseProfile(defense) || !std::isfinite(perceivedPositionUncertaintyMeters) ||
        perceivedPositionUncertaintyMeters < 0.0F)
    {
        return P700TerminalEngagementOutcome::SeekerLost;
    }
    // Uncertainty is already bounded by WeaponTargetingRequirements at launch/update. It contributes up to
    // another 20 percentage points of terminal seeker failure rather than becoming a hidden distance shortcut.
    const float uncertaintyPenalty = std::clamp(perceivedPositionUncertaintyMeters / 2'500.0F, 0.0F, 0.20F);
    if (P700UnitRandom(seed, 1U) < std::clamp(defense.seekerFailureProbability + uncertaintyPenalty, 0.0F, 1.0F))
        return P700TerminalEngagementOutcome::SeekerLost;
    if (P700UnitRandom(seed, 2U) < defense.softKillProbability)
        return P700TerminalEngagementOutcome::SoftKill;
    if (P700UnitRandom(seed, 3U) < defense.hardKillProbability)
        return P700TerminalEngagementOutcome::HardKill;
    if (P700UnitRandom(seed, 4U) < defense.maneuverDefeatProbability)
        return P700TerminalEngagementOutcome::ManeuverMiss;
    return P700TerminalEngagementOutcome::HitPath;
}
'''
s = replace_once(s, insert_after, insert_after + addition, "terminal helpers")

old_launch = '''    state.phase = carrier.launchDepthMeters > 0.05F
        ? P700GranitPhase::UnderwaterLaunch
        : P700GranitPhase::WaterExit;
    state.positionMeters = carrier.launchPositionMeters;
    state.launchForwardUnitVector = *launchForward;
    state.surfaceLevelYMeters = carrier.surfaceLevelYMeters;
    state.headingRadians = static_cast<float>(std::atan2(
        static_cast<double>(launchForward->y), static_cast<double>(launchForward->x)));
    state.speedMetersPerSecond = state.phase == P700GranitPhase::UnderwaterLaunch
        ? definition.underwaterExitSpeedMetersPerSecond
        : definition.waterExitSpeedMetersPerSecond;
    state.deploymentProgress = 0.0F;
    state.phaseStartTimeSeconds = simulationTimeSeconds;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.guidanceTrackId = targetTrack.trackId;
    state.perceivedAimPointMeters = targetTrack.estimatedPositionMeters;
    state.perceivedPositionUncertaintyMeters = targetTrack.positionUncertaintyMeters;
    return employment;'''
new_launch = '''    // Fire commits the weapon to the launch sequence, but the missile remains physically in its canister until
    // the selected paired hatch is fully open. Presentation consumes hatchOpenProgress from this same state.
    state.phase = P700GranitPhase::HatchOpening;
    state.positionMeters = carrier.launchPositionMeters;
    state.launchForwardUnitVector = *launchForward;
    state.surfaceLevelYMeters = carrier.surfaceLevelYMeters;
    state.headingRadians = static_cast<float>(std::atan2(
        static_cast<double>(launchForward->y), static_cast<double>(launchForward->x)));
    state.speedMetersPerSecond = 0.0F;
    state.hatchOpenProgress = 0.0F;
    state.postExitTransitionProgress = 0.0F;
    state.deploymentProgress = 0.0F;
    state.launchBoosterActive = false;
    state.launchBoosterAttached = true;
    state.noseProtectionCapAttached = true;
    state.mainEngineActive = false;
    state.terminalOutcome = P700TerminalEngagementOutcome::Unresolved;
    state.terminalRandomSeed = P700SplitMix64(targetTrack.trackId ^ 0x503730304752414EULL);
    state.phaseStartTimeSeconds = simulationTimeSeconds;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.guidanceTrackId = targetTrack.trackId;
    state.perceivedAimPointMeters = targetTrack.estimatedPositionMeters;
    state.perceivedPositionUncertaintyMeters = targetTrack.positionUncertaintyMeters;
    return employment;'''
s = replace_once(s, old_launch, new_launch, "launch sequence start")

s = replace_once(s, '''    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredCarrierBody = {})''', '''    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredCarrierBody = {},
    const std::optional<P700TerminalDefenseProfile>& targetDefense = std::nullopt)''', "advance signature")

s = replace_once(s, '''        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||
        !std::isfinite(state.deploymentProgress) || state.deploymentProgress < 0.0F || state.deploymentProgress > 1.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)''', '''        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||
        !std::isfinite(state.hatchOpenProgress) || state.hatchOpenProgress < 0.0F || state.hatchOpenProgress > 1.0F ||
        !std::isfinite(state.postExitTransitionProgress) || state.postExitTransitionProgress < 0.0F ||
        state.postExitTransitionProgress > 1.0F || !std::isfinite(state.deploymentProgress) ||
        state.deploymentProgress < 0.0F || state.deploymentProgress > 1.0F ||
        (targetDefense.has_value() && !ValidateP700TerminalDefenseProfile(*targetDefense)) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)''', "advance validation")

s = replace_once(s, '''    if (state.phase == P700GranitPhase::Impact)
    {
        state.phase = P700GranitPhase::Spent;''', '''    if (state.phase == P700GranitPhase::Impact || state.phase == P700GranitPhase::Defeated)
    {
        state.phase = P700GranitPhase::Spent;''', "impact/defeated consume")

needle = '''        if (state.phase == P700GranitPhase::UnderwaterLaunch)
        {'''
block = '''        if (state.phase == P700GranitPhase::HatchOpening)
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

        if (state.phase == P700GranitPhase::UnderwaterLaunch)
        {'''
s = replace_once(s, needle, block, "hatch opening phase")

s = replace_once(s, '''            if (state.deploymentProgress != 0.0F || state.launchForwardUnitVector.y <= 0.0F)
            {
                return std::unexpected("P-700 underwater launch/deployment contract is invalid");
            }''', '''            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||
                state.postExitTransitionProgress != 0.0F || state.launchForwardUnitVector.y <= 0.0F ||
                !state.launchBoosterActive || !state.launchBoosterAttached || !state.noseProtectionCapAttached ||
                state.mainEngineActive)
            {
                return std::unexpected("P-700 underwater booster launch/deployment contract is invalid");
            }''', "underwater component contract")

s = replace_once(s, '''            if (state.deploymentProgress != 0.0F)
            {
                return std::unexpected("P-700 must remain stowed through WaterExit");
            }''', '''            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F ||
                state.postExitTransitionProgress != 0.0F || !state.launchBoosterActive ||
                !state.launchBoosterAttached || !state.noseProtectionCapAttached || state.mainEngineActive)
            {
                return std::unexpected("P-700 must remain booster-driven and stowed through WaterExit");
            }''', "water exit component contract")

s = replace_once(s, '''                state.phase = P700GranitPhase::AirborneDeploying;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = definition.deploymentFlightSpeedMetersPerSecond;
                continue;''', '''                state.phase = P700GranitPhase::PostExitTransition;
                state.phaseStartTimeSeconds = cursorTime;
                state.postExitTransitionProgress = 0.0F;
                state.speedMetersPerSecond = definition.deploymentFlightSpeedMetersPerSecond;
                continue;''', "water exit to separation")

needle = '''        if (state.phase == P700GranitPhase::AirborneDeploying)
        {'''
block = '''        if (state.phase == P700GranitPhase::PostExitTransition)
        {
            if (state.deploymentProgress != 0.0F || state.hatchOpenProgress != 1.0F)
            {
                return std::unexpected("P-700 post-exit transition must precede aerodynamic deployment");
            }
            const double elapsed = cursorTime - state.phaseStartTimeSeconds;
            const double phaseRemaining = std::max(0.0, definition.postExitTransitionSeconds - elapsed);
            const double stepSeconds = std::min(remainingSeconds, phaseRemaining);
            if (stepSeconds > 0.0)
            {
                const auto impact = moveSegment(
                    state.launchForwardUnitVector, definition.deploymentFlightSpeedMetersPerSecond, stepSeconds);
                if (!impact) return std::unexpected(impact.error());
                if (*impact) return *impact;
                cursorTime += stepSeconds;
                remainingSeconds -= stepSeconds;
                state.lastUpdateTimeSeconds = cursorTime;
            }
            state.postExitTransitionProgress = std::clamp(
                static_cast<float>((cursorTime - state.phaseStartTimeSeconds) / definition.postExitTransitionSeconds),
                0.0F, 1.0F);
            // The protective nose cap clears first; the launch booster separates next; only then does the
            // main engine own thrust. Fractions are explicit visual/gameplay timing policy.
            if (state.postExitTransitionProgress >= 0.25F)
                state.noseProtectionCapAttached = false;
            if (state.postExitTransitionProgress >= 0.50F)
            {
                state.launchBoosterAttached = false;
                state.launchBoosterActive = false;
                state.mainEngineActive = true;
            }
            if (phaseRemaining <= stepSeconds + 1.0e-9)
            {
                state.postExitTransitionProgress = 1.0F;
                state.noseProtectionCapAttached = false;
                state.launchBoosterAttached = false;
                state.launchBoosterActive = false;
                state.mainEngineActive = true;
                state.phase = P700GranitPhase::AirborneDeploying;
                state.phaseStartTimeSeconds = cursorTime;
                state.speedMetersPerSecond = definition.deploymentFlightSpeedMetersPerSecond;
                continue;
            }
            break;
        }

        if (state.phase == P700GranitPhase::AirborneDeploying)
        {'''
s = replace_once(s, needle, block, "post-exit separation phase")

s = replace_once(s, '''        if (state.phase == P700GranitPhase::AirborneDeploying)
        {
            const double elapsed''', '''        if (state.phase == P700GranitPhase::AirborneDeploying)
        {
            if (!state.mainEngineActive || state.launchBoosterActive || state.launchBoosterAttached ||
                state.noseProtectionCapAttached || state.postExitTransitionProgress != 1.0F)
            {
                return std::unexpected("P-700 aerodynamic deployment requires completed launch-hardware separation and main-engine ignition");
            }
            const double elapsed''', "deploy component contract")

s = replace_once(s, '''        if (state.phase != P700GranitPhase::Cruise && state.phase != P700GranitPhase::Terminal)
        {
            return std::unexpected("P-700 lifecycle reached an unsupported active phase");
        }
        if (!state.perceivedAimPointMeters || !state.perceivedAimPointMeters->IsFinite() || state.deploymentProgress != 1.0F)''', '''        if (state.phase != P700GranitPhase::Cruise && state.phase != P700GranitPhase::Terminal)
        {
            return std::unexpected("P-700 lifecycle reached an unsupported active phase");
        }
        if (!state.mainEngineActive || state.launchBoosterActive || state.launchBoosterAttached ||
            state.noseProtectionCapAttached || !state.perceivedAimPointMeters ||
            !state.perceivedAimPointMeters->IsFinite() || state.deploymentProgress != 1.0F)''', "airborne component contract")

s = replace_once(s, '''        if (state.phase == P700GranitPhase::Cruise && targetDistance <= definition.terminalRangeMeters)
        {
            state.phase = P700GranitPhase::Terminal;
            state.phaseStartTimeSeconds = cursorTime;
        }
''', '''        if (state.phase == P700GranitPhase::Cruise && targetDistance <= definition.terminalRangeMeters)
        {
            state.phase = P700GranitPhase::Terminal;
            state.phaseStartTimeSeconds = cursorTime;
            if (state.terminalOutcome == P700TerminalEngagementOutcome::Unresolved)
            {
                state.terminalOutcome = targetDefense.has_value()
                    ? ResolveP700TerminalEngagement(
                          *targetDefense,
                          state.terminalRandomSeed,
                          state.perceivedPositionUncertaintyMeters.value_or(500.0F))
                    : P700TerminalEngagementOutcome::HitPath;
            }
            if (state.terminalOutcome != P700TerminalEngagementOutcome::HitPath)
            {
                state.phase = P700GranitPhase::Defeated;
                state.speedMetersPerSecond = 0.0F;
                state.mainEngineActive = false;
                state.lastUpdateTimeSeconds = cursorTime;
                return std::optional<P700GranitImpact>{};
            }
        }
''', "terminal probability resolution")

p.write_text(s, encoding="utf-8")

# Update headless lifecycle regression to assert the richer sequence.
p = Path("Tests/M5P700Checks.h")
t = p.read_text(encoding="utf-8")
t = replace_once(t, '''        .cruiseSpeedMetersPerSecond = 500.0F,
        .terminalSpeedMetersPerSecond = 500.0F,
        .maximumAirborneTurnRateRadiansPerSecond = 0.35F,
        .waterExitTransitionSeconds = 0.50,
        .deploymentSeconds = 1.50,''', '''        .cruiseSpeedMetersPerSecond = 680.0F,
        .terminalSpeedMetersPerSecond = 750.0F,
        .maximumAirborneTurnRateRadiansPerSecond = 0.35F,
        .launcherHatchOpeningSeconds = 0.75,
        .waterExitTransitionSeconds = 0.50,
        .postExitTransitionSeconds = 0.60,
        .deploymentSeconds = 1.50,''', "test definition")

t = replace_once(t, '''    if (!surfaceLaunch || !surfaceLaunch->allowed || surfaceRuntime->phase != P700GranitPhase::WaterExit ||
        surfaceRuntime->deploymentProgress != 0.0F)
    {
        return fail("surface launch must be accepted and bypass UnderwaterLaunch while remaining stowed");
    }''', '''    if (!surfaceLaunch || !surfaceLaunch->allowed || surfaceRuntime->phase != P700GranitPhase::HatchOpening ||
        surfaceRuntime->deploymentProgress != 0.0F || surfaceRuntime->hatchOpenProgress != 0.0F)
    {
        return fail("surface launch must still open the selected launcher hatch before booster ignition");
    }''', "surface launch expectation")

t = replace_once(t, '''    if (!launch || !launch->allowed || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.deploymentProgress != 0.0F || runtime.guidanceTrackId != targetTrack.trackId)
    {
        return fail("submerged launch must enter UnderwaterLaunch from a perceived target");
    }

    const auto underwater = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 0.50, carrierBody);
    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= 0.0F)
    {
        return fail("underwater exit movement must stay stowed before reaching the surface");
    }

    const auto waterExit = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 1.20, carrierBody);
    if (!waterExit || *waterExit || runtime.phase != P700GranitPhase::WaterExit ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y <= 0.0F)
    {
        return fail("water-exit phase must begin only after the physical surface crossing and remain stowed");
    }

    const auto deploying = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 2.00, carrierBody);
    if (!deploying || *deploying || runtime.phase != P700GranitPhase::AirborneDeploying ||
        runtime.deploymentProgress <= 0.0F || runtime.deploymentProgress >= 1.0F)
    {
        return fail("P700_Deploy presentation progress must start only after WaterExit");
    }

    const auto cruise = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 3.10, carrierBody);''', '''    if (!launch || !launch->allowed || runtime.phase != P700GranitPhase::HatchOpening ||
        runtime.deploymentProgress != 0.0F || runtime.guidanceTrackId != targetTrack.trackId ||
        runtime.launchBoosterActive || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("submerged launch must begin with the selected launcher hatch opening");
    }

    const auto hatchOpening = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 0.50, carrierBody);
    if (!hatchOpening || *hatchOpening || runtime.phase != P700GranitPhase::HatchOpening ||
        runtime.hatchOpenProgress <= 0.0F || runtime.hatchOpenProgress >= 1.0F || runtime.positionMeters.y != -30.0F)
    {
        return fail("launcher hatch must animate before missile motion/booster ignition");
    }

    const auto underwater = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 1.00, carrierBody);
    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.hatchOpenProgress != 1.0F || !runtime.launchBoosterActive ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= 0.0F)
    {
        return fail("underwater exit must use the attached launch booster and stay folded");
    }

    const auto waterExit = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 1.60, carrierBody);
    if (!waterExit || *waterExit || runtime.phase != P700GranitPhase::WaterExit ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y <= 0.0F || !runtime.launchBoosterAttached)
    {
        return fail("water-exit phase must begin only after physical surface crossing and retain launch hardware");
    }

    const auto separating = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 2.10, carrierBody);
    if (!separating || *separating || runtime.phase != P700GranitPhase::PostExitTransition ||
        runtime.postExitTransitionProgress <= 0.0F || runtime.deploymentProgress != 0.0F)
    {
        return fail("post-exit launch-hardware separation must precede P700_Deploy");
    }

    const auto deploying = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 2.70, carrierBody);
    if (!deploying || *deploying || runtime.phase != P700GranitPhase::AirborneDeploying ||
        runtime.launchBoosterAttached || runtime.noseProtectionCapAttached || !runtime.mainEngineActive ||
        runtime.deploymentProgress <= 0.0F || runtime.deploymentProgress >= 1.0F)
    {
        return fail("nose cap/booster must separate and main engine ignite before aerodynamic deployment");
    }

    const auto cruise = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, 4.10, carrierBody);''', "test full launch sequence")

t = replace_once(t, '''    const auto weakAdvance = AdvanceP700GranitWithCollision(
        definition, runtime, weakSameTrack, physicsWorld, 4.0, carrierBody);''', '''    const auto weakAdvance = AdvanceP700GranitWithCollision(
        definition, runtime, weakSameTrack, physicsWorld, 4.5, carrierBody);''', "weak track time")
t = t.replace('double timeSeconds = 4.0;', 'double timeSeconds = 4.5;')

# Add deterministic terminal-defense regression before cleanup.
marker = '''    if (!physicsWorld.DestroyBody(carrierBody) || !physicsWorld.DestroyBody(targetBody))
    {
        return fail("P-700 fixture cleanup");
    }
    return true;'''
addition = '''    // Probability is deterministic from the launch Track seed, so CI is reproducible. A 100% hard-kill
    // profile must defeat the weapon at terminal entry without fabricating a physics impact/damage event.
    const Physics::PhysicsBodyHandle defendedTargetBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 50.0F, .y = 10.0F, .z = 10.0F},
            .position = {.x = 30'000.0F, .y = -2.0F, .z = 0.0F}});
    if (!defendedTargetBody.IsValid())
        return fail("defended target fixture creation");
    const Perception::Track defendedTrack = MakeTrack(7010U, 30'000.0F);
    auto defendedRuntimeResult = CreateP700GranitRuntime(definition, 0.0);
    if (!defendedRuntimeResult)
        return fail("defended runtime creation");
    auto defendedRuntime = *defendedRuntimeResult;
    const auto defendedLaunch = LaunchP700Granit(definition, defendedRuntime, defendedTrack, SubmergedCarrier(), 0.0);
    if (!defendedLaunch || !defendedLaunch->allowed)
        return fail("defended launch employment");
    const P700TerminalDefenseProfile guaranteedHardKill{
        .seekerFailureProbability = 0.0F,
        .softKillProbability = 0.0F,
        .hardKillProbability = 1.0F,
        .maneuverDefeatProbability = 0.0F};
    std::optional<P700GranitImpact> defendedImpact{};
    double defendedTime = 0.0;
    for (int step = 0; step < 120 && defendedRuntime.phase != P700GranitPhase::Defeated; ++step)
    {
        defendedTime += 1.0;
        const auto advanced = AdvanceP700GranitWithCollision(
            definition, defendedRuntime, defendedTrack, physicsWorld, defendedTime, carrierBody, guaranteedHardKill);
        if (!advanced)
            return fail("defended terminal advance");
        if (*advanced)
            defendedImpact = **advanced;
    }
    if (defendedImpact || defendedRuntime.phase != P700GranitPhase::Defeated ||
        defendedRuntime.terminalOutcome != P700TerminalEngagementOutcome::HardKill ||
        defendedRuntime.impactedBody.has_value())
    {
        return fail("terminal hard-kill probability must defeat without a fake impact");
    }
    const auto defeatedSpent = AdvanceP700GranitWithCollision(
        definition, defendedRuntime, std::nullopt, physicsWorld, defendedTime + 0.1, carrierBody, guaranteedHardKill);
    if (!defeatedSpent || *defeatedSpent || defendedRuntime.phase != P700GranitPhase::Spent)
        return fail("defeated P-700 must consume cleanly into Spent");

    if (!physicsWorld.DestroyBody(carrierBody) || !physicsWorld.DestroyBody(targetBody) ||
        !physicsWorld.DestroyBody(defendedTargetBody))
    {
        return fail("P-700 fixture cleanup");
    }
    return true;'''
t = replace_once(t, marker, addition, "terminal defense regression")
p.write_text(t, encoding="utf-8")

print("P-700 full simulation launch-sequence patch applied")
