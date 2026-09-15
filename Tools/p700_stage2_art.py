from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def one(path: str, old: str, new: str) -> None:
    text = read(path)
    if text.count(old) != 1:
        raise SystemExit(f"{path}: anchor count {text.count(old)} for {old[:100]!r}")
    write(path, text.replace(old, new, 1))

# Stage1 is the validated integration baseline: latest-main merge happens in the workflow first.
exec(compile(read("Tools/p700_stage1_completion_v2.py"), "p700_stage1_completion_v2", "exec"))

# Reusable engine primitives; appended to preserve every existing numeric kind.
one(
    "Engine/Render/TransientVfx.h",
    "    Particulate,\n};",
    "    Particulate,\n    GasCavity,\n    SurfaceImpact,\n};")

shader = read("Shaders/Particles/TransientVfx.hlsl")
anchor = '''    else\n    {\n        center -= direction * r0 * ExtentKind.x;\n        center += side * signed0 * ExtentKind.y;\n        center += depthSide * signed1 * ExtentKind.z;\n        center += side * sin(phase) * VelocityTurbulence.w * 0.12F;\n        center += VelocityTurbulence.xyz * delayedAge * 0.08F;\n        alphaScale = saturate(0.65F - normalizedAge * 0.45F);\n    }'''
replacement = r'''    else if (kind == 11u)
    {
        // Generic transient gas cavity: an elongated, irregular two-sided volume whose boundary expands,
        // breaks up and collapses. This is deliberately not a transparent sphere or a chain of identical quads.
        const float axial01 = r0;
        const float axial = axial01 * ExtentKind.x;
        const float longitudinalEnvelope = sqrt(saturate(1.0F - (axial01 * 2.0F - 1.0F) *
                                                          (axial01 * 2.0F - 1.0F)));
        const float breakup = 0.48F + 0.52F * saturate(
            0.5F + 0.5F * sin(phase * 0.71F + r3 * 9.0F));
        const float expansion = 0.32F + 0.98F * sin(saturate(normalizedAge) * 3.14159265F);
        center -= direction * axial;
        center += side * signed0 * ExtentKind.y * longitudinalEnvelope * breakup * expansion;
        center += depthSide * signed1 * ExtentKind.z * longitudinalEnvelope *
                  (0.58F + 0.42F * r2) * expansion;
        center.y += (0.20F + 0.75F * r2) * delayedAge;
        center += side * sin(phase * 1.23F) * VelocityTurbulence.w *
                  (0.07F + 0.10F * normalizedAge);
        sizeMeters *= lerp(0.75F, 1.75F, r3) * (0.72F + 0.55F * expansion);
        alphaScale = saturate(sin(normalizedAge * 3.14159265F) * 0.78F +
                              (1.0F - normalizedAge) * 0.20F);
    }
    else if (kind == 12u)
    {
        // Generic distributed surface-return splash. Each analytic instance lands at a different deterministic
        // point in an elliptical footprint and throws a small ballistic crown after the parent heavy spray falls.
        const float angle = r0 * 6.28318530718F;
        const float radius = sqrt(r1);
        center.x += cos(angle) * radius * ExtentKind.x;
        center.z += sin(angle) * radius * ExtentKind.z;
        const float localAge = saturate(normalizedAge * 1.18F);
        const float upward = ExtentKind.y * (0.28F + 0.72F * r2);
        center.y = SurfaceAbsorptionR.x + upward * sin(localAge * 3.14159265F);
        center += wind * delayedAge * 0.025F;
        sizeMeters *= lerp(0.55F, 1.25F, r3) * (1.0F + 0.55F * localAge);
        alphaScale = saturate((1.0F - localAge) * 0.78F);
    }
    else
    {
        center -= direction * r0 * ExtentKind.x;
        center += side * signed0 * ExtentKind.y;
        center += depthSide * signed1 * ExtentKind.z;
        center += side * sin(phase) * VelocityTurbulence.w * 0.12F;
        center += VelocityTurbulence.xyz * delayedAge * 0.08F;
        alphaScale = saturate(0.65F - normalizedAge * 0.45F);
    }'''
