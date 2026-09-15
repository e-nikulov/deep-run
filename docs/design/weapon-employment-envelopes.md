# Weapon employment envelopes

Status: gameplay contract for M5/IG2. These values describe two different things and must not be conflated:

- **hard launch constraints** — carrier depth/speed, launcher sector, target domain, inventory, perceived-Track quality and other physical/ROE conditions that can genuinely prevent launch;
- **preferred/nominal range bands** — minimum/maximum target range used for player guidance and effectiveness modelling. Range alone does **not** lock the trigger.

This document is a gameplay/engineering contract, not a technical firing manual and does not claim access to classified fire-control data.

## Runtime contract

USET-80, 65-76A fast/economy and P-700 use the common employment evaluator before launch. `WeaponEmploymentAssessment::allowed` represents the hard launch decision, while `rangeOptimal` reports whether the perceived target lies inside the preferred/nominal range band.

A player may fire inside the preferred minimum range or beyond the nominal maximum range. The HUD must warn about the poor employment geometry rather than report `REJECTED` solely because of range.

The physical weapon runtime remains authoritative after launch. The three torpedo profiles carry travelled-distance budgets of 18/50/100 km and P-700 carries a 550 km travel budget. A deliberately over-range shot can therefore exhaust its travel/endurance budget, transition to `RangeExpired`, and be lost before reaching the target. Short-range torpedo shots keep their straight-run/seeker geometry and can suffer poor acquisition/overshoot geometry. Short-range P-700 shots give a defended combatant a better terminal-defense opportunity in Deep Run gameplay because the missile has less distance to establish its preferred flight profile.

Employment checks consume authoritative own-ship position/orientation/depth/speed plus the selected **perceived Track estimate**. Hostile ground-truth transforms are forbidden as launch authority. A failed **hard** constraint must leave the weapon unlaunched and return a readable reason; a range-only assessment remains launchable and returns a non-optimal warning.

The implementation deliberately separates three kinds of values:

- **OPEN-SOURCE** — range/depth/carrier-speed figures repeatedly published in public references.
- **GAME POLICY** — conservative boundaries or effectiveness rules used by Deep Run where no reliable public fire-control value exists, or where the game intentionally chooses a stricter interaction contract.
- **PROFILE POLICY** — a public weapon with multiple speed/range modes is represented by explicit profiles rather than silently changing its range.

## USET-80 (533 mm)

| Limit | Deep Run value | Meaning / basis |
| --- | ---: | --- |
| Minimum launch depth | 10 m | **Hard**, GAME POLICY: keeps tube/weapon clearly submerged. |
| Maximum launch depth | 400 m | **Hard**, OPEN-SOURCE weapon-specific figure. |
| Preferred minimum target range | 0.5 km | **Soft**, GAME POLICY. Shorter shots remain fireable but have poor straight-run/seeker geometry. |
| Nominal maximum target range | 18 km | **Soft launch rule / hard runtime endurance**, OPEN-SOURCE commonly quoted at about 43 kt. |
| Horizontal off-boresight | 0..60 deg | **Hard**, GAME POLICY. |
| Maximum carrier speed | 18 kt | **Hard**, OPEN-SOURCE Project 949/949A employment figure. |
| Target depth | 0..1000 m | **Hard**, OPEN-SOURCE USET-80 target-depth figure. |

The Project 949A platform is also described publicly as capable of torpedo firing to about 480 m. Deep Run uses the stricter 400 m USET-80 weapon-specific limit rather than the broader platform limit.

## 65-76A Kit (650 mm)

| Limit | Fast profile | Economy profile | Meaning / basis |
| --- | ---: | ---: | --- |
| Minimum launch depth | 10 m | 10 m | **Hard**, GAME POLICY. |
| Maximum launch depth | 480 m | 480 m | **Hard**, OPEN-SOURCE Project 949A / weapon figure. |
| Preferred minimum target range | 1 km | 1 km | **Soft**, GAME POLICY. |
| Nominal maximum target range | 50 km | 100 km | **Soft launch rule / hard runtime endurance**, OPEN-SOURCE figures associated with roughly 50 kt and 30-35 kt respectively. |
| Horizontal off-boresight | 0..45 deg | 0..45 deg | **Hard**, GAME POLICY. |
| Maximum carrier speed | 13 kt | 13 kt | **Hard**, OPEN-SOURCE Project 949A employment figure. |
| Target depth | 0..25 m | 0..25 m | **Hard**, GAME POLICY reflecting the anti-surface/wake-homing role. |

