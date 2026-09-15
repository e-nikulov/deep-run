# M5 weapon UX: automatic readiness and soft range envelopes

## Player interaction contract

Weapon selection is always available. Selecting another weapon immediately changes the selected fire-control profile and automatically starts that weapon's preparation timer. The player does not issue a separate `Prepare Weapon` command.

A projectile that has already been launched owns an independent runtime and continues its flight after the player changes the selected weapon. Resolution of that old projectile must not reset or cancel the newly selected weapon's preparation/readiness state.

## Range contract

`minimumTargetRangeMeters` and `maximumTargetRangeMeters` describe the preferred/nominal employment band. They inform the player and affect effectiveness, but they do not lock the trigger.

A shot inside the preferred minimum range is allowed. For P-700, a too-close launch gives a defended surface combatant an increased terminal-defense opportunity because the missile has less distance to establish its preferred flight profile. Conventional torpedoes retain their straight-run and seeker geometry, so very short shots can be poor acquisition/overshoot choices.

A shot beyond nominal maximum range is also allowed. Runtime travel/endurance budgets remain authoritative: a torpedo or P-700 that cannot physically cover the requested distance expires before intercept and is lost.

Depth, carrier speed, launcher sector, target-domain constraints, inventory, track quality and rules of engagement remain hard constraints unless explicitly changed by another gameplay contract.

## P-700 carrier-depth contract

Deep Run permits P-700 launch only from a submerged Antey. The current gameplay envelope is 10–50 m launch depth. The lower bound is a conservative presentation/gameplay policy to prevent a visually surfaced boat from launching; the 50 m maximum remains the existing submerged launch limit.
