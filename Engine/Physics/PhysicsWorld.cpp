#include "Engine/Physics/PhysicsWorld.h"

#include "Engine/Diagnostics/Logger.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vector.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace DeepRun::Physics
{
namespace
{
namespace ObjectLayers
{
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
}

namespace BroadPhaseLayers
{
const JPH::BroadPhaseLayer NonMoving(0);
const JPH::BroadPhaseLayer Moving(1);
constexpr std::uint32_t Count = 2;
}

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    BroadPhaseLayerInterface()
    {
        mapping_[ObjectLayers::NonMoving] = BroadPhaseLayers::NonMoving;
        mapping_[ObjectLayers::Moving] = BroadPhaseLayers::Moving;
    }

    [[nodiscard]] std::uint32_t GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::Count;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer layer) const override
    {
        return mapping_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer layer) const override
    {
        if (layer == BroadPhaseLayers::NonMoving)
        {
            return "NonMoving";
        }
        if (layer == BroadPhaseLayers::Moving)
        {
            return "Moving";
        }
        return "Invalid";
    }
#endif

private:
    JPH::BroadPhaseLayer mapping_[ObjectLayers::Count]{};
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    [[nodiscard]] bool ShouldCollide(
        const JPH::ObjectLayer objectLayer,
        const JPH::BroadPhaseLayer broadPhaseLayer) const override
    {
        switch (objectLayer)
        {
        case ObjectLayers::NonMoving: return broadPhaseLayer == BroadPhaseLayers::Moving;
        case ObjectLayers::Moving: return true;
        default: return false;
        }
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    [[nodiscard]] bool ShouldCollide(
        const JPH::ObjectLayer first,
        const JPH::ObjectLayer second) const override
    {
        if (first == ObjectLayers::NonMoving)
        {
            return second == ObjectLayers::Moving;
        }
        return true;
    }
};

void JoltTrace(const char* format, ...)
{
    std::va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

std::uint64_t NextWorldIdentity() noexcept
{
    static std::atomic_uint64_t nextIdentity{1};
    return nextIdentity.fetch_add(1, std::memory_order_relaxed);
}

// Backend bookkeeping for one Jolt body. Slots are append-only in M2 C1 (no recycling);
// generation increments on destroy as a safety invariant so stale handles can never alias
// a future body if slot recycling is introduced later.
struct BodySlot final
{
    JPH::BodyID bodyId{};
    std::uint32_t generation = 0;
    bool active = false;
};

bool ValidateDynamicBoxBodyCreateInfo(const DynamicBoxBodyCreateInfo& info, std::string& message)
{
    const auto reject = [&message](const char* field) {
        message = std::string("invalid dynamic box body input: ") + field;
        return false;
    };

    for (const float extent : {info.halfExtents.x, info.halfExtents.y, info.halfExtents.z})
    {
        if (!std::isfinite(extent))
        {
            return reject("halfExtents must be finite");
        }
        if (extent <= 0.0F)
        {
            return reject("halfExtents must be positive on every axis");
        }
    }

    if (!std::isfinite(info.mass))
    {
        return reject("mass must be finite");
    }
    if (info.mass <= 0.0F)
    {
        return reject("mass must be greater than zero");
    }

    if (!info.position.IsFinite())
    {
        return reject("position must be finite");
    }

    if (!info.orientation.IsFinite() || info.orientation.LengthSquared() <= 0.0F)
    {
        return reject("orientation must be a finite quaternion with non-zero length");
    }

    for (const float damping : {info.linearDamping, info.angularDamping})
    {
        if (!std::isfinite(damping))
        {
            return reject("damping must be finite");
        }
        if (damping < 0.0F)
        {
            return reject("damping must not be negative");
        }
    }

    if (!info.initialLinearVelocity.IsFinite() || !info.initialAngularVelocity.IsFinite())
    {
        return reject("initial velocities must be finite");
    }

    // M2 Slice C2.1: a dynamic body needs at least one allowed DOF; locking all six is the static-body case.
    if (!info.degreesOfFreedom.IsAnyAllowed())
    {
        return reject("degrees of freedom: at least one DOF must be allowed (use a static body to lock all)");
    }

    // Locked-DOF input rule: initial velocity components on locked axes are rejected as recoverable input.
    // The Jolt backend would silently zero them through its own DOF locking, but accepting them would hide
    // caller mistakes; the rejection keeps the creation contract explicit (see PhysicsTypes.h).
    if (!info.degreesOfFreedom.AllowsLinearVelocity(info.initialLinearVelocity))
    {
        return reject("initial linear velocity has a component on a locked translation axis");
    }
    if (!info.degreesOfFreedom.AllowsAngularVelocity(info.initialAngularVelocity))
    {
        return reject("initial angular velocity has a component on a locked rotation axis");
    }

    message.clear();
    return true;
}

// Maps the DeepRun-owned DOF representation onto Jolt's EAllowedDOFs bitmask (Jolt v5.5.0, pinned).
// This translation lives only in the backend: no JPH type is exposed through Engine/Physics public headers.
// The result is applied at body creation time via BodyCreationSettings::mAllowedDOFs; Jolt then locks the
// inverse mass/inertia on locked axes and re-locks velocity and position steps every simulation step, so no
// per-tick clamping or constraint is needed in DeepRun code.
JPH::EAllowedDOFs ToJoltAllowedDOFs(const PhysicsDegreesOfFreedom& dof) noexcept
{
    JPH::EAllowedDOFs result = JPH::EAllowedDOFs::None;
    if (dof.translationX)
        result |= JPH::EAllowedDOFs::TranslationX;
    if (dof.translationY)
        result |= JPH::EAllowedDOFs::TranslationY;
    if (dof.translationZ)
        result |= JPH::EAllowedDOFs::TranslationZ;
    if (dof.rotationX)
        result |= JPH::EAllowedDOFs::RotationX;
    if (dof.rotationY)
        result |= JPH::EAllowedDOFs::RotationY;
    if (dof.rotationZ)
        result |= JPH::EAllowedDOFs::RotationZ;
    return result;
}
}

class PhysicsWorld::Impl final
{
public:
    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger), worldIdentity(NextWorldIdentity())
    {
    }

    ~Impl()
    {
        if (!initialized)
        {
            return;
        }

        physicsSystem.reset();
        jobSystem.reset();
        tempAllocator.reset();
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        initialized = false;
        logger.Info(
            Diagnostics::LogCategory::Physics,
            "Jolt shut down (" + std::to_string(bodyCount) + " dynamic bodies created this session)");
    }

    PhysicsBodyHandle CreateDynamicBoxBody(const DynamicBoxBodyCreateInfo& info, PhysicsError* error)
    {
        const auto fail = [this, error](const PhysicsErrorCode code, const std::string& message) -> PhysicsBodyHandle {
            if (error != nullptr)
            {
                *error = PhysicsError{code, message};
            }
            logger.Warning(Diagnostics::LogCategory::Physics, "Dynamic body creation rejected: " + message);
            return {};
        };

        if (!initialized)
        {
            return fail(PhysicsErrorCode::NotInitialized, "physics world is not initialized");
        }

        std::string validationMessage;
        if (!ValidateDynamicBoxBodyCreateInfo(info, validationMessage))
        {
            return fail(PhysicsErrorCode::InvalidInput, validationMessage);
        }

        // Normalize before handing the rotation to Jolt and verify the result.
        const float length = std::sqrt(info.orientation.LengthSquared());
        const PhysicsQuaternion normalized{
            .x = info.orientation.x / length,
            .y = info.orientation.y / length,
            .z = info.orientation.z / length,
            .w = info.orientation.w / length};

        JPH::BodyCreationSettings settings(
            new JPH::BoxShape(JPH::Vec3(info.halfExtents.x, info.halfExtents.y, info.halfExtents.z)),
            JPH::RVec3(info.position.x, info.position.y, info.position.z),
            JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w),
            JPH::EMotionType::Dynamic,
            ObjectLayers::Moving);
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = info.mass;
        settings.mLinearDamping = info.linearDamping;
        settings.mAngularDamping = info.angularDamping;
        settings.mGravityFactor = info.gravityEnabled ? 1.0F : 0.0F;
        settings.mLinearVelocity = JPH::Vec3(
            info.initialLinearVelocity.x,
            info.initialLinearVelocity.y,
            info.initialLinearVelocity.z);
        settings.mAngularVelocity = JPH::Vec3(
            info.initialAngularVelocity.x,
            info.initialAngularVelocity.y,
            info.initialAngularVelocity.z);

        // M2 Slice C2.1: apply the DeepRun-owned DOF contract at creation time in Jolt's mass/motion
        // configuration. The default (all six allowed) maps to EAllowedDOFs::All and is exactly the C1 path;
        // restricted bodies get their locked axes enforced by Jolt itself for every step, with no per-tick
        // clamping or constraint in DeepRun code.
        settings.mAllowedDOFs = ToJoltAllowedDOFs(info.degreesOfFreedom);

        const JPH::BodyID bodyId = physicsSystem->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (bodyId.IsInvalid())
        {
            // The shape is owned by `settings` and released with it on this path.
            return fail(PhysicsErrorCode::InvalidInput, "Jolt could not allocate a body");
        }

        bodies.emplace_back(BodySlot{});
        BodySlot& slot = bodies.back();
        slot.bodyId = bodyId;
        slot.active = true;
        ++bodyCount;
        logger.Info(Diagnostics::LogCategory::Physics, "Dynamic box body created (slot " + std::to_string(bodies.size() - 1) + ")");
        return PhysicsBodyHandle(worldIdentity, bodies.size() - 1, slot.generation);
    }

    bool DestroyBody(PhysicsBodyHandle handle, PhysicsError* error)
    {
        const auto fail = [this, error](const PhysicsErrorCode code, const std::string& message) -> bool {
            if (error != nullptr)
            {
                *error = PhysicsError{code, message};
            }
            logger.Warning(Diagnostics::LogCategory::Physics, "Body destruction rejected: " + message);
            return false;
        };

        BodySlot* slot = Resolve(handle);
        if (slot == nullptr)
        {
            return fail(PhysicsErrorCode::InvalidHandle, "handle is invalid, foreign, or stale");
        }

        JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(slot->bodyId);
        bodyInterface.DestroyBody(slot->bodyId);
        slot->active = false;
        ++slot->generation; // safety invariant: stale handles must never validate against this slot again
        logger.Info(Diagnostics::LogCategory::Physics, "Dynamic body destroyed (slot " + std::to_string(handle.Slot()) + ")");
        return true;
    }

    std::optional<PhysicsBodyState> GetBodyState(PhysicsBodyHandle handle) const
    {
        if (!initialized || !handle.IsValid() || handle.WorldIdentity() != worldIdentity)
        {
            return std::nullopt;
        }
        if (handle.Slot() >= bodies.size())
        {
            return std::nullopt;
        }

        const BodySlot& slot = bodies[handle.Slot()];
        if (!slot.active || handle.Generation() != slot.generation)
        {
            return std::nullopt;
        }

        JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
        const JPH::RVec3 position = bodyInterface.GetPosition(slot.bodyId);
        const JPH::Quat rotation = bodyInterface.GetRotation(slot.bodyId);
        const JPH::Vec3 linearVelocity = bodyInterface.GetLinearVelocity(slot.bodyId);
        const JPH::Vec3 angularVelocity = bodyInterface.GetAngularVelocity(slot.bodyId);

        return PhysicsBodyState{
            .position = {static_cast<float>(position.GetX()), static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())},
            .orientation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()},
            .linearVelocity = {linearVelocity.GetX(), linearVelocity.GetY(), linearVelocity.GetZ()},
            .angularVelocity = {angularVelocity.GetX(), angularVelocity.GetY(), angularVelocity.GetZ()},
            .active = bodyInterface.IsActive(slot.bodyId)};
    }

    bool AddForceAtWorldPosition(PhysicsBodyHandle handle, PhysicsVector3 forceNewtons, PhysicsVector3 worldPositionMeters, PhysicsError* error)
    {
        const auto fail = [this, error](const PhysicsErrorCode code, const std::string& message) -> bool {
            if (error != nullptr)
            {
                *error = PhysicsError{code, message};
            }
            logger.Warning(Diagnostics::LogCategory::Physics, "Force application rejected: " + message);
            return false;
        };

        if (!initialized)
        {
            return fail(PhysicsErrorCode::NotInitialized, "physics world is not initialized");
        }

        BodySlot* slot = Resolve(handle);
        if (slot == nullptr)
        {
            return fail(PhysicsErrorCode::InvalidHandle, "handle is invalid, foreign, or stale");
        }

        // Finite-only validation: a zero force is a legitimate no-op (simulation systems can naturally
        // compute zero), and finite magnitudes are never clamped — there is no arbitrary Newtons cap.
        if (!forceNewtons.IsFinite())
        {
            return fail(PhysicsErrorCode::InvalidInput, "force must be finite");
        }
        if (!worldPositionMeters.IsFinite())
        {
            return fail(PhysicsErrorCode::InvalidInput, "world position must be finite");
        }

        // True zero-force no-op: only after full validation (including the world position) does a zero force
        // return success without touching Jolt at all. The BodyInterface force path would otherwise activate a
        // sleeping body even for a zero force, which would break the documented no-op semantics.
        if (forceNewtons.x == 0.0F && forceNewtons.y == 0.0F && forceNewtons.z == 0.0F)
        {
            return true;
        }

        // Pinned Jolt v5.5.0: BodyInterface::AddForce(bodyID, force, RVec3Arg inPoint, EActivation) accumulates
        // the linear force plus torque (inPosition - centerOfMass) x force for the next PhysicsSystem::Update and
        // resets it after that step — exactly the transient per-step contract this API documents. The activation
        // policy is passed explicitly rather than relying on the default argument: a non-zero force wakes a
        // sleeping dynamic body, so no Engine-side sleep management is needed.
        physicsSystem->GetBodyInterface().AddForce(
            slot->bodyId,
            JPH::Vec3(forceNewtons.x, forceNewtons.y, forceNewtons.z),
            JPH::RVec3(worldPositionMeters.x, worldPositionMeters.y, worldPositionMeters.z),
            JPH::EActivation::Activate);
        return true;
    }

    Diagnostics::Logger& logger;
    BroadPhaseLayerInterface broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilter objectLayerPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
    bool initialized = false;

