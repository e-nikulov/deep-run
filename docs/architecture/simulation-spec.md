# DeepRun Detection and Signature Simulation — Technical Specification v0.1

Status: Accepted architecture contract

Specification: A1 — Simulation Boundary

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. A1 is an architecture contract, not a milestone or an
implementation-sequence position.

This document is the canonical specification for perceived-world state,
sensor observations, contacts, tracks, persistent signatures and hydrodynamic
wake representation.

Gameplay acoustic propagation is defined in
`docs/architecture/acoustics-spec.md`.

Engine-level ownership and integration boundaries are defined in
`docs/architecture/engine-spec.md`.

<!-- BEGIN: detection-track-signature-contract -->

## Detection, persistent signatures and track knowledge

### Core knowledge rule

The simulation SHALL separate real world state from perceived world state.

The canonical information flow is:

Ground truth
-> signatures
-> propagation/environment
-> sensor observations
-> contacts
-> tracks
-> UI / AI / weapon knowledge.

Normal gameplay systems MUST NOT treat:

`target exists in sensor radius`

as equivalent to:

`target is known`.

### Ground truth

Ground truth contains authoritative simulation state such as:

- entity position;
- velocity;
- depth;
- orientation;
- physical type;
- physical state.

Ground truth is available to simulation internals.

It is NOT automatically available to:

- hostile AI decision making;
- player UI;
- weapon targeting;
- contact classification;
- normal gameplay logic.

Developer diagnostics and tests MAY compare estimated state with ground truth.

### SensorObservation

Sensors produce observations.

Observations are evidence.

They are not targets.

Different sensor types MAY produce different information.

Examples:

- passive sonar may primarily produce bearing;
- active sonar may provide stronger range information;
- optics may provide visual classification;
- wake sensors may reveal a recent passage rather than current position.

Observations SHALL preserve uncertainty.

### Contact

A `Contact` represents accumulated evidence that an unknown object, source or
trace exists.

A contact MAY initially be classified as:

- unknown;
- possible biological;
- possible vessel;
- possible submarine;
- possible weapon;
- transient;
- wake/trace;
- environmental event.

A Contact MUST NOT require perfect ground-truth identity.

### Track

A `Track` is a fused estimate of the state of a contact.

A track SHOULD support:

- track identifier;
- estimated position;
- estimated velocity;
- optional estimated depth;
- positional uncertainty;
- bearing/range uncertainty;
- classification probabilities;
- confidence;
- track quality;
- first observation time;
- last observation time.

A track is NOT the target entity.

### Track lifecycle

Suggested states include:

- tentative;
- confirmed;
- coasting;
- weak;
- lost;
- dropped.

When observations stop, a track MAY continue to coast.

While coasting:

- estimated motion continues;
- uncertainty increases;
- confidence decreases.

A new observation MAY reacquire or correct the track.

### No omniscient AI

Hostile AI SHALL make tactical decisions using its observations, contacts and
tracks.

It SHALL NOT obtain the player's current Transform merely because the player
exists in the simulation.

Difficulty MAY modify:

- sensor sensitivity;
- observation integration time;
- classification quality;
- uncertainty;
- track retention;
- operator skill.

Difficulty MUST NOT silently replace the sensor model with omniscience except
in explicit debug/testing modes.

### Symmetry

The player and hostile units participate in the same signature and detection
architecture.

The player's vessel MAY reveal itself through:

- propulsion;
- machinery;
- speed;
- cavitation;
- active sonar;
- torpedo launch;
- torpedo propulsion;
- missile launch;
- explosions;
- collisions;
- persistent wake;
- surface disturbance;
- optical mast/periscope exposure.

The same categories apply to non-player vessels.

### SignatureFieldWorld

Not every observable trace is an instantaneous acoustic event.

Persistent world traces SHALL use a simulation system conceptually equivalent
to `SignatureFieldWorld`.

Persistent fields MAY include:

- hydrodynamic wake;
- bubble/cavitation residue;
- surface disturbance;
- thermal trace;
- other future authored signature channels.

