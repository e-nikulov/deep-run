from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# Stage 1 owns current-main merge compatibility, local lighting, weather and camera integration.
exec(compile(read("Tools/p700_stage1_completion_v3.py"), "p700_stage2_base", "exec"))

# Reusable renderer primitives required by the remaining physical launch language.
replace_once(
    "Engine/Render/TransientVfx.h",
    "    Particulate,\n};",
    "    Particulate,\n    GasCavity,\n    SurfaceImpact,\n    SurfaceShadow,\n    RigidDebris,\n};")

shader_path = "Shaders/Particles/TransientVfx.hlsl"
shader = read(shader_path)
branch_anchor = r'''    else
    {
        center -= direction * r0 * ExtentKind.x;
        center += side * signed0 * ExtentKind.y;
        center += depthSide * signed1 * ExtentKind.z;
        center += side * sin(phase) * VelocityTurbulence.w * 0.12F;
        center += VelocityTurbulence.xyz * delayedAge * 0.08F;
        alphaScale = saturate(0.65F - normalizedAge * 0.45F);
    }

    const float2 corner = QuadCorner(vertexId);'''
branches = r'''    else if (kind == 11u)
    {
        // Generic compressible-gas cavity: a short-lived, irregular chain of expanding cells rather than
        // one transparent sphere. The semantic source remains Game-owned.
        const float axial = r0 * ExtentKind.x * (0.35F + 0.65F * normalizedAge);
        const float envelope = sin(saturate(normalizedAge) * 3.14159265F);
        const float radial = ExtentKind.y * (0.18F + 0.82F * r2) * (0.35F + 0.65F * envelope);
        center -= direction * axial;
        center += side * signed0 * radial;
        center += depthSide * signed1 * ExtentKind.z * (0.25F + 0.75F * r3);
        center += side * sin(phase * (1.3F + r4)) * VelocityTurbulence.w * 0.11F;
        center.y += (0.25F + 1.15F * r2) * delayedAge;
        sizeMeters *= (0.75F + 1.85F * envelope) * lerp(0.65F, 1.35F, r4);
        alphaScale = saturate(envelope * (0.48F + 0.42F * r3));
    }
    else if (kind == 12u)
    {
        // Delayed secondary surface impacts. Every instance gets a deterministic offset and slightly different
        // rise/collapse phase so the footprint does not read as a synchronized particle ring.
        const float angle = r0 * 6.28318530718F;
        const float radial = sqrt(r1) * ExtentKind.x;
        center.x += cos(angle) * radial;
        center.z += sin(angle) * radial * max(ExtentKind.z, 0.15F);
        center += wind * delayedAge * 0.018F;
        center.y = SurfaceAbsorptionR.x + 0.03F + ExtentKind.y * (0.25F + 0.75F * r2) *
            sin(saturate(normalizedAge) * 3.14159265F);
        sizeMeters *= lerp(0.55F, 1.35F, sin(saturate(normalizedAge) * 3.14159265F));
        alphaScale = saturate((1.0F - normalizedAge) * (0.55F + 0.35F * r4));
    }
    else if (kind == 13u)
    {
        // Projected moving surface shadow. It is deliberately subtle and exists only where Game submits it;
        // no fake wake or persistent trail is introduced.
        const float axial = signed0 * ExtentKind.x * 0.5F;
        center += direction * axial;
        center += side * signed1 * ExtentKind.y * 0.5F;
        center += depthSide * (r2 * 2.0F - 1.0F) * ExtentKind.z * 0.35F;
        center.y = SurfaceAbsorptionR.x + 0.035F;
        sizeMeters *= lerp(0.75F, 1.25F, r3);
        alphaScale = saturate((1.0F - normalizedAge * 0.12F) * (0.72F + 0.24F * r4));
    }
    else if (kind == 14u)
    {
        // Lightweight rigid debris presentation: ballistic translation plus visual tumble. It has no collision
        // or gameplay authority and is intended for small detached panels/covers.
        float3 velocity = VelocityTurbulence.xyz;
        velocity += side * signed0 * ExtentKind.y;
        velocity += depthSide * signed1 * ExtentKind.z;
        center += velocity * delayedAge;
        center.y -= 0.5F * CameraGravity.z * delayedAge * delayedAge;
        sizeMeters *= lerp(0.80F, 1.12F, r2);
        alphaScale = saturate(1.0F - normalizedAge * normalizedAge);
    }
    else
    {
        center -= direction * r0 * ExtentKind.x;
        center += side * signed0 * ExtentKind.y;
        center += depthSide * signed1 * ExtentKind.z;
        center += side * sin(phase) * VelocityTurbulence.w * 0.12F;
        center += VelocityTurbulence.xyz * delayedAge * 0.08F;
        alphaScale = saturate(0.65F - normalizedAge * 0.45F);
    }

    float2 corner = QuadCorner(vertexId);
    if (kind == 14u)
    {
        const float tumble = delayedAge * (7.0F + 5.0F * r2) + r1 * 6.28318530718F;
        const float c = cos(tumble);
        const float s = sin(tumble);
        corner = float2(c * corner.x - s * corner.y, s * corner.x + c * corner.y * 0.34F);
    }'''
