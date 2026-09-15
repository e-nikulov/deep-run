#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch(rel, old, new, label):
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

# Acceptance automation must follow the same player contract: selection is legal at any readiness phase and
# preparation starts automatically after selection.
patch(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit &&
            playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
        {
            commands[count++] = {.type = PlayerCombatCommandType::NextWeapon};
        }
''',
    '''        if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit)
        {
            commands[count++] = {.type = PlayerCombatCommandType::NextWeapon};
        }
''',
    "P700 acceptance selector")

patch(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''            else if (selected && selected->estimatedPositionMeters.has_value())
            {
                if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
                    commands[count++] = {.type = PlayerCombatCommandType::PrepareWeapon};
                else if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
                    commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};
            }
''',
    '''            else if (selected && selected->estimatedPositionMeters.has_value() &&
                     playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
            {
                commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};
            }
''',
    "P700 acceptance auto preparation")

# This integration test is about player command/readiness/launch ownership. Enemy decoy deployment has a dedicated
# test and its exact launch-tick timing changed when the redundant manual preparation wait disappeared.
patch(
    "Tests/M5PlayerControlledCombatChecks.h",
    '''        launched->playerCombat.weaponTargetTrackId != launched->playerCombat.selectedTrackId ||
        !runtime.PlayerTorpedo() || !runtime.Decoy() || !runtime.Decoy()->active)
''',
    '''        launched->playerCombat.weaponTargetTrackId != launched->playerCombat.selectedTrackId ||
        !runtime.PlayerTorpedo())
''',
    "remove incidental decoy timing assertion")

patch(
    "Tests/M5CombatImpactChecks.h",
    'return fail("M5-J2-B normal-play commander gating and explicit prepare-ready-fire sequence");',
    'return fail("M5-J2-B automatic weapon readiness, fire and in-flight selection sequence");',
    "M5-J2-B failure label")

print("weapon UX follow-up patch applied")
