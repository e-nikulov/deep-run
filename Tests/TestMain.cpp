#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/CoreServices.h"
#include "Engine/Core/Random.h"
#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"

#include <exception>
#include <functional>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Test = std::pair<std::string_view, std::function<bool()>>;

bool CoreStartupShutdown()
{
    DeepRun::Core::CoreServices core;
    core.Log().Info(DeepRun::Diagnostics::LogCategory::Core, "Core lifecycle test active");
    return true;
}

bool DeterministicRandom()
{
    DeepRun::Core::Random first(0xD33F1234U);
    DeepRun::Core::Random second(0xD33F1234U);
    for (int index = 0; index < 32; ++index)
    {
        if (first.NextUInt() != second.NextUInt())
        {
            return false;
        }
    }
    return true;
}

bool JoltInitialization()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld physics(logger);
    return physics.Initialize() && physics.IsInitialized();
}

bool RigidBodySimulation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld physics(logger);
    return physics.Initialize() && physics.RunGravitySmokeTest();
}

bool AudioBoundary()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Audio::AudioEngine audio(logger);
    const bool initialized = audio.Initialize();
    if (!initialized)
    {
        std::cout << "[SKIP] Audio device unavailable: " << audio.Status() << '\n';
    }
    return !audio.Status().empty();
}
}

int main()
{
    const std::vector<Test> tests{
        {"Core startup/shutdown", CoreStartupShutdown},
        {"Deterministic random", DeterministicRandom},
        {"Jolt initialization", JoltInitialization},
        {"Rigid-body gravity", RigidBodySimulation},
        {"Audio abstraction", AudioBoundary},
    };

    int failed = 0;
    for (const auto& [name, test] : tests)
    {
        try
        {
            if (test())
            {
                std::cout << "[PASS] " << name << '\n';
            }
            else
            {
                std::cerr << "[FAIL] " << name << '\n';
                ++failed;
            }
        }
        catch (const std::exception& exception)
        {
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
            ++failed;
        }
    }

    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
