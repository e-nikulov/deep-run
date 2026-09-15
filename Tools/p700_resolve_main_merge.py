from pathlib import Path

path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")

first_start = text.find("<<<<<<< HEAD\n", text.find("destroyerInitialXMeters") - 400)
if first_start < 0:
    raise SystemExit("first Main.cpp conflict marker not found")
first_end_marker = ">>>>>>> origin/main\n"
first_end = text.find(first_end_marker, first_start)
if first_end < 0:
    raise SystemExit("first Main.cpp conflict end not found")
# This first marker only wraps the beginning of the block; a second tiny marker follows the shared body.
second_start = text.find("<<<<<<< HEAD\n", first_end + len(first_end_marker))
second_end = text.find(first_end_marker, second_start)
if second_start < 0 or second_end < 0:
    raise SystemExit("second Main.cpp conflict marker not found")
shared_body = text[first_end + len(first_end_marker):second_start]
combined_first = '''                    if (!weatherVisualBeaufortForce.has_value())\n''' + shared_body + '''                    combatPlayground = std::move(*combat);\n'''
text = text[:first_start] + combined_first + text[second_end + len(first_end_marker):]

third_start = text.find("<<<<<<< HEAD\n")
if third_start < 0:
    raise SystemExit("render lambda conflict marker not found")
third_end = text.find(first_end_marker, third_start)
if third_end < 0:
    raise SystemExit("render lambda conflict end not found")
combined_capture = '''             &inputState, &frameCapture, &captureEnabled, &options, &weatherVisualBeaufortForce,\n             &renderFrames, &weatherVisualCaptured, &engineServices, &loggedP700AudioSubmissionFailure](DeepRun::Render::D3D12Renderer& renderer)\n'''
text = text[:third_start] + combined_capture + text[third_end + len(first_end_marker):]

if "<<<<<<<" in text or "=======" in text or ">>>>>>>" in text:
    raise SystemExit("unexpected unresolved Main.cpp conflict marker remains")
path.write_text(text, encoding="utf-8")
print("resolved DeepRun/Main.cpp P700/W1 merge")
