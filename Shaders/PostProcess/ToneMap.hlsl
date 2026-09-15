Texture2D<float4> SceneColorHDR : register(t0);
SamplerState SceneColorSampler : register(s0);

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
    // Fixed neutral exposure with a per-channel Reinhard shoulder. Procedural sky/sun is deliberately evaluated
    // before this function so SDR and HDR consume the same scene-linear radiance source.
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

float3 EvaluateProceduralDaySkySun(const float2 uv, const float skyBottom)
{
    const float safeSkyBottom = max(skyBottom, 1.0e-4F);
    const float skyT = saturate(uv.y / safeSkyBottom);
    const float smoothSkyT = skyT * skyT * (3.0F - 2.0F * skyT);

    // Three-stop ocean daylight gradient in scene-linear Rec.709. The horizon is intentionally brighter and
    // lower-contrast than the zenith; all interpolation remains FP16 until final display conversion.
    const float3 zenithColor = float3(0.030F, 0.120F, 0.380F);
    const float3 midSkyColor = float3(0.095F, 0.285F, 0.610F);
    const float3 horizonColor = float3(0.260F, 0.470F, 0.700F);
    float3 sky = lerp(zenithColor, midSkyColor, smoothstep(0.0F, 0.72F, smoothSkyT));
    const float horizonBlend = smoothstep(0.62F, 1.0F, skyT);
    sky = lerp(sky, horizonColor, horizonBlend * horizonBlend);

    // Fine atmospheric whitening immediately above the sea line keeps the horizon from reading as a hard
    // synthetic blue boundary. This is presentation haze, not weather or visibility simulation authority.
    const float horizonHaze = pow(saturate((skyT - 0.72F) / 0.28F), 2.0F);
    sky = lerp(sky, float3(0.390F, 0.550F, 0.720F), 0.20F * horizonHaze);

    // Recover viewport aspect from derivatives, so the sun remains circular at every supported resolution
    // without another renderer constant. UV Y grows downward, matching the existing M5 sky composition.
    const float uvPerPixelX = max(abs(ddx(uv.x)), 1.0e-7F);
    const float uvPerPixelY = max(abs(ddy(uv.y)), 1.0e-7F);
    const float aspectRatio = uvPerPixelY / uvPerPixelX;
    const float radiusY = min(0.018F, safeSkyBottom * 0.15F);
    const float2 sunCenter = float2(0.70F, safeSkyBottom * 0.38F);
    const float2 sunDelta = float2((uv.x - sunCenter.x) * aspectRatio, uv.y - sunCenter.y) / max(radiusY, 1.0e-5F);
    const float radialDistance = length(sunDelta);

    // fwidth-driven edge coverage gives a true sub-pixel anti-aliased disk instead of the former rectangular
    // raster approximation. The core intentionally exceeds scene-linear 1.0 so HDR can present real highlight
    // separation while SDR rolls the same radiance through the existing tone-map shoulder.
    const float edgeAa = max(fwidth(radialDistance) * 0.85F, 0.0015F);
    const float diskCoverage = 1.0F - smoothstep(1.0F - edgeAa, 1.0F + edgeAa, radialDistance);
    const float interior = saturate(1.0F - radialDistance);
    const float3 limbRadiance = float3(11.0F, 7.2F, 3.6F);
    const float3 coreRadiance = float3(30.0F, 27.0F, 20.0F);
    const float3 diskRadiance = lerp(limbRadiance, coreRadiance, pow(interior, 0.32F));

    // Analytic atmospheric aureole: a compact hot corona plus a broad low-energy warm scatter. It is not a
    // fake rectangle bloom and remains stable with resolution because it is evaluated from normalized radius.
    const float outsideDisk = max(radialDistance - 1.0F, 0.0F);
    const float corona = exp2(-outsideDisk * 1.55F) * (1.0F - diskCoverage) *
                         saturate(1.0F - outsideDisk / 7.0F);
    const float broadGlow = exp2(-radialDistance * radialDistance * 0.22F);
    const float3 warmScatter = float3(1.00F, 0.72F, 0.38F);
    sky += warmScatter * (0.75F * corona + 0.28F * broadGlow);

    // Subtle solar path toward the horizon ties the source into the atmosphere without introducing a separate
    // lens flare system. The effect is bounded to sky presentation and never changes world lighting authority.
    const float horizontalSolarDistance = abs((uv.x - sunCenter.x) * aspectRatio) / max(radiusY, 1.0e-5F);
    const float solarPath = exp2(-horizontalSolarDistance * 0.45F) * horizonHaze;
    sky += float3(0.35F, 0.22F, 0.10F) * (0.16F * solarPath);

    return sky + diskRadiance * diskCoverage;
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

float4 PSMain(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(scene.rgb));
    return float4(saturate(DitherForQuantization(encoded, 255.0F, input.position.xy)), 1.0F);
}

float4 PSMainSdr10(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(scene.rgb));
    return float4(saturate(DitherForQuantization(encoded, 1023.0F, input.position.xy)), 1.0F);
}

float4 PSHdrScRgb(FullscreenPixelInput input) : SV_TARGET
{
    const float4 scene = ResolveSceneLinear(input);
    const float3 scRgb = MapSceneLinearToHdrScRgb(scene.rgb);
    // FP16 scRGB stays high precision. Half an expected 10-bit scan-out LSB of static dither prevents common
    // compositor/display quantization from exposing contours in the sky and dark-water gradients.
    return float4(max(DitherForQuantization(scRgb, 1023.0F, input.position.xy), 0.0F.xxx), 1.0F);
}