if shader.count(anchor) != 1:
    raise SystemExit("TransientVfx generic fallback anchor missing")
shader = shader.replace(anchor, replacement, 1)
# Cavity sprites use a softer asymmetric footprint, avoiding opaque circular blob appearance.
old_ps = "    if (kind == 8u || kind == 9u)\n        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.35F + input.corner.y * input.corner.y));"
new_ps = "    if (kind == 8u || kind == 9u)\n        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.35F + input.corner.y * input.corner.y));\n    else if (kind == 11u)\n        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.62F + input.corner.y * input.corner.y * 0.88F));"
if shader.count(old_ps) != 1:
    raise SystemExit("TransientVfx pixel radial anchor missing")
shader = shader.replace(old_ps, new_ps, 1)
write("Shaders/Particles/TransientVfx.hlsl", shader)

# Data-driven production tuning.
header = read("Game/Weapons/P700LaunchVfx.h")
old_fields = '''    float underwaterEmissiveIntensity = 7.5F;\n    std::uint32_t underwaterCoreParticleCount = 56U;\n\n    float breachPreReactionDepthMeters = 6.5F;'''
new_fields = '''    float underwaterEmissiveIntensity = 7.5F;\n    std::uint32_t underwaterCoreParticleCount = 56U;\n    std::uint32_t gasCavityCount = 36U;\n    float gasCavityLifetimeSeconds = 1.85F;\n    float gasCavityMaximumLengthMeters = 13.5F;\n    float gasCavityRadiusMeters = 3.35F;\n    std::array<float, 2> gasCavitySizeMeters{0.72F, 2.45F};\n\n    float breachPreReactionDepthMeters = 6.5F;'''
if header.count(old_fields) != 1:
    raise SystemExit("P700 tuning underwater fields anchor missing")
header = header.replace(old_fields, new_fields, 1)
old_foam = '''    std::uint32_t foamCount = 150U;\n    float foamLifetimeSeconds = 8.0F;\n    float foamRadiusMeters = 11.0F;\n'''
new_foam = '''    std::uint32_t foamCount = 150U;\n    float foamLifetimeSeconds = 8.0F;\n    float foamRadiusMeters = 11.0F;\n    std::uint32_t secondaryImpactCount = 48U;\n    float secondaryImpactDelaySeconds = 0.82F;\n    float secondaryImpactLifetimeSeconds = 1.35F;\n    float secondaryImpactRadiusMeters = 10.5F;\n'''
if header.count(old_foam) != 1:
    raise SystemExit("P700 tuning foam fields anchor missing")
header = header.replace(old_foam, new_foam, 1)
write("Game/Weapons/P700LaunchVfx.h", header)

cpp = read("Game/Weapons/P700LaunchVfx.cpp")
old_counts = '''    if (t.bubbleMicroCount == 0U || t.bubbleMesoCount == 0U || t.bubbleMacroCount == 0U ||\n        t.underwaterCoreParticleCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||\n        t.mistCount == 0U || t.waterSheetCount == 0U || t.foamCount == 0U || t.airborneCoreCount == 0U ||'''
new_counts = '''    if (t.bubbleMicroCount == 0U || t.bubbleMesoCount == 0U || t.bubbleMacroCount == 0U ||\n        t.underwaterCoreParticleCount == 0U || t.gasCavityCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||\n        t.mistCount == 0U || t.waterSheetCount == 0U || t.foamCount == 0U || t.secondaryImpactCount == 0U ||\n        t.airborneCoreCount == 0U ||'''
if cpp.count(old_counts) != 1:
    raise SystemExit("P700 validation count anchor missing")
cpp = cpp.replace(old_counts, new_counts, 1)
old_scalar_decl = "    const std::array<float, 28> scalars{"
new_scalar_decl = "    const std::array<float, 35> scalars{"
if cpp.count(old_scalar_decl) != 1:
    raise SystemExit("P700 validation scalar array anchor missing")