if shader.count(branch_anchor) != 1:
    raise SystemExit("TransientVfx HLSL stage2 branch anchor missing")
shader = shader.replace(branch_anchor, branches, 1)
old_ps = r'''    if (kind == 8u || kind == 9u)
        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.35F + input.corner.y * input.corner.y));
    else if (kind == 3u)
        radial = saturate(1.0F - dot(input.corner, input.corner) * 1.35F);'''
new_ps = r'''    if (kind == 8u || kind == 9u || kind == 11u || kind == 12u)
        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.35F + input.corner.y * input.corner.y));
    else if (kind == 13u)
        radial = saturate(1.0F - (input.corner.x * input.corner.x * 0.18F + input.corner.y * input.corner.y * 1.8F));
    else if (kind == 14u)
        radial = saturate(1.0F - max(abs(input.corner.x), abs(input.corner.y) * 1.8F));
    else if (kind == 3u)
        radial = saturate(1.0F - dot(input.corner, input.corner) * 1.35F);'''
if shader.count(old_ps) != 1:
    raise SystemExit("TransientVfx HLSL stage2 pixel anchor missing")
shader = shader.replace(old_ps, new_ps, 1)
write(shader_path, shader)

# Data-driven controls for the new physical components.
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float underwaterTurbulence = 5.5F;\n\n    float underwaterCoreLengthMeters = 1.8F;",
    "    float underwaterTurbulence = 5.5F;\n    std::uint32_t gasCavityCount = 36U;\n    float gasCavityLifetimeSeconds = 1.45F;\n    float gasCavityLengthMeters = 9.0F;\n    float gasCavityRadiusMeters = 2.4F;\n    float gasCavityOpacity = 0.40F;\n\n    float underwaterCoreLengthMeters = 1.8F;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float waterSheetLifetimeSeconds = 1.15F;\n\n    float preBreachBulgeMaximumMeters = 1.8F;",
    "    float waterSheetLifetimeSeconds = 1.15F;\n    std::uint32_t secondaryImpactCount = 56U;\n    float secondaryImpactLifetimeSeconds = 3.2F;\n    float secondaryImpactRadiusMeters = 10.5F;\n\n    float preBreachBulgeMaximumMeters = 1.8F;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "    float condensationLifetimeSeconds = 0.45F;\n\n    float cameraImpulseAmplitudeAtTenMeters = 0.085F;",
    "    float condensationLifetimeSeconds = 0.45F;\n\n    float seaSkimShadowMaximumAltitudeMeters = 42.0F;\n    float seaSkimShadowLengthMeters = 10.0F;\n    float seaSkimShadowWidthMeters = 2.8F;\n    float seaSkimShadowOpacity = 0.22F;\n\n    float intakeCoverDebrisLifetimeSeconds = 0.95F;\n    float intakeCoverDebrisSizeMeters = 0.46F;\n\n    float cameraImpulseAmplitudeAtTenMeters = 0.085F;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "        bool previousBoosterAttached = true;\n        bool initialized = false;",
    "        bool previousBoosterAttached = true;\n        bool previousNoseProtectionCapAttached = true;\n        bool initialized = false;")
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    "        double breachTimeSeconds = 0.0;\n        double lastSeenTimeSeconds = 0.0;",
    "        double breachTimeSeconds = 0.0;\n        bool hasIntakeCoverSeparation = false;\n        std::array<float, 3> intakeCoverSeparationPosition{};\n        std::array<float, 3> intakeCoverSeparationDirection{1.0F, 0.0F, 0.0F};\n        double intakeCoverSeparationTimeSeconds = 0.0;\n        double lastSeenTimeSeconds = 0.0;")

