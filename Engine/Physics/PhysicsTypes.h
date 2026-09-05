#pragma once

#include <cmath>
#include <cstdint>

namespace DeepRun::Physics
{
// World-space vector used by the public physics API.
// This is intentionally separate from Assets::ModelVector3: assets and physics are different subsystems.
struct PhysicsVector3 final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] bool IsFinite() const noexcept
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    [[nodiscard]] bool operator==(const PhysicsVector3&) const noexcept = default;
};

// Rotation quaternion used by the public physics API.
// Component order is x, y, z, w everywhere in DeepRun (Jolt's internal ordering must never leak out).
struct PhysicsQuaternion final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    [[nodiscard]] bool IsFinite() const noexcept
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
    }

    [[nodiscard]] float LengthSquared() const noexcept
    {
        return x * x + y * y + z * z + w * w;
    }

    // q and -q represent the same rotation, so orientation comparison must use |dot|, not component equality.
    [[nodiscard]] static bool SameRotation(const PhysicsQuaternion& first, const PhysicsQuaternion& second) noexcept
    {
        return std::abs(first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w) > 1.0F - 1e-5F;
    }

    [[nodiscard]] bool operator==(const PhysicsQuaternion&) const noexcept = default;
};

// Opaque non-owning handle to a rigid body owned by one PhysicsWorld.
// A handle is only valid in the world that created it, and becomes invalid when its body is destroyed.
class PhysicsBodyHandle final
{
public:
    PhysicsBodyHandle() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return slot_ != InvalidSlot;
    }

    [[nodiscard]] bool operator==(const PhysicsBodyHandle&) const noexcept = default;

private:
    static constexpr std::size_t InvalidSlot = static_cast<std::size_t>(-1);

    // Backend-only identity. Public callers must not inspect it; only the owning PhysicsWorld resolves handles.
    [[nodiscard]] std::uint64_t WorldIdentity() const noexcept
    {
        return worldIdentity_;
    }

    [[nodiscard]] std::size_t Slot() const noexcept
    {
        return slot_;
    }

    // Prevents stale-handle aliasing if slot recycling is introduced later.
    [[nodiscard]] std::uint32_t Generation() const noexcept
    {
        return generation_;
    }

    PhysicsBodyHandle(const std::uint64_t worldIdentity, const std::size_t slot, const std::uint32_t generation) noexcept
        : worldIdentity_(worldIdentity), slot_(slot), generation_(generation)
    {
    }

    std::uint64_t worldIdentity_ = 0;
    std::size_t slot_ = InvalidSlot;
    std::uint32_t generation_ = 0;

    friend class PhysicsWorld;
};

// Snapshot of a rigid body's state. This is a copy: callers never hold references into physics memory.
struct PhysicsBodyState final
{
    PhysicsVector3 position{};
    PhysicsQuaternion orientation{};
    PhysicsVector3 linearVelocity{};
    PhysicsVector3 angularVelocity{};
    bool active = false;
};

// DeepRun-owned allowed degrees of freedom for a dynamic rigid body (M2 Slice C2.1).
// This is the only DOF representation in the public physics API: backends translate it into their own
// mechanism, and no backend enum or type may leak through this header. Axes are world-space axes of the
// DeepRun right-handed convention (+X gameplay direction, +Y vertical/depth, +Z toward camera).
struct PhysicsDegreesOfFreedom final
{
    bool translationX = true;
    bool translationY = true;
    bool translationZ = true;
    bool rotationX = true;
    bool rotationY = true;
    bool rotationZ = true;

    // The generic default: all six DOFs allowed. This preserves the pre-C2.1 body-creation behaviour, so
    // callers that never touch this field keep exactly the C1 semantics.
    [[nodiscard]] static constexpr PhysicsDegreesOfFreedom All() noexcept
    {
        return {};
    }

    // True when every DOF is allowed (the default). Backends may use this to skip any restriction work.
    [[nodiscard]] bool IsAllAllowed() const noexcept
    {
        return translationX && translationY && translationZ && rotationX && rotationY && rotationZ;
    }

    // A body with no DOFs at all is not a valid dynamic body: use a static body instead (backend contract).
    [[nodiscard]] bool IsAnyAllowed() const noexcept
    {
        return translationX || translationY || translationZ || rotationX || rotationY || rotationZ;
    }

    // True when the given initial linear velocity has no component on a locked translation axis.
    [[nodiscard]] bool AllowsLinearVelocity(const PhysicsVector3& velocity) const noexcept
    {
        return (translationX || velocity.x == 0.0F) && (translationY || velocity.y == 0.0F) &&
               (translationZ || velocity.z == 0.0F);
    }

    // True when the given initial angular velocity has no component on a locked rotation axis.
    [[nodiscard]] bool AllowsAngularVelocity(const PhysicsVector3& velocity) const noexcept
    {
        return (rotationX || velocity.x == 0.0F) && (rotationY || velocity.y == 0.0F) &&
               (rotationZ || velocity.z == 0.0F);
    }

    [[nodiscard]] bool operator==(const PhysicsDegreesOfFreedom&) const noexcept = default;
};

// Generic axis-aligned static box. PhysicsWorld owns the body lifetime.
struct StaticBoxBodyCreateInfo final
{
    PhysicsVector3 halfExtents{};
    PhysicsVector3 position{};
    bool operator==(const StaticBoxBodyCreateInfo&) const noexcept = default;
};

// Creation parameters for a dynamic box rigid body (M2 Slice C1).
// Box extents are half-extents: the collision box spans [-halfExtents, +halfExtents] around the body position.
struct DynamicBoxBodyCreateInfo final
{
    PhysicsVector3 halfExtents{};
    float mass = 0.0F;
    PhysicsVector3 position{};
    PhysicsQuaternion orientation{};
    bool gravityEnabled = true;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    PhysicsVector3 initialLinearVelocity{};
    PhysicsVector3 initialAngularVelocity{};

    // Allowed DOFs for the new body (M2 Slice C2.1). Default: all six allowed, i.e. unchanged C1 behaviour.
    // The restriction is applied at creation time in the backend's mass/motion configuration; it is not a
    // per-step clamp and no constraint or joint is introduced.
    PhysicsDegreesOfFreedom degreesOfFreedom = PhysicsDegreesOfFreedom::All();

    // Locked-DOF input rule (M2 Slice C2.1): initial velocities on locked axes are rejected as recoverable
    // InvalidInput errors instead of being silently dropped. The backend's own DOF locking would zero those
    // components anyway, but accepting them would hide caller mistakes; the rejection makes the contract
    // explicit and testable (see "Planar body rejects initial velocity on locked axes" in Tests/TestMain.cpp).
};
}