cpp = cpp.replace(old_scalar_decl, new_scalar_decl, 1)
old_scalar_slice = '''        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,\n        t.breachPreReactionDepthMeters, t.splashRadiusMeters, t.waterCrownLifetimeSeconds,'''
new_scalar_slice = '''        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,\n        t.gasCavityLifetimeSeconds, t.gasCavityMaximumLengthMeters, t.gasCavityRadiusMeters,\n        t.breachPreReactionDepthMeters, t.splashRadiusMeters, t.waterCrownLifetimeSeconds,'''
if cpp.count(old_scalar_slice) != 1:
    raise SystemExit("P700 validation gas scalar anchor missing")
cpp = cpp.replace(old_scalar_slice, new_scalar_slice, 1)
old_scalar_foam = '''        t.foamLifetimeSeconds, t.foamRadiusMeters, t.airborneCoreLengthMeters, t.airbornePlumeLengthMeters,'''
new_scalar_foam = '''        t.foamLifetimeSeconds, t.foamRadiusMeters, t.secondaryImpactDelaySeconds,\n        t.secondaryImpactLifetimeSeconds, t.secondaryImpactRadiusMeters, t.airborneCoreLengthMeters, t.airbornePlumeLengthMeters,'''
if cpp.count(old_scalar_foam) != 1:
    raise SystemExit("P700 validation secondary-impact scalar anchor missing")
cpp = cpp.replace(old_scalar_foam, new_scalar_foam, 1)
old_sizes = '''        !validSizeRange(t.bubbleMacroSizeMeters) || !validSizeRange(t.dropletSizeMeters))'''
new_sizes = '''        !validSizeRange(t.bubbleMacroSizeMeters) || !validSizeRange(t.gasCavitySizeMeters) ||\n        !validSizeRange(t.dropletSizeMeters))'''
if cpp.count(old_sizes) != 1:
    raise SystemExit("P700 validation size range anchor missing")
cpp = cpp.replace(old_sizes, new_sizes, 1)

# Parser additions.
parser_anchor = '''        ReadIfPresent(root, "underwaterCoreParticleCount", t.underwaterCoreParticleCount);\n        ReadIfPresent(root, "breachPreReactionDepthMeters", t.breachPreReactionDepthMeters);'''
parser_new = '''        ReadIfPresent(root, "underwaterCoreParticleCount", t.underwaterCoreParticleCount);\n        ReadIfPresent(root, "gasCavityCount", t.gasCavityCount);\n        ReadIfPresent(root, "gasCavityLifetimeSeconds", t.gasCavityLifetimeSeconds);\n        ReadIfPresent(root, "gasCavityMaximumLengthMeters", t.gasCavityMaximumLengthMeters);\n        ReadIfPresent(root, "gasCavityRadiusMeters", t.gasCavityRadiusMeters);\n        ReadArrayIfPresent(root, "gasCavitySizeMeters", t.gasCavitySizeMeters);\n        ReadIfPresent(root, "breachPreReactionDepthMeters", t.breachPreReactionDepthMeters);'''
if cpp.count(parser_anchor) != 1:
    raise SystemExit("P700 parser gas anchor missing")
cpp = cpp.replace(parser_anchor, parser_new, 1)
parser_foam = '''        ReadIfPresent(root, "foamRadiusMeters", t.foamRadiusMeters);\n        ReadIfPresent(root, "airborneCoreCount", t.airborneCoreCount);'''
parser_foam_new = '''        ReadIfPresent(root, "foamRadiusMeters", t.foamRadiusMeters);\n        ReadIfPresent(root, "secondaryImpactCount", t.secondaryImpactCount);\n        ReadIfPresent(root, "secondaryImpactDelaySeconds", t.secondaryImpactDelaySeconds);\n        ReadIfPresent(root, "secondaryImpactLifetimeSeconds", t.secondaryImpactLifetimeSeconds);\n        ReadIfPresent(root, "secondaryImpactRadiusMeters", t.secondaryImpactRadiusMeters);\n        ReadIfPresent(root, "airborneCoreCount", t.airborneCoreCount);'''
if cpp.count(parser_foam) != 1:
    raise SystemExit("P700 parser secondary impact anchor missing")
