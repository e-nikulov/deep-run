#include "Engine/Render/TransientVfx.h"

#include "Engine/Render/D3D12Renderer.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace DeepRun::Render
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr DXGI_FORMAT TransientVfxSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT TransientVfxDepthFormat = DXGI_FORMAT_D32_FLOAT;

[[nodiscard]] bool Finite3(const std::array<float, 3>& value) noexcept
{
    return std::ranges::all_of(value, [](const float component) { return std::isfinite(component); });
}

[[nodiscard]] float Length3(const std::array<float, 3>& value) noexcept
{
    return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

[[nodiscard]] std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unable to open transient VFX shader: " + path.string());
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty() || input.bad())
        throw std::runtime_error("Unable to read transient VFX shader: " + path.string());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

void ThrowIfFailed(const HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed");
}

struct TransientVfxDrawConstants final
{
    std::array<float, 16> viewProjection{};
    std::array<float, 4> originAge{};
    std::array<float, 4> directionLifetime{};
    std::array<float, 4> velocityTurbulence{};
    std::array<float, 4> extentKind{};
    std::array<float, 4> sizeOpacityEmissive{};
    std::array<float, 4> colorMedium{};
    std::array<float, 4> surfaceAbsorptionR{};
    std::array<float, 4> cameraGravity{};
    std::array<float, 4> seedSpawnWind{};
};
static_assert(sizeof(TransientVfxDrawConstants) == 52U * sizeof(std::uint32_t));
static_assert(52U < D3D12_MAX_ROOT_COST);
} // namespace

std::expected<void, std::string> ValidateTransientVfxEmitter(const TransientVfxEmitter& emitter)
{
    if (emitter.particleCount == 0U || emitter.particleCount > TransientVfxMaximumParticlesPerEmitter)
        return std::unexpected("transient VFX particle count is outside the bounded per-emitter budget");
    if (!Finite3(emitter.originWorldMeters) || !Finite3(emitter.directionWorldUnit) ||
        !Finite3(emitter.baseVelocityMetersPerSecond) || !Finite3(emitter.extentMeters) ||
        !Finite3(emitter.linearColor))
        return std::unexpected("transient VFX emitter contains non-finite vector data");
    const float directionLength = Length3(emitter.directionWorldUnit);
    if (!std::isfinite(directionLength) || directionLength < 0.99F || directionLength > 1.01F)
        return std::unexpected("transient VFX direction must be normalized");
    if (std::ranges::any_of(emitter.extentMeters, [](const float value) { return value < 0.0F; }) ||
        std::ranges::any_of(emitter.linearColor, [](const float value) { return value < 0.0F; }))
        return std::unexpected("transient VFX extents and linear colour must be non-negative");
    if (!std::isfinite(emitter.ageSeconds) || emitter.ageSeconds < 0.0F ||
        !std::isfinite(emitter.lifetimeSeconds) || emitter.lifetimeSeconds <= 0.0F ||
        !std::isfinite(emitter.minimumSizeMeters) || emitter.minimumSizeMeters <= 0.0F ||
        !std::isfinite(emitter.maximumSizeMeters) || emitter.maximumSizeMeters < emitter.minimumSizeMeters ||
        !std::isfinite(emitter.opacity) || emitter.opacity < 0.0F || emitter.opacity > 1.0F ||
        !std::isfinite(emitter.emissiveIntensity) || emitter.emissiveIntensity < 0.0F ||
        !std::isfinite(emitter.turbulence) || emitter.turbulence < 0.0F ||
        !std::isfinite(emitter.spawnRadiusMeters) || emitter.spawnRadiusMeters < 0.0F ||
        !std::isfinite(emitter.gravityMetersPerSecondSquared) || emitter.gravityMetersPerSecondSquared < 0.0F ||
        !std::isfinite(emitter.windSpeedMetersPerSecond) || emitter.windSpeedMetersPerSecond < 0.0F ||
        !std::isfinite(emitter.windDirectionRadians))
        return std::unexpected("transient VFX scalar parameters are invalid");
    return {};
}