Deep Run explicitly selects `fast` or `economy`; choosing the longer range without the corresponding profile is not allowed. This profile selection rule is independent of whether a particular shot is inside or outside the selected profile's preferred range band.

## P-700 Granit / 3M45

| Limit | Deep Run value | Meaning / basis |
| --- | ---: | --- |
| Minimum launch depth | 10 m | **Hard, GAME POLICY**: Deep Run's Antey launches Granit only while visibly submerged. |
| Maximum launch depth | 50 m | **Hard**, OPEN-SOURCE Project 949/949A submerged-launch figure. |
| Preferred minimum target range | 20 km | **Soft**, secondary technical compilation; shorter shots remain launchable but get an effectiveness penalty against defended ships. |
| Nominal maximum target range | 550 km | **Soft launch rule / hard runtime endurance**, OPEN-SOURCE canonical value selected for Deep Run. |
| Horizontal off-boresight | 0..90 deg | **Hard**, GAME POLICY. The fixed canister's ~40 deg elevation is not target bearing. |
| Maximum carrier speed | 5 kt | **Hard**, OPEN-SOURCE Project 949/949A launch figure. |
| Target depth | 0..25 m | **Hard**, GAME POLICY: anti-surface weapon. |

Deep Run intentionally uses a **10..50 m submerged-only Antey launch contract**. Some public descriptions discuss surface launch capability in broader P-700/platform contexts, but that does not override the game's carrier-specific rule requested for normal play. Sea-state, hatch, gas-generator and water-exit lifecycle states may add further physical constraints without changing the soft-range principle.

The authored SM-225/P-700 launcher geometry remains content authority. The employment sector is only a target-eligibility rule and must never rotate the launcher or rewrite asset geometry.

## Player interaction

Selecting a weapon immediately starts that weapon's preparation timer. There is no separate player `Prepare Weapon` action. Weapon selection remains available while another weapon is preparing, ready, or while an already materialized projectile is in flight. Launched projectiles own independent runtimes; changing the selected weapon does not cancel them, and their later resolution must not reset the newly selected weapon's preparation state.

## Public references consulted

- Weaponsystems.net, **USET-80**: <https://weaponsystems.net/system/458-USET-80> — 18 km at 43 kt, launch depth up to 400 m, target depth about 1 km.
- GlobalSecurity, **T-65 / 65-76A Kit**: <https://www.globalsecurity.org/military/world/russia/t-65-torpedo.htm> — 50 km / 50 kt or ~100 km / 30-35 kt, launch depth up to 480 m, carrier speed up to 13 kt, ~14 m travel depth.
- Soumarsov, **Project 949 characteristics**: <https://www.soumarsov.eu/Sous-marins/Post45/949/949_caract.htm> — Project 949A torpedo launch depth to 480 m; carrier-speed figures of about 13 kt for 65-76A and 18 kt for USET-80.
- Deepstorm, **Project 949A**: <https://www.deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm> — platform arrangement and public Project 949A characteristics.
- `universalinternetlibrary.ru`, **Kursk / Project 949A description**: <https://www.universalinternetlibrary.ru/book/36010/ogl.shtml> — Granit underwater launch to 50 m and carrier speed to 5 kt; launcher arrangement.
- Military.cz, **P-700 Granit**: <https://www.military.cz/russia/navy/weapons/granit/granit.htm> — secondary description of 30-50 m submerged-launch band, 20 km minimum and 550 km maximum range.
- Missilery.info, **P-700 Granit**: <https://missilery.info/missile/granit> — launcher/weapon technical background.

When stronger public primary material becomes available, OPEN-SOURCE values may be revised. GAME POLICY values must stay visibly marked as such and must never be rewritten as historical fact.
