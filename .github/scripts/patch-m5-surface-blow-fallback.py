from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "Game/Submarine/AnteyBallastControl.h",
    """    // Main ballast stays flooded during ordinary submerged depth changes. A sustained surface command may
    // begin normal blowing only in the final near-surface band; once partly blown, the operation may continue.
""",
    """    // Main ballast stays flooded during ordinary submerged depth changes. A sustained surface command begins
    // normal blowing in the final near-surface band. Deadlock fallback: if low-speed trim is already at
    // maximum buoyancy and the controller still requests that same maximum, continuing Surface also starts
    // the blow. Once partly blown, the operation may continue while Surface remains commanded.
""",
)

replace_once(
    "Game/Submarine/AnteyBallastControl.h",
    """    AnteyBallastState next = current;
    if (depthCommandFraction > config.depthCommandDeadzone && current.mainBallastFillFraction < 1.0F)
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastFillRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
    else if (depthCommandFraction < -config.depthCommandDeadzone &&
             (current.mainBallastFillFraction < 1.0F ||
              signedDepthMeters <= config.mainBallastBlowArmDepthMeters))
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastBlowRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }

    const float trimTargetKg = std::clamp(
        requestedTrimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);
""",
    """    AnteyBallastState next = current;
    const float trimTargetKg = std::clamp(
        requestedTrimMassDeltaKg,
        -config.maximumTrimMassKg,
        config.maximumTrimMassKg);
    constexpr float TrimSaturationToleranceKg = 1.0F;
    const bool surfaceCommandHeld = depthCommandFraction < -config.depthCommandDeadzone;
    const bool surfaceTrimSaturated =
        current.trimMassDeltaKg <= -config.maximumTrimMassKg + TrimSaturationToleranceKg &&
        trimTargetKg <= -config.maximumTrimMassKg + TrimSaturationToleranceKg;

    if (depthCommandFraction > config.depthCommandDeadzone && current.mainBallastFillFraction < 1.0F)
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastFillRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
    else if (surfaceCommandHeld &&
             (current.mainBallastFillFraction < 1.0F ||
              signedDepthMeters <= config.mainBallastBlowArmDepthMeters ||
              surfaceTrimSaturated))
    {
        next.mainBallastFillFraction = std::clamp(
            current.mainBallastFillFraction +
                depthCommandFraction * config.mainBallastBlowRateFractionPerSecond * fixedDeltaSeconds,
            0.0F,
            1.0F);
    }
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """// Exact tank timing remains explicit GAME POLICY in the pure ballast-state controller. Main ballast is not
// used for ordinary submerged depth changes; the controller only arms normal blowing in the final surface band.
""",
    """// Exact tank timing remains explicit GAME POLICY in the pure ballast-state controller. Main ballast is not
// used for ordinary submerged depth changes. Normal blowing starts in the final surface band, with a
// deadlock fallback when held Surface has saturated low-speed trim and still requests maximum buoyancy.
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    ballastState_ = {};
    committedDynamicMassKg_ = M5AnteySubmergedMassKg;
""",
    """    ballastState_ = {};
    committedMainBallastFlowFractionPerSecond_ = 0.0F;
    committedDynamicMassKg_ = M5AnteySubmergedMassKg;
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    if (!nextBallastState)
        return std::unexpected("physical playground ballast-state advance failed: " + nextBallastState.error());
    const float nextDynamicMassKg =
        Submarine::AnteyPhysicalMassKg(M5AnteyBallastControl, *nextBallastState, expendedOrdnanceMassKg_);
""",
    """    if (!nextBallastState)
        return std::unexpected("physical playground ballast-state advance failed: " + nextBallastState.error());
    const float nextMainBallastFlowFractionPerSecond =
        (nextBallastState->mainBallastFillFraction - ballastState_.mainBallastFillFraction) / fixedDeltaSeconds;
    if (!std::isfinite(nextMainBallastFlowFractionPerSecond))
        return std::unexpected("physical playground main-ballast flow is non-finite");
    const float nextDynamicMassKg =
        Submarine::AnteyPhysicalMassKg(M5AnteyBallastControl, *nextBallastState, expendedOrdnanceMassKg_);
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """    facingState_ = facingAdvance->nextState;
    ballastState_ = *nextBallastState;
    committedDynamicMassKg_ = nextDynamicMassKg;
""",
    """    facingState_ = facingAdvance->nextState;
    ballastState_ = *nextBallastState;
    committedMainBallastFlowFractionPerSecond_ = nextMainBallastFlowFractionPerSecond;
    committedDynamicMassKg_ = nextDynamicMassKg;
""",
)

