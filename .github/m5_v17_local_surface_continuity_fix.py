from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# Remove the historical 600 m vertical-composition activation threshold. Free-presentation
# framing must keep one continuous surface anchor across Detail/Local/Tactical zoom.
path = ROOT / 'Game/PhysicalPlayground.h'
text = path.read_text(encoding='utf-8')
old = '''        constexpr float M5SurfaceCompositionMinimumHorizontalSpanMeters = 600.0F;
        if (water_.has_value() && std::abs(targetOffsetYMeters) <= 1.0e-4F &&
            horizontalSpanMeters >= M5SurfaceCompositionMinimumHorizontalSpanMeters)
        {
'''
new = '''        // M5-V1.7 continuity: once free-presentation framing is active, every supported horizontal span
        // uses the same surface-anchor policy. The previous 600 m activation threshold produced a visible
        // vertical snap when zoom crossed 600 m (for example 0.54 km -> 0.64 km).
        if (water_.has_value() && std::abs(targetOffsetYMeters) <= 1.0e-4F)
        {
'''
text = replace_once(text, old, new, 'remove 600 m surface-composition threshold')
path.write_text(text, encoding='utf-8')

# Strengthen the pure composition regression around the historical discontinuity.
path = ROOT / 'Tests/M5ScalableEnvironmentPresentationChecks.h'
text = path.read_text(encoding='utf-8')
old = '''    const float localSky = AboveWaterFractionForPresentationSpanMeters(800.0F);
    const float transitionSky = AboveWaterFractionForPresentationSpanMeters(1'550.0F);
'''
new = '''    const float detailSky = AboveWaterFractionForPresentationSpanMeters(540.0F);
    const float preThresholdSky = AboveWaterFractionForPresentationSpanMeters(599.0F);
    const float postThresholdSky = AboveWaterFractionForPresentationSpanMeters(601.0F);
    const float localSky = AboveWaterFractionForPresentationSpanMeters(800.0F);
    const float transitionSky = AboveWaterFractionForPresentationSpanMeters(1'550.0F);
'''
text = replace_once(text, old, new, 'add local continuity samples')
old = '''    if (std::abs(localSky - 0.15F) > 0.0001F ||
        !(transitionSky > localSky && transitionSky < tacticalSky) ||
'''
new = '''    if (std::abs(detailSky - 0.15F) > 0.0001F ||
        std::abs(preThresholdSky - 0.15F) > 0.0001F ||
        std::abs(postThresholdSky - 0.15F) > 0.0001F ||
        std::abs(preThresholdSky - postThresholdSky) > 0.0001F ||
        std::abs(localSky - 0.15F) > 0.0001F ||
        !(transitionSky > localSky && transitionSky < tacticalSky) ||
'''
text = replace_once(text, old, new, 'assert local continuity across 600 m')
path.write_text(text, encoding='utf-8')
