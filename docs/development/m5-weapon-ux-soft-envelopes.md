# M5 weapon UX: automatic readiness and soft range envelopes

## Player interaction contract

Weapon selection is always available. Selecting another weapon immediately changes the selected fire-control profile and automatically starts that weapon's preparation timer. The player does not issue a separate `Prepare Weapon` command. The obsolete keyboard/mouse/controller preparation input has been removed as well, so there is no hidden second preparation step in the input layer.

A projectile that has already been launched owns an independent runtime and continues its flight after the player changes the selected weapon. Its launch-time weapon definition is captured with the projectile, so changing the selected torpedo profile cannot mutate the in-flight weapon's kinematics, identity or endurance. Resolution of that old projectile must not reset or cancel the newly selected weapon's preparation/readiness state. A torpedo impact remains observable through its authoritative impact frame, then the spent projectile is cleared on the following fixed step; range/endurance expiry clears immediately.

## Range contract

`minimumTargetRangeMeters` and `maximumTargetRangeMeters` describe the preferred/nominal employment band. They inform the player and affect effectiveness, but they do not lock the trigger.

A shot inside the preferred minimum range is allowed. For P-700, a too-close launch gives a defended surface combatant an increased terminal-defense opportunity because the missile has less distance to establish its preferred flight profile. Conventional torpedoes retain their straight-run and seeker geometry, so very short shots can be poor acquisition/overshoot choices.

A shot beyond nominal maximum range is also allowed. Runtime travel/endurance budgets remain authoritative: a torpedo or P-700 that cannot physically cover the requested distance expires before intercept and is lost.

Depth, carrier speed, launcher sector, target-domain constraints, inventory, track quality and rules of engagement remain hard constraints unless explicitly changed by another gameplay contract.

## P-700 carrier-depth contract

Deep Run permits P-700 launch only from a submerged Antey. The current gameplay envelope is 10–50 m launch depth. The lower bound is a conservative presentation/gameplay policy to prevent a visually surfaced boat from launching; the 50 m maximum remains the existing submerged launch limit.

## Regression coverage

M5 player-controlled combat checks cover automatic initial preparation, fire after readiness, and changing the selected weapon while a previously launched torpedo remains in flight. P-700 acceptance automation follows the same contract and no longer depends on a manual preparation command. The legacy automated M5 combat composition remains a deterministic one-shot acceptance scenario, so automatic re-arm after its resolved test round cannot trigger an unintended second player launch.
