#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "Game/Combat/CombatPlaygroundRuntime.h"
text = path.read_text(encoding="utf-8")
old = """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent)\n        {\n            // Impact, endurance and range expiry all resolve the launched projectile. CompleteResolvedLaunch()\n            // intentionally no-ops when the player already selected/prepared a different weapon.\n"""
new = """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent &&\n            (playerTorpedo_->terminalReason != Weapons::ConventionalTorpedoTerminalReason::Impact ||\n             !impact.has_value()))\n        {\n            // Range/endurance expiry resolves immediately. A physical impact remains observable for its impact\n            // fixed-step so acceptance/presentation can consume the authoritative Spent pose, then clears on\n            // the following step. CompleteResolvedLaunch() intentionally preserves a newly selected weapon.\n"""
count = text.count(old)
if count != 1:
    raise RuntimeError(f"resolution block: expected one match, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("M5 torpedo resolution timing fix applied")
