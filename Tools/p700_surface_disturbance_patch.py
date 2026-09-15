from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


# Generic bounded localized height disturbance contract.
replace_once(
    "Engine/Render/GerstnerSurface.h",
    "#include <expected>\n#include <string>\n#include <vector>\n",
    "#include <expected>\n#include <span>\n#include <string>\n#include <vector>\n",
)
replace_once(
    "Engine/Render/GerstnerSurface.h",
    """struct GerstnerSurfaceDrawStats final
{
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
};

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(""",
    """struct GerstnerSurfaceDrawStats final
{
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
};

inline constexpr std::size_t GerstnerSurfaceTransientDisturbanceCapacity = 3U;

// Generic presentation-only compact height disturbance. The renderer assigns no scene meaning to the source.
// Game supplies at most three bounded impulses and remains the sole owner of any semantic interpretation.
struct GerstnerSurfaceTransientDisturbance final
{
    float centerX = 0.0F;
    float radiusMeters = 0.0F;
    float verticalAmplitudeMeters = 0.0F;
    float profileExponent = 2.0F;
};

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfaceTransientDisturbances(
    std::span<const GerstnerSurfaceTransientDisturbance> disturbances);

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(""",
)
replace_once(
    "Engine/Render/GerstnerSurface.cpp",
    """std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters)
{""",
    """std::expected<void, std::string> ValidateGerstnerSurfaceTransientDisturbances(
    const std::span<const GerstnerSurfaceTransientDisturbance> disturbances)
{
    if (disturbances.size() > GerstnerSurfaceTransientDisturbanceCapacity)
        return std::unexpected("Gerstner transient disturbance count exceeds the bounded capacity");
    for (const auto& disturbance : disturbances)
    {
        if (!std::isfinite(disturbance.centerX) || std::abs(disturbance.centerX) > 1.0e6F ||
            !std::isfinite(disturbance.radiusMeters) || disturbance.radiusMeters <= 0.0F ||
            disturbance.radiusMeters > 128.0F ||
            !std::isfinite(disturbance.verticalAmplitudeMeters) ||
            std::abs(disturbance.verticalAmplitudeMeters) > 4.0F ||
            !std::isfinite(disturbance.profileExponent) || disturbance.profileExponent < 1.0F ||
            disturbance.profileExponent > 8.0F)
        {
            return std::unexpected("Gerstner transient disturbance is outside the restrained presentation range");
        }
    }
    return {};
}

std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters)
{""",
)

