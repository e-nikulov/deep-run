# P-700 Granit reference research

Research/access date: 2026-08-31. These materials are reference-only inputs for
an original scripted reconstruction; no third-party 3D mesh is redistributed by
the asset. Reference images remain outside the runtime GLB.

## Sources and provenance

| Description | Source/site and URL | Author/organization | Access date | License/copyright status | Used for |
|---|---|---|---|---|---|
| P-700 media index and license links | [Wikimedia Commons category](https://commons.wikimedia.org/wiki/Category:P-700_Granit) | Wikimedia Commons contributors | 2026-08-31 | Individual files have different terms; no media is redistributed as runtime content | Visual cross-check and provenance discovery |
| Flight silhouette sketch | [P-700-Granit sketch.svg](https://commons.wikimedia.org/wiki/File:P-700-Granit_sketch.svg); retained locally as `P-700-Granit_sketch.svg` | Allocer | 2026-08-31 | File-page terms were not established as a license for the reconstructed asset; reference-only | Length/diameter proportions, annular nose, swept wings, cross-tail and rear break |
| General design summary | [Wikipedia: P-700 Granit](https://en.wikipedia.org/wiki/P-700_Granit) | Wikipedia contributors | 2026-08-31 | Site content was not copied into the asset; no model license is inferred | Terminology, approximate length/mass and external configuration cross-check |
| Russian naming/design summary | [Russian Wikipedia: П-700 «Гранит»](https://ru.wikipedia.org/wiki/%D0%9F-700_%C2%AB%D0%93%D1%80%D0%B0%D0%BD%D0%B8%D1%82%D0%BB) | Wikipedia contributors | 2026-08-31 | Site content was not copied into the asset; no model license is inferred | Terminology and cigar-shaped/folding-surface cross-check |
| Technical characteristics summary | [Knifesburg P-700 Granit](https://knifesburg.ru/en/na-more/p-700-granit.html) | Knifesburg site; individual author not established | 2026-08-31 | Reference-only / copyright status not established | Corroboration for 10 m length, 2.6 m span, approximate diameter and launch booster |
| Deployed-surface visual | [ResearchGate figure](https://www.researchgate.net/figure/P-700-Granit-with-inlet-cover-installed-but-wings-and-control-surfaces-deployed-The_fig1_358520458) | Figure author not established in this record | 2026-08-31 | Copyrighted preview inspected; not copied or redistributed | Deployed wing/tail and annular inlet visual cross-check |
| Launch-state drawing | Local `granit_3.jpg`, supplied from `C:\Users\es.nikulov\Downloads\granit_3.jpg` | Author/source not established | 2026-08-31 | Reference-only / copyright status not established | Intake plug/fairing and external underwater-start booster arrangement |
| Specialist historical/technical page | [Test Pilot: Ракета П-700 «Гранит»](https://testpilot.ru/russia/chelomei/p/700/) | ЦКБМ / НПО машиностроения attribution on source page | 2026-08-31 | Reference-only / redistribution license for model derivatives not established | 10 m length, 2.6 m span, approximate diameter, external sustainer/booster separation |

## Chosen dimensions

The asset uses 9.98 m overall length, 0.884 m maximum body diameter, and 2.60 m
deployed main-wing span. Public figures vary slightly; the documented choice
keeps the model in the approximately 10 m class and follows the local sketch's
approximately 0.88 m maximum diameter without claiming centimetre-level
accuracy. The 20 mm length difference is the modeled booster envelope ending at
x=-4.98 and the nose marker at x=5.0.

## Cross-source disagreements and handling

- Diameter is variously reported as 0.80, 0.85 or 0.88 m; the asset uses 0.884 m.
- Engine terminology varies between secondary sources; only exterior housing and
  nozzle shapes are represented, with no internal propulsion claim.
- Wing root, intake-cover and booster proportions vary; the reviewed silhouette
  has priority and uncertain mechanisms are omitted.
- The runtime GLB is explicitly `P700_FLIGHT_DEPLOYED`: its main-wing span is
  2.60 m. It is not validated as in-launcher geometry.
- Stowed/launch configuration: `NOT YET AUTHORED`. The source BLEND retains
  launch-only reference parts, but they are not a stowed missile configuration
  and are excluded from the runtime GLB.
- Deployment transition: `DEFERRED TO FUTURE WEAPON/VFX WORK`. The flight
  wings and cross-tail retain authored pivots for that future work; no motion
  or deployment animation is present now.
- Internal electronics, warhead, guidance and engine internals are not modeled.
