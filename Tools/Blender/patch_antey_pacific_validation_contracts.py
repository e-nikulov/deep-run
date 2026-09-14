from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected exactly one match in {path}: found {count}: {old!r}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def main() -> None:
    source_first = ROOT / "Tools" / "Blender" / "validate_antey_source_first.py"
    replace_once(
        source_first,
        'required={"MAT_Antey_Hull","MAT_Antey_Propellers"}',
        'required={"MAT_Antey_Hull","MAT_Antey_LowerHull","MAT_Antey_Propellers"}',
    )

    runtime_glb = ROOT / "Tools" / "Blender" / "validate_runtime_glb.py"
    replace_once(
        runtime_glb,
        'expected_materials = ["MAT_Antey_Hull", "MAT_Antey_Propellers"]',
        'expected_materials = ["MAT_Antey_Hull", "MAT_Antey_LowerHull", "MAT_Antey_Propellers"]',
    )

    print("Antey Pacific validation contract patch: PASS")


if __name__ == "__main__":
    main()
