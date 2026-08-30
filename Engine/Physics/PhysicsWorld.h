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

    // Returns the backend's authoritative world-space gravity as a DeepRun-owned value. An uninitialized
    // world has no gravity value; no backend/Jolt type crosses this public boundary.
    [[nodiscard]] std::optional<PhysicsVector3> Gravity() const;

    // Creates a dynamic box rigid body. Returns an opaque handle valid only in this world.
    // Recoverable failures (invalid input, capacity) are reported through the error out-parameter.
    PhysicsBodyHandle CreateDynamicBoxBody(const DynamicBoxBodyCreateInfo& info, PhysicsError* error = nullptr);

    // Removes and destroys the body referenced by a handle from this world.
    // After a successful destroy the handle is invalid forever; destroying increments the slot's
    // generation so a stale handle can never alias a future body in the same slot if recycling is introduced later.
    bool DestroyBody(PhysicsBodyHandle handle, PhysicsError* error = nullptr);

    // Returns a copy of the body state. Invalid/foreign/stale handles are recoverable errors, never UB.
    [[nodiscard]] std::optional<PhysicsBodyState> GetBodyState(PhysicsBodyHandle handle) const;

    // Applies a force (unit: Newtons) at world-space position `worldPositionMeters` (unit: meters) to the
    // body referenced by `handle`, for the upcoming fixed simulation step. This is a force, not an impulse:
    // do NOT pre-scale by delta time, and do NOT pass force * dt as if it were an impulse. The accumulated
    // force is integrated by Jolt during the next Step(deltaSeconds) call and does not persist after that
    // step; continuous forces must be re-applied on every fixed tick (e.g. AddForceAtWorldPosition(...) then
    // Step(1/60)). A zero force (0, 0, 0) is a true no-op and not an error: it does not modify the accumulated
    // force and does not wake a sleeping body (the world position is still validated). When the application
    // point differs from the body's center of mass, Jolt derives the resulting torque itself; callers never
    // compute it. The backend activates a sleeping dynamic body on a successful non-zero call (standard Jolt
    // behaviour).
    // Returns false with PhysicsErrorCode::NotInitialized when the world is not initialized,
    // PhysicsErrorCode::InvalidHandle for invalid/foreign/stale handles, and
    // PhysicsErrorCode::InvalidInput when force or world position contains NaN/Inf.
    bool AddForceAtWorldPosition(
        PhysicsBodyHandle handle,
        PhysicsVector3 forceNewtons,
        PhysicsVector3 worldPositionMeters,
        PhysicsError* error = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