cpp_path = "Game/Weapons/P700LaunchVfx.cpp"
cpp = read(cpp_path)
cpp = cpp.replace(
    "        t.underwaterCoreParticleCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||\n        t.mistCount == 0U || t.waterSheetCount == 0U || t.foamCount == 0U || t.airborneCoreCount == 0U ||",
    "        t.underwaterCoreParticleCount == 0U || t.gasCavityCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||\n        t.mistCount == 0U || t.waterSheetCount == 0U || t.secondaryImpactCount == 0U || t.foamCount == 0U || t.airborneCoreCount == 0U ||", 1)
cpp = cpp.replace(
    "        t.bubbleLifetimeSeconds, t.underwaterTrailRadiusMeters, t.underwaterTurbulence,\n        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,",
    "        t.bubbleLifetimeSeconds, t.underwaterTrailRadiusMeters, t.underwaterTurbulence,\n        t.gasCavityLifetimeSeconds, t.gasCavityLengthMeters, t.gasCavityRadiusMeters,\n        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,", 1)
cpp = cpp.replace("const std::array<float, 28> scalars{", "const std::array<float, 37> scalars{", 1)
cpp = cpp.replace(
    "        t.dropletLifetimeSeconds, t.mistLifetimeSeconds, t.mistDensity, t.waterSheetLifetimeSeconds,\n        t.preBreachBulgeMaximumMeters,",
    "        t.dropletLifetimeSeconds, t.mistLifetimeSeconds, t.mistDensity, t.waterSheetLifetimeSeconds,\n        t.secondaryImpactLifetimeSeconds, t.secondaryImpactRadiusMeters,\n        t.preBreachBulgeMaximumMeters,", 1)
cpp = cpp.replace(
    "        t.cruiseExhaustLifetimeSeconds, t.condensationLifetimeSeconds};",
    "        t.cruiseExhaustLifetimeSeconds, t.condensationLifetimeSeconds,\n        t.seaSkimShadowMaximumAltitudeMeters, t.seaSkimShadowLengthMeters, t.seaSkimShadowWidthMeters,\n        t.intakeCoverDebrisLifetimeSeconds, t.intakeCoverDebrisSizeMeters};", 1)
cpp = cpp.replace(
    "    if (!std::isfinite(t.cruiseExhaustOpacity) || t.cruiseExhaustOpacity < 0.0F || t.cruiseExhaustOpacity > 1.0F ||",
    "    if (!std::isfinite(t.gasCavityOpacity) || t.gasCavityOpacity < 0.0F || t.gasCavityOpacity > 1.0F ||\n        !std::isfinite(t.seaSkimShadowOpacity) || t.seaSkimShadowOpacity < 0.0F || t.seaSkimShadowOpacity > 1.0F ||\n        !std::isfinite(t.cruiseExhaustOpacity) || t.cruiseExhaustOpacity < 0.0F || t.cruiseExhaustOpacity > 1.0F ||", 1)