cpp = cpp.replace(parser_foam, parser_foam_new, 1)

# Dedicated cavity alongside the three bubble scales; same physical event, separate visual structure.
bubble_anchor = '''            addBubbleScale(Render::TransientVfxPrimitive::BubbleMacro, tuning_.bubbleMacroCount,\n                           tuning_.bubbleMacroSizeMeters, 0.48F, 0x303U);\n        }'''
bubble_new = '''            addBubbleScale(Render::TransientVfxPrimitive::BubbleMacro, tuning_.bubbleMacroCount,\n                           tuning_.bubbleMacroSizeMeters, 0.48F, 0x303U);\n            if (underwaterActive && missile.launchBoosterActive)\n            {\n                auto cavity = emitterBase(Render::TransientVfxPrimitive::GasCavity, Render::TransientVfxBlendMode::Alpha,\n                    ScaledCount(tuning_.gasCavityCount, lodScale, salvoScale),\n                    std::fmod(phaseAge, tuning_.gasCavityLifetimeSeconds * 0.92F), tuning_.gasCavityLifetimeSeconds);\n                cavity.seed ^= 0x6A5CA71U;\n                cavity.originWorldMeters = trailEnd;\n                cavity.directionWorldUnit = trailDirection;\n                cavity.extentMeters = {(std::min)(trailLength, tuning_.gasCavityMaximumLengthMeters),\n                                       tuning_.gasCavityRadiusMeters, tuning_.gasCavityRadiusMeters * 0.72F};\n                cavity.minimumSizeMeters = tuning_.gasCavitySizeMeters[0];\n                cavity.maximumSizeMeters = tuning_.gasCavitySizeMeters[1];\n                cavity.opacity = 0.40F;\n                cavity.turbulence = tuning_.underwaterTurbulence * 1.18F;\n                cavity.spawnRadiusMeters = tuning_.gasCavityRadiusMeters * 0.18F;\n                cavity.applyDepthAttenuation = true;\n                cavity.linearColor = {0.62F, 0.79F, 0.87F};\n                append(cavity);\n            }\n        }'''
if cpp.count(bubble_anchor) != 1:
    raise SystemExit("P700 bubble/cavity insertion anchor missing")
cpp = cpp.replace(bubble_anchor, bubble_new, 1)

foam_anchor = '''            if (breachAge < tuning_.foamLifetimeSeconds)\n            {\n                auto foam = emitterBase(Render::TransientVfxPrimitive::Foam, Render::TransientVfxBlendMode::Alpha,'''
if cpp.count(foam_anchor) != 1:
    raise SystemExit("P700 foam anchor missing")
# Insert secondary impacts after the existing foam block by anchoring the close immediately before airborneBooster.
airborne_anchor = '''        }\n\n        const bool airborneBooster = missile.launchBoosterActive && missile.positionMeters.y >= missile.surfaceLevelYMeters - 0.15F;'''
secondary = '''            const float secondaryAge = breachAge - tuning_.secondaryImpactDelaySeconds;\n            if (secondaryAge >= 0.0F && secondaryAge < tuning_.secondaryImpactLifetimeSeconds)\n            {\n                auto impacts = emitterBase(Render::TransientVfxPrimitive::SurfaceImpact, Render::TransientVfxBlendMode::Alpha,\n                    ScaledCount(tuning_.secondaryImpactCount, lodScale, salvoScale), secondaryAge,\n                    tuning_.secondaryImpactLifetimeSeconds);\n                impacts.seed ^= 0x51A5BEEFU;\n                impacts.originWorldMeters = record.breachPosition;\n                impacts.extentMeters = {tuning_.secondaryImpactRadiusMeters * weatherSplashScale,\n                                        1.65F * weatherSplashScale,\n                                        tuning_.secondaryImpactRadiusMeters * 0.58F * weatherSplashScale};\n                impacts.minimumSizeMeters = 0.045F;\n                impacts.maximumSizeMeters = 0.22F;\n                impacts.opacity = 0.66F;\n                impacts.linearColor = {0.74F, 0.84F, 0.88F};\n                append(impacts);\n            }\n        }\n\n        const bool airborneBooster = missile.launchBoosterActive && missile.positionMeters.y >= missile.surfaceLevelYMeters - 0.15F;'''
if cpp.count(airborne_anchor) != 1:
    raise SystemExit("P700 secondary-impact placement anchor missing")
