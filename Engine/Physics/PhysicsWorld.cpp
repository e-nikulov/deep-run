#include "Engine/Physics/PhysicsWorld.h"

#include "Engine/Diagnostics/Logger.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <thread>

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
}

class PhysicsWorld::Impl final
{
public:
    explicit Impl(Diagnostics::Logger& logger)
        : logger(logger)
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
        logger.Info(Diagnostics::LogCategory::Physics, "Jolt shut down");
    }

    Diagnostics::Logger& logger;
    BroadPhaseLayerInterface broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilter objectLayerPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
    bool initialized = false;
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

    JPH::BodyInterface& bodies = impl_->physicsSystem->GetBodyInterface();
    const JPH::BodyID floor = bodies.CreateAndAddBody(
        JPH::BodyCreationSettings(
            new JPH::BoxShape(JPH::Vec3(10.0F, 0.5F, 10.0F)),
            JPH::RVec3(0.0F, -0.5F, 0.0F),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            ObjectLayers::NonMoving),
        JPH::EActivation::DontActivate);
    const JPH::BodyID dynamicBody = bodies.CreateAndAddBody(
        JPH::BodyCreationSettings(
            new JPH::BoxShape(JPH::Vec3(0.5F, 0.5F, 0.5F)),
            JPH::RVec3(0.0F, 5.0F, 0.0F),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic,
            ObjectLayers::Moving),
        JPH::EActivation::Activate);

    if (floor.IsInvalid() || dynamicBody.IsInvalid())
    {
        if (!dynamicBody.IsInvalid())
        {
            bodies.RemoveBody(dynamicBody);
            bodies.DestroyBody(dynamicBody);
        }
        if (!floor.IsInvalid())
        {
            bodies.RemoveBody(floor);
            bodies.DestroyBody(floor);
        }
        impl_->logger.Error(Diagnostics::LogCategory::Physics, "Jolt smoke bodies could not be created");
        return false;
    }

    impl_->physicsSystem->OptimizeBroadPhase();
    const float initialHeight = static_cast<float>(bodies.GetPosition(dynamicBody).GetY());
    for (int step = 0; step < 60; ++step)
    {
        Step(1.0F / 60.0F);
    }
    const float finalHeight = static_cast<float>(bodies.GetPosition(dynamicBody).GetY());

    bodies.RemoveBody(dynamicBody);
    bodies.DestroyBody(dynamicBody);
    bodies.RemoveBody(floor);
    bodies.DestroyBody(floor);

    const bool passed = finalHeight < initialHeight - 0.5F;
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
}
