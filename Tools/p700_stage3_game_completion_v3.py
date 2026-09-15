from pathlib import Path

exec(compile(Path("Tools/p700_stage3_game_completion_v2.py").read_text(encoding="utf-8"),
             "p700_stage3_game_completion_v3_base", "exec"))

# Correct the exact scalar count after adding five positive stage3 scalar controls.
path = Path("Game/Weapons/P700LaunchVfx.cpp")
text = path.read_text(encoding="utf-8")
if text.count("const std::array<float, 44> scalars{") != 1:
    raise SystemExit("stage3 scalar cardinality anchor missing")
path.write_text(text.replace("const std::array<float, 44> scalars{", "const std::array<float, 43> scalars{", 1),
                encoding="utf-8")

# The showcase weather helper uses clamp and the underwater-wide framing should keep the 154 m Antey close to
# one third of a 16:9 shot, leaving enough negative space for the inclined launch/bubble column to establish scale.
show_path = Path("Game/Combat/P700VfxShowcase.h")
show = show_path.read_text(encoding="utf-8")
if "#include <algorithm>" not in show:
    show = show.replace("#include <array>\n", "#include <algorithm>\n#include <array>\n", 1)
wide_old = '''    {"UNDERWATER_WIDE", P700VfxShowcaseCamera::UnderwaterWide,\n        {-115.0F, -20.0F, 115.0F}, {8.0F, -18.0F, 0.0F}, 230.0F, 0.0F, 4.0F, false, false},'''
wide_new = '''    {"UNDERWATER_WIDE", P700VfxShowcaseCamera::UnderwaterWide,\n        {-190.0F, -24.0F, 190.0F}, {10.0F, -18.0F, 0.0F}, 440.0F, 0.0F, 4.0F, false, false},'''
if show.count(wide_old) != 1:
    raise SystemExit("P700 underwater-wide framing anchor missing")
show_path.write_text(show.replace(wide_old, wide_new, 1), encoding="utf-8")

# Very weak above-water launch bounce into cloud/rain presentation. Underwater light is explicitly rejected by
# the surface-level test, so the ocean medium boundary remains respected. Values stay scene-linear pre tone-map.
tone_path = Path("Shaders/PostProcess/ToneMap.hlsl")
tone = tone_path.read_text(encoding="utf-8")
cloud_anchor = '''        sky = lerp(sky, lerp(brightCloud, stormCloud, stormTint),\n                   cloudMass * (0.35F + 0.55F * CloudCoverFraction));\n    }'''
cloud_new = '''        sky = lerp(sky, lerp(brightCloud, stormCloud, stormTint),\n                   cloudMass * (0.35F + 0.55F * CloudCoverFraction));\n        if (LocalLightControl.x > 0.5F && LocalLightPositionRadius0.y >= SurfaceLevelYMeters)\n        {\n            const float nightFactor = saturate(1.0F - SkyLuminanceMultiplier);\n            const float bounce = cloudMass * nightFactor *\n                saturate(LocalLightColorIntensity0.w / 12.0F) * 0.018F;\n            sky += LocalLightColorIntensity0.rgb * bounce;\n        }\n    }'''
if tone.count(cloud_anchor) != 1:
    raise SystemExit("ToneMap launch cloud-bounce anchor missing")
tone = tone.replace(cloud_anchor, cloud_new, 1)
rain_anchor = '''    float3 result = lerp(sceneLinear, AtmosphereFogColorRgb, mist);\n    result += float3(0.32F, 0.37F, 0.42F) * streak * (0.16F * PrecipitationFraction);\n    return result;'''
rain_new = '''    float3 result = lerp(sceneLinear, AtmosphereFogColorRgb, mist);\n    result += float3(0.32F, 0.37F, 0.42F) * streak * (0.16F * PrecipitationFraction);\n    if (LocalLightControl.x > 0.5F && LocalLightPositionRadius0.y >= SurfaceLevelYMeters)\n        result += LocalLightColorIntensity0.rgb * streak *\n            saturate(LocalLightColorIntensity0.w / 12.0F) * (0.055F * PrecipitationFraction);\n    return result;'''
if tone.count(rain_anchor) != 1:
    raise SystemExit("ToneMap launch-lit rain anchor missing")
tone_path.write_text(tone.replace(rain_anchor, rain_new, 1), encoding="utf-8")

print("P-700 stage3 v3 acceptance details applied")
