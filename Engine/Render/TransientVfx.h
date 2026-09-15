#pragma once

#include "Engine/Render/Camera.h"
#include "Engine/Render/DepthLighting.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace DeepRun::Render
{
class D3D12Renderer;

enum class TransientVfxPrimitive : std::uint32_t
{
    BubbleMicro = 0U,
    BubbleMeso,
    BubbleMacro,
    Droplet,
    Mist,
    Foam,
    ExhaustCore,
    ExhaustTurbulent,
    WaterSheet,
    Particulate,
};

enum class TransientVfxBlendMode : std::uint32_t
{
    Alpha,
    Additive,
};

// One renderer-neutral, analytic GPU emitter. CPU submits this bounded descriptor only; particle positions,
// breakup and ballistic/rising motion are expanded in HLSL from SV_InstanceID. This keeps transient VFX out
// of gameplay authority and avoids per-particle CPU updates.
struct TransientVfxEmitter final
{
    TransientVfxPrimitive primitive = TransientVfxPrimitive::BubbleMicro;
    TransientVfxBlendMode blendMode = TransientVfxBlendMode::Alpha;
    std::uint32_t seed = 0U;
    std::uint32_t particleCount = 0U;

    std::array<float, 3> originWorldMeters{};
    std::array<float, 3> directionWorldUnit{1.0F, 0.0F, 0.0F};
    std::array<float, 3> baseVelocityMetersPerSecond{};
    std::array<float, 3> extentMeters{1.0F, 1.0F, 1.0F};

    std::array<float, 3> linearColor{1.0F, 1.0F, 1.0F};
    float ageSeconds = 0.0F;
    float lifetimeSeconds = 1.0F;
    float minimumSizeMeters = 0.05F;
    float maximumSizeMeters = 0.10F;
    float opacity = 1.0F;
    float emissiveIntensity = 0.0F;
    float turbulence = 0.0F;
    float spawnRadiusMeters = 0.0F;
    float gravityMetersPerSecondSquared = 9.81F;
    float windSpeedMetersPerSecond = 0.0F;
    float windDirectionRadians = 0.0F;
    bool underwater = false;
};

struct TransientVfxDrawStats final
{
    std::uint32_t emitterCount = 0U;
    std::uint32_t particleCount = 0U;
    std::uint32_t drawCalls = 0U;
};

inline constexpr std::uint32_t TransientVfxMaximumEmittersPerFrame = 96U;
inline constexpr std::uint32_t TransientVfxMaximumParticlesPerEmitter = 2'048U;
inline constexpr std::uint32_t TransientVfxMaximumParticlesPerFrame = 16'384U;

[[nodiscard]] std::expected<void, std::string> ValidateTransientVfxEmitter(
    const TransientVfxEmitter& emitter);
[[nodiscard]] std::expected<void, std::string> ValidateTransientVfxBatch(
    std::span<const TransientVfxEmitter> emitters);

// D3D12 companion pass for bounded transient effects. It intentionally uses D3D12Renderer's public device
// and command-list boundary rather than introducing a second renderer or leaking presentation into Simulation.
class GpuTransientVfx final
{
public:
    GpuTransientVfx();
    ~GpuTransientVfx();

    GpuTransientVfx(GpuTransientVfx&&) noexcept;
    GpuTransientVfx& operator=(GpuTransientVfx&&) noexcept;
    GpuTransientVfx(const GpuTransientVfx&) = delete;
    GpuTransientVfx& operator=(const GpuTransientVfx&) = delete;

    [[nodiscard]] std::expected<void, std::string> Initialize(
        D3D12Renderer& renderer,
        const std::filesystem::path& shaderRoot);
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] std::expected<TransientVfxDrawStats, std::string> Draw(
        D3D12Renderer& renderer,
        std::span<const TransientVfxEmitter> emitters,
        const OrthographicCamera& camera,
        const DepthLightingParameters& depthLighting);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace DeepRun::Render
