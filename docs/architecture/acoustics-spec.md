# DeepRun Acoustic Simulation — Technical Specification v0.1

Status: Accepted architecture contract

This document is the canonical specification for gameplay-relevant underwater
acoustics, sonar propagation, acoustic signatures and acoustic sensor
observations.

Engine-level ownership and integration boundaries are defined in
`docs/architecture/engine-spec.md`.

Perceived-world state, contacts, tracks and persistent signature fields are
defined in `docs/architecture/simulation-spec.md`.

<!-- BEGIN: acoustic-world-contract -->

## AcousticWorld contract

### Purpose

`AcousticWorld` is the authoritative gameplay simulation for underwater
acoustic propagation.

It SHALL provide a common acoustic environment for:

- submarines;
- surface vessels;
- torpedoes;
- missile-launch events;
- explosions;
- collisions;
- environmental sources;
- marine fauna;
- active sonar;
- passive sonar.

Sonar MUST NOT be implemented as a simple target-detection radius.

The acoustic simulation produces sensor observations.

It does NOT directly reveal authoritative target entities.

### Audio is not acoustic simulation

Gameplay acoustics and runtime sound playback are separate systems.

`AcousticWorld` determines whether and how an acoustic signal reaches a
receiver.

The audio system based on miniaudio determines what the player hears through
the output device.

Changing:

- speaker volume;
- audio device;
- mute state;
- audio sample;
- mixer settings

MUST NOT change gameplay detection.

Likewise, loss of an audio device MUST NOT disable sonar simulation.

### Acoustic signatures

Every gameplay-relevant acoustic source MAY expose a signature.

A signature SHOULD be capable of describing:

- emission type;
- source strength;
- center frequency;
- bandwidth;
- spectral distribution;
- directivity;
- duration;
- modulation or waveform identifier;
- source velocity;
- emission timestamp.

Exact military frequencies or classified acoustic signatures are explicitly
out of scope.

The game SHALL use authored gameplay values.

### Spectral model

The initial implementation SHOULD use a small number of spectral bands rather
than a continuous high-resolution frequency simulation.

A recommended starting representation is:

- very low;
- low;
- medium;
- high.

The exact band boundaries are tuning data and are not part of the permanent
engine API.

Different sources SHALL be allowed to have different spectral signatures.

Examples:

- submarine machinery noise;
- pump noise;
- turbine noise;
- propeller blade signature;
- hull-flow noise;
- cavitation;
- torpedo launch transient;
- torpedo propulsion;
- highly cavitating weapon;
- missile launch from a submerged platform;
- explosion;
- collision;
- active sonar pulse;
- whale vocalisation;
- sperm-whale clicks;
- dolphin vocalisation;
- rain;
- storm;
- distant shipping.

### Continuous sources

Continuous sources represent ongoing acoustic emissions.

Examples include:

- propulsion;
- machinery;
- propellers;
- hull flow;
- cavitation;
- biological calls;
- nearby shipping.

Continuous source output MAY vary as simulation state changes.

A submarine therefore does not have one fixed noise value.

Its signature may change because of:

- speed;
- propulsion state;
- damaged machinery;
- depth;
- cavitation;
- manoeuvring;
- authored equipment state.

### Transient sources

Transient sources represent finite events.

Examples include:

- explosion;
- collision;
- torpedo ejection;
- torpedo motor start;
- missile launch;
- structural failure;
- hull impact.

A transient acoustic event SHALL preserve its simulation emission timestamp.

The engine MUST NOT create a physical expanding rigid-body circle for every
sound event.

### Propagation delay

Acoustic propagation is not instantaneous.

Every accepted propagation path SHALL have a path length.

An arrival time SHALL conceptually be derived from:

`emission time + propagation time`

where propagation time depends on path length and effective sound speed.

The initial implementation MAY use a global or region-specific effective
sound speed.

Later implementations MAY vary it using authored environmental data such as:

- depth;
- temperature;
- salinity;
- acoustic layers.

Sensor processing MUST operate on the scheduled arrival time rather than
immediately receiving distant events.

### Transmission loss

Received signal strength SHOULD depend on a tunable approximation of:

- distance;
- geometric spreading;
- frequency-dependent absorption;
- environmental layers;
- terrain effects;
- surface interaction;
- bottom interaction;
- local masking/noise.

A full real-world underwater acoustic equation is not required.

The result must be:

- deterministic where required;
- data-driven;
- explainable;
- performant;
- suitable for gameplay tuning.

### Propagation paths