# JSON loader additions.
cpp = cpp.replace(
    "        ReadIfPresent(root, \"underwaterTurbulence\", t.underwaterTurbulence);\n        ReadIfPresent(root, \"underwaterCoreLengthMeters\", t.underwaterCoreLengthMeters);",
    "        ReadIfPresent(root, \"underwaterTurbulence\", t.underwaterTurbulence);\n        ReadIfPresent(root, \"gasCavityCount\", t.gasCavityCount);\n        ReadIfPresent(root, \"gasCavityLifetimeSeconds\", t.gasCavityLifetimeSeconds);\n        ReadIfPresent(root, \"gasCavityLengthMeters\", t.gasCavityLengthMeters);\n        ReadIfPresent(root, \"gasCavityRadiusMeters\", t.gasCavityRadiusMeters);\n        ReadIfPresent(root, \"gasCavityOpacity\", t.gasCavityOpacity);\n        ReadIfPresent(root, \"underwaterCoreLengthMeters\", t.underwaterCoreLengthMeters);", 1)
cpp = cpp.replace(
    "        ReadIfPresent(root, \"waterSheetLifetimeSeconds\", t.waterSheetLifetimeSeconds);\n        ReadIfPresent(root, \"preBreachBulgeMaximumMeters\", t.preBreachBulgeMaximumMeters);",
    "        ReadIfPresent(root, \"waterSheetLifetimeSeconds\", t.waterSheetLifetimeSeconds);\n        ReadIfPresent(root, \"secondaryImpactCount\", t.secondaryImpactCount);\n        ReadIfPresent(root, \"secondaryImpactLifetimeSeconds\", t.secondaryImpactLifetimeSeconds);\n        ReadIfPresent(root, \"secondaryImpactRadiusMeters\", t.secondaryImpactRadiusMeters);\n        ReadIfPresent(root, \"preBreachBulgeMaximumMeters\", t.preBreachBulgeMaximumMeters);", 1)
cpp = cpp.replace(
    "        ReadIfPresent(root, \"condensationLifetimeSeconds\", t.condensationLifetimeSeconds);\n        ReadIfPresent(root, \"cameraImpulseAmplitudeAtTenMeters\", t.cameraImpulseAmplitudeAtTenMeters);",
    "        ReadIfPresent(root, \"condensationLifetimeSeconds\", t.condensationLifetimeSeconds);\n        ReadIfPresent(root, \"seaSkimShadowMaximumAltitudeMeters\", t.seaSkimShadowMaximumAltitudeMeters);\n        ReadIfPresent(root, \"seaSkimShadowLengthMeters\", t.seaSkimShadowLengthMeters);\n        ReadIfPresent(root, \"seaSkimShadowWidthMeters\", t.seaSkimShadowWidthMeters);\n        ReadIfPresent(root, \"seaSkimShadowOpacity\", t.seaSkimShadowOpacity);\n        ReadIfPresent(root, \"intakeCoverDebrisLifetimeSeconds\", t.intakeCoverDebrisLifetimeSeconds);\n        ReadIfPresent(root, \"intakeCoverDebrisSizeMeters\", t.intakeCoverDebrisSizeMeters);\n        ReadIfPresent(root, \"cameraImpulseAmplitudeAtTenMeters\", t.cameraImpulseAmplitudeAtTenMeters);", 1)