`SignatureFieldWorld` does NOT reveal the source entity directly.

Sensors observe the field and produce observations from it.

### Hydrodynamic wake

A moving vessel MAY leave a persistent hydrodynamic wake.

The wake SHOULD be capable of carrying several independent authored
signature channels:

- turbulence;
- bubble/cavitation residue;
- acoustic disturbance;
- surface disturbance;
- visual disturbance;
- future thermal or chemical channels.

The presence of a wake does not mean the originating vessel is still at that
position.

### Sparse wake representation

The wake MUST NOT be simulated using millions of water particles.

A vessel SHOULD instead leave bounded trail segments.

A conceptual wake segment contains:

- position;
- direction;
- width;
- creation timestamp;
- age;
- intensity per signature channel;
- local current influence.

Old segments decay.

Segments below a configured intensity threshold are removed.

Maximum segment count and trail lifetime MUST be bounded.

### Wake decay

Wake strength SHALL decrease over simulation time.

Decay MAY depend on authored environmental state such as:

- sea state;
- depth;
- currents;
- vessel speed;
- cavitation;
- vessel size.

Exact fluid dynamics are out of scope.

### Wake advection

Environmental current MAY move or distort wake trail segments after the
vessel has passed.

This permits the observed trace to differ from the exact historical vessel
path.

Full CFD is not required.

### Wake observation

Different sensors MAY observe different wake channels.

Examples:

- passive acoustic systems may observe remaining acoustic disturbance;
- active sonar may react to authored bubble/turbulence effects;
- periscope/optics may observe a surface wake;
- future sensors may observe other channels.

A wake observation SHOULD provide evidence such as:

- trace position;
- approximate direction;
- estimated age;
- trace strength;
- uncertainty;
- possible classification.

It MUST NOT automatically provide the current location of the source vessel.

### Following a wake

A vessel may lose direct contact with another vessel while retaining evidence
of its recent path.

The detection system MAY therefore estimate:

- previous direction;
- approximate trace age;
- probable continuation region.

Track management MAY use that evidence to update or reacquire a track.

The estimated continuation remains uncertain.

### Manoeuvre deception

Because wake observations describe historical passage rather than current
ground truth, a vessel can create misleading extrapolation naturally.

For example:

1. a submarine travels east;
2. it leaves a detectable wake;
3. it changes course sharply;
4. an observer later detects the older eastbound wake;
5. the observer extrapolates an eastbound probable search region;
6. the submarine is no longer on that course.

No special scripted deception mechanic is required.

It emerges from imperfect information.

### Sensor fusion

Multiple observation channels MAY update the same contact/track.

Potential inputs include:

- passive acoustic observations;
- active-sonar observations;
- wake observations;
- optical observations;
- weapon sensors;
- future environmental sensors.

Sensor fusion MUST preserve uncertainty.

One bearing-only observation MUST NOT magically generate perfect range.

### Weapon targeting boundary

Weapons SHOULD consume track/targeting information appropriate to their
guidance model.

They MUST NOT receive authoritative enemy ground truth solely because a
contact exists.

Different weapons MAY require different minimum track quality.

### Developer diagnostics

Developer diagnostics SHOULD eventually expose:

- ground-truth position;
- estimated track position;
- uncertainty region;
- observation history;
- contact classification;
- track confidence;
- wake segments;
- wake age;
- wake intensity;
- predicted/coasting track path.

Ground-truth overlays MUST remain outside normal gameplay knowledge.

## Shared authoritative simulation contracts

### Simulation time

All authoritative A1 state advances from `SimulationTime` as defined in A0 and
ADR-0009. This includes propagation arrivals, sensor integration, contact/track
ageing, persistent signatures, wake decay, AI perception memory and decisions,
weapon phases, damage, flooding, atmosphere, crew-task progress, repairs, and
gameplay-relevant mission timers.

Time compression executes additional fixed simulation progress per unit of
`RealTime`; it does not enlarge the physics timestep. Reduced-rate simulation
tiers may skip work only through deterministic scheduling based on simulation
state. Presentation or audio-device timing must not affect outcomes.

