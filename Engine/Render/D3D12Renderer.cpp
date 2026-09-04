#include "Engine/Render/D3D12Renderer.h"

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace DeepRun::Render
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr std::uint32_t BufferCount = 2;
constexpr DXGI_FORMAT BufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT SceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr std::uint32_t SceneColorRtvIndex = BufferCount;
constexpr std::uint32_t DescriptorHeapEntryCount = 2;

constexpr std::array<float, 4> DefaultSceneClearColor{
    0.00116099F,
    0.00444609F,
    0.00657139F,
    1.0F};

struct DrawRootConstants final
{
    std::array<float, 16> model{};
    std::array<float, 12> normal{};
    std::array<float, 16> viewProjection{};
    std::array<float, 4> baseColor{};
    float metallic = 0.0F;
    float roughness = 1.0F;
    std::array<float, 2> materialPadding{};
    std::array<float, 3> lightDirection{0.35F, -0.45F, -0.82F};
    float ambient = 0.24F;
};

static_assert(sizeof(DrawRootConstants) == sizeof(std::uint32_t) * 56);

std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Unable to open compiled shader: " + path.string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty() || input.bad())
    {
        throw std::runtime_error("Unable to read compiled shader: " + path.string());
    }
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::uint64_t NextRendererIdentity() noexcept
{
    static std::atomic_uint64_t nextIdentity{1};
    return nextIdentity.fetch_add(1, std::memory_order_relaxed);
}

void ThrowIfFailed(const HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        std::ostringstream message;
        message << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
                << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}

std::string ToUtf8(const wchar_t* text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

D3D12_HEAP_PROPERTIES HeapProperties(const D3D12_HEAP_TYPE type) noexcept
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(const std::uint64_t byteSize) noexcept
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Alignment = 0;
    description.Width = byteSize;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

struct GpuPrimitive final
{
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

struct GpuModel final
{
    std::vector<GpuPrimitive> primitives;
    GpuModelUploadStats stats;
};

struct UploadedGpuModel final
{
    std::size_t modelIndex = 0;
    GpuModelUploadStats stats;
};
}

class D3D12Renderer::Impl final
{
public:
    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger), rendererIdentity(NextRendererIdentity())
    {
    }

    ~Impl()
    {
        Shutdown();
    }

    bool Initialize(
        void* nativeHandle,
        const std::uint32_t requestedWidth,
        const std::uint32_t requestedHeight,
        const bool requestedVsync,
        const std::filesystem::path& shaderRoot)
    {
        try
        {
#if defined(DEEPRUN_DEBUG)
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
                logger.Info(Diagnostics::LogCategory::Render, "D3D12 debug layer enabled");
            }
            else
            {
                logger.Warning(Diagnostics::LogCategory::Render, "D3D12 debug layer unavailable");
            }
#endif

            ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
            SelectAdapter();
            ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");

#if defined(DEEPRUN_DEBUG)
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(device.As(&infoQueue)))
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
                infoQueue->ClearStoredMessages();

                // The current scene presentation intentionally performs multiple valid partial clear colours
                // on one HDR target. A fixed optimized-clear value cannot describe that path, so suppress
                // only D3D12's non-state performance advisory; every other warning and all errors remain
                // visible to the existing debug validation.
                D3D12_MESSAGE_ID ignoredMessages[]{
                    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE};
                D3D12_INFO_QUEUE_FILTER filter{};
                filter.DenyList.NumIDs = static_cast<UINT>(std::size(ignoredMessages));
                filter.DenyList.pIDList = ignoredMessages;
                ThrowIfFailed(infoQueue->AddStorageFilterEntries(&filter), "Filter HDR clear advisory");
            }
#endif

            D3D12_COMMAND_QUEUE_DESC queueDescription{};
            queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ThrowIfFailed(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&commandQueue)), "CreateCommandQueue");

            for (ComPtr<ID3D12CommandAllocator>& allocator : commandAllocators)
            {
                ThrowIfFailed(
                    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                    "CreateCommandAllocator");
            }

            ThrowIfFailed(
                device->CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    commandAllocators[0].Get(),
                    nullptr,
                    IID_PPV_ARGS(&commandList)),
                "CreateCommandList");
            ThrowIfFailed(commandList->Close(), "Close initial command list");

            D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
            rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDescription.NumDescriptors = BufferCount + 1;
            ThrowIfFailed(device->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&rtvHeap)), "Create RTV heap");
            rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            D3D12_DESCRIPTOR_HEAP_DESC imguiDescription{};
            imguiDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            // Slot 0 is the persistent SceneColorHDR SRV. Slot 1 remains owned by Dear ImGui for its font
            // texture, keeping both passes on the one shader-visible heap permitted by D3D12.
            imguiDescription.NumDescriptors = DescriptorHeapEntryCount;
            imguiDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(
                device->CreateDescriptorHeap(&imguiDescription, IID_PPV_ARGS(&imguiHeap)),
                "Create ImGui descriptor heap");
            imguiIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
            dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvDescription.NumDescriptors = 1;
            ThrowIfFailed(
                device->CreateDescriptorHeap(&dsvDescription, IID_PPV_ARGS(&dsvHeap)),
                "Create DSV descriptor heap");

            width = requestedWidth;
            height = requestedHeight;
            vsync = requestedVsync;
            CreateSwapChain(static_cast<HWND>(nativeHandle));
            CreateRenderTargets();
            CreateSceneColorTarget();
            CreateDepthBuffer();
            CreateModelPipeline(shaderRoot);
            CreateToneMapPipeline(shaderRoot);

            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
            fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (fenceEvent == nullptr)
            {
                throw std::runtime_error("CreateEventW for D3D12 fence failed");
            }