# Record the real lifecycle edge where the intake protection is no longer attached.
cap_anchor = '''        if (missile.phase == Weapons::P700GranitPhase::WaterExit && !record.hasBreach)\n        {\n            record.hasBreach = true;\n            record.breachPosition = {position[0], missile.surfaceLevelYMeters, position[2]};\n            record.breachTimeSeconds = simulationTimeSeconds;\n        }\n\n        const auto lod = SelectLod(missile, camera);'''
cap_repl = '''        if (missile.phase == Weapons::P700GranitPhase::WaterExit && !record.hasBreach)\n        {\n            record.hasBreach = true;\n            record.breachPosition = {position[0], missile.surfaceLevelYMeters, position[2]};\n            record.breachTimeSeconds = simulationTimeSeconds;\n        }\n        if (record.initialized && record.previousNoseProtectionCapAttached &&\n            !missile.noseProtectionCapAttached && !record.hasIntakeCoverSeparation)\n        {\n            record.hasIntakeCoverSeparation = true;\n            record.intakeCoverSeparationPosition = AddScaled(position, direction, 4.72F);\n            record.intakeCoverSeparationDirection = direction;\n            record.intakeCoverSeparationTimeSeconds = simulationTimeSeconds;\n        }\n\n        const auto lod = SelectLod(missile, camera);'''
if cpp.count(cap_anchor) != 1:
    raise SystemExit("P700 intake-cover edge anchor missing")
cpp = cpp.replace(cap_anchor, cap_repl, 1)

# Gas cavity is distinct from the three bubble scales. Large salvos trade meso/particulate emitters for the
# cavity so all 24 missiles remain represented inside the hard 96-emitter bound.
old_scales = '''            addBubbleScale(Render::TransientVfxPrimitive::BubbleMicro, tuning_.bubbleMicroCount,\n                           tuning_.bubbleMicroSizeMeters, 0.52F, 0x101U);\n            addBubbleScale(Render::TransientVfxPrimitive::BubbleMeso, tuning_.bubbleMesoCount,\n                           tuning_.bubbleMesoSizeMeters, 0.62F, 0x202U);\n            addBubbleScale(Render::TransientVfxPrimitive::BubbleMacro, tuning_.bubbleMacroCount,\n                           tuning_.bubbleMacroSizeMeters, 0.48F, 0x303U);\n        }'''
new_scales = '''            addBubbleScale(Render::TransientVfxPrimitive::BubbleMicro, tuning_.bubbleMicroCount,\n                           tuning_.bubbleMicroSizeMeters, 0.52F, 0x101U);\n            if (activeMissileCount <= 6U)\n                addBubbleScale(Render::TransientVfxPrimitive::BubbleMeso, tuning_.bubbleMesoCount,\n                               tuning_.bubbleMesoSizeMeters, 0.62F, 0x202U);\n            addBubbleScale(Render::TransientVfxPrimitive::BubbleMacro, tuning_.bubbleMacroCount,\n                           tuning_.bubbleMacroSizeMeters, 0.48F, 0x303U);\n\n            if (underwaterActive)\n            {\n                auto cavity = emitterBase(Render::TransientVfxPrimitive::GasCavity, Render::TransientVfxBlendMode::Alpha,\n                    ScaledCount(tuning_.gasCavityCount, lodScale, salvoScale),\n                    std::fmod(phaseAge, tuning_.gasCavityLifetimeSeconds * 0.92F), tuning_.gasCavityLifetimeSeconds);\n                cavity.originWorldMeters = tail;\n                cavity.directionWorldUnit = trailDirection;\n                cavity.extentMeters = {tuning_.gasCavityLengthMeters, tuning_.gasCavityRadiusMeters,\n                                       tuning_.gasCavityRadiusMeters * 0.72F};\n                cavity.minimumSizeMeters = 0.38F;\n                cavity.maximumSizeMeters = 1.55F;\n                cavity.opacity = tuning_.gasCavityOpacity;\n                cavity.turbulence = tuning_.underwaterTurbulence * 1.15F;\n                cavity.spawnRadiusMeters = tuning_.gasCavityRadiusMeters * 0.18F;\n                cavity.applyDepthAttenuation = true;\n                cavity.linearColor = {0.76F, 0.88F, 0.94F};\n                append(cavity);\n            }\n        }'''
if cpp.count(old_scales) != 1:
    raise SystemExit("P700 bubble-scale anchor missing")
