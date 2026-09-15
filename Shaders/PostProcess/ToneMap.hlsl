Texture2D<float4> SceneColorHDR : register(t0);
SamplerState SceneColorSampler : register(s0);

cbuffer ScenePresentationConstants : register(b1)
{
    float SurfaceLevelYMeters;
    float3 AttenuationPerMeterRgb;
    float3 DeepAmbientRgb;
    float FogExtinctionPerMeter;
    float3 CameraPlaneCenterWorldPosition;
    float ScenePresentationPadding0;
    float3 CameraViewDirection;
    float ScenePresentationPadding1;
    float3 FogColorRgb;
    float AtmosphereExtinctionPerMeter;
    float3 AtmosphereFogColorRgb;
    float CloudCoverFraction;
    float PrecipitationFraction;
    float SunTransmittance;
    float SkyLuminanceMultiplier;
    float HorizonHazeFraction;
    float CloudAdvection;
    float AtmosphereBoundaryViewportY;
    float PresentationTimeSeconds;
    float CloudPatternOffset;
    float LightningFlashIntensity;
    float LightningViewportX;
    float LightningPatternOffset;
    float ScenePresentationPadding2;
};

struct FullscreenPixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FullscreenPixelInput VSMain(const uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0F, -1.0F),
        float2(-1.0F, 3.0F),
        float2(3.0F, -1.0F)};

    FullscreenPixelInput output;
    output.position = float4(positions[vertexId], 0.0F, 1.0F);
    output.uv = float2(positions[vertexId].x * 0.5F + 0.5F, -positions[vertexId].y * 0.5F + 0.5F);
    return output;
}

float3 ToneMapSceneLinear(const float3 sceneLinear)
{
    // Fixed neutral exposure with a per-channel Reinhard shoulder. Procedural sky/sun/lightning is deliberately
    // evaluated before this function so SDR and HDR consume the same scene-linear radiance source.
    const float3 nonNegative = max(sceneLinear, 0.0F.xxx);
    return nonNegative / (1.0F.xxx + nonNegative);
}

float3 LinearToSrgb(const float3 linearColor)
{
    const float3 clamped = saturate(linearColor);
    const float3 low = clamped * 12.92F;
    const float3 high = 1.055F * pow(clamped, 1.0F / 2.4F) - 0.055F;
    return float3(
        clamped.r <= 0.0031308F ? low.r : high.r,
        clamped.g <= 0.0031308F ? low.g : high.g,
        clamped.b <= 0.0031308F ? low.b : high.b);
}

// M3-A.1 fixed presentation policy: scene-linear 1.0 is 80 nits / scRGB 1.0. The 1,000-nit engineering
// ceiling is a deterministic temporary shoulder, not display calibration or HDR10 metadata.
static const float HdrReferenceWhiteNits = 80.0F;
static const float ScRgbNominalWhiteNits = 80.0F;
static const float HdrOutputPeakNits = 1000.0F;

float3 MapSceneLinearToHdrScRgb(const float3 sceneLinear)
{
    const float3 nonNegative = max(sceneLinear, 0.0F.xxx);
    const float referenceWhiteScale = HdrReferenceWhiteNits / ScRgbNominalWhiteNits;
    const float peakScRgb = HdrOutputPeakNits / ScRgbNominalWhiteNits;
    return referenceWhiteScale * peakScRgb *
           (1.0F.xxx - (peakScRgb - 1.0F) / (nonNegative + (peakScRgb - 1.0F)));
}

float InterleavedGradientNoise(const float2 pixelPosition)
{
    return frac(52.9829189F * frac(dot(pixelPosition, float2(0.06711056F, 0.00583715F))));
}

float3 DitherForQuantization(const float3 value, const float quantizationSteps, const float2 pixelPosition)
{
    const float centeredNoise = InterleavedGradientNoise(pixelPosition) - 0.5F;
    const float3 ditherWeight = saturate(max(value, 0.0F.xxx) * quantizationSteps);
    return value + (centeredNoise / quantizationSteps) * ditherWeight;
}