`AcousticWorld` MAY create a small bounded collection of candidate paths.

Initial path classes SHOULD include:

- direct;
- surface reflected;
- bottom reflected;
- terrain-attenuated;
- acoustic-layer-modified.

The number of secondary paths MUST be bounded.

The engine MUST NOT perform full finite-element or numerical wave-equation
simulation of the ocean.

### Surface reflection

The sea surface MAY generate a delayed and attenuated secondary arrival.

Surface interaction MAY depend on authored values such as:

- sea state;
- incidence angle;
- frequency band;
- weather.

The initial implementation may represent surface reflection using:

- path-length increase;
- attenuation;
- spectral modification.

### Bottom reflection

The seabed MAY generate a delayed and attenuated secondary arrival.

Bottom reflection MAY depend on broad authored terrain classes such as:

- rock;
- sand;
- sediment;
- artificial structure.

Exact geological acoustics are out of scope.

### Geometry and occlusion

Large world geometry MAY affect propagation.

Examples include:

- underwater ridges;
- cliffs;
- wrecks;
- structures;
- very large vessels.

Jolt or scene queries MAY provide coarse intersection information.

Jolt does NOT own acoustic propagation.

Sound MUST NOT be treated exactly like visible light.

A blocked direct path MAY become heavily attenuated rather than necessarily
becoming zero.

### Multipath

The same acoustic event MAY arrive at a receiver more than once.

Example:

- direct arrival;
- surface-reflected arrival;
- bottom-reflected arrival.

Each accepted arrival SHOULD preserve:

- arrival timestamp;
- received strength;
- spectrum/bands;
- path class.

Closely spaced secondary arrivals MAY later be combined by receiver logic.

### Reverberation

Powerful acoustic events MAY create reverberation.

Examples include:

- active sonar pulses;
- underwater explosions;
- major collisions;
- weapon launch transients;
- strong cavitating sources near terrain.

Reverberation MUST NOT require individually simulating every physical
reflection.

It SHOULD instead use a bounded representation such as a reverberation
envelope containing:

- start time;
- duration;
- decay rate;
- spectral character;
- surface contribution;
- bottom contribution;
- environmental contribution.

Reverberation MAY raise the temporary local noise floor and mask weaker
contacts.

### Ambient acoustic environment

The ocean MUST NOT be acoustically empty.

Regions MAY provide frequency-dependent ambient noise contributed by:

- sea state;
- rain;
- storms;
- shipping;
- biological activity;
- ice;
- authored machinery;
- other environmental events.

Detection SHALL depend on signal relative to local noise and masking, not
only on absolute source strength.

### Self-noise

A vessel SHALL contribute to its own sensor noise floor.

Self-noise MAY be produced by:

- propulsion;
- machinery;
- speed through water;
- propeller state;
- cavitation;
- damage;
- onboard transients.

Increasing speed SHOULD generally make passive acoustic detection more
difficult.

This relationship is intentional gameplay:

more speed -> more mobility -> more self-noise -> poorer passive awareness.

### Acoustic layers

The world MAY provide simplified underwater acoustic layers such as
thermoclines.

Crossing a layer MAY modify:

- transmission loss;
- effective spectral bands;
- active echo strength;
- observation confidence.

The initial implementation SHOULD use authored modifiers.

Full physical acoustic refraction is not required.

### Passive sonar

Passive sonar receives acoustic energy without intentionally transmitting an
active signal.

Passive detection SHOULD depend on:

- source acoustic signature;
- propagation loss;
- receiver sensitivity;
- receiver directivity;
- ambient noise;
- self-noise;
- masking;
- observation/integration time.

Passive sonar SHOULD generally obtain bearing more easily than exact range.

A passive observation MUST NOT automatically provide perfect target position.

### Active sonar

Active sonar emits a directional acoustic pulse.

An active pulse SHOULD expose:

- origin;
- orientation;
- beam or beam pattern;
- source strength;
- center frequency;
- bandwidth;
- pulse/waveform identifier;
- duration;
- emission timestamp.

An active echo requires:

1. outbound propagation from emitter to reflector;
2. target reflection;
3. return propagation from reflector to receiver.

Therefore active sonar has round-trip delay.

### Active transmission reveals the transmitter

The outgoing active pulse is itself an acoustic signal.

Another receiver MAY detect that outgoing pulse before the transmitting
submarine receives the returning echo.

This behaviour is required.

Active sonar therefore creates information for both sides.

### Echo model

A sonar reflector MAY expose a simplified acoustic reflection profile.

