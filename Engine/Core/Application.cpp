#include "Engine/Core/Application.h"

#include "Engine/Core/Engine.h"
#include "Engine/Platform/Platform.h"
#include "Engine/Diagnostics/FrameStatistics.h"
#include "Engine/Render/D3D12Renderer.h"

#include <array>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <utility>

namespace DeepRun::Core
{
namespace
{
// Bounded acceptance evidence for the executable's existing scene, not a benchmark framework.
class PerformanceRun final
{
public:
    using Clock = std::chrono::steady_clock; // RealTime diagnostics; never advances gameplay.
    static constexpr double WarmupSeconds = 5.0;
    static constexpr std::size_t SampleLimit = 65'536;

    explicit PerformanceRun(const double measurementSeconds) : measurementSeconds_(measurementSeconds)
    {
        for (auto& values : samples_) values.reserve(SampleLimit);
    }

    bool Finished(const Clock::time_point now) const
    {
        return std::chrono::duration<double>(now - start_).count() >= WarmupSeconds + measurementSeconds_;
    }

    void Observe(Engine& engine, const Clock::time_point begin, const Clock::time_point end)
    {
        const double elapsed = std::chrono::duration<double>(begin - start_).count();
        const auto& gpu = engine.Renderer()->FrameDiagnostics();
        if (elapsed < WarmupSeconds)
        {
            previousBegin_ = begin;
            return;
        }
        const auto memory = engine.Renderer()->MemoryDiagnostics();
        // Resize stress is intentionally completed during warm-up. A queued Win32 resize can be observed
        // for one more rendered frame, so do not establish an acceptance baseline until the required
        // native-resolution back buffers are active again.
        if (!baselineCaptured_ && (memory.width != 2560U || memory.height != 1440U))
        {
            previousBegin_ = begin;
            return;
        }
        if (!baselineCaptured_)
        {
            firstGpuFrame_ = gpu.submittedFrame;
            firstBegin_ = begin;
            baseline_ = memory;
            baselineCaptured_ = true;
        }
        const double full = Milliseconds(end - begin);
        const auto& cpu = engine.FrameDiagnostics();
        const std::array<double, 6> values{
            full, std::max(0.0, full - gpu.presentMilliseconds - gpu.frameSlotWaitMilliseconds),
            cpu.fixedMilliseconds, cpu.gameSubmissionMilliseconds,
            gpu.presentMilliseconds, gpu.frameSlotWaitMilliseconds};
        if (samples_[0].size() < SampleLimit)
        {
            for (std::size_t i = 0; i < values.size(); ++i) samples_[i].push_back(values[i]);
            // Exclude the interval starting before the first measured frame.
            if (measuredFrames_ > 0) samples_[6].push_back(Milliseconds(begin - previousBegin_));
        }
        else overflow_ = true;
        if (gpu.gpuTimingAvailable && gpu.completedGpuFrame >= firstGpuFrame_ &&
            gpu.completedGpuFrame > lastGpuFrame_)
        {
            if (samples_[7].size() < SampleLimit) samples_[7].push_back(gpu.gpuMilliseconds);
            else overflow_ = true;
            lastGpuFrame_ = gpu.completedGpuFrame;
        }
        ++measuredFrames_;
        previousBegin_ = begin;
        lastEnd_ = end;
        contractFailed_ |= memory.width != 2560U || memory.height != 1440U ||
            memory.modelCount != baseline_.modelCount || memory.geometryBytes != baseline_.geometryBytes ||
            memory.trackedResourceCount != baseline_.trackedResourceCount;
        if (elapsed >= nextMemorySeconds_)
        {
            PrintMemory(elapsed, memory);
            nextMemorySeconds_ = elapsed + 5.0;
        }
    }

    bool Report(Engine& engine)
    {
        const bool complete = Finished(Clock::now()) && measuredFrames_ != 0;
        const auto finalMemory = engine.Renderer()->MemoryDiagnostics();
        PrintMemory(std::chrono::duration<double>(Clock::now() - start_).count(),
                    finalMemory);
        std::cout << std::fixed << std::setprecision(4)
                  << "[Benchmark] requested_resolution=2560x1440 actual_resolution="
                  << finalMemory.width << 'x' << finalMemory.height << " mode="
                  << (engine.Renderer()->OutputMode() == Render::DisplayOutputMode::HdrScRgb ? "HDR-scRGB" : "SDR")
                  << " warmup_s=" << WarmupSeconds << " requested_measurement_s=" << measurementSeconds_
                  << " actual_sample_span_s=" << std::chrono::duration<double>(lastEnd_ - firstBegin_).count()
                  << " frames=" << measuredFrames_ << " capacity=" << SampleLimit << '\n';
        constexpr std::array<const char*, 8> names{
            "cpu_full_update_render", "cpu_excluding_present_slot_wait", "fixed_game_physics",
            "game_render_submission", "present", "frame_slot_wait", "frame_pacing", "gpu_full_frame"};
        for (std::size_t index = 0; index < samples_.size(); ++index)
        {
            const auto statistics = Diagnostics::SummarizeFrameSamples(samples_[index]);
            std::cout << "[Benchmark] " << names[index] << " samples=" << samples_[index].size();
            if (!statistics || overflow_) std::cout << " NOT MEASURABLE";
            else
            {
                std::cout << " median_ms=" << statistics->median << " p95_ms=" << statistics->p95
                          << " p99_ms=" << statistics->p99 << " max_ms=" << statistics->maximum;
                if (index == 6U && statistics->median > 0.0)
                    std::cout << " median_equivalent_fps=" << 1000.0 / statistics->median;
            }
            std::cout << '\n';
        }
        std::cout << "[Benchmark] structural_contract=" << (complete && !contractFailed_ ? "PASS" : "FAIL")
                  << " sample_capacity=" << (overflow_ ? "EXCEEDED" : "PASS")
                  << " (machine timings are evidence, not exit-code assertions)\n";
        return complete && !contractFailed_;
    }

private:
    static double Milliseconds(const Clock::duration duration)
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    static void PrintMemory(const double seconds, const Render::RendererMemoryDiagnostics& memory)
    {
        const auto process = Platform::QueryProcessMemory();
        std::cout << "[BenchmarkMemory] elapsed_s=" << seconds
                  << " process_available=" << process.available
                  << " working_set_bytes=" << process.workingSetBytes
                  << " private_commit_bytes=" << process.privateCommitBytes
                  << " dxgi_local_available=" << memory.videoMemoryAvailable
                  << " dxgi_process_local_usage_bytes=" << memory.localUsageBytes
                  << " dxgi_process_local_budget_bytes=" << memory.localBudgetBytes
                  << " logical_geometry_bytes=" << memory.geometryBytes
                  << " tracked_resources=" << memory.trackedResourceCount
                  << " models=" << memory.modelCount << '\n';
    }

