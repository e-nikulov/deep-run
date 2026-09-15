from pathlib import Path

path = Path("Tests/TestMain.cpp")
text = path.read_text(encoding="utf-8")
old = "    auto tooManySamples = parameters;\n    tooManySamples.horizontalSampleCount = 514U;\n"
new = "    auto tooManySamples = parameters;\n    tooManySamples.horizontalSampleCount = DeepRun::Render::GerstnerSurfaceMaximumHorizontalSampleCount + 1U;\n"
if text.count(old) != 1:
    raise SystemExit(f"expected one legacy Gerstner maximum-sample fixture, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("W1-J legacy M3-E invalid sample-count fixture updated")