private:
    // Returns the live slot for a handle, or nullptr when the handle is invalid/foreign/stale.
    BodySlot* Resolve(PhysicsBodyHandle handle)
    {
        if (!initialized || !handle.IsValid() || handle.WorldIdentity() != worldIdentity)
        {
            return nullptr;
        }
        if (handle.Slot() >= bodies.size())
        {
            return nullptr;
        }

        BodySlot* slot = &bodies[handle.Slot()];
        if (!slot->active || handle.Generation() != slot->generation)
        {
            return nullptr;
        }
        return slot;
    }

    std::uint64_t worldIdentity;
    std::vector<BodySlot> bodies;
    std::size_t bodyCount = 0;
};

PhysicsWorld::PhysicsWorld(Diagnostics::Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::Initialize()
{
    if (impl_->initialized)
    {
        return true;
    }

    if (JPH::Factory::sInstance != nullptr)
    {
        impl_->logger.Error(Diagnostics::LogCategory::Physics, "Jolt is already initialized by another PhysicsWorld");
        return false;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    impl_->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const unsigned int workerCount = hardwareThreads > 1U ? hardwareThreads - 1U : 1U;
    impl_->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        static_cast<int>(workerCount));
    impl_->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    impl_->physicsSystem->Init(
        1'024,
        0,
        1'024,
        1'024,
        impl_->broadPhaseLayerInterface,
        impl_->objectVsBroadPhaseLayerFilter,
        impl_->objectLayerPairFilter);
    impl_->physicsSystem->SetGravity(JPH::Vec3(0.0F, -9.81F, 0.0F));
    impl_->initialized = true;
    impl_->logger.Info(Diagnostics::LogCategory::Physics, "Jolt initialized");
    return true;
}

void PhysicsWorld::Step(const float deltaSeconds)
{
    if (!impl_->initialized)
    {
        return;
    }
    impl_->physicsSystem->Update(deltaSeconds, 1, impl_->tempAllocator.get(), impl_->jobSystem.get());
}

bool PhysicsWorld::RunGravitySmokeTest()
{
    if (!impl_->initialized)
    {
        return false;
    }

    JPH::BodyInterface& joltBodies = impl_->physicsSystem->GetBodyInterface();
    const JPH::BodyID floor = joltBodies.CreateAndAddBody(
        JPH::BodyCreationSettings(
            new JPH::BoxShape(JPH::Vec3(10.0F, 0.5F, 10.0F)),
            JPH::RVec3(0.0F, -0.5F, 0.0F),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            ObjectLayers::NonMoving),
        JPH::EActivation::DontActivate);

    DynamicBoxBodyCreateInfo bodyInfo;
    bodyInfo.halfExtents = {0.5F, 0.5F, 0.5F};
    bodyInfo.mass = 1.0F;
    bodyInfo.position = {0.0F, 5.0F, 0.0F};
    PhysicsError error;
    const PhysicsBodyHandle dynamicBody = CreateDynamicBoxBody(bodyInfo, &error);

    if (floor.IsInvalid() || !dynamicBody.IsValid())
    {
        if (!dynamicBody.IsValid())
        {
            impl_->logger.Error(Diagnostics::LogCategory::Physics, "Jolt smoke body could not be created: " + error.message);
        }
        else
        {
            DestroyBody(dynamicBody);
        }
        if (!floor.IsInvalid())
        {
            joltBodies.RemoveBody(floor);
            joltBodies.DestroyBody(floor);
        }
        impl_->logger.Error(Diagnostics::LogCategory::Physics, "Jolt smoke bodies could not be created");
        return false;
    }

    impl_->physicsSystem->OptimizeBroadPhase();
    const auto initialState = GetBodyState(dynamicBody);
    for (int step = 0; step < 60; ++step)
    {
        Step(1.0F / 60.0F);
    }
    const auto finalState = GetBodyState(dynamicBody);

    DestroyBody(dynamicBody);
    joltBodies.RemoveBody(floor);
    joltBodies.DestroyBody(floor);

    if (!initialState || !finalState)
    {
        impl_->logger.Error(Diagnostics::LogCategory::Physics, "Jolt smoke body state became unavailable");
        return false;
    }

    const bool passed = finalState->position.y < initialState->position.y - 0.5F;
    impl_->logger.Log(
        Diagnostics::LogCategory::Physics,
        passed ? Diagnostics::LogLevel::Info : Diagnostics::LogLevel::Error,
        passed ? "Jolt gravity smoke simulation passed" : "Jolt gravity smoke simulation failed");
    return passed;
}

bool PhysicsWorld::IsInitialized() const noexcept
{
    return impl_->initialized;
}

std::optional<PhysicsVector3> PhysicsWorld::Gravity() const
{
    if (!impl_->initialized)
    {
        return std::nullopt;
    }

    const JPH::Vec3 gravity = impl_->physicsSystem->GetGravity();
    return PhysicsVector3{gravity.GetX(), gravity.GetY(), gravity.GetZ()};
}

PhysicsBodyHandle PhysicsWorld::CreateDynamicBoxBody(const DynamicBoxBodyCreateInfo& info, PhysicsError* error)
{
    assert(impl_ != nullptr);
    return impl_->CreateDynamicBoxBody(info, error);
}

bool PhysicsWorld::DestroyBody(PhysicsBodyHandle handle, PhysicsError* error)
{
    assert(impl_ != nullptr);
    return impl_->DestroyBody(handle, error);
}

std::optional<PhysicsBodyState> PhysicsWorld::GetBodyState(PhysicsBodyHandle handle) const
{
    assert(impl_ != nullptr);
    return impl_->GetBodyState(handle);
}

bool PhysicsWorld::AddForceAtWorldPosition(
    PhysicsBodyHandle handle,
    PhysicsVector3 forceNewtons,
    PhysicsVector3 worldPositionMeters,
    PhysicsError* error)
{
    assert(impl_ != nullptr);
    return impl_->AddForceAtWorldPosition(handle, forceNewtons, worldPositionMeters, error);
}
}
