# Project 949A / Antey handling contract

Status: production gameplay contract for the canonical Deep Run Project 949A-inspired player submarine. Contract acceptance requires both Windows Debug and Release CI, including M5 weapon/input regressions and the windowed combat smoke.

## Public-source boundary

Deep Run's production player submarine is explicitly **Project 949A `Antey` / Oscar II**, not Project 949 `Granit` / Oscar I. For hydrostatic runtime authority we use the RussianShips Project 949A table directly: **14,820 t surfaced / 19,254 t submerged**. Deepstorm/Apalkov's **14,700 / 19,400 (24,000?) t** and other published values remain provenance/reference data only; they are not averaged into gameplay physics.

Runtime hydrostatics therefore use **14,820,000 kg surfaced / 19,254,000 kg fully submerged**, with a main-ballast mass delta of **4,434,000 kg** (~29.92% of surfaced displacement). At 1025 kg/m^3 the fixed full displaced volume is ~**18,784.390 m^3**. There is no positional buoyancy/render/body offset and no hand-authored waterline target: with the existing production waterplane response, the level flat-water equilibrium follows naturally at ~2.34648 m body-centre depth.

References:
- RussianShips Project 949/949A table (primary hydrostatic and ammunition-count authority): <https://russianships.info/podlodki/949.htm>
- Rubin Design Bureau, Project 949A public project page: <https://ckb-rubin.ru/proekty/voennoe_korablestroenie/podvodnye_lodki/proekt_949a/>
- Deepstorm Project 949A public compilation (Apalkov-sourced alternate characteristics): <https://www.deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm>
- RusNavy Project 949A vessel pages (14,700 / 23,860 t examples): <https://rusnavy.com/nowadays/strength/submarines/tomsk/>
- NTI Project 949A profile documenting a wider published displacement spread: <https://www.nti.org/wp-content/uploads/2021/09/project_949A_antey_oscarII_1.pdf>

## Ordnance mass and launch compensation

Project 949A source-backed physical inventory is **24 P-700 + 18 rounds in the 533 mm pool + 10 rounds in the 650 mm pool**. RussianShips explicitly lists mixed weapon families inside those pools, so Deep Run does not claim that all 18 were USET-80 or all 10 were 65-76A. Until the additional historical weapons are implemented, USET-80 and 65-76A are the playable representatives. Using their current GAME mass approximations (2.0 t and 4.5 t) yields a configured representative ordnance mass of **249 t** including 168 t of P-700. FAST/ECONOMY 65-76A selections share the same finite 10-round 650 mm pool.

A successful launch immediately removes the actual round mass from the Jolt rigid body. The submarine therefore becomes physically lighter until compensation water catches up. Dedicated weapon-compensation water then slews toward the cumulative expended-ordnance mass at **2.8 t/s GAME POLICY**; the fixed hull displaced volume never changes. The chosen rate keeps the effect finite and observable while roughly matching a 14 t two-P-700 mass change over five seconds. Public descriptions support torpedo compensation tanks and seawater-flooded missile/torpedo launch arrangements, but Deep Run does **not** assert an undocumented Project 949A pump rate or exact valve sequence.

## Ahead / braking / astern

`Throttle` is a signed command. Positive drive spins the aggregate synchronized twin-propeller shaft state ahead; negative drive commands astern. If the shaft is still rotating ahead when astern is requested, the generic propulsion simulation first reduces RPM toward exactly zero and only subsequent fixed ticks build reverse RPM. The same mechanism makes reverse thrust a physical braking command while the boat still has forward inertia.

No reliable public Project 949A maximum-astern speed figure was found. Therefore Deep Run's asymmetry is explicitly **GAME POLICY**: `180 rpm / 3.35 MN` ahead versus `90 rpm / 0.8375 MN` astern in the current coarse propulsion model. The purpose is to make sustained astern motion materially slower than ahead motion, not to publish a historical performance number.

Both production propeller nodes are hub-centred, local `+X` articulated assets. Their visible angle is derived from signed authoritative shaft RPM, so they spin in the opposite direction under astern command. Presentation never drives thrust.

### Production propeller presentation hierarchy

The canonical runtime GLB exposes `SM_Propeller_Port` and `SM_Propeller_Starboard` as transform-only semantic roots. Their visible hub/blade geometry lives in drawable child nodes. Game code therefore retains only the opaque production root binding; the Assets layer resolves its complete drawable subtree and Render applies the signed shaft rotation to every descendant. Child GLB names are not gameplay API, and the hierarchy must not be flattened into hard-coded blade/hub bindings in `PhysicalPlayground`.

Propeller articulation is explicitly re-based around the geometry-derived production `localOrigin` (hub centre) from the Antey sidecar, not around the imported transform-only root origin. Rotation therefore cannot translate/orbit or visually enlarge the assembly. The normal production camera uses an 8-degree presentation-only side yaw: world Y remains the screen vertical axis, while the small depth cant makes both real twin propellers volumetrically readable without changing simulation, collision, shaft orientation, or the canonical `Antey.glb`.

Acceptance explicitly covers both headless resolution of each semantic root to a non-empty drawable subtree and the normal windowed production render path; source-architecture guards must not depend on call-site whitespace or line wrapping.

## 2.5D turn-around

The Jolt body remains a 2.5D XY body: X/Y translation and Z pitch are physical; screen-depth translation/yaw is not promoted to a new simulation DOF. A separate Game-owned `longitudinalSign` tells the rest of gameplay whether the bow points screen-right (`+1`) or screen-left (`-1`).

Pressing `TurnAround` starts a **60 s GAME-POLICY** 180-degree visual turn. During the turn the production model yaws smoothly through the screen-depth axis. Longitudinal thrust is projected by `cos(pi * progress)`: it fades to zero at the visual 90-degree midpoint and grows with the opposite sign during the second half. Existing inertia and hydrodynamic drag remain authoritative, so neither heading nor velocity can snap-reverse.

Astern remains independent of turn-around: negative shaft thrust always acts backward relative to the current/transitioning longitudinal facing. Weapon employment is blocked while the carrier is mid-turn; once stable, launch-sector heading follows the final facing sign. This prevents a target on the old side from being fired upon as though the boat had already completed its turn.

The turn duration, reverse thrust/RPM and 2.5D projection are gameplay abstractions, not measured Project 949A turning-circle data.