cpp = cpp.replace(old_scales, new_scales, 1)
cpp = cpp.replace(
    "            auto particulate = emitterBase(Render::TransientVfxPrimitive::Particulate, Render::TransientVfxBlendMode::Alpha,\n                ScaledCount(74U, lodScale, salvoScale), std::fmod(phaseAge, 1.5F), 1.6F);",
    "            auto particulate = emitterBase(Render::TransientVfxPrimitive::Particulate, Render::TransientVfxBlendMode::Alpha,\n                ScaledCount(activeMissileCount <= 6U ? 74U : 1U, lodScale, salvoScale), std::fmod(phaseAge, 1.5F), 1.6F);", 1)
cpp = cpp.replace(
    "            append(particulate);\n\n            const float depth =",
    "            if (activeMissileCount <= 6U) append(particulate);\n\n            const float depth =", 1)

# Secondary splash impacts live longer than the primary crown but remain much smaller than the breach.
impact_anchor = '''            if (breachAge < tuning_.foamLifetimeSeconds)\n            {'''
impact_block = '''            if (breachAge > 0.55F && breachAge < tuning_.secondaryImpactLifetimeSeconds)\n            {\n                auto impacts = emitterBase(Render::TransientVfxPrimitive::SurfaceImpact, Render::TransientVfxBlendMode::Alpha,\n                    ScaledCount(tuning_.secondaryImpactCount, lodScale, salvoScale), breachAge - 0.55F,\n                    tuning_.secondaryImpactLifetimeSeconds - 0.55F);\n                impacts.originWorldMeters = record.breachPosition;\n                impacts.directionWorldUnit = record.launchDirection;\n                impacts.extentMeters = {tuning_.secondaryImpactRadiusMeters * weatherSplashScale,\n                                        1.05F * weatherSplashScale, 0.72F};\n                impacts.minimumSizeMeters = 0.05F;\n                impacts.maximumSizeMeters = 0.22F;\n                impacts.opacity = 0.54F;\n                impacts.turbulence = 1.4F;\n                impacts.linearColor = {0.72F, 0.83F, 0.88F};\n                append(impacts);\n            }\n            if (breachAge < tuning_.foamLifetimeSeconds)\n            {'''
if cpp.count(impact_anchor) != 1:
    raise SystemExit("P700 secondary-impact anchor missing")
cpp = cpp.replace(impact_anchor, impact_block, 1)

