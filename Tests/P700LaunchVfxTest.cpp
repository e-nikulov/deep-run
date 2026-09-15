#include "Game/Combat/P700VfxShowcase.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <vector>

namespace
{
using DeepRun::Game::Armament::P700LaunchAudioEvent;
using DeepRun::Game::Armament::P700LaunchVfxFrame;
using DeepRun::Game::Armament::P700LaunchVfxSystem;
using DeepRun::Render::TransientVfxPrimitive;
using DeepRun::Weapons::P700GranitPhase;
using DeepRun::Weapons::P700GranitRuntimeState;

[[nodiscard]] DeepRun::Render::OrthographicCamera TestCamera()
{
    DeepRun::Render::OrthographicCamera camera{};
    camera.width = 600.0F;
    camera.height = 337.5F;
    camera.target = {.x = 0.0F, .y = -20.0F, .z = 0.0F};
    return camera;
}

[[nodiscard]] P700GranitRuntimeState MakeMissile(
    const std::uint64_t seed,
    const P700GranitPhase phase,
    const float x,
    const float y,
    const double phaseStartTimeSeconds = 1.0)
{
    P700GranitRuntimeState missile{};
    missile.definitionId = "p700.production";
    missile.phase = phase;
    missile.positionMeters = {.x = x, .y = y, .z = 0.0F};
    missile.launchForwardUnitVector = {.x = 0.76604444F, .y = 0.64278761F, .z = 0.0F};
    missile.surfaceLevelYMeters = 0.0F;
    missile.headingRadians = 0.0F;
    missile.speedMetersPerSecond = phase == P700GranitPhase::UnderwaterLaunch ? 50.0F : 180.0F;
    missile.hatchOpenProgress = 1.0F;
    missile.launcherFloodProgress = 1.0F;
    missile.launchBoosterActive = phase == P700GranitPhase::UnderwaterLaunch || phase == P700GranitPhase::WaterExit;
    missile.launchBoosterAttached = phase != P700GranitPhase::PostExitTransition &&
                                    phase != P700GranitPhase::AirborneDeploying &&
                                    phase != P700GranitPhase::Cruise;
    missile.noseProtectionCapAttached = phase <= P700GranitPhase::WaterExit;
    missile.mainEngineActive = phase == P700GranitPhase::PostExitTransition ||
                               phase == P700GranitPhase::AirborneDeploying ||
                               phase == P700GranitPhase::Cruise;
    missile.postExitTransitionProgress = phase == P700GranitPhase::PostExitTransition ? 0.45F :
                                         (phase > P700GranitPhase::PostExitTransition ? 1.0F : 0.0F);
    missile.deploymentProgress = phase == P700GranitPhase::AirborneDeploying ? 0.45F :
                                 (phase > P700GranitPhase::AirborneDeploying ? 1.0F : 0.0F);
    missile.terminalRandomSeed = seed;
    missile.phaseStartTimeSeconds = phaseStartTimeSeconds;
    missile.lastUpdateTimeSeconds = phaseStartTimeSeconds;
    missile.guidanceTrackId = 9000U + seed;
    return missile;
}

[[nodiscard]] bool HasPrimitive(const P700LaunchVfxFrame& frame, const TransientVfxPrimitive primitive)
{
    for (const auto& emitter : frame.emitters)
        if (emitter.primitive == primitive)
            return true;
    return false;
}

[[nodiscard]] bool HasAudio(const P700LaunchVfxFrame& frame, const P700LaunchAudioEvent event)
{
    for (const auto& hook : frame.audioHooks)
        if (hook.event == event)
            return true;
    return false;
}

[[nodiscard]] bool RunDataDrivenChecks()
{
#ifndef DEEPRUN_SOURCE_ROOT
    std::cerr << "DEEPRUN_SOURCE_ROOT is unavailable\n";
    return false;
#else
    const std::filesystem::path config = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Config" / "p700_vfx.json";
    const auto tuning = DeepRun::Game::Armament::LoadP700LaunchVfxTuning(config);
    if (!tuning || !DeepRun::Game::Armament::ValidateP700LaunchVfxTuning(*tuning))
    {
        std::cerr << "P-700 VFX production tuning did not load/validate\n";
        return false;
    }
    if (!(tuning->lodParticleScales[0] > tuning->lodParticleScales[1] &&
          tuning->lodParticleScales[1] > tuning->lodParticleScales[2] &&
          tuning->lodParticleScales[2] > tuning->lodParticleScales[3]) ||
        tuning->foamLifetimeSeconds <= tuning->dropletLifetimeSeconds ||
        tuning->waterCrownCount == 0U || tuning->waterCrownLifetimeSeconds <= 0.0F)
    {
        std::cerr << "P-700 VFX LOD/persistent-water/crown tuning is not production-bounded\n";
        return false;
    }
    return true;
#endif
}

[[nodiscard]] bool RunLifecycleChecks()
{
    P700LaunchVfxSystem vfx{};
    auto camera = TestCamera();
    const auto environment = DeepRun::Game::Combat::P700VfxShowcaseProfileFor(
        DeepRun::Game::Combat::P700VfxShowcaseEnvironment::DayCalm).environment;

    P700GranitRuntimeState missile = MakeMissile(101U, P700GranitPhase::UnderwaterLaunch, 0.0F, -28.0F);
    const P700GranitRuntimeState* pointer = &missile;
    auto underwater = vfx.BuildFrame(std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, environment, 1.10);
    if (!underwater || !HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMicro) ||
        !HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMeso) ||
        !HasPrimitive(*underwater, TransientVfxPrimitive::BubbleMacro) ||
        !HasPrimitive(*underwater, TransientVfxPrimitive::ExhaustCore) ||
        !HasPrimitive(*underwater, TransientVfxPrimitive::Particulate) ||
        !HasAudio(*underwater, P700LaunchAudioEvent::UnderwaterIgnition) ||
        !underwater->cameraImpulse.has_value())
    {
        std::cerr << "P-700 underwater ignition did not produce the required multi-scale VFX/events\n";
        return false;
    }

    missile.phase = P700GranitPhase::WaterExit;
    missile.positionMeters.y = 0.2F;
    missile.phaseStartTimeSeconds = 1.20;
    missile.launchBoosterActive = true;
    auto breach = vfx.BuildFrame(std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, environment, 1.24);
    if (!breach || !HasPrimitive(*breach, TransientVfxPrimitive::WaterCrown) ||
        !HasPrimitive(*breach, TransientVfxPrimitive::Droplet) ||
        !HasPrimitive(*breach, TransientVfxPrimitive::WaterSheet) ||
        !HasPrimitive(*breach, TransientVfxPrimitive::Mist) ||
        !HasPrimitive(*breach, TransientVfxPrimitive::Foam) ||
        !HasAudio(*breach, P700LaunchAudioEvent::SurfaceBreach) ||
        !HasAudio(*breach, P700LaunchAudioEvent::BoosterAir))
    {
        std::cerr << "P-700 surface breach did not preserve the crown/sheet/droplet/mist/foam water event\n";
        return false;
    }

    missile.phase = P700GranitPhase::PostExitTransition;
    missile.positionMeters = {.x = 16.0F, .y = 11.0F, .z = 0.0F};
    missile.phaseStartTimeSeconds = 1.30;
    missile.launchBoosterActive = false;
    missile.launchBoosterAttached = false;
    missile.mainEngineActive = true;
    auto transition = vfx.BuildFrame(std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, environment, 1.36);
    if (!transition || HasPrimitive(*transition, TransientVfxPrimitive::ExhaustCore) ||
        !HasPrimitive(*transition, TransientVfxPrimitive::ExhaustTurbulent) ||
        !HasAudio(*transition, P700LaunchAudioEvent::BoosterSeparate) ||
        !HasAudio(*transition, P700LaunchAudioEvent::TurbojetStart))
    {
        std::cerr << "P-700 rocket-to-turbojet transition kept rocket-flame visual language or missed event hooks\n";
        return false;
    }

    missile.phase = P700GranitPhase::AirborneDeploying;
    missile.phaseStartTimeSeconds = 1.40;
    missile.deploymentProgress = 0.42F;
    auto humid = environment;
    humid.humidityFraction = 0.96F;
    auto deploy = vfx.BuildFrame(std::span<const P700GranitRuntimeState* const>(&pointer, 1U), camera, humid, 1.46);
    if (!deploy || !HasAudio(*deploy, P700LaunchAudioEvent::WingDeploy) ||
        !HasPrimitive(*deploy, TransientVfxPrimitive::Mist))
    {
        std::cerr << "P-700 wing deployment/humidity condensation presentation is missing\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunSalvoBudgetCheck(const std::size_t missileCount)
{
    P700LaunchVfxSystem vfx{};
    auto camera = TestCamera();
    const auto environment = DeepRun::Game::Combat::P700VfxShowcaseProfileFor(
        DeepRun::Game::Combat::P700VfxShowcaseEnvironment::Storm).environment;

    std::vector<P700GranitRuntimeState> missiles;
    missiles.reserve(missileCount);
    for (std::size_t index = 0; index < missileCount; ++index)
    {
        auto missile = MakeMissile(1000U + index, P700GranitPhase::UnderwaterLaunch,
                                   static_cast<float>(index) * 2.15F, -26.0F + static_cast<float>(index % 3U));
        missile.positionMeters.z = static_cast<float>(index % 2U == 0U ? 6.0F : -6.0F);
        missiles.push_back(missile);
    }
    std::vector<const P700GranitRuntimeState*> pointers;
    pointers.reserve(missiles.size());
    for (const auto& missile : missiles)
        pointers.push_back(&missile);

    const auto frame = vfx.BuildFrame(pointers, camera, environment, 1.12);
    if (!frame || frame->submittedParticles > DeepRun::Render::TransientVfxMaximumParticlesPerFrame ||
        frame->emitters.size() > DeepRun::Render::TransientVfxMaximumEmittersPerFrame ||
        frame->submittedParticles == 0U)
    {
        std::cerr << "P-700 " << missileCount << "-missile salvo exceeded bounded GPU VFX budget\n";
        return false;
    }
    const std::uint32_t counted = frame->missileCountByLod[0] + frame->missileCountByLod[1] +
                                  frame->missileCountByLod[2] + frame->missileCountByLod[3];
    if (counted != missileCount)
    {
        std::cerr << "P-700 salvo LOD accounting lost missiles for count " << missileCount << '\n';
        return false;
    }
    std::cout << "P700 VFX salvo " << missileCount << ": submittedParticles=" << frame->submittedParticles
              << " requestedParticles=" << frame->requestedParticles
              << " emitters=" << frame->emitters.size()
              << " clamped=" << (frame->particleBudgetClamped ? "yes" : "no") << '\n';
    return true;
}

[[nodiscard]] bool RunCpuPresentationBenchmark()
{
    constexpr std::size_t MissileCount = 24U;
    constexpr std::size_t Iterations = 600U;
    P700LaunchVfxSystem vfx{};
    auto camera = TestCamera();
    const auto environment = DeepRun::Game::Combat::P700VfxShowcaseProfileFor(
        DeepRun::Game::Combat::P700VfxShowcaseEnvironment::Storm).environment;

    std::vector<P700GranitRuntimeState> missiles;
    missiles.reserve(MissileCount);
    for (std::size_t index = 0; index < MissileCount; ++index)
    {
        auto missile = MakeMissile(7000U + index, P700GranitPhase::UnderwaterLaunch,
                                   static_cast<float>(index) * 2.15F, -24.0F);
        missile.positionMeters.z = static_cast<float>(index % 2U == 0U ? 6.0F : -6.0F);
        missiles.push_back(missile);
    }
    std::vector<const P700GranitRuntimeState*> pointers;
    pointers.reserve(MissileCount);
    for (const auto& missile : missiles)
        pointers.push_back(&missile);

    std::uint64_t checksum = 0U;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < Iterations; ++iteration)
    {
        const double time = 1.12 + static_cast<double>(iteration) * 0.00001;
        const auto frame = vfx.BuildFrame(pointers, camera, environment, time);
        if (!frame)
        {
            std::cerr << "P-700 CPU presentation benchmark frame failed: " << frame.error() << '\n';
            return false;
        }
        checksum += frame->submittedParticles + frame->emitters.size();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double totalMicroseconds = std::chrono::duration<double, std::micro>(elapsed).count();
    const double averageMicroseconds = totalMicroseconds / static_cast<double>(Iterations);
    std::cout << "P700 VFX CPU BuildFrame 24-missile storm: avg_us=" << averageMicroseconds
              << " iterations=" << Iterations << " checksum=" << checksum << '\n';
    return true;
}

[[nodiscard]] bool RunShowcaseChecks()
{
    using namespace DeepRun::Game::Combat;
    const auto calm = P700VfxShowcaseProfileFor(P700VfxShowcaseEnvironment::DayCalm);
    const auto storm = P700VfxShowcaseProfileFor(P700VfxShowcaseEnvironment::Storm);
    const auto night = P700VfxShowcaseProfileFor(P700VfxShowcaseEnvironment::Night);
    if (calm.deterministicSeed == storm.deterministicSeed || calm.nominalDurationSeconds < 8.0F ||
        calm.nominalDurationSeconds > 15.0F || storm.environment.significantWaveHeightMeters <= calm.environment.significantWaveHeightMeters ||
        !storm.environment.lightningVisible || night.environment.daylightFraction != 0.0F ||
        P700VfxShowcaseCameras.size() != 6U || P700VfxShowcaseSpeedRamp.front().presentationRate != 1.0F ||
        P700VfxShowcaseSpeedRamp.back().presentationRate != 1.0F)
    {
        std::cerr << "P700VfxShowcase deterministic environment/camera contract is invalid\n";
        return false;
    }
    return true;
}
} // namespace

int main()
{
    if (!RunDataDrivenChecks() || !RunLifecycleChecks() || !RunSalvoBudgetCheck(1U) ||
        !RunSalvoBudgetCheck(2U) || !RunSalvoBudgetCheck(6U) || !RunSalvoBudgetCheck(24U) ||
        !RunCpuPresentationBenchmark() || !RunShowcaseChecks())
    {
        return EXIT_FAILURE;
    }
    std::cout << "P-700 signature launch VFX regression: PASS\n";
    return EXIT_SUCCESS;
}
