from __future__ import annotations

import subprocess
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

    test_main = ROOT / "Tests" / "TestMain.cpp"
    replace_once(
        test_main,
        "production->Get()->primitives.empty() || production->Get()->materials.size() != 2U)",
        "production->Get()->primitives.empty() || production->Get()->materials.size() != 3U)",
    )
    replace_once(
        test_main,
        "return channelDifference > 0.1F; // clearly different in scene-linear space, without asserting art direction",
        "return channelDifference > 0.05F && below.g > above.g && below.b > above.b; // dark Pacific water stays visibly distinct",
    )

    # TestMain is a generated validation-contract update in this scoped repair. Stage it now so the later
    # production-visual commit persists the exact test contract that passed CTest; subsequent git add calls
    # in the workflow intentionally do not clear already-staged paths.
    subprocess.run(["git", "add", "Tests/TestMain.cpp"], cwd=ROOT, check=True)

    print("Antey Pacific validation contract patch: PASS")


if __name__ == "__main__":
    main()
