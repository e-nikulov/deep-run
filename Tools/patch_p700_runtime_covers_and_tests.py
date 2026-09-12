from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# The canonical source-first names are SM_Antey_P700_Cover_*.  Keep the raw
# node identity private at the production loader boundary.
p = Path("Game/Submarine/ProductionAnteyAsset.cpp")
s = p.read_text(encoding="utf-8")
s = replace_once(
    s,
    '"SM_P700_Hatch_{}_{}", port ? "Port" : "Starboard", ordinal)',
    '"SM_Antey_P700_Cover_{}_{}", port ? "Port" : "Starboard", ordinal)',
    "production hatch node reference",
)
p.write_text(s, encoding="utf-8")


# The source-derived cover geometry is production presentation geometry.  Only
# its Blender QA animation is authoring-only; runtime angle/progress remains
# Game-owned.
p = Path("Tools/Blender/build_antey_source_first.py")
s = p.read_text(encoding="utf-8")
s = replace_once(
    s,
    'obj["P700_DEPLOYMENT_OPERATION"] = qa_animation_name; obj["AUTHORING_ONLY"] = True; obj["RUNTIME_EXPORT"] = False;',
    'obj["P700_DEPLOYMENT_OPERATION"] = qa_animation_name; obj["AUTHORING_ONLY"] = False; obj["RUNTIME_EXPORT"] = True; obj["runtime_export"] = True; obj["lod"] = 0;',
    "source-first cover runtime flags",
)
p.write_text(s, encoding="utf-8")

p = Path("Tools/Blender/fix_antey_animation_ownership.py")
s = p.read_text(encoding="utf-8")
s = replace_once(
    s,
    '''        obj["AUTHORING_ONLY"] = True
        obj["RUNTIME_EXPORT"] = False
        obj["P700_DEPLOYMENT_OPERATION"] = QA_ANIMATION''',
    '''        # Cover geometry is canonical runtime presentation content.  The QA NLA/action below remains
        # authoring-only, while gameplay supplies the actual opening progress at runtime.
        obj["AUTHORING_ONLY"] = False
        obj["RUNTIME_EXPORT"] = True
        obj["runtime_export"] = True
        obj["lod"] = 0
        obj["P700_DEPLOYMENT_OPERATION"] = QA_ANIMATION''',
    "animation-ownership cover flags",
)
p.write_text(s, encoding="utf-8")


# Replace fragile absolute-time assumptions with phase-driven checks.  This
# proves the missile reaches WaterExit only after it physically crosses the
# surface, independent of harmless tuning/substep changes.
p = Path("Tests/M5P700Checks.h")
s = p.read_text(encoding="utf-8")
start = s.find("    const auto hatchOpening = AdvanceP700GranitWithCollision(")
end = s.find("    bool sawTerminal = false;", start)
if start < 0 or end < 0:
    raise RuntimeError("P-700 lifecycle test block not found")
