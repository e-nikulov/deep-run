from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", required=True, type=Path)
    parser.add_argument("--glb", required=True, type=Path)
    parser.add_argument("--asset-json", required=True, type=Path)
    parser.add_argument("--authoring-json", required=True, type=Path)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def main() -> None:
    options = parse_args()
    blend = options.blend.resolve(strict=True)
    glb = options.glb.resolve(strict=True)
    asset_path = options.asset_json.resolve(strict=True)
    authoring_path = options.authoring_json.resolve(strict=True)

    asset = json.loads(asset_path.read_text(encoding="utf-8"))
    authoring = json.loads(authoring_path.read_text(encoding="utf-8"))

    if asset.get("assetId") != "C0 Player Submarine" or asset.get("name") != "Antey":
        raise RuntimeError("unexpected Antey asset sidecar identity")
    if asset.get("technicalAssetStatus") != "ACCEPTED" or asset.get("userVisualApproval") != "PASS":
        raise RuntimeError("existing production acceptance contract must remain intact")
    if authoring.get("schemaVersion") != 1:
        raise RuntimeError("unexpected Antey authoring schema")

    # Visual-only repair changes the canonical GameReady bytes and GLB material payload, but it must not
    # regenerate semantic bindings from a stale authoring BLEND. Keep the already accepted authoring sidecar
    # byte-for-byte and refresh only the hashes/material inventory that are owned by the visual artifact.
    asset["blendSha256"] = sha256(blend)
    asset["glbSha256"] = sha256(glb)
    asset["materials"] = ["MAT_Antey_Hull", "MAT_Antey_LowerHull", "MAT_Antey_Propellers"]

    asset_path.write_text(json.dumps(asset, indent=2) + "\n", encoding="utf-8", newline="\n")

    report = {
        "blendSha256": asset["blendSha256"],
        "glbSha256": asset["glbSha256"],
        "materials": asset["materials"],
        "authoringPreserved": True,
        "status": "PASS",
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