# A small real separation event for the intake protection even though the accepted eight-object GLB has no
# dedicated cap node. This is a generic ballistic debris presentation, not a body scale/visibility trick.
cond_anchor = '''        if (missile.phase == Weapons::P700GranitPhase::AirborneDeploying &&\n            environment.humidityFraction >= tuning_.condensationHumidityThreshold &&'''
cap_block = '''        if (record.hasIntakeCoverSeparation)\n        {\n            const float coverAge = static_cast<float>(simulationTimeSeconds - record.intakeCoverSeparationTimeSeconds);\n            if (coverAge >= 0.0F && coverAge < tuning_.intakeCoverDebrisLifetimeSeconds)\n            {\n                auto cover = emitterBase(Render::TransientVfxPrimitive::RigidDebris, Render::TransientVfxBlendMode::Alpha,\n                    1U, coverAge, tuning_.intakeCoverDebrisLifetimeSeconds);\n                cover.originWorldMeters = record.intakeCoverSeparationPosition;\n                cover.directionWorldUnit = record.intakeCoverSeparationDirection;\n                cover.baseVelocityMetersPerSecond = {\n                    record.intakeCoverSeparationDirection[0] * missile.speedMetersPerSecond * 0.72F,\n                    record.intakeCoverSeparationDirection[1] * missile.speedMetersPerSecond * 0.72F + 3.2F,\n                    record.intakeCoverSeparationDirection[2] * missile.speedMetersPerSecond * 0.72F};\n                cover.extentMeters = {0.0F, 1.35F, 0.75F};\n                cover.minimumSizeMeters = tuning_.intakeCoverDebrisSizeMeters;\n                cover.maximumSizeMeters = tuning_.intakeCoverDebrisSizeMeters * 1.08F;\n                cover.opacity = 0.82F;\n                cover.gravityMetersPerSecondSquared = 9.81F;\n                cover.linearColor = {0.20F, 0.22F, 0.23F};\n                append(cover);\n            }\n        }\n\n        const float altitudeAboveSurfaceMeters = missile.positionMeters.y - missile.surfaceLevelYMeters;\n        if ((missile.phase == Weapons::P700GranitPhase::AirborneDeploying ||\n             missile.phase == Weapons::P700GranitPhase::Cruise ||\n             missile.phase == Weapons::P700GranitPhase::Terminal) &&\n            altitudeAboveSurfaceMeters >= 0.0F &&\n            altitudeAboveSurfaceMeters <= tuning_.seaSkimShadowMaximumAltitudeMeters &&\n            lod != P700VfxLod::Lod3Strategic)\n        {\n            auto shadow = emitterBase(Render::TransientVfxPrimitive::SurfaceShadow, Render::TransientVfxBlendMode::Alpha,\n                ScaledCount(14U, lodScale, salvoScale), std::fmod(phaseAge, 0.80F), 0.85F);\n            shadow.originWorldMeters = {position[0], missile.surfaceLevelYMeters + 0.035F, position[2]};\n            shadow.directionWorldUnit = direction;\n            shadow.extentMeters = {tuning_.seaSkimShadowLengthMeters, tuning_.seaSkimShadowWidthMeters,\n                                   tuning_.seaSkimShadowWidthMeters};\n            shadow.minimumSizeMeters = 0.42F;\n            shadow.maximumSizeMeters = 0.95F;\n            const float altitudeFade = 1.0F - altitudeAboveSurfaceMeters / tuning_.seaSkimShadowMaximumAltitudeMeters;\n            shadow.opacity = tuning_.seaSkimShadowOpacity * (0.35F + 0.65F * altitudeFade);\n            shadow.linearColor = {0.015F, 0.020F, 0.025F};\n            append(shadow);\n        }\n\n        if (missile.phase == Weapons::P700GranitPhase::AirborneDeploying &&\n            environment.humidityFraction >= tuning_.condensationHumidityThreshold &&'''
if cpp.count(cond_anchor) != 1:
    raise SystemExit("P700 cap/shadow insertion anchor missing")
cpp = cpp.replace(cond_anchor, cap_block, 1)
cpp = cpp.replace(
    "        record.previousBoosterAttached = missile.launchBoosterAttached;\n        record.initialized = true;",
    "        record.previousBoosterAttached = missile.launchBoosterAttached;\n        record.previousNoseProtectionCapAttached = missile.noseProtectionCapAttached;\n        record.initialized = true;", 1)
write(cpp_path, cpp)

# Production defaults remain external to C++.
config_path = "Config/p700_vfx.json"
config = read(config_path)
config = config.replace(
    '  "underwaterTurbulence": 5.5,\n\n  "underwaterCoreLengthMeters": 1.8,',
    '  "underwaterTurbulence": 5.5,\n  "gasCavityCount": 36,\n  "gasCavityLifetimeSeconds": 1.45,\n  "gasCavityLengthMeters": 9.0,\n  "gasCavityRadiusMeters": 2.4,\n  "gasCavityOpacity": 0.40,\n\n  "underwaterCoreLengthMeters": 1.8,', 1)
config = config.replace(
    '  "waterSheetLifetimeSeconds": 1.15,\n  "preBreachBulgeMaximumMeters": 1.8,',
    '  "waterSheetLifetimeSeconds": 1.15,\n  "secondaryImpactCount": 56,\n  "secondaryImpactLifetimeSeconds": 3.2,\n  "secondaryImpactRadiusMeters": 10.5,\n  "preBreachBulgeMaximumMeters": 1.8,', 1)