replacement = r'''    double lifecycleTimeSeconds = 0.50;
    const auto hatchOpening = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!hatchOpening || *hatchOpening || runtime.phase != P700GranitPhase::HatchOpening ||
        runtime.hatchOpenProgress <= 0.0F || runtime.hatchOpenProgress >= 1.0F || runtime.positionMeters.y != -30.0F)
    {
        return fail("launcher hatch must animate before missile motion/booster ignition");
    }

    lifecycleTimeSeconds = 1.00;
    const auto underwater = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!underwater || *underwater || runtime.phase != P700GranitPhase::UnderwaterLaunch ||
        runtime.hatchOpenProgress != 1.0F || !runtime.launchBoosterActive ||
        runtime.deploymentProgress != 0.0F || runtime.positionMeters.y >= runtime.surfaceLevelYMeters)
    {
        return fail("underwater exit must use the attached launch booster and stay folded");
    }

    const auto advanceUntilPhase = [&](const P700GranitPhase expectedPhase, const double deadlineSeconds)
    {
        while (runtime.phase != expectedPhase && lifecycleTimeSeconds + 1.0e-9 < deadlineSeconds)
        {
            lifecycleTimeSeconds = (std::min)(deadlineSeconds, lifecycleTimeSeconds + 0.05);
            const auto advanced = AdvanceP700GranitWithCollision(
                definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
            if (!advanced || *advanced)
            {
                return false;
            }
        }
        return runtime.phase == expectedPhase;
    };

    if (!advanceUntilPhase(P700GranitPhase::WaterExit, 3.0) ||
        runtime.deploymentProgress != 0.0F ||
        runtime.positionMeters.y < runtime.surfaceLevelYMeters - 0.001F ||
        !runtime.launchBoosterActive || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("water-exit phase must begin only after physical surface crossing and retain launch hardware");
    }

    const double waterExitObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::PostExitTransition,
            waterExitObservedSeconds + definition.waterExitTransitionSeconds + 0.20) ||
        runtime.deploymentProgress != 0.0F || !runtime.launchBoosterAttached || !runtime.noseProtectionCapAttached)
    {
        return fail("bounded water-exit climb must precede launch-hardware separation");
    }

    lifecycleTimeSeconds += 0.20;
    const auto separating = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!separating || *separating || runtime.phase != P700GranitPhase::PostExitTransition ||
        runtime.postExitTransitionProgress <= 0.0F || runtime.postExitTransitionProgress >= 1.0F ||
        runtime.deploymentProgress != 0.0F || runtime.noseProtectionCapAttached ||
        !runtime.launchBoosterAttached || !runtime.launchBoosterActive || runtime.mainEngineActive)
    {
        return fail("nose cap must clear before booster separation and before P700_Deploy");
    }

    const double separationObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::AirborneDeploying,
            separationObservedSeconds + definition.postExitTransitionSeconds + 0.50) ||
        runtime.launchBoosterAttached || runtime.noseProtectionCapAttached || !runtime.mainEngineActive)
    {
        return fail("booster must separate and main engine ignite before aerodynamic deployment");
    }

    lifecycleTimeSeconds += 0.05;
    const auto deploying = AdvanceP700GranitWithCollision(
        definition, runtime, targetTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!deploying || *deploying || runtime.phase != P700GranitPhase::AirborneDeploying ||
        runtime.deploymentProgress <= 0.0F || runtime.deploymentProgress >= 1.0F)
    {
        return fail("P700_Deploy must visibly advance only after main-engine transition");
    }

    const double deploymentObservedSeconds = lifecycleTimeSeconds;
    if (!advanceUntilPhase(
            P700GranitPhase::Cruise,
            deploymentObservedSeconds + definition.deploymentSeconds + 0.50) ||
        std::abs(runtime.deploymentProgress - 1.0F) > 1.0e-6F)
    {
        return fail("deployment must complete before Cruise");
    }

    Perception::Track wrongTrack = targetTrack;
    wrongTrack.trackId = targetTrack.trackId + 1U;
    lifecycleTimeSeconds += 0.10;
    if (AdvanceP700GranitWithCollision(
            definition, runtime, wrongTrack, physicsWorld, lifecycleTimeSeconds, carrierBody))
    {
        return fail("in-flight P-700 must reject retargeting to a different perceived Track identity");
    }

    // A weak same-ID update cannot erase the last accepted perceived aim point.
    auto weakSameTrack = targetTrack;
    weakSameTrack.confidence = 0.1F;
    lifecycleTimeSeconds += 0.10;
    const auto weakAdvance = AdvanceP700GranitWithCollision(
        definition, runtime, weakSameTrack, physicsWorld, lifecycleTimeSeconds, carrierBody);
    if (!weakAdvance || *weakAdvance || !runtime.perceivedAimPointMeters)
    {
        return fail("weak same-ID evidence must preserve the last qualified aim point");
    }

'''
s = s[:start] + replacement + s[end:]
s = replace_once(
    s,
    "    double timeSeconds = 4.5;",
    "    double timeSeconds = lifecycleTimeSeconds;",
    "P-700 impact-loop start time",
)
p.write_text(s, encoding="utf-8")

print("P700 runtime cover/loader/lifecycle patches applied")