replace_once(
    "Game/PhysicalPlayground.cpp",
    """        .throttleFraction = committedThrottleFraction_,
        .mainBallastFillFraction = ballastState_.mainBallastFillFraction,
        .trimMassDeltaKg = ballastState_.trimMassDeltaKg,
""",
    """        .throttleFraction = committedThrottleFraction_,
        .mainBallastFillFraction = ballastState_.mainBallastFillFraction,
        .mainBallastFlowFractionPerSecond = committedMainBallastFlowFractionPerSecond_,
        .trimMassDeltaKg = ballastState_.trimMassDeltaKg,
""",
)

replace_once(
    "Game/PhysicalPlayground.h",
    """    float throttleFraction = 0.0F;
    float mainBallastFillFraction = 1.0F;
    float trimMassDeltaKg = 0.0F;
""",
    """    float throttleFraction = 0.0F;
    float mainBallastFillFraction = 1.0F;
    // Negative = blowing (water leaving main ballast), positive = flooding.
    float mainBallastFlowFractionPerSecond = 0.0F;
    float trimMassDeltaKg = 0.0F;
""",
)

replace_once(
    "Game/PhysicalPlayground.h",
    """    Submarine::AnteyBallastState ballastState_{};
    float expendedOrdnanceMassKg_ = 0.0F;
    float committedDynamicMassKg_ = 0.0F;
""",
    """    Submarine::AnteyBallastState ballastState_{};
    float expendedOrdnanceMassKg_ = 0.0F;
    float committedMainBallastFlowFractionPerSecond_ = 0.0F;
    float committedDynamicMassKg_ = 0.0F;
""",
)

replace_once(
    "Game/Combat/CombatCommandUi.h",
    """    float throttleFraction = 0.0F;
    float mainBallastFillFraction = 1.0F;
    float trimMassDeltaKg = 0.0F;
""",
    """    float throttleFraction = 0.0F;
    float mainBallastFillFraction = 1.0F;
    float mainBallastFlowFractionPerSecond = 0.0F;
    float trimMassDeltaKg = 0.0F;
""",
)

replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    """    return "NEUTRAL / TRIMMED";
}

std::optional<ImVec2> ProjectWorldToMainViewport(
""",
    """    return "NEUTRAL / TRIMMED";
}

const char* MainBallastStateName(const VesselNavigationHudSnapshot& snapshot) noexcept
{
    constexpr float flowThreshold = 1.0e-4F;
    if (snapshot.mainBallastFlowFractionPerSecond < -flowThreshold)
    {
        return "BLOWING";
    }
    if (snapshot.mainBallastFlowFractionPerSecond > flowThreshold)
    {
        return "FLOODING";
    }
    if (snapshot.mainBallastFillFraction <= 1.0e-3F)
    {
        return "EMPTY";
    }
    if (snapshot.mainBallastFillFraction >= 1.0F - 1.0e-3F)
    {
        return "FULL";
    }
    return "HOLDING";
}

std::optional<ImVec2> ProjectWorldToMainViewport(
""",
)

replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    """    ImGui::Text("Buoyancy / trim: %s", BuoyancyTrimStateName(snapshot));
    ImGui::Text("Main ballast: %.0f%% | Trim: %+0.1f t",
                snapshot.mainBallastFillFraction * 100.0F, snapshot.trimMassDeltaKg / 1000.0F);
    ImGui::Text("Physical mass: %.0f t", snapshot.dynamicMassKg / 1000.0F);
""",
    """    ImGui::Text("Buoyancy / trim: %s", BuoyancyTrimStateName(snapshot));
    ImGui::Text("Main ballast: %.0f%% / %s | Trim: %+0.1f t",
                snapshot.mainBallastFillFraction * 100.0F,
                MainBallastStateName(snapshot),
                snapshot.trimMassDeltaKg / 1000.0F);
    if (snapshot.mainBallastFlowFractionPerSecond < -1.0e-4F)
    {
        ImGui::TextUnformatted("Surface hold (W / LS UP): MAIN BALLAST BLOW");
    }
    ImGui::Text("Physical mass: %.0f t", snapshot.dynamicMassKg / 1000.0F);
""",
)

