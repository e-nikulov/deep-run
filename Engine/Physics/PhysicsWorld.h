#pragma once

#include <memory>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Physics
{
class PhysicsWorld final
{
public:
    explicit PhysicsWorld(Diagnostics::Logger& logger);
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    [[nodiscard]] bool Initialize();
    void Step(float deltaSeconds);
    [[nodiscard]] bool RunGravitySmokeTest();
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
