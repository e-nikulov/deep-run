from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    "Status: CORRECTIVE CLOSURE CANDIDATE — FINAL CI / HUMAN ACCEPTANCE PENDING",
    """Status: ACCEPTED / M5 COMPLETE

Final corrective acceptance is anchored by PR #11 head `0efd5ff2eaa6fa504c2f60a02a381cec93331626` and GitHub Actions run `34775791243` (#770). Both Windows Debug and Release passed production Antey contract validation, configure, build, all registered CTest targets, windowed acoustic smoke, P-700 production launch smoke, and M5/P-700 acceptance-artifact retention. The accepted tree was merged to `main` as `678201074d093fb71806101cf21be375d0e271fc`.""",
)

replace_once(
    "docs/development/m5-combat-playground.md",
    "## Milestone closure\n\n",
    """## Milestone closure

Final corrective M5 closure is anchored by PR #11 head `0efd5ff2eaa6fa504c2f60a02a381cec93331626`, GitHub Actions run `34775791243` (#770), and merge commit `678201074d093fb71806101cf21be375d0e271fc`. Debug and Release both passed production Antey contract validation, configure, build, the complete registered CTest set, windowed acoustic smoke, P-700 production launch smoke, and M5/P-700 acceptance-artifact retention. This supersedes the earlier technical closure anchor below while preserving it as milestone history.

""",
)
