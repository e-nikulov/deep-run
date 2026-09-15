from pathlib import Path

path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")
old = '''                const float initialDepthMeters = weatherVisualBeaufortForce.has_value()\n                    ? 3.0F\n                    : options.p700SmokeTest'''
new = '''                const float initialDepthMeters = weatherVisualBeaufortForce.has_value()\n                    ? 5.0F\n                    : options.p700SmokeTest'''
if text.count(old) != 1:
    raise SystemExit(f"expected one W1-I acceptance depth anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("W1-I acceptance depth set to 5.0 m")