// The M5 Game presentation writes exactly this RGB sentinel into pixels above the projected sea surface and
// stores the waterline viewport fraction in alpha. Geometry rendered afterwards replaces the marker normally,
// so this final scene-linear resolve affects background sky only and never paints over ships or missiles.
static const float3 ProceduralSkyMarkerRgb = float3(0.00390625F, 0.00781250F, 0.01562500F);

bool IsProceduralSkyMarker(const float4 scene)
{
    const float3 delta = abs(scene.rgb - ProceduralSkyMarkerRgb);
    const float maxDelta = max(delta.r, max(delta.g, delta.b));
    return scene.a > 0.0F && scene.a <= 0.45F && maxDelta < 0.0015F;
}

float HashAtmosphere21(const float2 p)
{
    return frac(sin(dot(p, float2(127.1F, 311.7F))) * 43758.5453123F);
}

float AtmosphereValueNoise(const float2 p)
{
    const float2 c = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0F - 2.0F * f);
    return lerp(
        lerp(HashAtmosphere21(c), HashAtmosphere21(c + float2(1, 0)), u.x),
        lerp(HashAtmosphere21(c + float2(0, 1)), HashAtmosphere21(c + float2(1, 1)), u.x), u.y);
}

float AtmosphereFbm(float2 p)
{
    float sum = 0.0F;
    float amplitude = 0.55F;
    [unroll] for (int octave = 0; octave < 4; ++octave)
    {
        sum += AtmosphereValueNoise(p) * amplitude;
        p = p * 2.03F + float2(17.1F, 9.2F);
        amplitude *= 0.48F;
    }
    return sum;
}

float LightningChannelCoverage(const float2 uv, const float safeSkyBottom)
{
    if (LightningFlashIntensity <= 1.0e-4F)
        return 0.0F;

    const float skyT = saturate(uv.y / safeSkyBottom);
    const float verticalMask = smoothstep(0.10F, 0.18F, skyT) *
                               (1.0F - smoothstep(0.88F, 0.98F, skyT));
    const float jagged = AtmosphereValueNoise(float2(
        skyT * 19.0F + LightningPatternOffset * 23.0F,
        LightningPatternOffset * 41.0F));
    const float bend = (jagged - 0.5F) * (0.035F + 0.055F * skyT);
    const float primaryX = LightningViewportX + bend;
    const float pixelWidth = max(fwidth(uv.x) * 1.35F, 0.00075F);
    const float primary = 1.0F - smoothstep(pixelWidth, pixelWidth * 2.35F, abs(uv.x - primaryX));

    // One restrained lower branch makes the silhouette read as lightning without turning the sky into a neon tree.
    const float branchMask = smoothstep(0.48F, 0.58F, skyT) *
                             (1.0F - smoothstep(0.76F, 0.88F, skyT));
    const float branchDirection = LightningPatternOffset >= 0.5F ? 1.0F : -1.0F;
    const float branchX = primaryX + branchDirection * (skyT - 0.50F) * 0.11F;
    const float branch = (1.0F - smoothstep(
        pixelWidth * 0.8F,
        pixelWidth * 2.0F,
        abs(uv.x - branchX))) * branchMask * 0.55F;
    return saturate(max(primary, branch) * verticalMask);
}

