from pathlib import Path


def patch(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, got {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


patch(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''                if (impact->physicsHit.body == destroyer_.body)
                {
                    const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
''',
    '''                if (impact->physicsHit.body == destroyer_.body && !destroyer_.integrity.destroyed)
                {
                    const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
''',
    "primary torpedo destroyed-target guard")
patch(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''                else if (civilian_ && impact->physicsHit.body == civilian_->body)
                {
                    const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, impact->damage);
''',
    '''                else if (civilian_ && impact->physicsHit.body == civilian_->body && !civilian_->integrity.destroyed)
                {
                    const auto damaged = ApplySimpleCivilianVesselDamage(civilianDefinition_, *civilian_, impact->damage);
''',
    "primary torpedo civilian destroyed-target guard")

patch(
    "Game/Combat/P700VisualAcceptance.h",
    '''    float hatchOpenProgress = 0.0F;
    float deploymentProgress = 0.0F;
''',
    '''    float hatchOpenProgress = 0.0F;
    float launcherFloodProgress = 0.0F;
    float deploymentProgress = 0.0F;
''',
    "P700 acceptance flood field")
patch(
    "Game/Combat/P700VisualAcceptance.h",
    '''            if (!missile.positionMeters.IsFinite() || !std::isfinite(missile.hatchOpenProgress) ||
                !std::isfinite(missile.deploymentProgress))
''',
    '''            if (!missile.positionMeters.IsFinite() || !std::isfinite(missile.hatchOpenProgress) ||
                !std::isfinite(missile.launcherFloodProgress) || !std::isfinite(missile.deploymentProgress))
''',
    "P700 acceptance flood validation")
patch(
    "Game/Combat/P700VisualAcceptance.h",
    '''            snapshot.hatchOpenProgress = missile.hatchOpenProgress;
            snapshot.deploymentProgress = missile.deploymentProgress;
''',
    '''            snapshot.hatchOpenProgress = missile.hatchOpenProgress;
            snapshot.launcherFloodProgress = missile.launcherFloodProgress;
            snapshot.deploymentProgress = missile.deploymentProgress;
''',
    "P700 acceptance flood snapshot")
patch(
    "DeepRun/Main.cpp",
    '''            {"hatch_open_progress", state.hatchOpenProgress},
            {"deployment_progress", state.deploymentProgress},
''',
    '''            {"hatch_open_progress", state.hatchOpenProgress},
            {"launcher_flood_progress", state.launcherFloodProgress},
            {"deployment_progress", state.deploymentProgress},
''',
    "P700 acceptance JSON flood field")

# Explicitly document that timing constants are gameplay policy rather than public engineering data.
p = Path("docs/development/m5-weapon-ux-soft-envelopes.md")
text = p.read_text(encoding="utf-8")
needle = "Already launched torpedoes and Granits keep independent flight runtimes, so launcher readiness rather than flight duration controls follow-on shots."
addition = needle + " The acceptance report also records `launcher_flood_progress`, so CI artifacts can prove that the wet-launch preparation occurred before missile motion."
if text.count(needle) != 1:
    raise RuntimeError("launcher-cycle documentation anchor mismatch")
p.write_text(text.replace(needle, addition, 1), encoding="utf-8")

print("launcher-cycle finalization applied")
