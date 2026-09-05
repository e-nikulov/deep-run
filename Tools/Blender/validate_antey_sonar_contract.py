"""Validate the canonical Antey bow-sonar content contract sidecar."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


MAIN_BOW_SONAR_SEMANTIC_ID = "MGK540_BOW_ARRAY"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--authoring-json", required=True, type=Path)
    options = parser.parse_args()

    authoring_path = options.authoring_json.resolve(strict=True)
    authoring = json.loads(authoring_path.read_text(encoding="utf-8"))
    regions = [
        region
        for region in authoring.get("semanticRegions", [])
        if region.get("semanticId") == MAIN_BOW_SONAR_SEMANTIC_ID
    ]
    if len(regions) != 1:
        raise RuntimeError(f"Expected one {MAIN_BOW_SONAR_SEMANTIC_ID} semantic region")
    region = regions[0]
    if region.get("semanticClass") != "RESERVED_CONTENT_REGION":
        raise RuntimeError("Main bow sonar region is not a reserved content region")
    if region.get("anchorMarker") is not None or region.get("anchorStatus") != "NO_GEOMETRIC_ANCHOR_AUTHORED":
        raise RuntimeError("Main bow sonar region must not claim an unmodeled geometric anchor")
    sonar_markers = [name for name in authoring.get("markers", {}) if "sonar" in name.casefold()]
    if sonar_markers:
        raise RuntimeError(f"Semantic-only sonar contract must not publish sonar markers: {sonar_markers}")
    if region.get("geometryContract") != "NO_INTERNAL_ARRAY_GEOMETRY_REQUIRED":
        raise RuntimeError("Main bow sonar region incorrectly requires internal antenna geometry")
    if region.get("authoringConstraint") != (
        "NO_TORPEDO_INTERNAL_WEAPON_OR_LARGE_BOW_ELEMENT_INTERSECTION_"
        "WITHOUT_EXPLICIT_CONTENT_CONTRACT_REVIEW"
    ):
        raise RuntimeError("Main bow sonar region lacks the required separation constraint")
    if region.get("spatialValidation") != "DOCUMENTED_ONLY_NO_BOUNDED_GEOMETRY":
        raise RuntimeError("Main bow sonar spatial-validation boundary is not explicit")

    tubes = authoring.get("torpedoTubes", [])
    tubes_533 = [tube for tube in tubes if tube.get("tubeClass") == "533"]
    tubes_650 = [tube for tube in tubes if tube.get("tubeClass") == "650"]
    if len(tubes) != 6 or len(tubes_533) != 4 or len(tubes_650) != 2:
        raise RuntimeError("Torpedo contract must remain 4x533 plus 2x650")
    if any(len(tube.get("position", [])) != 3 or tube["position"][2] <= 0.0 for tube in tubes):
        raise RuntimeError("Every bow torpedo marker must remain in the upper bow hemisphere")

    print(
        "ANTEY_SONAR_CONTENT_CONTRACT_OK "
        f"semantic_id={MAIN_BOW_SONAR_SEMANTIC_ID} torpedo_533={len(tubes_533)} "
        f"torpedo_650={len(tubes_650)} spatial_guard=DOCUMENTED_ONLY"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