Echo strength MAY depend on:

- outbound propagation loss;
- target size;
- target aspect;
- target acoustic reflectivity;
- frequency;
- return propagation loss;
- receiver noise;
- reverberation.

Exact military target-strength data are out of scope.

### AcousticObservation

Acoustic receivers SHALL create observations rather than authoritative
enemy references.

An acoustic observation SHOULD be capable of containing:

- sensor identifier;
- observation timestamp;
- arrival timestamp;
- measured bearing;
- bearing uncertainty;
- optional estimated range;
- range uncertainty;
- received spectral energy;
- signal-to-noise estimate;
- pulse/waveform features;
- Doppler or radial-motion features when implemented;
- confidence;
- classification features;
- path/reverberation metadata.

Normal sensor consumers MUST NOT be handed the ground-truth source entity
identity.

### Acoustic classification

Observed acoustic features MAY contribute to classification probabilities.

Possible classifications include:

- unknown;
- biological;
- surface vessel;
- submarine;
- torpedo;
- strongly cavitating object;
- explosion;
- launch transient;
- collision;
- environmental source.

Classification is a sensor/track result.

A source MUST NOT leak its authoritative gameplay type merely by emitting an
event.

### Weapon signatures

Weapons participate in the same acoustic environment as every other source.

A torpedo MAY produce several different stages:

- launch/ejection transient;
- propulsion startup;
- running propulsion;
- increasing flow noise;
- cavitation;
- impact;
- detonation.

A highly cavitating weapon MAY deliberately produce an extreme broadband
signature.

Its detectability should emerge from that signature rather than from code
equivalent to:

`if super_cavitating_torpedo then reveal_to_enemy`.

### Submerged missile launch

A submerged missile launch MAY generate a powerful authored sequence of
acoustic transients.

The sequence MAY include signatures associated with:

- launch mechanism;
- gas/water displacement;
- structural transient;
- departure through the water;
- later authored events.

A remote receiver receives those signals according to normal propagation,
delay and sensor rules.

A strong launch transient does NOT automatically reveal perfect launcher
coordinates.

### Biological sources

Marine fauna MAY participate in the acoustic simulation.

Examples include:

- whales;
- sperm whales;
- dolphins;
- other authored marine animals.

Animals MAY:

- emit acoustic signatures;
- produce sonar contacts;
- create ambiguous classification;
- react to explosions;
- react to nearby vessels;
- react to authored sonar events.

Biological signals SHOULD be able to create uncertain or initially
misclassified contacts.

### Physics boundary

Jolt owns:

- rigid-body movement;
- collisions;
- physical contacts;
- geometry queries;
- ray/shape queries.

Jolt MAY help AcousticWorld obtain coarse geometry information.

Jolt does NOT:

- propagate sonar waves;
- determine acoustic detection;
- create contacts;
- classify signals.

A physical collision MAY create an acoustic event.

The acoustic consequence belongs to `AcousticWorld`.

### Runtime audio boundary

miniaudio owns audible presentation.

It MAY render:

- hydrophone audio;
- sonar operator sound;
- filtered received contacts;
- explosion audio;
- reflected/reverberated presentation effects.

miniaudio MUST NOT determine:

- propagation;
- detection;
- classification;
- AI knowledge;
- contact state;
- track state.

### Determinism

Acoustic events SHALL use simulation time.

Where deterministic simulation/replay is required:

- propagation;
- scheduled arrivals;
- observation uncertainty;
- classification noise

SHALL use deterministic simulation-owned data/random sources.

Presentation frame timing and audio-device timing MUST NOT affect gameplay
results.

### Performance contract

The acoustic simulation SHOULD scale through:

- spatial partitioning;
- emitter/receiver interest filtering;
- coarse frequency bands;
- bounded path counts;
- bounded reverberation representation;
- scheduled transient arrivals;
- fixed-rate sensor integration;
- reduced update rates for distant sources.

Explicitly rejected approaches include:

- one particle per sound wave;
- one rigid body per wavefront;
- per-audio-sample gameplay simulation;
- full CFD;
- full numerical acoustic wave simulation.

### Developer diagnostics

Developer tools SHOULD eventually visualize:

- acoustic emitters;
- source bands;
- active sonar beams;
- propagation candidates;
- direct paths;
- reflected paths;
- arrival times;
- received strength;
- ambient noise;
- self-noise;
- reverberation;
- acoustic observations.

Ground-truth diagnostic information MUST remain developer-only.

<!-- END: acoustic-world-contract -->
