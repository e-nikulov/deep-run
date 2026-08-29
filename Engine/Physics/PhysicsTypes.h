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
};
}