replace_once(
    "Engine/Render/D3D12Renderer.h",
    """    [[nodiscard]] std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(
        const OrthographicCamera& camera, double simulationTimeSeconds);""",
    """    [[nodiscard]] std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(
        const OrthographicCamera& camera,
        double simulationTimeSeconds,
        std::span<const GerstnerSurfaceTransientDisturbance> disturbances = {});""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """    std::array<float, 4> horizontalSteepness0{};
    std::array<float, 4> horizontalSteepness1{};
    std::array<float, 4> deepFillColor{};
    std::array<float, 4> surfaceTintColor{};
};""",
    """    std::uint32_t packedSteepness0 = 0U;
    std::uint32_t packedSteepness1AndCount = 0U;
    std::uint32_t packedDeepFillRgb = 0U;
    std::uint32_t packedSurfaceTintRgb = 0U;
    std::array<float, 4> disturbance0{};
    std::array<float, 4> disturbance1{};
    std::array<float, 4> disturbance2{};
};""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """    std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(
        const OrthographicCamera& camera, const double simulationTimeSeconds)
    {""",
    """    std::expected<GerstnerSurfaceDrawStats, std::string> DrawGerstnerSurface(
        const OrthographicCamera& camera, const double simulationTimeSeconds,
        const std::span<const GerstnerSurfaceTransientDisturbance> disturbances)
    {""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """        if (!IsFinite(camera.viewProjection) || !std::isfinite(camera.width) || !std::isfinite(camera.height) ||
            camera.width <= 0.0F || camera.height <= 0.0F)
        {
            return std::unexpected("Gerstner surface draw received invalid camera projection data");
        }

        const GerstnerSurfacePresentationParameters& parameters = gerstnerSurface.parameters;""",
    """        if (!IsFinite(camera.viewProjection) || !std::isfinite(camera.width) || !std::isfinite(camera.height) ||
            camera.width <= 0.0F || camera.height <= 0.0F)
        {
            return std::unexpected("Gerstner surface draw received invalid camera projection data");
        }
        if (const auto valid = ValidateGerstnerSurfaceTransientDisturbances(disturbances); !valid)
            return std::unexpected(valid.error());

        const GerstnerSurfacePresentationParameters& parameters = gerstnerSurface.parameters;""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """        const float authoredSpanMeters = parameters.maximumX - parameters.minimumX;
        const float horizontalScale = (std::max)(1.0F, camera.width * 1.05F / authoredSpanMeters);
        const GerstnerDrawConstants constants{""",
    """        const auto packNormalizedByte = [](const float value) noexcept -> std::uint32_t
        {
            return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        };
        const auto packRgb10 = [](const std::array<float, 3>& rgb) noexcept -> std::uint32_t
        {
            const auto channel = [](const float value) noexcept -> std::uint32_t
            {
                return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 1023.0F));
            };
            return channel(rgb[0]) | (channel(rgb[1]) << 10U) | (channel(rgb[2]) << 20U);
        };
        const auto disturbanceConstants = [&disturbances](const std::size_t index) noexcept
        {
            if (index >= disturbances.size()) return std::array<float, 4>{};
            const auto& value = disturbances[index];
            return std::array<float, 4>{
                value.centerX, value.radiusMeters, value.verticalAmplitudeMeters, value.profileExponent};
        };
        const std::uint32_t packedSteepness0 =
            packNormalizedByte(parameters.components[0].horizontalSteepness) |
            (packNormalizedByte(parameters.components[1].horizontalSteepness) << 8U) |
            (packNormalizedByte(parameters.components[2].horizontalSteepness) << 16U) |
            (packNormalizedByte(parameters.components[3].horizontalSteepness) << 24U);
        const std::uint32_t packedSteepness1AndCount =
            packNormalizedByte(parameters.components[4].horizontalSteepness) |
            (packNormalizedByte(parameters.components[5].horizontalSteepness) << 8U) |
            (packNormalizedByte(parameters.components[6].horizontalSteepness) << 16U) |
            (static_cast<std::uint32_t>(parameters.activeComponentCount) << 24U);
        const float authoredSpanMeters = parameters.maximumX - parameters.minimumX;
        const float horizontalScale = (std::max)(1.0F, camera.width * 1.05F / authoredSpanMeters);
        const GerstnerDrawConstants constants{""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """            .horizontalSteepness0 = {
                parameters.components[0].horizontalSteepness,
                parameters.components[1].horizontalSteepness,
                parameters.components[2].horizontalSteepness,
                parameters.components[3].horizontalSteepness},
            .horizontalSteepness1 = {
                parameters.components[4].horizontalSteepness,
                parameters.components[5].horizontalSteepness,
                parameters.components[6].horizontalSteepness,
                static_cast<float>(parameters.activeComponentCount)},
            .deepFillColor = {
                parameters.deepFillRgb[0], parameters.deepFillRgb[1], parameters.deepFillRgb[2], 1.0F},
            .surfaceTintColor = {
                parameters.surfaceTintRgb[0], parameters.surfaceTintRgb[1], parameters.surfaceTintRgb[2], 1.0F}};""",
    """            .packedSteepness0 = packedSteepness0,
            .packedSteepness1AndCount = packedSteepness1AndCount,
            .packedDeepFillRgb = packRgb10(parameters.deepFillRgb),
            .packedSurfaceTintRgb = packRgb10(parameters.surfaceTintRgb),
            .disturbance0 = disturbanceConstants(0U),
            .disturbance1 = disturbanceConstants(1U),
            .disturbance2 = disturbanceConstants(2U)};""",
)
replace_once(
    "Engine/Render/D3D12Renderer.cpp",
    """std::expected<GerstnerSurfaceDrawStats, std::string> D3D12Renderer::DrawGerstnerSurface(
    const OrthographicCamera& camera, const double simulationTimeSeconds)
{
    return impl_->DrawGerstnerSurface(camera, simulationTimeSeconds);
}""",
    """std::expected<GerstnerSurfaceDrawStats, std::string> D3D12Renderer::DrawGerstnerSurface(
    const OrthographicCamera& camera, const double simulationTimeSeconds,
    const std::span<const GerstnerSurfaceTransientDisturbance> disturbances)
{
    return impl_->DrawGerstnerSurface(camera, simulationTimeSeconds, disturbances);
}""",
)

Path("Shaders/GerstnerSurface.hlsl").write_text(
    r'''cbuffer GerstnerDrawConstants : register(b0)
{
    column_major float4x4 ViewProjection;
    float4 ReferenceLevelAndTime;
    float4 Wave0;
    float4 Wave1;
    float4 Wave2;
    float4 Wave3;
    float4 Wave4;
    float4 Wave5;
    float4 Wave6;
    uint4 PackedPresentation;
    float4 Disturbance0;
    float4 Disturbance1;
    float4 Disturbance2;
};

struct VSInput
{
    float2 basePosition : POSITION;
    float surfaceWeight : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float surfaceWeight : TEXCOORD0;
};

float UnpackNormalizedByte(const uint packed, const uint shift)
{
    return (float)((packed >> shift) & 0xffU) / 255.0F;
}

float3 UnpackRgb10(const uint packed)
{
    return float3(
        (float)(packed & 0x3ffU),
        (float)((packed >> 10U) & 0x3ffU),
        (float)((packed >> 20U) & 0x3ffU)) / 1023.0F;
}

float2 EvaluateComponent(const float baseX, const float timeSeconds, const float4 wave, const float steepness)
{
    const float waveNumber = 6.28318530718F / wave.y;
    const float theta = waveNumber * baseX - wave.z * timeSeconds + wave.w;
    return float2(steepness * wave.x * cos(theta), wave.x * sin(theta));
}

float EvaluateTransientDisturbance(const float baseX, const float4 disturbance)
{
    if (disturbance.y <= 0.0F || abs(disturbance.z) <= 1.0e-5F)
        return 0.0F;
    const float u = abs(baseX - disturbance.x) / disturbance.y;
    if (u >= 1.0F)
        return 0.0F;
    const float compactEnvelope = pow(saturate(1.0F - u * u), max(disturbance.w, 1.0F));
    return disturbance.z * cos(3.14159265359F * u) * compactEnvelope;
}

VSOutput VSMain(const VSInput input)
{
    const float timeSeconds = ReferenceLevelAndTime.y;
    const float cameraCenterX = ReferenceLevelAndTime.z;
    const float horizontalScale = ReferenceLevelAndTime.w;
    const float baseWorldX = cameraCenterX + input.basePosition.x * horizontalScale;
    const uint activeComponentCount = (PackedPresentation.y >> 24U) & 0xffU;
    float2 displacement = float2(0.0F, 0.0F);
    if (input.surfaceWeight > 0.5F)
    {
        if (activeComponentCount > 0U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave0, UnpackNormalizedByte(PackedPresentation.x, 0U));
        if (activeComponentCount > 1U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave1, UnpackNormalizedByte(PackedPresentation.x, 8U));
        if (activeComponentCount > 2U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave2, UnpackNormalizedByte(PackedPresentation.x, 16U));
        if (activeComponentCount > 3U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave3, UnpackNormalizedByte(PackedPresentation.x, 24U));
        if (activeComponentCount > 4U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave4, UnpackNormalizedByte(PackedPresentation.y, 0U));
        if (activeComponentCount > 5U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave5, UnpackNormalizedByte(PackedPresentation.y, 8U));
        if (activeComponentCount > 6U) displacement += EvaluateComponent(baseWorldX, timeSeconds, Wave6, UnpackNormalizedByte(PackedPresentation.y, 16U));
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance0);
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance1);
        displacement.y += EvaluateTransientDisturbance(baseWorldX, Disturbance2);
    }

    const float surfaceWeight = input.surfaceWeight;
    const float worldX = baseWorldX + displacement.x * surfaceWeight;
    const float worldY = input.basePosition.y + displacement.y * surfaceWeight;
    VSOutput output;
    output.position = mul(ViewProjection, float4(worldX, worldY, 0.0F, 1.0F));
    output.surfaceWeight = surfaceWeight;
    return output;
}

float4 PSMain(const VSOutput input) : SV_Target
{
    const float narrowSurfaceBand = pow(saturate(input.surfaceWeight), 24.0F);
    const float3 deepFillColor = UnpackRgb10(PackedPresentation.z);
    const float3 surfaceTintColor = UnpackRgb10(PackedPresentation.w);
    return float4(lerp(deepFillColor, surfaceTintColor, narrowSurfaceBand), 1.0F);
}
''',
    encoding="utf-8",
)

# Data-driven P-700 surface tuning.
replace_once(
    "Game/Weapons/P700LaunchVfx.h",
    """    std::uint32_t waterSheetCount = 54U;
    float waterSheetLifetimeSeconds = 1.15F;

    std::uint32_t foamCount = 150U;""",
    """    std::uint32_t waterSheetCount = 54U;
    float waterSheetLifetimeSeconds = 1.15F;

    float preBreachBulgeMaximumMeters = 1.8F;
    float preBreachBulgeRadiusMeters = 8.5F;
    float breachSurfaceRelaxationSeconds = 1.4F;
    float breachSurfaceCollapseMeters = 0.55F;
    float breachSurfaceExpansionMetersPerSecond = 4.0F;
    float surfaceDisturbanceProfileExponent = 2.1F;

    std::uint32_t foamCount = 150U;""",
)
replace_once(
    "Game/Weapons/P700LaunchVfx.cpp",
    """    const std::array<float, 22> scalars{
        t.bubbleLifetimeSeconds, t.underwaterTrailRadiusMeters, t.underwaterTurbulence,
        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,
        t.breachPreReactionDepthMeters, t.splashRadiusMeters, t.waterCrownLifetimeSeconds,
        t.dropletLifetimeSeconds, t.mistLifetimeSeconds, t.mistDensity, t.waterSheetLifetimeSeconds,
        t.foamLifetimeSeconds, t.foamRadiusMeters, t.airborneCoreLengthMeters, t.airbornePlumeLengthMeters,
        t.airborneEmissiveIntensity, t.airbornePlumeLifetimeSeconds, t.cruiseExhaustLengthMeters,
        t.cruiseExhaustLifetimeSeconds, t.condensationLifetimeSeconds};""",
    """    const std::array<float, 28> scalars{
        t.bubbleLifetimeSeconds, t.underwaterTrailRadiusMeters, t.underwaterTurbulence,
        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,
        t.breachPreReactionDepthMeters, t.splashRadiusMeters, t.waterCrownLifetimeSeconds,
        t.dropletLifetimeSeconds, t.mistLifetimeSeconds, t.mistDensity, t.waterSheetLifetimeSeconds,
        t.preBreachBulgeMaximumMeters, t.preBreachBulgeRadiusMeters, t.breachSurfaceRelaxationSeconds,
        t.breachSurfaceCollapseMeters, t.breachSurfaceExpansionMetersPerSecond, t.surfaceDisturbanceProfileExponent,
        t.foamLifetimeSeconds, t.foamRadiusMeters, t.airborneCoreLengthMeters, t.airbornePlumeLengthMeters,
        t.airborneEmissiveIntensity, t.airbornePlumeLifetimeSeconds, t.cruiseExhaustLengthMeters,
        t.cruiseExhaustLifetimeSeconds, t.condensationLifetimeSeconds};""",
)
replace_once(
    "Game/Weapons/P700LaunchVfx.cpp",
    """        ReadIfPresent(root, "waterSheetCount", t.waterSheetCount);
        ReadIfPresent(root, "waterSheetLifetimeSeconds", t.waterSheetLifetimeSeconds);
        ReadIfPresent(root, "foamCount", t.foamCount);""",
    """        ReadIfPresent(root, "waterSheetCount", t.waterSheetCount);
        ReadIfPresent(root, "waterSheetLifetimeSeconds", t.waterSheetLifetimeSeconds);
        ReadIfPresent(root, "preBreachBulgeMaximumMeters", t.preBreachBulgeMaximumMeters);
        ReadIfPresent(root, "preBreachBulgeRadiusMeters", t.preBreachBulgeRadiusMeters);
        ReadIfPresent(root, "breachSurfaceRelaxationSeconds", t.breachSurfaceRelaxationSeconds);
        ReadIfPresent(root, "breachSurfaceCollapseMeters", t.breachSurfaceCollapseMeters);
        ReadIfPresent(root, "breachSurfaceExpansionMetersPerSecond", t.breachSurfaceExpansionMetersPerSecond);
        ReadIfPresent(root, "surfaceDisturbanceProfileExponent", t.surfaceDisturbanceProfileExponent);
        ReadIfPresent(root, "foamCount", t.foamCount);""",
)
replace_once(
    "Config/p700_vfx.json",
    """  "waterSheetCount": 54,
  "waterSheetLifetimeSeconds": 1.15,
  "foamCount": 150,""",
    """  "waterSheetCount": 54,
  "waterSheetLifetimeSeconds": 1.15,
  "preBreachBulgeMaximumMeters": 1.8,
  "preBreachBulgeRadiusMeters": 8.5,
  "breachSurfaceRelaxationSeconds": 1.4,
  "breachSurfaceCollapseMeters": 0.55,
  "breachSurfaceExpansionMetersPerSecond": 4.0,
  "surfaceDisturbanceProfileExponent": 2.1,
  "foamCount": 150,""",
)

Path("Game/Weapons/P700SurfacePresentation.h").write_text(
    r'''#pragma once

#include "Engine/Render/GerstnerSurface.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Armament
{
[[nodiscard]] inline std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>
BuildP700SurfaceDisturbances(
    const std::span<const Weapons::P700GranitRuntimeState* const> missiles,
    const P700LaunchVfxTuning& tuning,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        return std::unexpected("P-700 surface presentation time must be finite and non-negative");
    if (const auto valid = ValidateP700LaunchVfxTuning(tuning); !valid)
        return std::unexpected(valid.error());

    std::vector<Render::GerstnerSurfaceTransientDisturbance> candidates;
    candidates.reserve(missiles.size());
    constexpr float Pi = 3.14159265358979323846F;
    for (const Weapons::P700GranitRuntimeState* missile : missiles)
    {
        if (missile == nullptr || missile->phase == Weapons::P700GranitPhase::Stored ||
            missile->phase == Weapons::P700GranitPhase::Spent)
            continue;
        if (!missile->positionMeters.IsFinite() || !std::isfinite(missile->surfaceLevelYMeters) ||
            !std::isfinite(missile->phaseStartTimeSeconds) || simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 surface presentation observed invalid missile state");

        if (missile->phase == Weapons::P700GranitPhase::UnderwaterLaunch)
        {
            const float depth = missile->surfaceLevelYMeters - missile->positionMeters.y;
            if (depth >= 0.0F && depth < tuning.breachPreReactionDepthMeters)
            {
                const float reaction = std::clamp(1.0F - depth / tuning.breachPreReactionDepthMeters, 0.0F, 1.0F);
                const float smoothReaction = reaction * reaction * (3.0F - 2.0F * reaction);
                candidates.push_back({
                    .centerX = missile->positionMeters.x,
                    .radiusMeters = tuning.preBreachBulgeRadiusMeters * (0.55F + 0.45F * smoothReaction),
                    .verticalAmplitudeMeters = tuning.preBreachBulgeMaximumMeters * smoothReaction * smoothReaction,
                    .profileExponent = tuning.surfaceDisturbanceProfileExponent});
            }
        }
        else if (missile->phase == Weapons::P700GranitPhase::WaterExit)
        {
            const float age = static_cast<float>(simulationTimeSeconds - missile->phaseStartTimeSeconds);
            if (age >= 0.0F && age < tuning.breachSurfaceRelaxationSeconds)
            {
                const float normalizedAge = std::clamp(age / tuning.breachSurfaceRelaxationSeconds, 0.0F, 1.0F);
                const float retainedBulge = tuning.preBreachBulgeMaximumMeters * (1.0F - normalizedAge);
                const float collapsingShoulder = tuning.breachSurfaceCollapseMeters * std::sin(Pi * normalizedAge);
                const float amplitude = retainedBulge - collapsingShoulder;
                if (std::abs(amplitude) > 0.01F)
                {
                    candidates.push_back({
                        .centerX = missile->positionMeters.x,
                        .radiusMeters = tuning.preBreachBulgeRadiusMeters +
                            tuning.breachSurfaceExpansionMetersPerSecond * age,
                        .verticalAmplitudeMeters = amplitude,
                        .profileExponent = tuning.surfaceDisturbanceProfileExponent});
                }
            }
        }
    }

    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return std::abs(left.verticalAmplitudeMeters) > std::abs(right.verticalAmplitudeMeters);
    });
    if (candidates.size() > Render::GerstnerSurfaceTransientDisturbanceCapacity)
        candidates.resize(Render::GerstnerSurfaceTransientDisturbanceCapacity);
    if (const auto valid = Render::ValidateGerstnerSurfaceTransientDisturbances(candidates); !valid)
        return std::unexpected("P-700 surface presentation produced invalid disturbances: " + valid.error());
    return candidates;
}
} // namespace DeepRun::Game::Armament
''',
    encoding="utf-8",
)

replace_once(
    "Game/Combat/CombatPlaygroundView.h",
    '#include "Game/Weapons/P700LaunchVfx.h"\n',
    '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Game/Weapons/P700SurfacePresentation.h"\n',
)
replace_once(
    "Game/Combat/CombatPlaygroundView.h",
    """    void SetP700VfxEnvironment(const Armament::P700LaunchVfxEnvironment& environment) noexcept
    {
        p700VfxEnvironment_ = environment;
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(""",
    """    void SetP700VfxEnvironment(const Armament::P700LaunchVfxEnvironment& environment) noexcept
    {
        p700VfxEnvironment_ = environment;
    }

    [[nodiscard]] std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>
    BuildP700SurfaceDisturbances(const CombatPlaygroundRuntime& runtime, const double simulationTimeSeconds) const
    {
        std::vector<const Weapons::P700GranitRuntimeState*> missiles;
        missiles.reserve(1U + runtime.PlayerP700Wingmen().size() + runtime.AdditionalPlayerP700Missiles().size());
        if (const auto& leader = runtime.PlayerP700(); leader) missiles.push_back(&*leader);
        for (const auto& wingman : runtime.PlayerP700Wingmen()) missiles.push_back(&wingman);
        for (const auto& ripple : runtime.AdditionalPlayerP700Missiles()) missiles.push_back(&ripple);
        return Armament::BuildP700SurfaceDisturbances(missiles, p700VfxSystem_.Tuning(), simulationTimeSeconds);
    }

    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation(""",
)
replace_once(
    "Game/Combat/CombatPlaygroundWindowedComposition.h",
    """    [[nodiscard]] GameplayPacingMetricsSnapshot PacingMetrics() const noexcept
    {
        return pacingMetrics_.Snapshot();
    }

    // Read-only resize acceptance hook.""",
    """    [[nodiscard]] GameplayPacingMetricsSnapshot PacingMetrics() const noexcept
    {
        return pacingMetrics_.Snapshot();
    }

    [[nodiscard]] std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>
    BuildP700SurfaceDisturbances(const double simulationTimeSeconds) const
    {
        if (!runtime_.has_value()) return std::vector<Render::GerstnerSurfaceTransientDisturbance>{};
        return view_.BuildP700SurfaceDisturbances(*runtime_, simulationTimeSeconds);
    }

    // Read-only resize acceptance hook.""",
)

replace_once(
    "Game/PhysicalPlayground.h",
    """    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        double simulationTimeSeconds,
        double presentationTimeSeconds) const;""",
    """    [[nodiscard]] std::expected<Render::ModelDrawStats, std::string> Render(
        Render::D3D12Renderer& renderer,
        double simulationTimeSeconds,
        double presentationTimeSeconds,
        std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances = {}) const;""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    """std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer,
    const double simulationTimeSeconds,
    const double presentationTimeSeconds) const""",
    """std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
    Render::D3D12Renderer& renderer,
    const double simulationTimeSeconds,
    const double presentationTimeSeconds,
    const std::span<const Render::GerstnerSurfaceTransientDisturbance> surfaceDisturbances) const""",
)
replace_once(
    "Game/PhysicalPlayground.cpp",
    "renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds);",
    "renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds, surfaceDisturbances);",
)

replace_once(
    "DeepRun/Main.cpp",
    """                const auto rendered = playground.Render(renderer, simulationTimeSeconds, presentationTimeSeconds);
                if (!rendered)""",
    """                std::vector<DeepRun::Render::GerstnerSurfaceTransientDisturbance> p700SurfaceDisturbances;
                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())
                {
                    const auto disturbances = combatPlayground->BuildP700SurfaceDisturbances(simulationTimeSeconds);
                    if (!disturbances)
                    {
                        std::cerr << "[Game][ERROR] P-700 surface disturbance composition failed: "
                                  << disturbances.error() << '\\n';
                        return false;
                    }
                    p700SurfaceDisturbances = *disturbances;
                }
                const auto rendered = playground.Render(
                    renderer, simulationTimeSeconds, presentationTimeSeconds,
                    std::span<const DeepRun::Render::GerstnerSurfaceTransientDisturbance>(
                        p700SurfaceDisturbances.data(), p700SurfaceDisturbances.size()));
                if (!rendered)""",
)

replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    '#include "Game/Weapons/P700LaunchVfx.h"\n',
    '#include "Game/Weapons/P700LaunchVfx.h"\n#include "Game/Weapons/P700SurfacePresentation.h"\n',
)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    """    return true;
}

