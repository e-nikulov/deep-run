#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <memory>
#include <optional>
#include <string>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Physics
{
// Recoverable error for physics API calls. Programming invariants still use assertions (ADR-0002 / A0).
enum class PhysicsErrorCode
{
    NotInitialized,
    InvalidInput,
    InvalidHandle,
};

struct PhysicsError final
{
    PhysicsErrorCode code = PhysicsErrorCode::InvalidInput;
    std::string message;
};

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

    // Creates a dynamic box rigid body. Returns an opaque handle valid only in this world.
    // Recoverable failures (invalid input, capacity) are reported through the error out-parameter.
    PhysicsBodyHandle CreateDynamicBoxBody(const DynamicBoxBodyCreateInfo& info, PhysicsError* error = nullptr);

    // Removes and destroys the body referenced by a handle from this world.
    // After a successful destroy the handle is invalid forever: slot reuse bumps the generation.
    bool DestroyBody(PhysicsBodyHandle handle, PhysicsError* error = nullptr);

    // Returns a copy of the body state. Invalid/foreign/stale handles are recoverable errors, never UB.
    [[nodiscard]] std::optional<PhysicsBodyState> GetBodyState(PhysicsBodyHandle handle) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
