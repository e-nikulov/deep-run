# DeepRun Documentation Map

Status: Canonical specification taxonomy

DeepRun documentation uses four independent classification axes:

```text
M = what and when we implement
A = how the system is technically structured
D = how the game plays
C = what content fills the game
```

The axes are not one shared sequence. `M2`, `A2`, `D2`, and `C2` have no
automatic relationship to one another.

## M — Milestones

`M0 -> M1 -> M2 -> ...` is the only mandatory sequential scale.

A milestone states what must be implemented and verifiable at a development
stage. It owns scope, Definition of Done, and acceptance criteria. The
canonical roadmap is [roadmap/milestones.md](roadmap/milestones.md).

A milestone may reference any number of A, D, and C specifications. For
example, M2 may use A0 and A1, implement part of D1, and require an applicable
C package. Such references do not couple the numbers of the four axes.

## A — Architecture specifications

`A0`, `A1`, `A2`, ... identify technical contracts and invariants. They do not
define implementation order and may be written before the milestone that
fully realizes them.

One specification ID may have one primary document and one or more specialized
contracts. A specialization refines part of the same specification; it does not
receive another ID merely because it is stored in a separate file.

Current registry:

| ID | Specification | Primary document | Specialized contracts |
|---|---|---|---|
| A0 | Engine Architecture Baseline | [architecture/engine-spec.md](architecture/engine-spec.md) | [architecture/rendering-spec.md](architecture/rendering-spec.md) |
| A1 | Simulation Boundary | [architecture/simulation-spec.md](architecture/simulation-spec.md) | [architecture/acoustics-spec.md](architecture/acoustics-spec.md) |

Accepted decisions under [adr/](adr/) support the architecture specifications;
they are not additional A-IDs by themselves.

A0 preserves this dependency direction:

```text
Game -> Simulation -> Engine -> Platform
```

Dependencies point downward. Engine must not know DeepRun domain concepts
such as submarines, sonar, acoustics, torpedoes, crew, compartments, or the
campaign. New A-IDs are created only for independent architectural questions,
not to fill the numbering.

## D — Design specifications

`D0`, `D1`, `D2`, ... identify player-facing gameplay and design contracts.
They describe how the game plays, feels, and communicates rules, not milestone
scope or particular C++ classes.

The same primary-document and specialization rule applies to design
specifications.

Current registry:

| ID | Specification | Primary document | Specialized contracts |
|---|---|---|---|
| D0 | Core Game Design / Game Loop | [design/game-loop.md](design/game-loop.md) | [design/submarine-command.md](design/submarine-command.md) |
| D1 | Controls & Handling | [design/controls.md](design/controls.md) | None |

D1 preserves this boundary:

```text
Physical Input
    -> Engine Input
    -> Game Commands
    -> Simulation
```

`Throttle`, `Depth`, and other gameplay commands are not raw Engine Input.
New D-IDs are added only when a distinct design contract is ready to be
specified; possible future subjects include sonar gameplay, combat,
crew/damage, and progression.

## C — Content specifications

`C0`, `C1`, `C2`, ... identify logically complete content specifications or
packages. They describe the concrete visual, audio, data-driven, and world
content required by the game: models, textures, materials, UI artwork, VFX,
particles, audio, ambience, music, props, and coherent vessel, weapon, or
environment sets.

A C-ID belongs to a package such as `Player Submarine Prototype`, which may
require a model, textures, materials, sounds, VFX, an icon, and related data
definitions. It does not belong to each individual asset such as
`antey_hull.glb`, `antey_albedo.dds`, or `propeller_loop.wav`.

Current registry:

| ID | Package | Primary document | Supporting contract |
|---|---|---|---|
| C0 | Player Submarine package: prototype + Antey/P700 canonical production assets | [content/submarine-prototype.md](content/submarine-prototype.md); [content/antey-asset.md](content/antey-asset.md); [content/p700-asset.md](content/p700-asset.md) | [content/asset-pipeline.md](content/asset-pipeline.md) |

## Content specifications, authored content, and asset runtime

The repository uses three distinct concepts:

| Path | Responsibility |
|---|---|
| `docs/content/` | Text C-specifications describing which logical content package must be created |
| `Content/` | Editable/source game assets and authored data definitions |
| `Engine/Assets/` | Runtime asset code plus runtime-ready assets in asset-specific subdirectories |

`docs/architecture/engine-spec.md` is the canonical source for the repository
structure and the concrete `Content/Models/`, `Content/Textures/`,
`Content/Audio/`, `Content/Materials/`, `Content/Submarines/`, `Content/Ships/`,
`Content/Weapons/`, and `Content/Encounters/` paths. This taxonomy does not
introduce a parallel top-level content tree.

An authored asset is a concrete file. A content specification is the coherent
package that explains which assets and associated data are required and why.

## Responsibility example: sea mine

| Axis | Responsibility |
|---|---|
| C | Model, texture, explosion VFX, and sounds |
| D | Gameplay purpose, detection distance, threat level, and counterplay |
| A / Simulation | World representation, collision, detection interaction, and damage event |
| M | The vertical slice in which the mine becomes implemented and verifiable |

The same separation applies to submarines, torpedoes, sonar presentation, and
other game objects.

Specification IDs are metadata, not filename prefixes. Keep semantic filenames
such as `engine-spec.md`, `controls.md`, and `game-loop.md`, and do not break
links merely to add an ID.

## Legal and provenance records

Repository-level copyright and dependency-notice policy is recorded in
[`../COPYRIGHT.md`](../COPYRIGHT.md) and
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Content provenance,
shipping gates, and the canonical metadata fields for external assets are
defined in [content/asset-provenance.md](content/asset-provenance.md). These
records document status; they do not grant rights or replace legal review.
