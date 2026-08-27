#include "Engine/Render/D3D12Renderer.h"

#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace DeepRun::Render
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr std::uint32_t BufferCount = 2;
constexpr DXGI_FORMAT BufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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
}

class D3D12Renderer::Impl final
{
public:
    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger)
    {
    }

    ~Impl()
    {
        Shutdown();
    }

    bool Initialize(void* nativeHandle, const std::uint32_t requestedWidth, const std::uint32_t requestedHeight)
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
            rtvDescription.NumDescriptors = BufferCount;
            ThrowIfFailed(device->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&rtvHeap)), "Create RTV heap");
            rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            D3D12_DESCRIPTOR_HEAP_DESC imguiDescription{};
            imguiDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            imguiDescription.NumDescriptors = 1;
            imguiDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(
                device->CreateDescriptorHeap(&imguiDescription, IID_PPV_ARGS(&imguiHeap)),
                "Create ImGui descriptor heap");

            width = requestedWidth;
            height = requestedHeight;
            CreateSwapChain(static_cast<HWND>(nativeHandle));
            CreateRenderTargets();

            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
            fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (fenceEvent == nullptr)
            {
                throw std::runtime_error("CreateEventW for D3D12 fence failed");
            }

            initialized = true;
            logger.Info(Diagnostics::LogCategory::Render, "D3D12 device and swap chain created");
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
            frameFenceValues.fill(0);
            ThrowIfFailed(
                swapChain->ResizeBuffers(BufferCount, newWidth, newHeight, BufferFormat, 0),
                "ResizeBuffers");
            width = newWidth;
            height = newHeight;
            frameIndex = swapChain->GetCurrentBackBufferIndex();
            CreateRenderTargets();
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

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0F;
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->OMSetRenderTargets(1, &rtvHandles[frameIndex], FALSE, nullptr);
        constexpr float clearColor[] = {0.015F, 0.055F, 0.075F, 1.0F};
        commandList->ClearRenderTargetView(rtvHandles[frameIndex], clearColor, 0, nullptr);
        ID3D12DescriptorHeap* heaps[] = {imguiHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);
    }

    void EndFrame()
    {
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
        ThrowIfFailed(swapChain->Present(1, 0), "Present");

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
    std::array<ComPtr<ID3D12Resource>, BufferCount> backBuffers;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, BufferCount> rtvHandles{};
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::array<std::uint64_t, BufferCount> frameFenceValues{};
    std::uint64_t nextFenceValue = 1;
    std::uint32_t frameIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    UINT rtvIncrement = 0;
    bool initialized = false;
};

D3D12Renderer::D3D12Renderer(Diagnostics::Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

D3D12Renderer::~D3D12Renderer() = default;

bool D3D12Renderer::Initialize(void* windowHandle, const std::uint32_t width, const std::uint32_t height)
{
    return impl_->Initialize(windowHandle, width, height);
}

void D3D12Renderer::Resize(const std::uint32_t width, const std::uint32_t height)
{
    impl_->Resize(width, height);
}

void D3D12Renderer::BeginFrame()
{
    impl_->BeginFrame();
}

void D3D12Renderer::EndFrame()
{
    impl_->EndFrame();
}

bool D3D12Renderer::IsInitialized() const noexcept
{
    return impl_->initialized;
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
    return impl_->imguiHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Renderer::ImGuiGpuHandle() const noexcept
{
    return impl_->imguiHeap->GetGPUDescriptorHandleForHeapStart();
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