#if defined(DEEPRUN_DEBUG)
            if (ValidateDebugMessages("Renderer initialization") != 0)
            {
                throw std::runtime_error("D3D12 validation reported a renderer initialization warning or error");
            }
#endif

            initialized = true;
            logger.Info(
                Diagnostics::LogCategory::Render,
                "D3D12 device, SceneColorHDR (R16G16B16A16_FLOAT), SDR tone-map pipeline, swap chain, and depth buffer created");
            return true;
        }
        catch (const std::exception& exception)
        {
            logger.Error(Diagnostics::LogCategory::Render, exception.what());
            Shutdown();
            return false;
        }
    }

    void SelectAdapter()
    {
        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter4> candidate;
            if (factory->EnumAdapterByGpuPreference(
                    index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC3 description{};
            candidate->GetDesc3(&description);
            if ((description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0)
            {
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
            {
                adapter = candidate;
                LogAdapter(description, false);
                return;
            }
        }

        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "EnumWarpAdapter");
        ThrowIfFailed(warpAdapter.As(&adapter), "Query WARP adapter");
        DXGI_ADAPTER_DESC3 description{};
        adapter->GetDesc3(&description);
        LogAdapter(description, true);
    }

    void LogAdapter(const DXGI_ADAPTER_DESC3& description, const bool software)
    {
        std::ostringstream message;
        message << "Selected " << (software ? "software fallback" : "hardware") << " adapter: "
                << ToUtf8(description.Description) << " (dedicated VRAM "
                << (description.DedicatedVideoMemory / (1024ULL * 1024ULL)) << " MiB)";
        logger.Info(Diagnostics::LogCategory::Render, message.str());
    }

    void CreateSwapChain(HWND windowHandle)
    {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = BufferFormat;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = BufferCount;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> initialSwapChain;
        ThrowIfFailed(
            factory->CreateSwapChainForHwnd(
                commandQueue.Get(), windowHandle, &description, nullptr, nullptr, &initialSwapChain),
            "CreateSwapChainForHwnd");
        ThrowIfFailed(factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
        ThrowIfFailed(initialSwapChain.As(&swapChain), "Query IDXGISwapChain4");
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    void CreateRenderTargets()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t index = 0; index < BufferCount; ++index)
        {
            ThrowIfFailed(swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffers[index])), "Get swap-chain buffer");
            device->CreateRenderTargetView(backBuffers[index].Get(), nullptr, handle);
            rtvHandles[index] = handle;
            handle.ptr += rtvIncrement;
        }
    }

    void CreateDepthBuffer()
    {
        const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DepthFormat;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DepthFormat;
        clearValue.DepthStencil.Depth = 1.0F;
        ThrowIfFailed(
            device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(&depthBuffer)),
            "Create depth buffer");

        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = DepthFormat;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(depthBuffer.Get(), &view, dsvHandle);
    }

    void CreateSceneColorTarget()
    {
        const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = SceneColorFormat;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        // This target may be cleared with several valid scene colours. It deliberately has no one
        // optimized-clear value, because D3D12 would warn whenever a partial presentation clear used a
        // different colour; the target's persistent lifetime matters more than that incompatible optimization.
        ThrowIfFailed(
            device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr,
                IID_PPV_ARGS(&sceneColorHdr)),
            "Create SceneColorHDR render target");

        D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        sceneRtv.ptr += static_cast<SIZE_T>(SceneColorRtvIndex) * rtvIncrement;
        device->CreateRenderTargetView(sceneColorHdr.Get(), nullptr, sceneRtv);
        sceneColorRtvHandle = sceneRtv;

        sceneColorSrvCpuHandle = imguiHeap->GetCPUDescriptorHandleForHeapStart();
        sceneColorSrvGpuHandle = imguiHeap->GetGPUDescriptorHandleForHeapStart();
        imguiCpuHandle = sceneColorSrvCpuHandle;
        imguiCpuHandle.ptr += imguiIncrement;
        imguiGpuHandle = sceneColorSrvGpuHandle;
        imguiGpuHandle.ptr += imguiIncrement;

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = SceneColorFormat;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(sceneColorHdr.Get(), &view, sceneColorSrvCpuHandle);
    }

    void CreateModelPipeline(const std::filesystem::path& shaderRoot)
    {
        const std::vector<std::byte> vertexShader = ReadBinaryFile(shaderRoot / "ModelVS.cso");
        const std::vector<std::byte> pixelShader = ReadBinaryFile(shaderRoot / "ModelPS.cso");

        D3D12_ROOT_PARAMETER constantsParameter{};
        constantsParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        constantsParameter.Constants.ShaderRegister = 0;
        constantsParameter.Constants.RegisterSpace = 0;
        constantsParameter.Constants.Num32BitValues = sizeof(DrawRootConstants) / sizeof(std::uint32_t);
        constantsParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &constantsParameter;
        rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> serializedRoot;
        ComPtr<ID3DBlob> rootErrors;
        const HRESULT serializeResult = D3D12SerializeRootSignature(
            &rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRoot,
            &rootErrors);
        if (FAILED(serializeResult))
        {
            const std::string detail = rootErrors != nullptr
                                           ? std::string(
                                                 static_cast<const char*>(rootErrors->GetBufferPointer()),
                                                 rootErrors->GetBufferSize())
                                           : "unknown root-signature error";
            throw std::runtime_error("Serialize model root signature failed: " + detail);
        }
        ThrowIfFailed(
            device->CreateRootSignature(
                0,
                serializedRoot->GetBufferPointer(),
                serializedRoot->GetBufferSize(),
                IID_PPV_ARGS(&modelRootSignature)),
            "Create model root signature");

        const std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}}};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = modelRootSignature.Get();
        pipeline.VS = {vertexShader.data(), vertexShader.size()};
        pipeline.PS = {pixelShader.data(), pixelShader.size()};
        pipeline.BlendState.AlphaToCoverageEnable = FALSE;
        pipeline.BlendState.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC& targetBlend = pipeline.BlendState.RenderTarget[0];
        targetBlend.BlendEnable = FALSE;
        targetBlend.LogicOpEnable = FALSE;
        targetBlend.SrcBlend = D3D12_BLEND_ONE;
        targetBlend.DestBlend = D3D12_BLEND_ZERO;
        targetBlend.BlendOp = D3D12_BLEND_OP_ADD;
        targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        targetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        targetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        targetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = std::numeric_limits<UINT>::max();
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pipeline.RasterizerState.FrontCounterClockwise = TRUE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = TRUE;
        pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pipeline.DepthStencilState.StencilEnable = FALSE;
        pipeline.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
        pipeline.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = SceneColorFormat;
        pipeline.DSVFormat = DepthFormat;
        pipeline.SampleDesc.Count = 1;
        ThrowIfFailed(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&modelPipeline)),
                      "Create model graphics pipeline");
    }

    void CreateToneMapPipeline(const std::filesystem::path& shaderRoot)
    {
        const std::vector<std::byte> vertexShader = ReadBinaryFile(shaderRoot / "ToneMapVS.cso");
        const std::vector<std::byte> pixelShader = ReadBinaryFile(shaderRoot / "ToneMapPS.cso");

        D3D12_DESCRIPTOR_RANGE sceneColorRange{};
        sceneColorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneColorRange.NumDescriptors = 1;
        sceneColorRange.BaseShaderRegister = 0;
        sceneColorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER sceneColorParameter{};
        sceneColorParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sceneColorParameter.DescriptorTable.NumDescriptorRanges = 1;
        sceneColorParameter.DescriptorTable.pDescriptorRanges = &sceneColorRange;
        sceneColorParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sceneColorSampler{};
        sceneColorSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sceneColorSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sceneColorSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sceneColorSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sceneColorSampler.MipLODBias = 0.0F;
        sceneColorSampler.MaxAnisotropy = 1;
        sceneColorSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sceneColorSampler.MinLOD = 0.0F;
        sceneColorSampler.ShaderRegister = 0;
        sceneColorSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sceneColorSampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &sceneColorParameter;
        rootDescription.NumStaticSamplers = 1;
        rootDescription.pStaticSamplers = &sceneColorSampler;
        rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> serializedRoot;
        ComPtr<ID3DBlob> rootErrors;
        const HRESULT serializeResult = D3D12SerializeRootSignature(
            &rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRoot,
            &rootErrors);
        if (FAILED(serializeResult))
        {
            const std::string detail = rootErrors != nullptr
                                           ? std::string(
                                                 static_cast<const char*>(rootErrors->GetBufferPointer()),
                                                 rootErrors->GetBufferSize())
                                           : "unknown root-signature error";
            throw std::runtime_error("Serialize tone-map root signature failed: " + detail);
        }
        ThrowIfFailed(
            device->CreateRootSignature(
                0,
                serializedRoot->GetBufferPointer(),
                serializedRoot->GetBufferSize(),
                IID_PPV_ARGS(&toneMapRootSignature)),
            "Create tone-map root signature");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = toneMapRootSignature.Get();
        pipeline.VS = {vertexShader.data(), vertexShader.size()};
        pipeline.PS = {pixelShader.data(), pixelShader.size()};
        pipeline.BlendState.AlphaToCoverageEnable = FALSE;
        pipeline.BlendState.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC& targetBlend = pipeline.BlendState.RenderTarget[0];
        targetBlend.BlendEnable = FALSE;
        targetBlend.LogicOpEnable = FALSE;
        targetBlend.SrcBlend = D3D12_BLEND_ONE;
        targetBlend.DestBlend = D3D12_BLEND_ZERO;
        targetBlend.BlendOp = D3D12_BLEND_OP_ADD;
        targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        targetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        targetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        targetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = std::numeric_limits<UINT>::max();
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = FALSE;
        pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.StencilEnable = FALSE;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = BufferFormat;
        pipeline.SampleDesc.Count = 1;
        ThrowIfFailed(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&toneMapPipeline)),
                      "Create tone-map graphics pipeline");
    }

    void WaitForFrame(const std::uint32_t index)
    {
        const std::uint64_t value = frameFenceValues[index];
        if (value != 0 && fence->GetCompletedValue() < value)
        {
            ThrowIfFailed(fence->SetEventOnCompletion(value, fenceEvent), "SetEventOnCompletion");
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void FlushGpu()
    {
        if (commandQueue == nullptr || fence == nullptr || fenceEvent == nullptr)
        {
            return;
        }
        const std::uint64_t value = nextFenceValue++;
        ThrowIfFailed(commandQueue->Signal(fence.Get(), value), "Signal flush fence");
        ThrowIfFailed(fence->SetEventOnCompletion(value, fenceEvent), "Set flush event");
        WaitForSingleObject(fenceEvent, INFINITE);
    }

#if defined(DEEPRUN_DEBUG)
    void ClearDebugMessages()
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (device != nullptr && SUCCEEDED(device.As(&infoQueue)))
        {
            infoQueue->ClearStoredMessages();
        }
    }

    std::size_t ValidateDebugMessages(const std::string_view phase)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (device == nullptr || FAILED(device.As(&infoQueue)))
        {
            return 0;
        }

        std::size_t warningCount = 0;
        const std::uint64_t messageCount = infoQueue->GetNumStoredMessages();
        for (std::uint64_t index = 0; index < messageCount; ++index)
        {
            SIZE_T messageBytes = 0;
            if (FAILED(infoQueue->GetMessage(index, nullptr, &messageBytes)) || messageBytes == 0)
            {
                continue;
            }

            std::vector<std::byte> storage(messageBytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(infoQueue->GetMessage(index, message, &messageBytes)) ||
                message->Severity > D3D12_MESSAGE_SEVERITY_WARNING)
            {
                continue;
            }

            ++warningCount;
            std::string detail(phase);
            detail.append(" D3D12 validation: ");
            const std::size_t descriptionLength =
                message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0;
            detail.append(message->pDescription, descriptionLength);
            logger.Error(Diagnostics::LogCategory::Render, detail);
        }
        infoQueue->ClearStoredMessages();
        if (warningCount == 0)
        {
            std::string detail(phase);
            detail.append(" D3D12 debug validation clean");
            logger.Info(Diagnostics::LogCategory::Render, detail);
        }
        return warningCount;
    }