config = config.replace(
    '  "condensationLifetimeSeconds": 0.45,\n\n  "cameraImpulseAmplitudeAtTenMeters": 0.085,',
    '  "condensationLifetimeSeconds": 0.45,\n\n  "seaSkimShadowMaximumAltitudeMeters": 42.0,\n  "seaSkimShadowLengthMeters": 10.0,\n  "seaSkimShadowWidthMeters": 2.8,\n  "seaSkimShadowOpacity": 0.22,\n  "intakeCoverDebrisLifetimeSeconds": 0.95,\n  "intakeCoverDebrisSizeMeters": 0.46,\n\n  "cameraImpulseAmplitudeAtTenMeters": 0.085,', 1)
write(config_path, config)

# Extend regression acceptance for each newly explicit physical component.
test_path = "Tests/P700LaunchVfxTest.cpp"
test = read(test_path)
test = test.replace(
    '!HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMacro) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::ExhaustCore)',
    '!HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMacro) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::GasCavity) ||\n        !HasPrimitive(*underwater, TransientVfxPrimitive::ExhaustCore)', 1)
test = test.replace(
    '!HasPrimitive(*breach, TransientVfxPrimitive::Mist) ||\n        !HasPrimitive(*breach, TransientVfxPrimitive::Foam) ||',
    '!HasPrimitive(*breach, TransientVfxPrimitive::Mist) ||\n        !HasPrimitive(*breach, TransientVfxPrimitive::Foam) ||', 1)
# Advance the same breach state far enough for deterministic falling-water secondary impacts.
needle = '''    missile.phase = P700GranitPhase::PostExitTransition;\n    missile.positionMeters = {.x = 16.0F, .y = 11.0F, .z = 0.0F};'''
insert = '''    auto lateBreach = vfx.BuildFrame(std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, environment, 1.90);\n    if (!lateBreach || !HasPrimitive(*lateBreach, TransientVfxPrimitive::SurfaceImpact))\n    {\n        std::cerr << "P-700 breach did not preserve delayed secondary surface impacts\\n";\n        return false;\n    }\n\n    missile.phase = P700GranitPhase::PostExitTransition;\n    missile.positionMeters = {.x = 16.0F, .y = 11.0F, .z = 0.0F};'''
if test.count(needle) != 1:
    raise SystemExit("P700 lifecycle late-breach test anchor missing")
test = test.replace(needle, insert, 1)
# Make the cap lifecycle edge observable, then verify its one-piece rigid debris and low-altitude shadow.
test = test.replace(
    '    missile.launchBoosterActive = false;\n    missile.launchBoosterAttached = false;\n    missile.mainEngineActive = true;',
    '    missile.launchBoosterActive = false;\n    missile.launchBoosterAttached = false;\n    missile.noseProtectionCapAttached = false;\n    missile.mainEngineActive = true;', 1)
test = test.replace(
    '!HasPrimitive(*transition, TransientVfxPrimitive::ExhaustTurbulent) ||\n        !HasAudio(*transition, P700LaunchAudioEvent::BoosterSeparate)',
    '!HasPrimitive(*transition, TransientVfxPrimitive::ExhaustTurbulent) ||\n        !HasPrimitive(*transition, TransientVfxPrimitive::RigidDebris) ||\n        !HasAudio(*transition, P700LaunchAudioEvent::BoosterSeparate)', 1)
test = test.replace(
    '!HasPrimitive(*deploy, TransientVfxPrimitive::Mist))',
    '!HasPrimitive(*deploy, TransientVfxPrimitive::Mist) ||\n        !HasPrimitive(*deploy, TransientVfxPrimitive::SurfaceShadow))', 1)
write(test_path, test)

print("P-700 stage2 physical VFX completion patch applied")
