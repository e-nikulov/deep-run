# Project 949A / Antey handling contract

Status: production gameplay contract for the canonical Deep Run Project 949A-inspired player submarine.

## Public-source boundary

The official Rubin public Project 949A page identifies the design and its mission/architecture but does not publish a public displacement table. Open specialist/educational references commonly quote approximately `14,700 t` surfaced and `24,000 t` submerged/full displacement; some specialist compilations instead show `19,400 t (24,000?)`, so the exact public displacement definition is not perfectly consistent.

Deep Run uses **24,000,000 kg** as the canonical fully-submerged gameplay rigid-body mass. This is an explicit open-source convention for the submerged game state, not a claim of access to classified hydrostatic documentation. The buoyancy system derives neutral displaced volume from this mass and authoritative seawater density, so changing the mass cannot silently leave a 12,000 t neutral-buoyancy model behind.

References:
- Rubin Design Bureau, Project 949A public project page: <https://ckb-rubin.ru/proekty/voennoe_korablestroenie/podvodnye_lodki/proekt_949a/>
- Deepstorm Project 949A public compilation (Apalkov-sourced characteristics): <https://www.deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm>

## Ahead / braking / astern

`Throttle` is a signed command. Positive drive spins the aggregate synchronized twin-propeller shaft state ahead; negative drive commands astern. If the shaft is still rotating ahead when astern is requested, the generic propulsion simulation first reduces RPM toward exactly zero and only subsequent fixed ticks build reverse RPM. The same mechanism makes reverse thrust a physical braking command while the boat still has forward inertia.

No reliable public Project 949A maximum-astern speed figure was found. Therefore Deep Run's asymmetry is explicitly **GAME POLICY**: `180 rpm / 12 MN` ahead versus `90 rpm / 3 MN` astern in the current coarse propulsion model. The purpose is to make sustained astern motion materially slower than ahead motion, not to publish a historical performance number.

Both production propeller nodes are hub-centred, local `+X` articulated assets. Their visible angle is derived from signed authoritative shaft RPM, so they spin in the opposite direction under astern command. Presentation never drives thrust.

## 2.5D turn-around

The Jolt body remains a 2.5D XY body: X/Y translation and Z pitch are physical; screen-depth translation/yaw is not promoted to a new simulation DOF. A separate Game-owned `longitudinalSign` tells the rest of gameplay whether the bow points screen-right (`+1`) or screen-left (`-1`).

Pressing `TurnAround` starts a **60 s GAME-POLICY** 180-degree visual turn. During the turn the production model yaws smoothly through the screen-depth axis. Longitudinal thrust is projected by `cos(pi * progress)`: it fades to zero at the visual 90-degree midpoint and grows with the opposite sign during the second half. Existing inertia and hydrodynamic drag remain authoritative, so neither heading nor velocity can snap-reverse.

Astern remains independent of turn-around: negative shaft thrust always acts backward relative to the current/transitioning longitudinal facing. Weapon employment is blocked while the carrier is mid-turn; once stable, launch-sector heading follows the final facing sign. This prevents a target on the old side from being fired upon as though the boat had already completed its turn.

The turn duration, reverse thrust/RPM and 2.5D projection are gameplay abstractions, not measured Project 949A turning-circle data.