std::expected<void, std::string> ValidateTransientVfxBatch(const std::span<const TransientVfxEmitter> emitters)
{
    if (emitters.size() > TransientVfxMaximumEmittersPerFrame)
        return std::unexpected("transient VFX emitter batch exceeds the frame budget");
    std::uint64_t totalParticles = 0U;
    for (const auto& emitter : emitters)
    {
        if (const auto valid = ValidateTransientVfxEmitter(emitter); !valid)
            return valid;
        totalParticles += emitter.particleCount;
        if (totalParticles > TransientVfxMaximumParticlesPerFrame)
            return std::unexpected("transient VFX batch exceeds the bounded particle frame budget");
    }
    return {};
}

class GpuTransientVfx::Impl final
{
public:
    std::expected<void, std::string> Initialize(D3D12Renderer& renderer, const std::filesystem::path& shaderRoot)
    {
        if (ready)
            return std::unexpected("transient VFX renderer is already initialized");
        if (!renderer.IsInitialized() || renderer.Device() == nullptr)
            return std::unexpected("transient VFX requires an initialized D3D12 renderer");

        try
        {
            const auto vertexShader = ReadBinaryFile(shaderRoot / "TransientVfxVS.cso");
            const auto pixelShader = ReadBinaryFile(shaderRoot / "TransientVfxPS.cso");

            D3D12_ROOT_PARAMETER rootParameter{};
            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParameter.Constants.ShaderRegister = 0;
            rootParameter.Constants.RegisterSpace = 0;
            rootParameter.Constants.Num32BitValues = sizeof(TransientVfxDrawConstants) / sizeof(std::uint32_t);
            rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rootDescription{};
            rootDescription.NumParameters = 1U;
            rootDescription.pParameters = &rootParameter;
            rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

            ComPtr<ID3DBlob> serializedRoot;
            ComPtr<ID3DBlob> rootErrors;
            const HRESULT serialized = D3D12SerializeRootSignature(
                &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRoot, &rootErrors);
            if (FAILED(serialized))
            {
                const std::string detail = rootErrors != nullptr
                    ? std::string(static_cast<const char*>(rootErrors->GetBufferPointer()), rootErrors->GetBufferSize())
                    : "unknown root-signature error";
                return std::unexpected("transient VFX root signature serialization failed: " + detail);
            }
            ThrowIfFailed(renderer.Device()->CreateRootSignature(
                0, serializedRoot->GetBufferPointer(), serializedRoot->GetBufferSize(), IID_PPV_ARGS(&rootSignature)),
                "Create transient VFX root signature");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = rootSignature.Get();
            pipeline.VS = {vertexShader.data(), vertexShader.size()};
            pipeline.PS = {pixelShader.data(), pixelShader.size()};
            pipeline.SampleMask = std::numeric_limits<UINT>::max();
            pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable = TRUE;
            pipeline.DepthStencilState.DepthEnable = TRUE;
            pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            pipeline.DepthStencilState.StencilEnable = FALSE;
            pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets = 1U;
            pipeline.RTVFormats[0] = TransientVfxSceneColorFormat;
            pipeline.DSVFormat = TransientVfxDepthFormat;
            pipeline.SampleDesc.Count = 1U;

            pipeline.BlendState.AlphaToCoverageEnable = FALSE;
            pipeline.BlendState.IndependentBlendEnable = FALSE;
            auto& blend = pipeline.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.LogicOpEnable = FALSE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            ThrowIfFailed(renderer.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&alphaPipeline)),
                          "Create transient VFX alpha pipeline");

            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_ONE;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_ONE;
            ThrowIfFailed(renderer.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&additivePipeline)),
                          "Create transient VFX additive pipeline");
            ready = true;
            return {};
        }
        catch (const std::exception& exception)
        {
            return std::unexpected(exception.what());
        }
    }

    std::expected<TransientVfxDrawStats, std::string> Draw(
        D3D12Renderer& renderer,
        const std::span<const TransientVfxEmitter> emitters,
        const OrthographicCamera& camera,
        const DepthLightingParameters& depthLighting)
    {
        if (!ready || rootSignature == nullptr || alphaPipeline == nullptr || additivePipeline == nullptr)
            return std::unexpected("transient VFX renderer is not ready");
        if (renderer.CommandList() == nullptr || !IsFinite(camera.viewProjection) ||
            !std::isfinite(camera.width) || !std::isfinite(camera.height) || camera.width <= 0.0F || camera.height <= 0.0F)
            return std::unexpected("transient VFX draw received invalid renderer/camera state");
        if (const auto valid = ValidateDepthLightingParameters(depthLighting); !valid)
            return std::unexpected("transient VFX depth-lighting snapshot is invalid: " + valid.error());
        if (const auto valid = ValidateTransientVfxBatch(emitters); !valid)
            return std::unexpected(valid.error());

        auto* commandList = renderer.CommandList();
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        TransientVfxDrawStats stats{};
        TransientVfxBlendMode boundBlend = static_cast<TransientVfxBlendMode>(~0U);
        for (const auto& emitter : emitters)
        {
            if (emitter.ageSeconds >= emitter.lifetimeSeconds || emitter.opacity <= 0.0F)
                continue;
            if (emitter.blendMode != boundBlend)
            {
                commandList->SetPipelineState(
                    emitter.blendMode == TransientVfxBlendMode::Additive ? additivePipeline.Get() : alphaPipeline.Get());
                boundBlend = emitter.blendMode;
            }

            TransientVfxDrawConstants constants{
                .viewProjection = camera.viewProjection.values,
                .originAge = {emitter.originWorldMeters[0], emitter.originWorldMeters[1], emitter.originWorldMeters[2], emitter.ageSeconds},
                .directionLifetime = {emitter.directionWorldUnit[0], emitter.directionWorldUnit[1], emitter.directionWorldUnit[2], emitter.lifetimeSeconds},
                .velocityTurbulence = {emitter.baseVelocityMetersPerSecond[0], emitter.baseVelocityMetersPerSecond[1], emitter.baseVelocityMetersPerSecond[2], emitter.turbulence},
                .extentKind = {emitter.extentMeters[0], emitter.extentMeters[1], emitter.extentMeters[2], static_cast<float>(emitter.primitive)},
                .sizeOpacityEmissive = {emitter.minimumSizeMeters, emitter.maximumSizeMeters, emitter.opacity, emitter.emissiveIntensity},
                .colorMedium = {emitter.linearColor[0], emitter.linearColor[1], emitter.linearColor[2], emitter.applyDepthAttenuation ? 1.0F : 0.0F},
                .surfaceAbsorptionR = {depthLighting.surfaceLevelYMeters, depthLighting.attenuationPerMeterRgb[0], depthLighting.attenuationPerMeterRgb[1], depthLighting.attenuationPerMeterRgb[2]},
                .cameraGravity = {camera.width, camera.height, emitter.gravityMetersPerSecondSquared, 0.0F},
                .seedSpawnWind = {std::bit_cast<float>(emitter.seed), emitter.spawnRadiusMeters, emitter.windSpeedMetersPerSecond, emitter.windDirectionRadians}};
            commandList->SetGraphicsRoot32BitConstants(
                0, sizeof(TransientVfxDrawConstants) / sizeof(std::uint32_t), &constants, 0);
            commandList->DrawInstanced(6U, emitter.particleCount, 0U, 0U);
            ++stats.emitterCount;
            stats.particleCount += emitter.particleCount;
            ++stats.drawCalls;
        }
        return stats;
    }

    bool ready = false;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> alphaPipeline;
    ComPtr<ID3D12PipelineState> additivePipeline;
};

GpuTransientVfx::GpuTransientVfx() : impl_(std::make_unique<Impl>()) {}
GpuTransientVfx::~GpuTransientVfx() = default;
GpuTransientVfx::GpuTransientVfx(GpuTransientVfx&&) noexcept = default;
GpuTransientVfx& GpuTransientVfx::operator=(GpuTransientVfx&&) noexcept = default;

std::expected<void, std::string> GpuTransientVfx::Initialize(
    D3D12Renderer& renderer, const std::filesystem::path& shaderRoot)
{
    return impl_->Initialize(renderer, shaderRoot);
}

bool GpuTransientVfx::IsReady() const noexcept
{
    return impl_ != nullptr && impl_->ready;
}

std::expected<TransientVfxDrawStats, std::string> GpuTransientVfx::Draw(
    D3D12Renderer& renderer,
    const std::span<const TransientVfxEmitter> emitters,
    const OrthographicCamera& camera,
    const DepthLightingParameters& depthLighting)
{
    if (impl_ == nullptr)
        return std::unexpected("transient VFX renderer implementation is unavailable");
    return impl_->Draw(renderer, emitters, camera, depthLighting);
}
} // namespace DeepRun::Render