float3 EvaluateProceduralDaySkySun(const float2 uv, const float skyBottom)
{
    const float safeSkyBottom = max(skyBottom, 1.0e-4F);
    const float skyT = saturate(uv.y / safeSkyBottom);
    const float smoothSkyT = skyT * skyT * (3.0F - 2.0F * skyT);
    const float3 zenithColor = float3(0.030F, 0.120F, 0.380F);
    const float3 midSkyColor = float3(0.095F, 0.285F, 0.610F);
    const float3 horizonColor = float3(0.260F, 0.470F, 0.700F);
    float3 sky = lerp(zenithColor, midSkyColor, smoothstep(0.0F, 0.72F, smoothSkyT));
    const float horizonBlend = smoothstep(0.62F, 1.0F, skyT);
    sky = lerp(sky, horizonColor, horizonBlend * horizonBlend);
    const float horizonHaze = pow(saturate((skyT - 0.72F) / 0.28F), 2.0F);
    sky = lerp(sky, AtmosphereFogColorRgb, HorizonHazeFraction * horizonHaze);
    sky *= SkyLuminanceMultiplier;

    if (CloudCoverFraction > 1.0e-4F)
    {
        const float drift = PresentationTimeSeconds * (0.006F + abs(CloudAdvection) * 0.010F);
        float2 cloudUv = float2(uv.x * 5.4F, skyT * 2.7F) +
            float2(CloudPatternOffset * 31.7F, CloudPatternOffset * 17.3F);
        cloudUv.x += drift * (CloudAdvection >= 0.0F ? 1.0F : -1.0F);
        const float density = AtmosphereFbm(cloudUv);
        const float threshold = lerp(0.86F, 0.36F, CloudCoverFraction);
        const float cloud = smoothstep(threshold, threshold + 0.18F, density);
        const float lowerDeck = smoothstep(0.50F, 1.0F, skyT) * CloudCoverFraction;
        const float cloudMass = saturate(max(cloud, lowerDeck * 0.42F));
        const float3 brightCloud = float3(0.88F, 0.90F, 0.92F) * (0.72F + 0.28F * (1.0F - skyT));
        const float3 stormCloud = float3(0.25F, 0.29F, 0.33F);
        const float stormTint = saturate(PrecipitationFraction * 0.72F + CloudCoverFraction * 0.20F);
        sky = lerp(sky, lerp(brightCloud, stormCloud, stormTint),
                   cloudMass * (0.35F + 0.55F * CloudCoverFraction));
    }

    const float aspectRatio =
        max(abs(ddy(uv.y)), 1.0e-7F) / max(abs(ddx(uv.x)), 1.0e-7F);
    const float radiusY = min(0.018F, safeSkyBottom * 0.15F);
    const float2 sunCenter = float2(0.70F, safeSkyBottom * 0.38F);
    const float2 sunDelta = float2((uv.x - sunCenter.x) * aspectRatio, uv.y - sunCenter.y) /
                            max(radiusY, 1.0e-5F);
    const float radialDistance = length(sunDelta);
    const float edgeAa = max(fwidth(radialDistance) * 0.85F, 0.0015F);
    const float diskCoverage = 1.0F - smoothstep(1.0F - edgeAa, 1.0F + edgeAa, radialDistance);
    const float interior = saturate(1.0F - radialDistance);
    const float3 diskRadiance =
        lerp(float3(11.0F, 7.2F, 3.6F), float3(30.0F, 27.0F, 20.0F),
             pow(interior, 0.32F)) * SunTransmittance;
    const float outsideDisk = max(radialDistance - 1.0F, 0.0F);
    const float corona = exp2(-outsideDisk * 1.55F) * (1.0F - diskCoverage) *
                         saturate(1.0F - outsideDisk / 7.0F);
    const float broadGlow = exp2(-radialDistance * radialDistance * 0.22F);
    sky += float3(1.00F, 0.72F, 0.38F) *
           (0.75F * corona + 0.28F * broadGlow) * SunTransmittance;
    const float solarPath =
        exp2(-abs((uv.x - sunCenter.x) * aspectRatio) / max(radiusY, 1.0e-5F) * 0.45F) * horizonHaze;
    sky += float3(0.35F, 0.22F, 0.10F) * (0.16F * solarPath * SunTransmittance);

    if (LightningFlashIntensity > 1.0e-4F)
    {
        const float channel = LightningChannelCoverage(uv, safeSkyBottom);
        const float lateralGlow = exp2(-abs(uv.x - LightningViewportX) * 7.5F);
        const float3 flashColor = float3(0.72F, 0.86F, 1.25F);
        sky += flashColor * LightningFlashIntensity * (0.24F + 0.34F * lateralGlow);
        sky += float3(8.5F, 10.5F, 15.0F) * (channel * LightningFlashIntensity);
    }
    return sky + diskRadiance * diskCoverage;
}