replace_once(
    "DeepRun/Main.cpp",
    """                            .throttleFraction = navigationTelemetry->throttleFraction,
                            .mainBallastFillFraction = navigationTelemetry->mainBallastFillFraction,
                            .trimMassDeltaKg = navigationTelemetry->trimMassDeltaKg,
""",
    """                            .throttleFraction = navigationTelemetry->throttleFraction,
                            .mainBallastFillFraction = navigationTelemetry->mainBallastFillFraction,
                            .mainBallastFlowFractionPerSecond = navigationTelemetry->mainBallastFlowFractionPerSecond,
                            .trimMassDeltaKg = navigationTelemetry->trimMassDeltaKg,
""",
)

replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    """    const auto nearSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, fullMainBallast, -1.0F, 2.5F, -100'000.0F, 0.0F, 1.0F);
    const AnteyBallastState partlyBlown{.mainBallastFillFraction = 0.5F, .trimMassDeltaKg = 0.0F};
""",
    """    const auto nearSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, fullMainBallast, -1.0F, 2.5F, -100'000.0F, 0.0F, 1.0F);
    const AnteyBallastState saturatedSurfaceTrim{
        .mainBallastFillFraction = 1.0F,
        .trimMassDeltaKg = -ballastStateConfig.maximumTrimMassKg};
    const auto stalledSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, saturatedSurfaceTrim, -1.0F, 4.2F,
        -ballastStateConfig.maximumTrimMassKg, 0.0F, 1.0F);
    const AnteyBallastState partlyBlown{.mainBallastFillFraction = 0.5F, .trimMassDeltaKg = 0.0F};
""",
)

replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    """    if (!deepSurfaceBallast || !nearSurfaceBallast || !diveFromSurfaceBallast ||
        std::abs(deepSurfaceBallast->mainBallastFillFraction - 1.0F) > 1.0e-6F ||
        !(nearSurfaceBallast->mainBallastFillFraction < 1.0F) ||
        !(diveFromSurfaceBallast->mainBallastFillFraction > partlyBlown.mainBallastFillFraction) ||
""",
    """    if (!deepSurfaceBallast || !nearSurfaceBallast || !stalledSurfaceBallast || !diveFromSurfaceBallast ||
        std::abs(deepSurfaceBallast->mainBallastFillFraction - 1.0F) > 1.0e-6F ||
        !(nearSurfaceBallast->mainBallastFillFraction < 1.0F) ||
        !(stalledSurfaceBallast->mainBallastFillFraction < 1.0F) ||
        !(diveFromSurfaceBallast->mainBallastFillFraction > partlyBlown.mainBallastFillFraction) ||
""",
)

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """In ordinary submerged manoeuvring the main ballast remains flooded: quiet depth changes use bounded trim-water mass at low speed and the stern planes at hydrodynamic speed. Normal main-ballast blowing is armed only in the final near-surface band, while a dive command floods any partly empty main ballast.
""",
    """In ordinary submerged manoeuvring the main ballast remains flooded: quiet depth changes use bounded trim-water mass at low speed and the stern planes at hydrodynamic speed. Normal main-ballast blowing is armed in the final near-surface band. To prevent a surface-command deadlock, continuing Surface also starts the blow if low-speed trim has actually reached its maximum-buoyancy limit and the controller still requests that same maximum; a normal deep ascent that is still making vertical progress therefore does not blow main ballast. A dive command floods any partly empty main ballast.
""",
)

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """- submerged surface intent -> reduce bounded trim-water mass; sustained intent in the final surface band may then blow main ballast;
""",
    """- submerged surface intent -> reduce bounded trim-water mass; sustained intent in the final surface band may then blow main ballast, and saturated/stalled maximum-buoyancy trim escalates to main-ballast blow even if the boat stalls just outside that band;
""",
)

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """- ordinary deep/periscope-depth commands do not blow the main ballast tanks;
""",
    """- ordinary deep/periscope-depth commands do not blow the main ballast tanks while bounded trim still has effective authority; a held Surface command may escalate only after maximum-buoyancy trim is saturated and still demanded;
""",
)

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    """The normal NAV HUD exposes the committed simulation state in player-readable terms, including actual axial speed, main-ballast fill percentage, signed trim-water mass and total physical mass, plus expended-ordnance / weapon-compensation water mass and the semantic trim state:
""",
    """The normal NAV HUD exposes the committed simulation state in player-readable terms, including actual axial speed, main-ballast fill percentage, live main-ballast state (`FULL`, `BLOWING`, `FLOODING`, `HOLDING`, `EMPTY`), signed trim-water mass and total physical mass, plus expended-ordnance / weapon-compensation water mass and the semantic trim state. While Surface is held and main ballast is actively blowing, the HUD explicitly shows `Surface hold (W / LS UP): MAIN BALLAST BLOW`.

The semantic trim state remains:
""",
)