### World and environment ownership

World/environment data must allow the same authored feature to participate
independently in several representations:

```text
authored world feature / stable identifier
    +-> render representation
    +-> coarse Jolt collision/query representation
    +-> navigation constraint representation
    +-> AcousticWorld terrain/material query representation
    `-> sensor-occlusion approximation where applicable
```

The representations may share stable IDs, bounds, and authored metadata. They
do not have to share mesh topology or update rate. Raw render vertices are not
stable gameplay identifiers.

The environment contract supports seabed, rock formations, ridges, cliffs,
drop-offs, trenches, large formations, underwater ice, ice shelves, icebergs,
and the ocean/water-surface boundary. Jolt owns physical collision and geometry
queries. `AcousticWorld` may consume coarse terrain queries but remains the sole
owner of underwater acoustic propagation and attenuation/reflection
approximations.

`WaterBody` remains the authoritative source for gameplay water-level, signed
depth, and density queries. Visual surface displacement cannot silently replace
that state. When Gerstner or other CPU-queryable surface motion becomes relevant
to floating-body gameplay, Simulation owns the authoritative query model and
publishes a compatible render snapshot.

Logical operating areas remain on the order of `100 x 100 km`, with near, mid,
and strategic simulation tiers defined by A0. Environment records should have
stable chunk-compatible IDs and bounds so future streaming can be added without
requiring production streaming in M3. Full CFD, particle oceans, and full-world
maximum-detail simulation are excluded.

### Flora

Initial kelp, seaweed, environmentally appropriate seagrass, and benthic growth
are presentation-first content. They are not individual rigid bodies or an
ecosystem simulation. Only authored large/gameplay-relevant flora may expose an
optional coarse collision or query representation. Rendering may instance or
batch flora without affecting authoritative state.

### Marine fauna and surface birds

The architecture reserves lightweight authored behaviour for whales, sperm
whales, dolphins, fish/small schools, gulls, and pelicans. An entity may opt into
independent capabilities:

- ambient presentation/animation;
- AcousticWorld emissions;
- uncertain sonar observations and contacts;
- reactions to nearby vessels, explosions, or active sonar;
- mission/event participation.

Possible future behaviours include whale surfacing/diving, sperm-whale click
activity, dolphin pods near the surface, schooling-fish presentation, birds over
water, and reactions to major surface disturbances. This is not a requirement
for complex animal AI.

Whales, sperm whales, and dolphins may emit gameplay-relevant acoustic
signatures, but their identity follows the ordinary pipeline:

```text
biological source signature
    -> propagation
    -> observation
    -> contact
    -> uncertain classification / track
```

No sensor consumer receives a definitive whale/dolphin entity type merely
because Simulation knows it. Surface birds normally remain lightweight
presentation entities unless a later authored mission gives them a concrete
gameplay role.

## Tactical AI contract

Normal hostile AI uses a role-scoped knowledge model built from observations,
contacts, and tracks. It must not read the player's authoritative Transform or
other hostile ground truth simply because that entity exists.

The future AI boundary is conceptually layered:

```text
perception evidence
    -> local belief / track memory
    -> tactical state
    -> explainable utility or authored decision selection
    -> role logic
    -> navigation / manoeuvre / weapon-employment requests
    -> group coordination where the milestone requires it
```

The accepted A0 direction is Hierarchical State Machines + Blackboard +
Perception + Tactical Director. This consolidation preserves that repository
decision while leaving concrete class names and data layout to implementation.
Runtime AI should be data-driven, deterministic enough for replay/tests,
explainable, tunable, cheap, and headless-capable. Shipping tactical gameplay
must work fully offline and must not require an LLM. LLMs may be used only in
offline authoring, testing, scenario generation, or development tools.

Difficulty may tune sensor sensitivity, operator competence, observation
integration, classification quality, uncertainty, reaction time, track
retention, and decision quality. It must not silently grant omniscience outside
explicit developer/debug modes.

Future role logic may cover enemy submarines, surface ASW/patrol/escort vessels,
ASW helicopters, fixed-wing ASW aircraft, tactical groups, and appropriate
static/semi-static threats. M5 introduces only the simple combat AI required by
its bounded torpedo slice. Advanced coordination, tactical-group behaviour, and
specialized combined-force roles remain later warfare work.

## Weapon architecture contract

### Data and ownership boundaries

Weapons use four independent representations:

```text
content/source asset
    geometry, authored configurations, nodes, pivots, animations, markers