float3 ApplyPrecipitationPresentation(const float3 sceneLinear, const float2 uv)
{
    if (PrecipitationFraction <= 1.0e-4F ||
        AtmosphereBoundaryViewportY <= 1.0e-4F ||
        uv.y > AtmosphereBoundaryViewportY)
        return sceneLinear;

    const float normalizedY = uv.y / max(AtmosphereBoundaryViewportY, 1.0e-4F);
    const float2 rainUv = float2(
        uv.x * 150.0F + normalizedY * CloudAdvection * 27.0F,
        normalizedY * 95.0F + PresentationTimeSeconds * (17.0F + 19.0F * PrecipitationFraction));
    const float2 cell = floor(rainUv);
    const float2 local = frac(rainUv);
    const float random = HashAtmosphere21(cell + CloudPatternOffset * 97.0F);
    const float streak = smoothstep(0.045F, 0.0F, abs(local.x - random)) *
        smoothstep(0.92F, 0.18F, local.y) *
        step(0.42F - 0.30F * PrecipitationFraction, random);
    const float mist = PrecipitationFraction * (0.035F + 0.075F * normalizedY);
    float3 result = lerp(sceneLinear, AtmosphereFogColorRgb, mist);
    result += float3(0.32F, 0.37F, 0.42F) * streak * (0.16F * PrecipitationFraction);
    return result;
}

float3 ApplyLightningPresentation(const float3 sceneLinear, const float2 uv)
{
    if (LightningFlashIntensity <= 1.0e-4F ||
        AtmosphereBoundaryViewportY <= 1.0e-4F ||
        uv.y > AtmosphereBoundaryViewportY)
        return sceneLinear;

    // A real lightning flash lifts the whole above-water scene for a fraction of a second; it is not a white
    // fullscreen card. Keep the cold spectral bias and lateral falloff around the deterministic strike azimuth.
    const float lateral = 0.45F + 0.55F * exp2(-abs(uv.x - LightningViewportX) * 3.5F);
    return sceneLinear + float3(0.34F, 0.45F, 0.72F) *
        (LightningFlashIntensity * lateral);
}

float4 ResolveSceneLinear(const FullscreenPixelInput input)
{
    // Tone mapping is 1:1 with SceneColorHDR, so Load avoids filtering the sky sentinel across the waterline.
    const int2 pixel = int2(input.position.xy);
    const float4 scene = SceneColorHDR.Load(int3(pixel, 0));
    if (IsProceduralSkyMarker(scene))
    {
        return float4(EvaluateProceduralDaySkySun(input.uv, scene.a), 1.0F);
    }
    return float4(scene.rgb, 1.0F);
}

float3 ResolveWeatherPresentation(const float3 sceneLinear, const float2 uv)
{
    const float3 precipitated = ApplyPrecipitationPresentation(sceneLinear, uv);
    return ApplyLightningPresentation(precipitated, uv);
}

float4 PSMain(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 weatherPresented = ResolveWeatherPresentation(scene.rgb, input.uv);
    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(weatherPresented));
    return float4(saturate(DitherForQuantization(encoded, 255.0F, input.position.xy)), 1.0F);
}

float4 PSMainSdr10(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 weatherPresented = ResolveWeatherPresentation(scene.rgb, input.uv);
    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(weatherPresented));
    return float4(saturate(DitherForQuantization(encoded, 1023.0F, input.position.xy)), 1.0F);
}

float4 PSHdrScRgb(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 weatherPresented = ResolveWeatherPresentation(scene.rgb, input.uv);
    const float3 scRgb = MapSceneLinearToHdrScRgb(weatherPresented);
    // FP16 scRGB stays high precision. Half an expected 10-bit scan-out LSB of static dither prevents common
    // compositor/display quantization from exposing contours in the sky and dark-water gradients.
    return float4(max(DitherForQuantization(scRgb, 1023.0F, input.position.xy), 0.0F.xxx), 1.0F);
}