cpp = cpp.replace(airborne_anchor, secondary, 1)
write("Game/Weapons/P700LaunchVfx.cpp", cpp)

# JSON tuning.
config = read("Config/p700_vfx.json")
config = config.replace('''  "underwaterCoreParticleCount": 56,\n\n  "breachPreReactionDepthMeters": 6.5,''',
'''  "underwaterCoreParticleCount": 56,\n  "gasCavityCount": 36,\n  "gasCavityLifetimeSeconds": 1.85,\n  "gasCavityMaximumLengthMeters": 13.5,\n  "gasCavityRadiusMeters": 3.35,\n  "gasCavitySizeMeters": [0.72, 2.45],\n\n  "breachPreReactionDepthMeters": 6.5,''', 1)
config = config.replace('''  "foamLifetimeSeconds": 8.0,\n  "foamRadiusMeters": 11.0,\n''',
'''  "foamLifetimeSeconds": 8.0,\n  "foamRadiusMeters": 11.0,\n  "secondaryImpactCount": 48,\n  "secondaryImpactDelaySeconds": 0.82,\n  "secondaryImpactLifetimeSeconds": 1.35,\n  "secondaryImpactRadiusMeters": 10.5,\n''', 1)
write("Config/p700_vfx.json", config)

# Regression: explicit cavity, data-driven bounds, and late return splash.
test = read("Tests/P700LaunchVfxTest.cpp")
test = test.replace('''        tuning->foamLifetimeSeconds <= tuning->dropletLifetimeSeconds ||\n        tuning->waterCrownCount == 0U || tuning->waterCrownLifetimeSeconds <= 0.0F)''',
'''        tuning->foamLifetimeSeconds <= tuning->dropletLifetimeSeconds ||\n        tuning->gasCavityCount == 0U || tuning->gasCavityLifetimeSeconds <= 0.0F ||\n        tuning->secondaryImpactCount == 0U || tuning->secondaryImpactDelaySeconds <= 0.0F ||\n        tuning->waterCrownCount == 0U || tuning->waterCrownLifetimeSeconds <= 0.0F)''', 1)
test = test.replace('''        !HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMacro) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::ExhaustCore) ||''',
'''        !HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMacro) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::GasCavity) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::ExhaustCore) ||''', 1)
late_anchor = '''    missile.phase = P700GranitPhase::PostExitTransition;'''
late_test = '''    auto returningWater = vfx.BuildFrame(\n        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, environment, 2.18);\n    if (!returningWater || !HasPrimitive(*returningWater, TransientVfxPrimitive::SurfaceImpact) ||\n        !HasPrimitive(*returningWater, TransientVfxPrimitive::Foam))\n    {\n        std::cerr << "P-700 breach did not retain secondary return impacts/foam after the primary crown\\n";\n        return false;\n    }\n\n    missile.phase = P700GranitPhase::PostExitTransition;'''
if test.count(late_anchor) != 1:
    raise SystemExit("P700 lifecycle late-water test anchor missing")
test = test.replace(late_anchor, late_test, 1)
write("Tests/P700LaunchVfxTest.cpp", test)

print("P-700 stage2 art patch applied")