weapon definition
    authored gameplay parameters, phase graph, guidance/payload/signature data

runtime weapon entity
    authoritative phase, movement domain, kinematics, guidance knowledge,
    payload state, damage state, and SimulationTime

presentation state
    render animation, VFX, audio, camera feedback, and haptics derived from the
    runtime entity
```

Presentation never owns weapon truth. A content animation is not an
authoritative phase transition. Stable authored nodes/markers/hardpoints are
consumed by definitions/runtime; arbitrary mesh vertices are not identifiers.

Reusable concepts may include `WeaponDefinition`, `WeaponRuntimeState`,
`WeaponPhase`, `MovementDomain`, `MediumTransition`, `WeaponGuidance`,
`WeaponSignatureSet`, and `WeaponPayload` when concrete weapons share them.
Avoid one giant type with per-weapon conditional branches.

A definition selects only phases it requires from a vocabulary such as:

```text
Stored -> Prepared -> Launch -> Ejection -> UnderwaterTravel
       -> SurfaceTransition -> AirborneTravel -> Search -> Terminal
       -> Impact -> Detonation -> Spent / Destroyed
```

The vocabulary is not a mandatory single state machine. A mine, conventional
torpedo, decoy, and missile may use different authored phase graphs.

Runtime weapons may participate in Jolt/marine motion, AcousticWorld and
persistent signatures, observations/contacts/tracks, AI, damage, and derived
VFX/audio. Jolt remains generic and does not contain weapon-specific phase or
guidance logic.

Guidance consumes only the targeting/track knowledge appropriate to that
weapon's seeker and data link. A `Track` does not authorize access to the
tracked entity's ground truth. Values are authored for gameplay; classified or
precise real-world performance is not required.

### Movement domains and medium transitions

Movement domains may include stored/attached, underwater, surface-transition,
airborne, and spent/inactive states. A medium transition is an authoritative
runtime event, not a VFX trigger pretending to be physics.

Water entry/exit is determined from `WaterBody` or its accepted successor's
authoritative surface query. The same runtime entity should survive a transition
where practical, switching to the authored domain model at a deterministic
transition event. Underwater motion may combine Jolt, buoyancy, drag, thrust,
and authored stability; airborne motion may use gravity, thrust, simplified
drag/lift/control, and authored guidance. Full CFD is not required.

Transition outputs such as plume, spray, surface disturbance, ignition, camera
feedback, acoustic transient, persistent signature, audible effect, and haptic
feedback are independent consumers. Underwater-relevant events enter
`AcousticWorld`; airborne audible presentation does not automatically become an
underwater gameplay signal.

The same abstraction also permits air-to-water dropped ASW weapons and
deployable sonobuoys without placing aircraft or weapon logic in `WaterBody` or
generic `PhysicsWorld`.

### Torpedo families

The weapon-definition/phase model must support at least:

- a conventional heavyweight torpedo;
- a smaller/lighter torpedo or compact underwater weapon;
- a high-speed supercavitating archetype inspired by VA-111 Shkval.

A conventional lifecycle may cover tube preparation, ejection, propulsion
startup, underwater travel, guidance/search, terminal run, impact, and
detonation. Its signature set may independently author ejection, motor start,
running propulsion, flow noise, cavitation, terminal noise, impact, and
explosion.

A supercavitating definition may express extreme speed, high signature, strong
cavitation, a bubble/cavity presentation, different manoeuvrability and guidance
constraints, and large self-noise. Detectability emerges from ordinary
signature/propagation/sensor processing; there is no special reveal-to-everyone
rule.

### P700 content and future runtime

The accepted P700 content contract is canonical in
`docs/content/p700-asset.md`. It already contains STOWED and DEPLOYED geometry,
validated moving-surface pivots, four LODs, markers/metadata, and the
`P700_Deploy` GLB animation. This A1 contract does not rename or recreate them.

Later warfare owns the P700 weapon definition and runtime entity. Its authored
phase graph may include stored, prepared, launch/ejection, underwater departure
and ascent, authoritative surface intersection/breach, airborne transition,
powered flight, terminal behaviour, impact, and detonation. The current asset
remains stowed through launcher exit/underwater launch as specified by C0;
runtime weapon state determines when presentation consumes `P700_Deploy`.

### Hostile weapons and ASW threats

The generic architecture may later represent enemy torpedoes; bottom, moored,
or influence mines; anti-submarine nets; depth-charge-like and dropped ASW
weapons; sonobuoys; surface ASW vessels/patrol boats; enemy submarines;
helicopters; fixed-wing aircraft; and contextual explosive hazards.

- Mine triggers require an authored physical/signature/sensor mechanism and do
  not identify the player omnisciently.
- Sonobuoys are deployable sensor entities whose detections enter the ordinary
  observation/contact/track pipeline.
- Aircraft use an airborne movement domain; their underwater sensing still uses
  the shared perception model.
- Anti-submarine nets are authored world obstacles with optional later coarse
  collision/damage/entanglement, not a separate simulation world.
- Explosive environmental objects reuse damage, explosion, and signature
  contracts and are not required in every mission.

## Submarine systems and damage-control boundary

The detailed player-facing contract is defined in
`docs/design/submarine-command.md`; implementation remains M6 and systemic proof
remains M7.

Simulation owns authoritative physical/environmental compartment state:
boundaries and access constraints, breach/flood rates and water volume,
pressure effects where relevant, fire, smoke/contaminants, heat, O2/CO2 and
other habitability values. Game owns submarine-specific system definitions,
crew identities/roles/qualifications, task selection, team formation, and
validated player/AI commands. UI only issues commands and displays derived
knowledge; it never owns the access graph or writes state directly.

Flooding, atmosphere, fire/smoke spread, pumping, repair progress, and crew work
are rate/state based over `SimulationTime`. Continuous state is not resolved by
per-frame random rolls. Seeded simulation-owned randomness may resolve discrete
events such as a damaged closure jamming, hazardous-access injury, or an
improvised repair outcome.

Authoritative boundaries may be open, closed/sealed, leaking, jammed, damaged,
or destroyed. Boundary and hazard state derives the access graph. Closing a
boundary may slow flooding/fire/smoke while isolating crew or equipment;
reopening may restore rescue/repair access while reintroducing spread risk.

Crew records may include identity, rank, specialty, qualifications, health,
availability, compartment, assignment, protection, readiness/fatigue, and
isolation. Production interaction is team/role/watch oriented, with automatic
best-qualified, available, reachable, and protected team proposals plus optional
override; it does not require manual movement of hundreds of individuals.

Repairs may require qualifications, personnel, access, tools/protection, time,
and surviving equipment, with proper, degraded/bypass, temporary stabilization,
containment-only, or failure outcomes. Crew loss/isolation must affect actual
capability.

Electrical state represents authored generation, distribution, damaged
circuits/systems, local availability, and restoration/bypass. It is not a
universal power-mana resource. It may disable/degrade pumps, sensors, lighting,
equipment, or other authored loads and interacts with rather than replaces
damage control.

## Headless and diagnostic boundary

Authoritative time compression, acoustics, perception, AI, weapon phases,
medium transitions, damage, flooding, pumping, atmosphere, access, crew tasks,
and repairs must remain headless-capable. Simulation-owned deterministic random
sources are required where replay/test determinism matters; render frame timing
cannot change outcomes.

Developer diagnostics may compare ground truth with observations, tracks, AI
beliefs, weapon state, terrain/acoustic queries, and compartment/system state.
Ground truth remains excluded from ordinary player, AI, and weapon knowledge.

<!-- END: detection-track-signature-contract -->
