

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

<!-- END: detection-track-signature-contract -->
