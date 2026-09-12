# Weapon employment envelopes

Status: gameplay contract for M5/IG2. These limits are used to decide whether a weapon may leave the carrier. They are **not** a technical firing manual and do not claim access to classified fire-control data.

Implementation status: the live M5 player USET-80 fire path is gated **before** the `WeaponRuntime` launch transition, and `canFireWeapon` in the HUD is masked by the same assessment. IG2 P-700 and any future selectable 65-76A profile must reuse this common evaluator before committing their own launch/hatch lifecycle. Direct-runtime regression fixtures bind an authoritative `PhysicsWorld` ownship proxy so tests exercise the same employment gate as windowed play.

The implementation deliberately separates three kinds of values:

- **OPEN-SOURCE** — range/depth/carrier-speed figures repeatedly published in public references.
- **GAME POLICY** — conservative minimum range, horizontal target sector or shallow-target band when a reliable public fire-control limit was not found.
- **PROFILE POLICY** — a public weapon with multiple speed/range modes is represented by explicit profiles rather than silently changing its range.

Employment checks consume authoritative own-ship position/orientation/depth/speed plus the selected **perceived Track estimate**. Hostile ground-truth transforms are forbidden. A rejected envelope must leave `WeaponRuntimeState` unlaunched and return a readable command reason.

## USET-80 (533 mm)

| Limit | Deep Run value | Basis |
| --- | ---: | --- |
| Minimum launch depth | 10 m | GAME POLICY: keeps the tube/weapon clearly submerged; no reliable public hard minimum was found. |
| Maximum launch depth | 400 m | OPEN-SOURCE weapon-specific figure. |
| Minimum target range | 0.5 km | GAME POLICY: safety/settling/maneuver envelope; not presented as a historical arming distance. |
| Maximum target range | 18 km | OPEN-SOURCE, commonly quoted at about 43 kt. |
| Horizontal off-boresight | 0..60 deg | GAME POLICY. The real fire-control/gyro-angle limit was not found in reliable public sources. |
| Maximum carrier speed | 18 kt | OPEN-SOURCE Project 949/949A employment figure. |
| Target depth | 0..1000 m | OPEN-SOURCE USET-80 target-depth figure. |

The Project 949A platform is also described publicly as capable of torpedo firing to about 480 m. Deep Run uses the stricter 400 m USET-80 weapon-specific limit rather than the broader platform limit.

## 65-76A Kit (650 mm)

| Limit | Fast profile | Economy profile | Basis |
| --- | ---: | ---: | --- |
| Minimum launch depth | 10 m | 10 m | GAME POLICY. |
| Maximum launch depth | 480 m | 480 m | OPEN-SOURCE Project 949A / weapon figure. |
| Minimum target range | 1 km | 1 km | GAME POLICY. |
| Maximum target range | 50 km | 100 km | OPEN-SOURCE figures associated with roughly 50 kt and 30-35 kt respectively. |
| Horizontal off-boresight | 0..45 deg | 0..45 deg | GAME POLICY; no reliable public fire-control limit found. |
| Maximum carrier speed | 13 kt | 13 kt | OPEN-SOURCE Project 949A employment figure. |
| Target depth | 0..25 m | 0..25 m | GAME POLICY reflecting the anti-surface/wake-homing role and public ~14-20 m travel-depth descriptions. |

Deep Run must explicitly select `fast` or `economy`; selecting the longer range without the corresponding future speed profile is not allowed.

## P-700 Granit / 3M45

| Limit | Deep Run value | Basis |
| --- | ---: | --- |
| Minimum launch depth | 30 m | OPEN-SOURCE/CONSERVATIVE: public secondary descriptions commonly state underwater launch around 30-50 m. |
| Maximum launch depth | 50 m | OPEN-SOURCE Project 949/949A figure. |
| Minimum target range | 20 km | OPEN-SOURCE secondary technical compilation. |
| Maximum target range | 550 km | OPEN-SOURCE Project 949A / P-700 figure selected as the conservative canonical value. |
| Horizontal off-boresight | 0..90 deg | GAME POLICY. The fixed canister's ~40 deg elevation is **not** the same thing as target bearing; no reliable public horizontal fire-control limit was found. |
| Maximum carrier speed | 5 kt | OPEN-SOURCE Project 949/949A launch figure. |
| Target depth | 0..25 m | GAME POLICY: anti-surface weapon; prevents a submerged Track from qualifying. |

The authored SM-225/P-700 launcher geometry remains content authority. The employment sector is only a target-eligibility rule and must never rotate the launcher or rewrite asset geometry.

## Public references consulted

- Weaponsystems.net, **USET-80**: <https://weaponsystems.net/system/458-USET-80> — 18 km at 43 kt, launch depth up to 400 m, target depth about 1 km.
- GlobalSecurity, **T-65 / 65-76A Kit**: <https://www.globalsecurity.org/military/world/russia/t-65-torpedo.htm> — 50 km / 50 kt or ~100 km / 30-35 kt, launch depth up to 480 m, carrier speed up to 13 kt, ~14 m travel depth.
- Soumarsov, **Project 949 characteristics**: <https://www.soumarsov.eu/Sous-marins/Post45/949/949_caract.htm> — Project 949A torpedo launch depth to 480 m; carrier-speed figures of about 13 kt for 65-76A and 18 kt for USET-80.
- Deepstorm, **Project 949A**: <https://www.deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm> — platform arrangement and public Project 949A characteristics.
- `universalinternetlibrary.ru`, **Kursk / Project 949A description**: <https://www.universalinternetlibrary.ru/book/36010/ogl.shtml> — Granit underwater launch to 50 m and carrier speed to 5 kt; launcher arrangement.
- Military.cz, **P-700 Granit**: <https://www.military.cz/russia/navy/weapons/granit/granit.htm> — secondary description of 30-50 m launch band, 20 km minimum and 550 km maximum range.
- Missilery.info, **P-700 Granit**: <https://missilery.info/missile/granit> — launcher/weapon technical background.

When stronger public primary material becomes available, OPEN-SOURCE values may be revised. GAME POLICY values must stay visibly marked as such and must never be rewritten as historical fact.
