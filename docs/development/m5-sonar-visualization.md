# M5 V2 — Perceived-world sonar visualization

Status: CANDIDATE pending Debug/Release CI and manual visual review.

The normal-play sonar scope is a read-only Game presentation layer. It consumes only the player's own navigation state, perceived `Track` values, the player's own `ActiveAcousticPulse`, and measured `AcousticObservation` echo evidence. It does not accept a hostile Transform, `PhysicsBodyHandle`, `AcousticReflector`, entity identity, or destroyer runtime state.

Bearing-only passive tracks remain bearing-only: the scope renders their relative bearing and angular uncertainty without inventing a range. A ranged/spatial contact appears only after perception owns an `estimatedPositionMeters`; displayed range is derived from that perceived estimate relative to ownship and retains `positionUncertaintyMeters` as an uncertainty ring. Lost tracks are removed from the scope.

The scope is bow-relative. Ownship heading comes from the production physical proxy plus the 2.5D `gameplayLongitudinalFacingSign`, so completion of TurnAround naturally flips the sonar frame without changing the underlying perceived Track. An outgoing active ping is rendered from the player's own pulse direction and beam width. A returned echo is rendered only from the measured active `AcousticObservation` bearing/range/uncertainty and ages out of presentation after a short bounded interval; it does not mutate Track state.

Headless M5 regression checks cover bearing-only preservation, perceived-position range derivation, ownship-relative bearing, own-pulse/echo projection, echo ageing, and Lost-track removal. Acceptance additionally requires the standard Windows Debug and Release configure/build/CTest/windowed-smoke gate plus manual verification that the scope remains readable and does not overlap the existing combat/navigation UI.
