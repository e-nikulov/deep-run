# DeepRun Asset Provenance and Shipping Gate

Status: Canonical content provenance policy

This document records repository hygiene, not legal advice. It does not infer
ownership or permission from possession of a file, a public URL, technical
validation, modification, or removal of identifying marks.

## Required record

Every externally sourced model, image, texture, material, audio file, font,
reference, or other shipping input must have a provenance record containing,
where applicable:

| Field | Requirement |
|---|---|
| Asset name | Stable human-readable identity |
| Author | Recorded or `UNKNOWN` |
| Copyright holder | Recorded or `UNKNOWN` |
| Source | Provider/publication/file description |
| Source URL | Canonical URL or `NOT AVAILABLE` |
| Licence | Exact licence identifier/name or `UNKNOWN` |
| Licence version | Version or `NOT APPLICABLE`/`UNKNOWN` |
| Commercial use | `PERMITTED`, `NOT PERMITTED`, or `UNKNOWN` with evidence |
| Modification | `PERMITTED`, `NOT PERMITTED`, or `UNKNOWN` with evidence |
| Attribution | Exact requirement or `UNKNOWN` |
| Date obtained | ISO date when known |
| Original/raw path | Immutable retained source or `NOT RETAINED` |
| Processed/shipping path | Every derived or candidate shipping asset |
| Modifications | Factual transformation summary |
| Notes | Restrictions, evidence, review owner, and unresolved questions |
| Shipping status | `CLEARED`, `EXCLUDED`, or `NON-SHIPPABLE UNTIL RESOLVED` |

A URL is not a licence. A reference used only for facts or visual research must
still record its source and redistribution status, but it is not automatically
a derivative-asset licence.

## Storage and review

- Keep per-package records next to the source under `Content/` when practical.
- Keep externally supplied raw source immutable or hash-bound when retained.
- Keep review/reference files out of runtime exports unless explicitly cleared.
- Record all derived/shipping paths so a blocked source can be removed
  completely from a distribution.
- Preserve required attribution and licence text in the eventual shipping
  notice bundle.
- Re-run legal/provenance review whenever a source, licence, material input, or
  intended distribution model changes.

Technical validation, human art approval, runtime integration, and legal
shipping clearance are independent gates.

## Current registry

| Package/input | Record | Current shipping status |
|---|---|---|
| Antey supplied Blender source and derivative production asset | `docs/content/third-party/antey-asset-license-review.md`; `Content/submarines/Antey/References/reference_manifest.md` | **NON-SHIPPABLE UNTIL RESOLVED** — author, source URL, redistribution permission, and derivative rights are not established |
| P700 supplied Blender source and derivative production asset | `docs/content/third-party/p700-asset-license-review.md`; `Content/Weapons/P700/References/reference_manifest.md` | **NON-SHIPPABLE UNTIL RESOLVED** — source-model provenance and redistribution/derivative rights are not established |
| `DeepStorm_949A_views.png` / `.gif` | `Content/submarines/Antey/References/reference_manifest.md` | **NON-SHIPPABLE UNTIL RESOLVED** — author/licence not established; development review only |
| `granit_3.jpg` | `Content/Weapons/P700/References/reference_manifest.md` | **NON-SHIPPABLE UNTIL RESOLVED** — author/source/licence unknown; reference only |
| Wikimedia Project 949A/P700 drawings | Per-package reference manifests | Reference-specific terms only; do not infer rights for source models or other derived assets |

The current Antey and P700 technical/art contracts remain authoritative for
content quality and structure. This registry changes only their legal shipping
gate; it does not rename, regenerate, or redesign either asset.
