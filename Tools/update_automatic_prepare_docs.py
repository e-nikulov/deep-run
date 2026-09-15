#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
controls_path = root / "docs/design/controls.md"
engine_path = root / "docs/architecture/engine-spec.md"
controls = controls_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

controls = replace_once(controls, "PrepareWeapon\n", "", "controls canonical PrepareWeapon")
controls = replace_once(
    controls,
    "| LT | prepare weapon / targeting |",
    "| LT | reserved / contextual; weapon preparation is automatic |",
    "controls LT binding",
)
controls = replace_once(
    controls,
    "| Right Mouse | prepare weapon / alternate according to context |",
    "| Right Mouse | alternate / contextual; weapon preparation is automatic |",
    "controls RMB binding",
)
controls = replace_once(
    controls,
    "M5-J5 implementation note: the combat playground now uses the reference `LT` prepare, `RT` fire, `RB` active\nsonar, `Y` contact-select and contextual `X` decoy bindings. In the current tactical-camera context, right-stick\nX pans and right-stick Y zooms so the weapon triggers no longer have a conflicting presentation meaning. This is\nan M5 context mapping, not a change to the semantic-action boundary or a claim about the final rebindable layout.",
    "M5 weapon-UX implementation note: selecting a weapon automatically starts its preparation timer; there is no\nplayer-facing `PrepareWeapon` action. `RT` fires, `RB` performs active sonar ranging, `Y` selects contacts and\ncontextual `X` deploys a decoy. `LT` and RMB are therefore free for future contextual bindings. In the current\ntactical-camera context, right-stick X pans and right-stick Y zooms.",
    "controls M5 implementation note",
)

prepare_count = engine.count("PrepareWeapon\n")
if prepare_count != 1:
    raise RuntimeError(f"engine-spec canonical PrepareWeapon: expected one match, found {prepare_count}")
engine = engine.replace("PrepareWeapon\n", "", 1)

controls_path.write_text(controls, encoding="utf-8", newline="\n")
engine_path.write_text(engine, encoding="utf-8", newline="\n")
print("automatic weapon preparation docs updated")
