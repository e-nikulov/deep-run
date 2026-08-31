# P-700 Granit reference research

Research performed 2026-08-31 for the DeepRun game asset. The primary flight silhouette is the local `P-700-Granit_sketch.svg`; the local `granit_3.jpg` is the primary launch-state reference for the intake plug and underwater start booster. Open visual files in this directory are retained for provenance; the model is an original game-ready reconstruction and does not copy a third-party 3D mesh.

## Sources

| Source | Local file / URL | Use and reliability |
|---|---|---|
| Wikimedia Commons category, P-700 Granit | [Category:P-700 Granit](https://commons.wikimedia.org/wiki/Category:P-700_Granit) | Index of five open media files, names and licensing. Useful as a visual cross-check; medium reliability because Commons entries include both photographs and user-created drawings. |
| Allocer, Wikimedia Commons | [P-700-Granit sketch.svg](https://commons.wikimedia.org/wiki/File:P-700-Granit_sketch.svg) | Local `P-700-Granit_sketch.svg`; side silhouette, annular nose intake, swept wings, cross-tail, rear section break. Self-published technical sketch; medium reliability for proportions, not a factory drawing. Its file history explicitly notes a maximum diameter of 0.88 m. |
| Wikipedia, P-700 Granit | [P-700 Granit](https://en.wikipedia.org/wiki/P-700_Granit) | Independent summary: 10 m length, approximately 7,000 kg, swept-back wings/tail, detachable solid-fuel booster, annular nose intake. Secondary source; geometry claims were checked against images. |
| Russian Wikipedia | [П-700 «Гранит»](https://ru.wikipedia.org/wiki/%D0%9F-700_%C2%AB%D0%93%D1%80%D0%B0%D0%BD%D0%B8%D1%82%D1%BB) | Independent Russian-language naming and design summary: cigar-shaped body and folding cross-tail. Secondary source; useful terminology only. |
| Knifesburg technical summary | [P-700 Granit technical characteristics](https://knifesburg.ru/en/na-more/p-700-granit.html) | Lists 10 m length, 2.6 m wingspan and 0.85 m diameter, and describes the annular intake, folding wings/tail and launch booster. Secondary source with unsourced/contradictory engine statements; dimensions used as corroboration, not as sole authority. |
| ResearchGate figure | [P-700 Granit with inlet cover installed](https://www.researchgate.net/figure/P-700-Granit-with-inlet-cover-installed-but-wings-and-control-surfaces-deployed-The_fig1_358520458) | Independent visual reference for deployed wings/control surfaces and the annular inlet. Copyrighted preview was inspected but not copied into the repository. Medium reliability. |
| Local launch-state drawing | `granit_3.jpg` copied from `C:\Users\es.nikulov\Downloads\granit_3.jpg` | Primary launch-state visual reference supplied for this task: intake plug/fairing before the air intake and the annular solid-fuel start/boost stage at the tail. Technical drawing/compiled reference; medium reliability for the external launch arrangement. |
| Test Pilot (ЦКБМ / НПО машиностроения) | [Ракета П-700 «Гранит»](https://testpilot.ru/russia/chelomei/p/700/) | Primary-ish specialist historical/technical page: 10 m length, 2.6 m wing span, 0.85 m diameter, KR-93 sustainer and ring solid-fuel booster starting under water. Used to separate the flight body, intake, sustainer and launch booster without modeling internals. |

## Chosen dimensions

The model uses 9.98 m overall length, 0.884 m maximum body diameter, and 2.60 m deployed main-wing span. The 10 m / 2.6 m / 0.85 m figures are repeated by Test Pilot and the other technical summaries; the supplied SVG sketch records a maximum diameter of about 0.88 m. The body was therefore kept close to 0.88 m rather than forcing the less precise 0.85 m figure. The final 20 mm length difference is the result of the modeled booster envelope ending at x=-4.98 and the nose marker at x=5.0.

## Cross-source disagreements and handling

* Diameter is commonly reported as 0.80, 0.85 or 0.88 m. The asset uses 0.884 m at the largest body station, supported by the sketch and consistent with the 0.85 m rounded specification.
* Some secondary sources call the sustainer a turbojet and others a ramjet. Only exterior engine housing, nozzle and the separately detachable booster are modeled; no internal propulsion claim is encoded.
* Illustrations differ on exact wing root station, intake cover shape, and booster proportions. The photographed/diagrammed silhouette has priority; uncertain small mechanisms, panels and markings are intentionally omitted.
* Public references show both stowed and deployed control surfaces. The asset stores wings and four tail surfaces as separate mesh objects with simple pivot metadata, but does not invent a folding rig.
* The supplied `granit_3.jpg` depicts several launch/transport configurations and some cutaway views. Only the confidently visible external intake plug and ring booster are modeled; internal electronics, warhead and engine internals remain absent.
