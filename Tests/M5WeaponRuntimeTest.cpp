#include "Simulation/Weapons/WeaponRuntime.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace
{
void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

DeepRun::Perception::Track MakeTrack()
{
    return DeepRun::Perception::Track{
        .trackId = 42U,
        .contactId = 7U,
        .lifecycle = DeepRun::Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = DeepRun::Physics::PhysicsVector3{.x = 1200.0F, .y = -140.0F, .z = 0.0F},
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.25F,
        .bearingUncertaintyRadians = 0.05F,
        .confidence = 0.90F,
        .observationCount = 4U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 8.0};
}
} // namespace

int main()
{
    using DeepRun::Perception::TrackLifecycleState;
    using DeepRun::Weapons::AdvanceWeaponReadiness;
    using DeepRun::Weapons::AssignWeaponTarget;
    using DeepRun::Weapons::CreateWeaponRuntime;
    using DeepRun::Weapons::LaunchWeapon;
    using DeepRun::Weapons::PrepareWeapon;
    using DeepRun::Weapons::WeaponDefinition;
    using DeepRun::Weapons::WeaponPhase;
    using DeepRun::Weapons::WeaponTargetingRequirements;

    const WeaponDefinition torpedo{
        .id = "m5.conventional-heavyweight",
        .preparationSeconds = 5.0,
        .targeting = WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};

    auto runtimeResult = CreateWeaponRuntime(torpedo, 10.0);
    Require(runtimeResult.has_value(), "valid weapon runtime must be created");
    auto runtime = *runtimeResult;
    Require(runtime.phase == WeaponPhase::Stored, "weapon must start Stored");
    Require(!runtime.targetTrackId.has_value(), "weapon must start without a target");

    Require(!LaunchWeapon(torpedo, runtime, 10.0).has_value(), "Stored weapon must not launch");
    Require(PrepareWeapon(torpedo, runtime, 10.0).has_value(), "Stored weapon must enter preparation");
    Require(runtime.phase == WeaponPhase::Preparing, "weapon must be Preparing before preparation time elapses");

    auto bearingOnlyTrack = MakeTrack();
    bearingOnlyTrack.estimatedPositionMeters = std::nullopt;
    Require(!AssignWeaponTarget(torpedo, runtime, bearingOnlyTrack).has_value(),
            "M4-style bearing-only track must not satisfy position-requiring torpedo targeting");
    Require(!runtime.targetTrackId.has_value(), "rejected track must not leak into weapon target state");

    auto weakTrack = MakeTrack();
    weakTrack.confidence = 0.50F;
    Require(!AssignWeaponTarget(torpedo, runtime, weakTrack).has_value(), "low-confidence track must be rejected");

    auto uncertainTrack = MakeTrack();
    uncertainTrack.bearingUncertaintyRadians = 0.20F;
    Require(!AssignWeaponTarget(torpedo, runtime, uncertainTrack).has_value(),
            "track above weapon bearing-uncertainty budget must be rejected");

    auto coastingTrack = MakeTrack();
    coastingTrack.lifecycle = TrackLifecycleState::Coasting;
    Require(!AssignWeaponTarget(torpedo, runtime, coastingTrack).has_value(),
            "coasting track must be rejected unless the definition explicitly allows it");

    const auto goodTrack = MakeTrack();
    Require(AssignWeaponTarget(torpedo, runtime, goodTrack).has_value(), "qualified perceived-world track must be accepted");
    Require(runtime.targetTrackId == goodTrack.trackId, "weapon runtime must retain only perceived-world track identity");

    Require(AdvanceWeaponReadiness(torpedo, runtime, 14.99).has_value(), "readiness must advance monotonically");
    Require(runtime.phase == WeaponPhase::Preparing, "weapon must remain Preparing before authored delay elapses");
    Require(!LaunchWeapon(torpedo, runtime, 14.99).has_value(), "Preparing weapon must not launch");

    Require(AdvanceWeaponReadiness(torpedo, runtime, 15.0).has_value(), "weapon must reach authored readiness time");
    Require(runtime.phase == WeaponPhase::Ready, "weapon must transition to Ready at authored preparation time");
    Require(!AdvanceWeaponReadiness(torpedo, runtime, 14.0).has_value(), "weapon runtime must reject SimulationTime reversal");

    Require(LaunchWeapon(torpedo, runtime, 15.0).has_value(), "Ready weapon with accepted track must launch");
    Require(runtime.phase == WeaponPhase::Launched, "launch must become authoritative weapon phase");
    Require(runtime.targetTrackId == goodTrack.trackId, "launch must preserve track identity without gaining ground truth");

    const WeaponDefinition badDefinition{.id = "", .preparationSeconds = -1.0};
    Require(!CreateWeaponRuntime(badDefinition, 0.0).has_value(), "invalid weapon definitions must fail deterministically");

    auto untargetedResult = CreateWeaponRuntime(torpedo, 20.0);
    Require(untargetedResult.has_value(), "second weapon runtime must be created");
    auto untargeted = *untargetedResult;
    Require(PrepareWeapon(torpedo, untargeted, 20.0).has_value(), "second weapon must prepare");
    Require(AdvanceWeaponReadiness(torpedo, untargeted, 25.0).has_value(), "second weapon must become Ready");
    Require(!LaunchWeapon(torpedo, untargeted, 25.0).has_value(), "Ready weapon without a perceived target must not launch");

    auto coastingDefinition = torpedo;
    coastingDefinition.id = "m5.coasting-test";
    coastingDefinition.targeting.allowCoastingTrack = true;
    auto coastingRuntimeResult = CreateWeaponRuntime(coastingDefinition, 30.0);
    Require(coastingRuntimeResult.has_value(), "coasting-capable definition must create runtime");
    auto coastingRuntime = *coastingRuntimeResult;
    Require(PrepareWeapon(coastingDefinition, coastingRuntime, 30.0).has_value(), "coasting-capable weapon must prepare");
    Require(AssignWeaponTarget(coastingDefinition, coastingRuntime, coastingTrack).has_value(),
            "coasting track must be accepted only when explicitly authored");

    std::cout << "M5 weapon runtime checks passed\n";
    return EXIT_SUCCESS;
}