#endif

    std::expected<UploadedGpuModel, std::string> UploadModel(const Assets::ModelAsset& model)
    {
        if (!initialized)
        {
            return std::unexpected("D3D12 renderer must be initialized before model upload");
        }

        const auto layoutResult = BuildIndexedGeometryLayout(model);
        if (!layoutResult)
        {
            return std::unexpected(layoutResult.error());
        }

        try
        {
#if defined(DEEPRUN_DEBUG)
            ClearDebugMessages();
#endif
            ComPtr<ID3D12CommandAllocator> uploadAllocator;
            ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
            ThrowIfFailed(
                device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator)),
                "Create model upload command allocator");
            ThrowIfFailed(
                device->CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    uploadAllocator.Get(),
                    nullptr,
                    IID_PPV_ARGS(&uploadCommandList)),
                "Create model upload command list");

            GpuModel gpuModel;
            gpuModel.primitives.reserve(layoutResult->primitives.size());
            std::vector<ComPtr<ID3D12Resource>> uploadResources;
            uploadResources.reserve(layoutResult->primitives.size() * 2);

            for (const IndexedPrimitiveLayout& primitiveLayout : layoutResult->primitives)
            {
                const Assets::MeshPrimitiveData& source = model.primitives[primitiveLayout.primitiveIndex];
                GpuPrimitive gpuPrimitive;
                gpuPrimitive.vertexCount = primitiveLayout.vertexCount;
                gpuPrimitive.indexCount = primitiveLayout.indexCount;

                const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
                const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
                const D3D12_RESOURCE_DESC vertexDescription = BufferDescription(primitiveLayout.vertexBytes);
                const D3D12_RESOURCE_DESC indexDescription = BufferDescription(primitiveLayout.indexBytes);

                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &defaultHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &vertexDescription,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(&gpuPrimitive.vertexBuffer)),
                    "Create GPU vertex buffer");
                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &defaultHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &indexDescription,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(&gpuPrimitive.indexBuffer)),
                    "Create GPU index buffer");

                ComPtr<ID3D12Resource> vertexUpload;
                ComPtr<ID3D12Resource> indexUpload;
                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &uploadHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &vertexDescription,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&vertexUpload)),
                    "Create vertex upload buffer");
                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &uploadHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &indexDescription,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&indexUpload)),
                    "Create index upload buffer");

                void* mappedVertices = nullptr;
                ThrowIfFailed(vertexUpload->Map(0, nullptr, &mappedVertices), "Map vertex upload buffer");
                std::memcpy(mappedVertices, source.vertices.data(), primitiveLayout.vertexBytes);
                vertexUpload->Unmap(0, nullptr);

                void* mappedIndices = nullptr;
                ThrowIfFailed(indexUpload->Map(0, nullptr, &mappedIndices), "Map index upload buffer");
                std::memcpy(mappedIndices, source.indices.data(), primitiveLayout.indexBytes);
                indexUpload->Unmap(0, nullptr);

                uploadCommandList->CopyBufferRegion(
                    gpuPrimitive.vertexBuffer.Get(), 0, vertexUpload.Get(), 0, primitiveLayout.vertexBytes);
                uploadCommandList->CopyBufferRegion(
                    gpuPrimitive.indexBuffer.Get(), 0, indexUpload.Get(), 0, primitiveLayout.indexBytes);

                std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
                barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[0].Transition.pResource = gpuPrimitive.vertexBuffer.Get();
                barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[1].Transition.pResource = gpuPrimitive.indexBuffer.Get();
                barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
                barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                uploadCommandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

                gpuPrimitive.vertexView.BufferLocation = gpuPrimitive.vertexBuffer->GetGPUVirtualAddress();
                gpuPrimitive.vertexView.SizeInBytes = primitiveLayout.vertexBytes;
                gpuPrimitive.vertexView.StrideInBytes = sizeof(Assets::MeshVertex);
                gpuPrimitive.indexView.BufferLocation = gpuPrimitive.indexBuffer->GetGPUVirtualAddress();
                gpuPrimitive.indexView.SizeInBytes = primitiveLayout.indexBytes;
                gpuPrimitive.indexView.Format = DXGI_FORMAT_R32_UINT;

                uploadResources.push_back(std::move(vertexUpload));
                uploadResources.push_back(std::move(indexUpload));
                gpuModel.primitives.push_back(std::move(gpuPrimitive));
            }

            ThrowIfFailed(uploadCommandList->Close(), "Close model upload command list");
            ID3D12CommandList* uploadLists[] = {uploadCommandList.Get()};
            commandQueue->ExecuteCommandLists(1, uploadLists);
            FlushGpu();