    double measurementSeconds_;
    Clock::time_point start_ = Clock::now();
    Clock::time_point previousBegin_ = start_;
    Clock::time_point firstBegin_ = start_;
    Clock::time_point lastEnd_ = start_;
    std::array<std::vector<double>, 8> samples_;
    Render::RendererMemoryDiagnostics baseline_{};
    bool baselineCaptured_ = false;
    std::uint64_t firstGpuFrame_ = 0;
    std::uint64_t lastGpuFrame_ = 0;
    std::uint64_t measuredFrames_ = 0;
    double nextMemorySeconds_ = WarmupSeconds;
    bool overflow_ = false;
    bool contractFailed_ = false;
};
}

ApplicationOptions ApplicationOptions::Parse(const std::span<const std::string_view> arguments)
{
    ApplicationOptions options;
    for (const std::string_view argument : arguments)
    {
        if (argument == "--headless")
        {
            options.headless = true;
        }
        else if (argument == "--smoke-test")
        {
            options.smokeTest = true;
        }
        else if (argument == "--benchmark-m3") options.benchmarkM3 = true;
        else if (argument == "--benchmark-hdr") options.benchmarkHdr = true;
        else if (argument == "--benchmark-stability") options.benchmarkStability = true;
    }
    return options;
}

Application::Application(
    const ApplicationOptions options,
    StartupHook startupHook,
    FixedUpdateHook fixedUpdateHook,
    RenderHook renderHook)
    : options_(options), startupHook_(std::move(startupHook)), fixedUpdateHook_(std::move(fixedUpdateHook)),
      renderHook_(std::move(renderHook))
{
}

int Application::Run()
{
    if ((options_.benchmarkM3 && (options_.headless || options_.smokeTest)) ||
        (!options_.benchmarkM3 && (options_.benchmarkHdr || options_.benchmarkStability)))
    {
        std::cerr << "[Benchmark] Invalid option combination\n";
        return 12;
    }
    const std::filesystem::path executableDirectory = Platform::ExecutablePath().parent_path();
    Engine engine({
        .headless = options_.headless,
        .smokeTest = options_.smokeTest,
        .performanceRun = options_.benchmarkM3,
        .performanceHdr = options_.benchmarkHdr,
        .resizeStress = options_.benchmarkStability,
        .configPath = executableDirectory / "Config" / "engine.json",
        .contentRoot = executableDirectory / "Content",
        .shaderRoot = executableDirectory / "Shaders"});
    if (!engine.Initialize())
    {
        return engine.ExitCode();
    }
    if (startupHook_ && !startupHook_(engine))
    {
        engine.Shutdown();
        return 9;
    }

    const Engine::FixedUpdateHook activeFixedUpdateHook = options_.headless ? Engine::FixedUpdateHook{} : fixedUpdateHook_;
    std::optional<PerformanceRun> performance;
    if (options_.benchmarkM3) performance.emplace(options_.benchmarkStability ? 65.0 : 25.0);
    while (engine.Lifecycle() == EngineLifecycle::Running)
    {
        const auto begin = PerformanceRun::Clock::now();
        if (performance && performance->Finished(begin))
        {
            engine.RequestShutdown();
            break;
        }
        // Ordinary --headless keeps its established self-contained physics smoke path. A composition fixed
        // hook is a windowed gameplay concern in the executable; direct headless integration tests use
        // lower-level simulation APIs without constructing the playground or renderer.
        if (engine.Update(activeFixedUpdateHook))
        {
            const bool rendered = engine.Render(renderHook_);
            const auto end = PerformanceRun::Clock::now();
            if (performance && rendered) performance->Observe(engine, begin, end);
        }
    }
    const bool performancePassed = !performance || performance->Report(engine);
    engine.Shutdown();
    return engine.ExitCode() != 0 ? engine.ExitCode() : performancePassed ? 0 : 12;
}
}
