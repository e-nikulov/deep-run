from pathlib import Path

source = Path("Tools/p700_stage1_completion.py").read_text(encoding="utf-8")
old_old = '''old_capture = "             &renderFrames, &engineServices, &loggedP700AudioSubmissionFailure](DeepRun::Render::D3D12Renderer& renderer)"'''
new_old = '''old_capture = "             &renderFrames, &weatherVisualCaptured, &engineServices, &loggedP700AudioSubmissionFailure](DeepRun::Render::D3D12Renderer& renderer)"'''
old_new = '''new_capture = "             &renderFrames, &engineServices, &loggedP700AudioSubmissionFailure, &p700CameraImpulse,\\n             &p700ShowcaseProfile](DeepRun::Render::D3D12Renderer& renderer)"'''
new_new = '''new_capture = "             &renderFrames, &weatherVisualCaptured, &engineServices, &loggedP700AudioSubmissionFailure, &p700CameraImpulse,\\n             &p700ShowcaseProfile](DeepRun::Render::D3D12Renderer& renderer)"'''
if source.count(old_old) != 1 or source.count(old_new) != 1:
    raise SystemExit("stage1 capture matcher definitions not found exactly once")
source = source.replace(old_old, new_old, 1).replace(old_new, new_new, 1)
exec(compile(source, "p700_stage1_completion_v2", "exec"))
