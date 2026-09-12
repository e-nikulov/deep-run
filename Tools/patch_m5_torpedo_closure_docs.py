from pathlib import Path

combat = Path("docs/development/m5-combat-playground.md")
text = combat.read_text(encoding="utf-8")
anchor = '''The retained Debug/Release artifacts are `m5-combat-visual-*` and `m5-p700-visual-*`; each P-700 package contains
its machine-readable report plus launch, water-exit, deploy, cruise/terminal and impact captures. Detailed
compartment/flooding/system damage remains M6 scope.

## Current accepted baseline
'''
replacement = '''The retained Debug/Release artifacts are `m5-combat-visual-*` and `m5-p700-visual-*`; each P-700 package contains
its machine-readable report plus launch, water-exit, deploy, cruise/terminal and impact captures. Detailed
compartment/flooding/system damage remains M6 scope.

### M5 closure hardening — autonomous torpedo seeker and causal misses

Status: ACCEPTED as corrective M5 closure work; this does not open a new gameplay milestone.

The conventional torpedo path now closes the remaining autonomy gap after launch. Following the bounded straight-run
phase, live carrier Track updates stop controlling the weapon and the torpedo owns a local mixed seeker state machine:
`Dormant -> PassiveSearch/PassiveTrack -> ActiveSearch/ActiveTrack -> Reacquire -> Exhausted`. Passive and active
observations both cross the normal `AcousticWorld -> SensorObservation -> TrackManager` perceived-world boundary;
no hostile Transform/body identity or privileged decoy flag enters seeker guidance.

Misses are causal rather than a hidden percentage roll: insufficient SNR, propagation delay, thermocline attenuation,
active-beam geometry, weak echo, contact loss/reacquisition failure, acoustic-decoy seduction, bounded turn authority,
stale onboard solution, physical off-target collision, pure near-miss, and finite GAME-POLICY endurance can all prevent
a hit. Endurance expiry terminates as `Spent / EnduranceExpired` without fabricated damage. A real collision with a
non-selected physical body is a legitimate terminal miss relative to the selected target instead of a runtime error.

The hostile torpedo's active-seeker transmission is also an ordinary timestamped acoustic emission in incoming-threat
perception, so active homing is not acoustically invisible. The warning path uses the same propagation, SNR and
`AcousticEnvironment` thermocline attenuation as the rest of M5 acoustics. See
`docs/development/m5-torpedo-active-passive-seeker.md` for the full authority and failure-mode contract.

## Current accepted baseline
'''
if text.count(anchor) != 1:
    raise SystemExit("M5 combat closure anchor mismatch")
combat.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")

testing = Path("docs/development/testing.md")
text = testing.read_text(encoding="utf-8")
anchor = '''`--smoke-test` must produce the five combat acceptance checkpoints and machine-readable report. `--smoke-p700` must produce five P-700 state checkpoints plus the mandatory lifecycle markers from hatch opening through terminal and a real `PHYSICAL_IMPACT damage=100`; the report must retain `23/24` launcher inventory and identify the destroyer Jolt body as the physical impact target. Both smoke paths retain BMP/JSON artifact packages in CI. Final technical M5 closure is evidenced by run `34712161730` (#488) on `851a922dd4a2f4055dba523d3dc1a7773dad4ed2`, where Debug and Release passed all six CTest targets and both windowed smokes.
'''
replacement = '''`--smoke-test` must produce the five combat acceptance checkpoints and machine-readable report. `--smoke-p700` must produce five P-700 state checkpoints plus the mandatory lifecycle markers from hatch opening through terminal and a real `PHYSICAL_IMPACT damage=100`; the report must retain `23/24` launcher inventory and identify the destroyer Jolt body as the physical impact target. Both smoke paths retain BMP/JSON artifact packages in CI. The original technical M5 closure is evidenced by run `34712161730` (#488) on `851a922dd4a2f4055dba523d3dc1a7773dad4ed2`, where Debug and Release passed all six CTest targets and both windowed smokes.

The later M5 torpedo-seeker closure hardening does not add a seventh CTest target or a new smoke command. Its mixed passive/active state machine, delayed active echo, off-beam failure, quiet-target passive failure, decoy seduction, bounded steering, finite endurance and impact terminal-state checks execute inside the existing M5 test composition. Final branch promotion must therefore rerun the same six CTest targets and both windowed smokes in Debug and Release on the clean closure HEAD. The authoritative contract is documented in `m5-torpedo-active-passive-seeker.md`.
'''
if text.count(anchor) != 1:
    raise SystemExit("testing M5 closure anchor mismatch")
testing.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")
