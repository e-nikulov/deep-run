# M5 V2 — Perceived-world sonar visualization

Status: ACCEPTED. Final Debug/Release milestone gate passed in CI `34712161730` (#488) on `851a922dd4a2f4055dba523d3dc1a7773dad4ed2`.

The normal-play sonar scope is a read-only Game presentation layer. It consumes only the player's own navigation state, perceived `Track` values, the player's own `ActiveAcousticPulse`, and measured `AcousticObservation` echo evidence. It does not accept a hostile Transform, `PhysicsBodyHandle`, `AcousticReflector`, entity identity, or destroyer runtime state.

Bearing-only passive tracks remain bearing-only: the scope renders their relative bearing and angular uncertainty without inventing a range. A ranged/spatial contact appears only after perception owns an `estimatedPositionMeters`; displayed range is derived from that perceived estimate relative to ownship and retains `positionUncertaintyMeters` as an uncertainty ring. Lost tracks are removed from the scope.

The scope is bow-relative. Ownship heading comes from the production physical proxy plus the 2.5D `gameplayLongitudinalFacingSign`, so completion of TurnAround naturally flips the sonar frame without changing the underlying perceived Track. An outgoing active ping is rendered from the player's own pulse direction and beam width. A returned echo is rendered only from the measured active `AcousticObservation` bearing/range/uncertainty and ages out of presentation after a short bounded interval; it does not mutate Track state.

Headless M5 regression checks cover bearing-only preservation, perceived-position range derivation, ownship-relative bearing, own-pulse/echo projection, echo ageing, and Lost-track removal. Acceptance additionally requires the standard Windows Debug and Release configure/build/CTest/windowed-smoke gate plus manual verification that the scope remains readable and does not overlap the existing combat/navigation UI.

The user-facing `Sonar visualization: On / Off` presentation option defaults to `On`. Turning it off suppresses only sonar drawing; acoustic propagation, pulse/echo timing, observations, TrackManager state and weapon targeting/guidance continue unchanged.

## Authentic bearing-scope presentation

The normal-play scope presents passive acoustic awareness as a full 360-degree, bow-relative bearing display. This is a presentation of the existing coarse passive receiver model: the simulation already accepts direct-path passive evidence from any azimuth and still applies ambient noise, own-ship self-noise, sensitivity and SNR thresholds. No fabricated hard aft blind zone or classified MGK-540 directivity is introduced.

The scope uses submarine-relative cardinal bearings: `0° / BOW` is up, `90° / STBD` is right, `180° / AFT` is down and `270° / PORT` is left. Passive evidence is shown as `PAS/BRG`: a line-of-bearing sector with angular uncertainty and no invented range. A contact that owns a perceived range/spatial estimate is shown as `RNG` with its position uncertainty. The current measured active echo remains a distinct orange `ACTIVE ECHO` mark, while the outgoing active pulse remains a steered sector rather than an omnidirectional radar sweep.

This distinction is intentional: passive sonar listens without emitting and initially provides bearing-quality evidence; active sonar deliberately transmits into the selected perceived bearing, exposes the submarine acoustically, and may return bounded range evidence. The UI never converts a passive-only bearing into a fake range. Ranged tracks are labelled generically as `RNG` because a perceived range can also come from another legitimate sensor path such as a resolved periscope/stadimeter observation; only the orange echo marker is explicitly active-sonar evidence.