[[nodiscard]] bool RunSalvoBudgetCheck(const std::size_t missileCount)""",
    """    return true;
}

[[nodiscard]] bool RunSurfaceDisturbanceChecks()
{
    const auto tuning = DeepRun::Game::Armament::DefaultP700LaunchVfxTuning();
    auto missile = MakeMissile(404U, P700GranitPhase::UnderwaterLaunch, 12.0F, -2.0F, 1.0);
    const P700GranitRuntimeState* pointer = &missile;
    const auto preBreach = DeepRun::Game::Armament::BuildP700SurfaceDisturbances(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), tuning, 1.10);
    if (!preBreach || preBreach->size() != 1U || preBreach->front().verticalAmplitudeMeters <= 0.0F ||
        preBreach->front().centerX != missile.positionMeters.x ||
        !DeepRun::Render::ValidateGerstnerSurfaceTransientDisturbances(*preBreach))
    {
        std::cerr << "P-700 pre-breach state did not produce a bounded positive surface bulge\\n";
        return false;
    }

    missile.phase = P700GranitPhase::WaterExit;
    missile.positionMeters.y = 0.2F;
    missile.phaseStartTimeSeconds = 1.20;
    const auto relaxation = DeepRun::Game::Armament::BuildP700SurfaceDisturbances(
        std::span<const P700GranitRuntimeState* const>(&pointer, 1U), tuning, 1.70);
    if (!relaxation || relaxation->empty() ||
        relaxation->front().radiusMeters <= preBreach->front().radiusMeters)
    {
        std::cerr << "P-700 breach did not transition into an expanding surface relaxation\\n";
        return false;
    }

    std::array<P700GranitRuntimeState, 6> salvo{};
    std::array<const P700GranitRuntimeState*, 6> pointers{};
    for (std::size_t index = 0; index < salvo.size(); ++index)
    {
        salvo[index] = MakeMissile(500U + index, P700GranitPhase::UnderwaterLaunch,
                                   static_cast<float>(index) * 2.15F, -1.5F, 1.0);
        pointers[index] = &salvo[index];
    }
    const auto boundedSalvo = DeepRun::Game::Armament::BuildP700SurfaceDisturbances(pointers, tuning, 1.10);
    if (!boundedSalvo || boundedSalvo->size() != DeepRun::Render::GerstnerSurfaceTransientDisturbanceCapacity)
    {
        std::cerr << "P-700 surface disturbance salvo did not enforce the three-impulse GPU bound\\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunSalvoBudgetCheck(const std::size_t missileCount)""",
)
replace_once(
    "Tests/P700LaunchVfxTest.cpp",
    """    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSalvoBudgetCheck(1U) ||""",
    """    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSurfaceDisturbanceChecks() ||
        !RunSalvoBudgetCheck(1U) ||""",
)