#if defined(DEEPRUN_DEBUG)
            if (ValidateDebugMessages("Model upload") != 0)
            {
                return std::unexpected("D3D12 validation reported a model upload warning or error");
            }
#endif

            gpuModel.stats = layoutResult->totals;
            gpuModel.stats.uploadCompleted = true;
            const GpuModelUploadStats stats = gpuModel.stats;
            const std::size_t modelIndex = gpuModels.size();
            gpuModels.push_back(std::move(gpuModel));

            std::ostringstream message;
            message << "GPU model uploaded: primitives=" << stats.primitiveCount
                    << ", vertices=" << stats.vertexCount
                    << ", indices=" << stats.indexCount
                    << ", vertex bytes=" << stats.vertexBytes
                    << ", index bytes=" << stats.indexBytes;
            logger.Info(Diagnostics::LogCategory::Render, message.str());
            return UploadedGpuModel{.modelIndex = modelIndex, .stats = stats};
        }
        catch (const std::exception& exception)
        {
            logger.Error(Diagnostics::LogCategory::Render, exception.what());
            return std::unexpected(exception.what());
        }
    }

    std::expected<ModelDrawStats, std::string> DrawModel(
        const std::size_t modelIndex,
        const std::span<const ModelDrawInstance> draws,
        const OrthographicCamera& camera)
    {
        if (!frameOpen)
        {
            return std::unexpected("model draw is only valid between BeginFrame and EndFrame");
        }
        if (modelIndex >= gpuModels.size())
        {
            return std::unexpected("GPU model handle does not resolve in this renderer");
        }
        if (modelPipeline == nullptr || modelRootSignature == nullptr || depthBuffer == nullptr)
        {
            return std::unexpected("model pipeline or depth buffer is not ready");
        }
        if (!IsFinite(camera.viewProjection) || !std::isfinite(camera.width) ||
            !std::isfinite(camera.height) || camera.width <= 0.0F || camera.height <= 0.0F)
        {
            return std::unexpected("model draw received invalid camera projection data");
        }

        GpuModel& gpuModel = gpuModels[modelIndex];
        commandList->SetGraphicsRootSignature(modelRootSignature.Get());
        commandList->SetPipelineState(modelPipeline.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ModelDrawStats stats;
        for (const ModelDrawInstance& draw : draws)
        {
            if (draw.primitiveIndex >= gpuModel.primitives.size())
            {
                std::ostringstream message;
                message << "draw references invalid GPU primitive " << draw.primitiveIndex;
                return std::unexpected(message.str());
            }

            const GpuPrimitive& primitive = gpuModel.primitives[draw.primitiveIndex];
            DrawRootConstants constants;
            constants.model = draw.modelToWorld.values;
            constants.normal = draw.normalToWorld.rows;
            constants.viewProjection = camera.viewProjection.values;
            constants.baseColor = draw.material.baseColorFactor;
            constants.metallic = draw.material.metallicFactor;
            constants.roughness = draw.material.roughnessFactor;
            commandList->SetGraphicsRoot32BitConstants(
                0,
                sizeof(DrawRootConstants) / sizeof(std::uint32_t),
                &constants,
                0);
            commandList->IASetVertexBuffers(0, 1, &primitive.vertexView);
            commandList->IASetIndexBuffer(&primitive.indexView);
            commandList->DrawIndexedInstanced(primitive.indexCount, 1, 0, 0, 0);

            ++stats.drawCalls;
            ++stats.submittedPrimitives;
            stats.submittedIndices += primitive.indexCount;
        }

        if (logNextDraw)
        {
            std::ostringstream message;
            message << "Indexed model draw: draw calls=" << stats.drawCalls
                    << ", submitted primitives=" << stats.submittedPrimitives
                    << ", submitted indices=" << stats.submittedIndices
                    << ", camera horizontal span=" << camera.width
                    << ", vertical span=" << camera.height
                    << ", aspect=" << camera.width / camera.height;
            logger.Info(Diagnostics::LogCategory::Render, message.str());
            logNextDraw = false;
        }
        return stats;
    }

    std::expected<void, std::string> ClearViewportRect(const ViewportRect& rect, const RgbaColor& color)
    {
        if (!frameOpen)
        {
            return std::unexpected("viewport clear is only valid between BeginFrame and EndFrame");
        }
        if (width == 0 || height == 0)
        {
            return std::unexpected("render target has no size for viewport clear");
        }
        const auto validatedColor = ValidateRgbaColor(color);
        if (!validatedColor)
        {
            return std::unexpected(validatedColor.error());
        }

        const auto validatedRect = ValidateViewportRect(rect);
        if (!validatedRect)
        {
            return std::unexpected(validatedRect.error());
        }
        const auto pixelRect = ToPixelRect(*validatedRect, width, height);
        if (!pixelRect)
        {
            return std::unexpected(pixelRect.error());
        }

        // D3D12 ClearRenderTargetView is bounded by its OWN rect array — not by the rasterizer scissor.
        // NumRects=0/pRects=nullptr would clear the ENTIRE render target regardless of any scissor, so the
        // pixel rect must be passed explicitly (M2 Slice D2 correction). No scissor save/restore is needed:
        // the model draw path keeps the full-viewport scissor set by BeginFrame.
        const D3D12_RECT clearRect{
            .left = static_cast<LONG>(pixelRect->left),
            .top = static_cast<LONG>(pixelRect->top),
            .right = static_cast<LONG>(pixelRect->right),
            .bottom = static_cast<LONG>(pixelRect->bottom)};
        const FLOAT clearColor[4] = {validatedColor->r, validatedColor->g, validatedColor->b,
                                     validatedColor->a};
        commandList->ClearRenderTargetView(sceneColorRtvHandle, clearColor, 1, &clearRect);
        return {};
    }

    void Resize(const std::uint32_t newWidth, const std::uint32_t newHeight)
    {
        if (!initialized || newWidth == 0 || newHeight == 0 || (newWidth == width && newHeight == height))
        {
            return;
        }

        try
        {
            FlushGpu();
            for (ComPtr<ID3D12Resource>& buffer : backBuffers)
            {
                buffer.Reset();
            }
            sceneColorHdr.Reset();
            depthBuffer.Reset();
            frameFenceValues.fill(0);
            ThrowIfFailed(
                swapChain->ResizeBuffers(BufferCount, newWidth, newHeight, BufferFormat, 0),
                "ResizeBuffers");
            width = newWidth;
            height = newHeight;
            frameIndex = swapChain->GetCurrentBackBufferIndex();
            CreateRenderTargets();
            CreateSceneColorTarget();
            CreateDepthBuffer();
            logNextDraw = true;
            logger.Info(Diagnostics::LogCategory::Render, "Swap chain resized");
        }
        catch (const std::exception& exception)
        {
            logger.Error(Diagnostics::LogCategory::Render, exception.what());
            throw;
        }
    }

    void BeginFrame()
    {
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        WaitForFrame(frameIndex);
        ThrowIfFailed(commandAllocators[frameIndex]->Reset(), "Reset command allocator");
        ThrowIfFailed(commandList->Reset(commandAllocators[frameIndex].Get(), nullptr), "Reset command list");

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        barrier.Transition.pResource = sceneColorHdr.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0F;
        // Full-viewport rasterizer scissor for the model draw path. ClearViewportRect does NOT rely on it:
        // D3D12 RTV clears are bounded by their own rect array, not by the scissor (M2 Slice D2 correction).
        const D3D12_RECT fullScissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &fullScissor);
        commandList->OMSetRenderTargets(1, &sceneColorRtvHandle, FALSE, &dsvHandle);
        // Generic default background for frames before/without game content. The Game paints its own
        // presentation colors over this (M2 Slice D2) — this value carries no contract to gameplay.
        commandList->ClearRenderTargetView(
            sceneColorRtvHandle,
            DefaultSceneClearColor.data(),
            0,
            nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
        ID3D12DescriptorHeap* heaps[] = {imguiHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);
        frameOpen = true;
        sceneColorOutputPending = true;
    }

    void ToneMapSceneToSdr()
    {
        if (!frameOpen || !sceneColorOutputPending)
        {
            throw std::runtime_error("tone mapping is only valid once between BeginFrame and EndFrame");
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = sceneColorHdr.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        commandList->OMSetRenderTargets(1, &rtvHandles[frameIndex], FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = {imguiHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootSignature(toneMapRootSignature.Get());
        commandList->SetPipelineState(toneMapPipeline.Get());
        commandList->SetGraphicsRootDescriptorTable(0, sceneColorSrvGpuHandle);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);
        sceneColorOutputPending = false;
    }

    void EndFrame()
    {
        if (!frameOpen || sceneColorOutputPending)
        {
            throw std::runtime_error("EndFrame requires the HDR scene to be tone mapped first");
        }
        frameOpen = false;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
        ThrowIfFailed(commandList->Close(), "Close command list");

        ID3D12CommandList* lists[] = {commandList.Get()};
        commandQueue->ExecuteCommandLists(1, lists);
        ThrowIfFailed(swapChain->Present(vsync ? 1U : 0U, 0), "Present");

        const std::uint64_t signalValue = nextFenceValue++;
        ThrowIfFailed(commandQueue->Signal(fence.Get(), signalValue), "Signal frame fence");
        frameFenceValues[frameIndex] = signalValue;
    }

    void Shutdown()
    {
        if (initialized)
        {
            try
            {
                FlushGpu();
            }
            catch (const std::exception& exception)
            {
                logger.Error(Diagnostics::LogCategory::Render, exception.what());
            }
        }

        gpuModels.clear();
        toneMapPipeline.Reset();
        toneMapRootSignature.Reset();
        modelPipeline.Reset();
        modelRootSignature.Reset();
        sceneColorHdr.Reset();
        depthBuffer.Reset();
        frameOpen = false;
        sceneColorOutputPending = false;
#if defined(DEEPRUN_DEBUG)
        ValidateDebugMessages("Renderer shutdown");
#endif

        if (fenceEvent != nullptr)
        {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
        initialized = false;
        if (device != nullptr)
        {
            logger.Info(Diagnostics::LogCategory::Render, "D3D12 renderer shut down");
        }
    }

    Diagnostics::Logger& logger;
    const std::uint64_t rendererIdentity;
    UINT factoryFlags = 0;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    std::array<ComPtr<ID3D12CommandAllocator>, BufferCount> commandAllocators;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> imguiHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    std::array<ComPtr<ID3D12Resource>, BufferCount> backBuffers;
    ComPtr<ID3D12Resource> sceneColorHdr;
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12RootSignature> modelRootSignature;
    ComPtr<ID3D12PipelineState> modelPipeline;
    ComPtr<ID3D12RootSignature> toneMapRootSignature;
    ComPtr<ID3D12PipelineState> toneMapPipeline;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, BufferCount> rtvHandles{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorRtvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE imguiCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE imguiGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::array<std::uint64_t, BufferCount> frameFenceValues{};
    std::uint64_t nextFenceValue = 1;
    std::uint32_t frameIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    UINT rtvIncrement = 0;
    UINT imguiIncrement = 0;
    std::vector<GpuModel> gpuModels;
    bool initialized = false;
    bool vsync = true;
    bool frameOpen = false;
    bool sceneColorOutputPending = false;
    bool logNextDraw = true;
};

D3D12Renderer::D3D12Renderer(Diagnostics::Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

D3D12Renderer::~D3D12Renderer() = default;

bool D3D12Renderer::Initialize(
    void* windowHandle,
    const std::uint32_t width,
    const std::uint32_t height,
    const bool vsync,
    const std::filesystem::path& shaderRoot)
{
    return impl_->Initialize(windowHandle, width, height, vsync, shaderRoot);
}

void D3D12Renderer::Resize(const std::uint32_t width, const std::uint32_t height)
{
    impl_->Resize(width, height);
}

void D3D12Renderer::WaitForIdle()
{
    impl_->FlushGpu();
}

std::expected<GpuModelUploadResult, std::string> D3D12Renderer::UploadModel(
    const Assets::ModelAsset& model)
{
    const auto upload = impl_->UploadModel(model);
    if (!upload)
    {
        return std::unexpected(upload.error());
    }
    return GpuModelUploadResult{
        .handle = GpuModelHandle(impl_->rendererIdentity, upload->modelIndex),
        .stats = upload->stats};
}

bool D3D12Renderer::IsGpuModelValid(const GpuModelHandle handle) const noexcept
{
    return handle.IsValid() && handle.rendererIdentity_ == impl_->rendererIdentity &&
           handle.modelIndex_ < impl_->gpuModels.size();
}

std::expected<ModelDrawStats, std::string> D3D12Renderer::DrawModel(
    const GpuModelHandle handle,
    const std::span<const ModelDrawInstance> draws,
    const OrthographicCamera& camera)
{
    if (!IsGpuModelValid(handle))
    {
        return std::unexpected("invalid or foreign GPU model handle");
    }
    return impl_->DrawModel(handle.modelIndex_, draws, camera);
}

std::expected<void, std::string> D3D12Renderer::ClearViewportRect(
    const ViewportRect& rect,
    const RgbaColor& color)
{
    return impl_->ClearViewportRect(rect, color);
}

void D3D12Renderer::BeginFrame()
{
    impl_->BeginFrame();
}

void D3D12Renderer::ToneMapSceneToSdr()
{
    impl_->ToneMapSceneToSdr();
}

void D3D12Renderer::EndFrame()
{
    impl_->EndFrame();
}

bool D3D12Renderer::IsInitialized() const noexcept
{
    return impl_->initialized;
}

bool D3D12Renderer::IsModelPipelineReady() const noexcept
{
    return impl_->modelPipeline != nullptr && impl_->modelRootSignature != nullptr;
}

bool D3D12Renderer::IsDepthBufferReady() const noexcept
{
    return impl_->depthBuffer != nullptr;
}

float D3D12Renderer::AspectRatio() const noexcept
{
    return impl_->height != 0 ? static_cast<float>(impl_->width) / static_cast<float>(impl_->height) : 0.0F;
}

ID3D12Device* D3D12Renderer::Device() const noexcept
{
    return impl_->device.Get();
}

ID3D12GraphicsCommandList* D3D12Renderer::CommandList() const noexcept
{
    return impl_->commandList.Get();
}

ID3D12DescriptorHeap* D3D12Renderer::ImGuiDescriptorHeap() const noexcept
{
    return impl_->imguiHeap.Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Renderer::ImGuiCpuHandle() const noexcept
{
    return impl_->imguiCpuHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Renderer::ImGuiGpuHandle() const noexcept
{
    return impl_->imguiGpuHandle;
}

DXGI_FORMAT D3D12Renderer::BackBufferFormat() const noexcept
{
    return BufferFormat;
}

std::uint32_t D3D12Renderer::FrameCount() const noexcept
{
    return BufferCount;
}

}
