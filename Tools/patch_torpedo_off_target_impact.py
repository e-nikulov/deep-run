from pathlib import Path

# Live combat: an off-target physical collision is a valid terminal miss, not a frame error.
path = Path('Game/Combat/CombatPlaygroundRuntime.h')
text = path.read_text(encoding='utf-8')
old = '''                if (impact->physicsHit.body != destroyer_.body)
                {
                    return std::unexpected("M5-E.1 torpedo struck an unexpected physical body");
                }
                const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                if (!damaged)
                {
                    return std::unexpected("M5-E.1 destroyer damage application failed: " + damaged.error());
                }
'''
new = '''                // Any Jolt hit physically consumes the weapon. Only a hit on the intended destroyer body
                // applies destroyer integrity damage; terrain/other-body contact is a legitimate terminal miss.
                if (impact->physicsHit.body == destroyer_.body)
                {
                    const auto damaged = ApplySimpleDestroyerDamage(destroyerDefinition_, destroyer_, impact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-E.1 destroyer damage application failed: " + damaged.error());
                    }
                }
'''
if text.count(old) != 1:
    raise SystemExit(f'live off-target impact anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1), encoding='utf-8')

# Physics regression explicitly records impact as a different terminal reason from endurance exhaustion.
path = Path('Tests/M5CombatImpactChecks.h')
text = path.read_text(encoding='utf-8')
old = '''    if (impact.physicsHit.body != targetBody || torpedo.movementDomain != MovementDomain::Spent ||
        torpedo.impactedBody != targetBody || torpedo.speedMetersPerSecond != 0.0F ||
        std::abs(torpedo.positionMeters.x - 26.0F) > 0.1F ||'''
new = '''    if (impact.physicsHit.body != targetBody || torpedo.movementDomain != MovementDomain::Spent ||
        torpedo.terminalReason != ConventionalTorpedoTerminalReason::Impact ||
        torpedo.impactedBody != targetBody || torpedo.speedMetersPerSecond != 0.0F ||
        std::abs(torpedo.positionMeters.x - 26.0F) > 0.1F ||'''
if text.count(old) != 1:
    raise SystemExit(f'impact terminal reason anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1), encoding='utf-8')
