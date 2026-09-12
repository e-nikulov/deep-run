from pathlib import Path

ROOT = Path('.')


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected exactly one match, got {count}: {old[:100]!r}')
    write(path, text.replace(old, new, 1))


def append_once(path: str, marker: str, block: str) -> None:
    text = read(path)
    if marker in text:
        return
    write(path, text.rstrip() + '\n\n' + block.rstrip() + '\n')


# Canonical 2.5D Antey handling policy. Public displacement is kept separate from gameplay-only maneuver tuning.
write('Game/Submarine/AnteyHandlingModel.h', r'''#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Simulation/Marine/PropulsionComponent.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Project 949A public sources commonly publish about 24,000 t full/submerged displacement. Rubin's public
// Project 949A page identifies the design but does not publish a displacement table, while specialist public
// references disagree on whether 19,400 t or ~24,000 t is the appropriate submerged/full-load figure.
// Deep Run therefore uses 24,000 t as the canonical fully-submerged gameplay mass, not as a classified claim.
inline constexpr float AnteyCanonicalFullSubmergedMassKg = 24'000'000.0F;

// GAME POLICY. No reliable public Project 949A maximum-astern figure was found. Reverse drive is deliberately
// much weaker than ahead drive, while shaft reversal itself remains physical: an ahead-turning shaft must spin
// down through zero before it can build astern RPM.
inline constexpr Marine::PropulsionComponent AnteyGameplayPropulsion{
    .maxForwardRpm = 180.0F,
    .maxReverseRpm = 90.0F,
    .maxForwardThrustNewtons = 12'000'000.0F,
    .maxReverseThrustNewtons = 3'000'000.0F,
    .spinUpRateRpmPerSecond = 30.0F,
    .spinDownRateRpmPerSecond = 45.0F};

// GAME POLICY. A 154 m / ~24,000 t boat must not snap-flip in a side-view game. The production rigid body
// remains constrained to the XY gameplay plane; this state provides the missing longitudinal facing dimension.
inline constexpr float AnteyTurnAroundDurationSeconds = 60.0F;

struct AnteyFacingState final
{
    // +1: bow points toward world +X (screen right). -1: bow points toward world -X (screen left).
    int longitudinalSign = 1;
    bool turningAround = false;
    float turnProgress = 0.0F; // [0, 1] only while turningAround
};

struct AnteyFacingAdvance final
{
    AnteyFacingState nextState{};
    bool turnRequestAccepted = false;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateAnteyFacingState(const AnteyFacingState& state)
{
    if ((state.longitudinalSign != 1 && state.longitudinalSign != -1) ||
        !std::isfinite(state.turnProgress) || state.turnProgress < 0.0F || state.turnProgress > 1.0F ||
        (!state.turningAround && state.turnProgress != 0.0F))
    {
        return std::unexpected("Antey 2.5D facing state is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<AnteyFacingAdvance, std::string> AdvanceAnteyFacing(
    const AnteyFacingState& current,
    const bool requestTurnAround,
    const float fixedDeltaSeconds)
{
    const auto valid = ValidateAnteyFacingState(current);
    if (!valid || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected(valid ? "Antey facing delta must be finite and positive" : valid.error());
    }

    AnteyFacingAdvance result{.nextState = current};
    if (requestTurnAround && !result.nextState.turningAround)
    {
        result.nextState.turningAround = true;
        result.nextState.turnProgress = 0.0F;
        result.turnRequestAccepted = true;
    }

    if (result.nextState.turningAround)
    {
        result.nextState.turnProgress = (std::min)(
            1.0F,
            result.nextState.turnProgress + fixedDeltaSeconds / AnteyTurnAroundDurationSeconds);
        if (result.nextState.turnProgress >= 1.0F)
        {
            result.nextState.longitudinalSign = -result.nextState.longitudinalSign;
            result.nextState.turningAround = false;
            result.nextState.turnProgress = 0.0F;
        }
    }
    return result;
}

// 2.5D maneuver projection: visual yaw reaches 90 degrees halfway through the turn, where longitudinal thrust
// projects to zero. It then changes sign continuously. Existing inertia/drag keep acting, so a turn is never an
// instantaneous velocity reversal and astern shaft thrust remains available independently of facing.
[[nodiscard]] inline float AnteyLongitudinalForwardProjection(const AnteyFacingState& state) noexcept
{
    constexpr float Pi = 3.14159265358979323846F;
    if (!state.turningAround)
    {
        return static_cast<float>(state.longitudinalSign);
    }
    return static_cast<float>(state.longitudinalSign) *
        std::cos(Pi * std::clamp(state.turnProgress, 0.0F, 1.0F));
}

[[nodiscard]] inline float AnteyFacingPresentationYawRadians(const AnteyFacingState& state) noexcept
{
    constexpr float Pi = 3.14159265358979323846F;
    const float base = state.longitudinalSign > 0 ? 0.0F : Pi;
    return base + (state.turningAround ? Pi * std::clamp(state.turnProgress, 0.0F, 1.0F) : 0.0F);
}

[[nodiscard]] inline Assets::ModelTransform BuildAnteyFacingPresentationTransform(
    const AnteyFacingState& state) noexcept
{
    const float radians = AnteyFacingPresentationYawRadians(state);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Assets::ModelTransform transform{};
    // Column-major Y-axis rotation. Runtime Y is vertical, so this is a presentation-only turn through the
    // screen-depth dimension while the Jolt body stays in the 2.5D XY plane.
    transform.values[0] = cosine;
    transform.values[2] = -sine;
    transform.values[8] = sine;
    transform.values[10] = cosine;
    return transform;
}

static_assert(AnteyCanonicalFullSubmergedMassKg == 24'000'000.0F);
static_assert(AnteyGameplayPropulsion.maxReverseRpm < AnteyGameplayPropulsion.maxForwardRpm);
static_assert(AnteyGameplayPropulsion.maxReverseThrustNewtons < AnteyGameplayPropulsion.maxForwardThrustNewtons);
static_assert(AnteyTurnAroundDurationSeconds >= 30.0F);
} // namespace DeepRun::Game::Submarine
''')

# Input budget: turn-around on LS click / T; fire also gets a keyboard-only Enter binding.
replace_once('Engine/Input/InputState.h',
'''    ToggleDebugUi,\n    SelectContact,''',
'''    ToggleDebugUi,\n    TurnAround,\n    SelectContact,''')

replace_once('Engine/Platform/Window.h',
'''    F1,\n    Tab,\n    Space,''',
'''    F1,\n    Tab,\n    Enter,\n    Space,''')
replace_once('Engine/Platform/Window.h',
'''    R,\n    F,\n    Left,''',
'''    R,\n    F,\n    T,\n    Left,''')

replace_once('Engine/Platform/Windows/WinWindow.cpp',
'''    case VK_TAB: return Key::Tab;\n    case VK_SPACE: return Key::Space;''',
'''    case VK_TAB: return Key::Tab;\n    case VK_RETURN: return Key::Enter;\n    case VK_SPACE: return Key::Space;''')
replace_once('Engine/Platform/Windows/WinWindow.cpp',
'''    case 'R': return Key::R;\n    case 'F': return Key::F;''',
'''    case 'R': return Key::R;\n    case 'F': return Key::F;\n    case 'T': return Key::T;''')

replace_once('Engine/Input/InputSystem.h',
'''struct ControllerSemanticActions final\n{\n    bool selectContact = false;''',
'''struct ControllerSemanticActions final\n{\n    bool turnAround = false;\n    bool selectContact = false;''')
replace_once('Engine/Input/InputSystem.h',
'''    bool selectContactKeyDown_ = false;\n    bool prepareWeaponKeyDown_ = false;''',
'''    bool turnAroundKeyDown_ = false;\n    bool selectContactKeyDown_ = false;\n    bool prepareWeaponKeyDown_ = false;\n    bool fireWeaponKeyDown_ = false;''')

replace_once('Engine/Input/InputSystem.cpp',
'''    return ControllerSemanticActions{\n        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),''',
'''    return ControllerSemanticActions{\n        .turnAround = HasGamepadButton(gamepad, GamepadButton::LeftStick),\n        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),''')
replace_once('Engine/Input/InputSystem.cpp',
'''            else if (event.key == Platform::Key::Tab)\n            {\n                selectContactKeyDown_ = true;\n            }''',
'''            else if (event.key == Platform::Key::T)\n            {\n                turnAroundKeyDown_ = true;\n            }\n            else if (event.key == Platform::Key::Tab)\n            {\n                selectContactKeyDown_ = true;\n            }''')
replace_once('Engine/Input/InputSystem.cpp',
'''            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = true;\n            }''',
'''            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = true;\n            }\n            else if (event.key == Platform::Key::Enter)\n            {\n                fireWeaponKeyDown_ = true;\n            }''')
replace_once('Engine/Input/InputSystem.cpp',
'''            else if (event.key == Platform::Key::Tab)\n            {\n                selectContactKeyDown_ = false;\n            }''',
'''            else if (event.key == Platform::Key::T)\n            {\n                turnAroundKeyDown_ = false;\n            }\n            else if (event.key == Platform::Key::Tab)\n            {\n                selectContactKeyDown_ = false;\n            }''')
replace_once('Engine/Input/InputSystem.cpp',
'''            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = false;\n            }''',
'''            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = false;\n            }\n            else if (event.key == Platform::Key::Enter)\n            {\n                fireWeaponKeyDown_ = false;\n            }''')
replace_once('Engine/Input/InputSystem.cpp',
'''    const ControllerSemanticActions controller = SemanticActionsForGamepad(state_.Gamepad());\n    state_.SetActionDown(InputAction::SelectContact, selectContactKeyDown_ || controller.selectContact);''',
'''    const ControllerSemanticActions controller = SemanticActionsForGamepad(state_.Gamepad());\n    state_.SetActionDown(InputAction::TurnAround, turnAroundKeyDown_ || controller.turnAround);\n    state_.SetActionDown(InputAction::SelectContact, selectContactKeyDown_ || controller.selectContact);''')
replace_once('Engine/Input/InputSystem.cpp',
'''    state_.SetActionDown(\n        InputAction::FireWeapon,\n        state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||\n            controller.fireWeapon);''',
'''    state_.SetActionDown(\n        InputAction::FireWeapon,\n        fireWeaponKeyDown_ ||\n            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||\n            controller.fireWeapon);''')

# Fixed-tick vessel command carries only a semantic edge sequence, never a physical button.
replace_once('Game/Submarine/VesselCommandState.h',
'''#include <expected>\n#include <string>''',
'''#include <cstdint>\n#include <expected>\n#include <string>''')
replace_once('Game/Submarine/VesselCommandState.h',
'''    // [-1, +1]: surface / nose-up -> dive / nose-down.\n    float depthCommandFraction = 0.0F;''',
'''    // [-1, +1]: surface / nose-up -> dive / nose-down.\n    float depthCommandFraction = 0.0F;\n    // Monotonic semantic edge; PhysicalPlayground consumes each requested 180-degree 2.5D turn once.\n    std::uint64_t turnAroundPressSequence = 0;''')
replace_once('Game/Submarine/VesselCommandState.cpp',
'''    const VesselCommandState command{\n        .throttleFraction = input.Axis(Input::InputAxis::Throttle),\n        .depthCommandFraction = input.Axis(Input::InputAxis::Depth)};''',
'''    const VesselCommandState command{\n        .throttleFraction = input.Axis(Input::InputAxis::Throttle),\n        .depthCommandFraction = input.Axis(Input::InputAxis::Depth),\n        .turnAroundPressSequence = input.PressSequence(Input::InputAction::TurnAround)};''')

# Physical/combat read-only bridge carries the Game-facing orientation needed by weapon employment.
replace_once('Game/Submarine/AnteyPhysicalCollisionProxy.h',
'''    Physics::PhysicsQuaternion orientation{};\n    Physics::PhysicsVector3 halfExtentsMeters{};''',
'''    Physics::PhysicsQuaternion orientation{};\n    Physics::PhysicsVector3 halfExtentsMeters{};\n    // 2.5D longitudinal facing is Game authority, not an extra Jolt degree of freedom.\n    float gameplayLongitudinalFacingSign = 1.0F;\n    bool turningAround = false;''')

# Mass/propulsion/facing/propeller composition.
replace_once('Game/PhysicalPlayground.h',
'''#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"\n#include "Game/Submarine/VesselCommandState.h"''',
'''#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"\n#include "Game/Submarine/AnteyHandlingModel.h"\n#include "Game/Submarine/VesselCommandState.h"''')
replace_once('Game/PhysicalPlayground.h',
'''            .positionMeters = bodyState->position,\n            .orientation = bodyState->orientation,\n            .halfExtentsMeters = submarineCollisionHalfExtents_};''',
'''            .positionMeters = bodyState->position,\n            .orientation = bodyState->orientation,\n            .halfExtentsMeters = submarineCollisionHalfExtents_,\n            .gameplayLongitudinalFacingSign = static_cast<float>(facingState_.longitudinalSign),\n            .turningAround = facingState_.turningAround};''')
replace_once('Game/PhysicalPlayground.h',
'''        const Assets::ModelTransform modelToWorld = Render::Multiply(*bodyToWorld, modelToBody_);''',
'''        const Assets::ModelTransform facingPresentation =\n            Submarine::BuildAnteyFacingPresentationTransform(facingState_);\n        const Assets::ModelTransform modelToWorld =\n            Render::Multiply(Render::Multiply(*bodyToWorld, facingPresentation), modelToBody_);''')
replace_once('Game/PhysicalPlayground.h',
'''    std::array<float, 2> committedControlSurfaceDeflections_{};\n    float committedThrottleFraction_ = 0.0F;\n    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};''',
'''    std::array<float, 2> committedControlSurfaceDeflections_{};\n    float committedThrottleFraction_ = 0.0F;\n    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};\n    std::vector<std::size_t> propellerMeshNodeIndices_{};\n    Submarine::AnteyFacingState facingState_{};\n    std::uint64_t consumedTurnAroundPressSequence_ = 0;''')
replace_once('Game/PhysicalPlayground.h',
'''    // Presentation state derived only from authoritative shaft RPM. Production propeller hierarchy animation\n    // is intentionally deferred beyond IG1-B; this state remains M2 simulation-compatible but is not drawn.\n    float propellerPresentationAngleRadians_ = 0.0F;''',
'''    // Presentation state derived only from authoritative signed shaft RPM. Both production propeller bindings\n    // consume this angle, so astern shaft rotation visibly reverses without feeding presentation back to physics.\n    float propellerPresentationAngleRadians_ = 0.0F;''')

replace_once('Game/PhysicalPlayground.cpp',
'''// Temporary Game-owned Antey playground tuning (ADR-0008). This is not production mass authoring and is\n// not a claim about Project 949A hydrostatics. Production collision/buoyancy proxies provide spatial\n// authority only; effective neutral displacement remains mass / water density for this accepted M2/M3 scenario.\nconstexpr float M2GameAnteyMassTuningKg = 12'000'000.0F;''',
'''// Fully-submerged Project 949A gameplay mass. The public-source basis and displacement-definition caveat live\n// in AnteyHandlingModel.h; collision/buoyancy proxy geometry remains spatial authority only.\nconstexpr float M2GameAnteyMassTuningKg = Submarine::AnteyCanonicalFullSubmergedMassKg;''')
replace_once('Game/PhysicalPlayground.cpp',
'''// G2 Game-owned one-shaft prototype tuning. These are gameplay values, not measured or classified vessel\n// data, not mesh/collision-derived, and not universal submarine constants.\nconstexpr Marine::PropulsionComponent M2Propulsion{\n    .maxForwardRpm = 180.0F,\n    .maxReverseRpm = 120.0F,\n    .maxForwardThrustNewtons = 12'000'000.0F,\n    .maxReverseThrustNewtons = 4'800'000.0F,\n    .spinUpRateRpmPerSecond = 30.0F,\n    .spinDownRateRpmPerSecond = 45.0F};''',
'''// Aggregate synchronized twin-propeller gameplay drive. Ahead/astern asymmetry is explicit GAME POLICY; the\n// generic Marine propulsion system still owns signed shaft spin-down-through-zero and thrust calculation.\nconstexpr Marine::PropulsionComponent M2Propulsion = Submarine::AnteyGameplayPropulsion;''')
replace_once('Game/PhysicalPlayground.cpp',
'''Assets::ModelTransform DepthPlanePostTransform(const float committedDeflectionFraction) noexcept\n{''',
'''Assets::ModelTransform PropellerPostTransform(const float radians) noexcept\n{\n    const float cosine = std::cos(radians);\n    const float sine = std::sin(radians);\n    Assets::ModelTransform transform{};\n    // Production propeller semantic contract is local +X; origins are hub-centred.\n    transform.values[5] = cosine;\n    transform.values[6] = sine;\n    transform.values[9] = -sine;\n    transform.values[10] = cosine;\n    return transform;\n}\n\nAssets::ModelTransform DepthPlanePostTransform(const float committedDeflectionFraction) noexcept\n{''')

replace_once('Game/PhysicalPlayground.cpp',
'''    if (depthPlaneMeshNodeIndices[M2BowPlaneIndex].size() != 2U ||\n        depthPlaneMeshNodeIndices[M2SternPlaneIndex].size() != 2U)\n    {\n        return std::unexpected("physical playground requires two production bow and two stern depth-plane nodes");\n    }''',
'''    if (depthPlaneMeshNodeIndices[M2BowPlaneIndex].size() != 2U ||\n        depthPlaneMeshNodeIndices[M2SternPlaneIndex].size() != 2U)\n    {\n        return std::unexpected("physical playground requires two production bow and two stern depth-plane nodes");\n    }\n\n    std::vector<std::size_t> propellerMeshNodeIndices;\n    propellerMeshNodeIndices.reserve(productionDefinition->propellers.size());\n    for (const Submarine::ProductionPropellerAnchor& propeller : productionDefinition->propellers)\n    {\n        if (propeller.rotationAxis != "+X" ||\n            propeller.presentationNodeBindingIndex >= (*model)->nodeBindings.size())\n        {\n            return std::unexpected("physical playground production propeller semantic binding is invalid");\n        }\n        const auto meshNodeIndex = (*model)->nodeBindings[propeller.presentationNodeBindingIndex].meshNodeIndex;\n        if (!meshNodeIndex.has_value())\n        {\n            return std::unexpected("physical playground production propeller binding is not drawable");\n        }\n        propellerMeshNodeIndices.push_back(*meshNodeIndex);\n    }\n    if (propellerMeshNodeIndices.size() != 2U)\n    {\n        return std::unexpected("physical playground requires two production Antey propeller nodes");\n    }''')

replace_once('Game/PhysicalPlayground.cpp',
'''    depthPlaneMeshNodeIndices_ = std::move(depthPlaneMeshNodeIndices);\n    propellerPresentationAngleRadians_ = 0.0F;''',
'''    depthPlaneMeshNodeIndices_ = std::move(depthPlaneMeshNodeIndices);\n    propellerMeshNodeIndices_ = std::move(propellerMeshNodeIndices);\n    facingState_ = {};\n    consumedTurnAroundPressSequence_ = 0;\n    propellerPresentationAngleRadians_ = 0.0F;''')

replace_once('Game/PhysicalPlayground.cpp',
'''    if (buoyancy_.points.empty() || surfaceFloatBuoyancy_.points.size() != M3SurfaceFloatBuoyancyPointPositions.size())\n    {\n        return std::unexpected("physical playground buoyancy configuration is unavailable");\n    }''',
'''    if (buoyancy_.points.empty() || surfaceFloatBuoyancy_.points.size() != M3SurfaceFloatBuoyancyPointPositions.size())\n    {\n        return std::unexpected("physical playground buoyancy configuration is unavailable");\n    }\n\n    const bool turnAroundRequested = command.turnAroundPressSequence != consumedTurnAroundPressSequence_;\n    const auto facingAdvance = Submarine::AdvanceAnteyFacing(\n        facingState_, turnAroundRequested, fixedDeltaSeconds);\n    if (!facingAdvance)\n    {\n        return std::unexpected("physical playground 2.5D facing advance failed: " + facingAdvance.error());\n    }\n    const float facingProjection =\n        Submarine::AnteyLongitudinalForwardProjection(facingAdvance->nextState);''')

replace_once('Game/PhysicalPlayground.cpp',
'''    const auto propulsionForceWorld = RotateBodyLocalVectorToWorld(\n        state->orientation,\n        {propulsionResult->thrustNewtons, 0.0F, 0.0F});''',
'''    const auto propulsionForceWorld = RotateBodyLocalVectorToWorld(\n        state->orientation,\n        {propulsionResult->thrustNewtons * facingProjection, 0.0F, 0.0F});''')
replace_once('Game/PhysicalPlayground.cpp',
'''    const auto propulsorWorldPosition = TransformBodyLocalPointToWorld(\n        state->position, state->orientation, propulsorBodyLocalPosition_);''',
'''    Physics::PhysicsVector3 projectedPropulsorBodyLocalPosition = propulsorBodyLocalPosition_;\n    projectedPropulsorBodyLocalPosition.x *= facingProjection;\n    const auto propulsorWorldPosition = TransformBodyLocalPointToWorld(\n        state->position, state->orientation, projectedPropulsorBodyLocalPosition);''')
replace_once('Game/PhysicalPlayground.cpp',
'''    propulsionState_ = propulsionResult->nextState;\n    propellerPresentationAngleRadians_ = *nextPresentationAngle;\n    committedThrottleFraction_ = command.throttleFraction;''',
'''    propulsionState_ = propulsionResult->nextState;\n    propellerPresentationAngleRadians_ = *nextPresentationAngle;\n    facingState_ = facingAdvance->nextState;\n    consumedTurnAroundPressSequence_ = command.turnAroundPressSequence;\n    committedThrottleFraction_ = command.throttleFraction;''')

# Render transform and production propeller articulation.
replace_once('Game/PhysicalPlayground.cpp',
'''    const Assets::ModelTransform modelToWorld = Render::Multiply(*bodyToWorld, modelToBody_);''',
'''    const Assets::ModelTransform facingPresentation =\n        Submarine::BuildAnteyFacingPresentationTransform(facingState_);\n    const Assets::ModelTransform modelToWorld =\n        Render::Multiply(Render::Multiply(*bodyToWorld, facingPresentation), modelToBody_);''')
replace_once('Game/PhysicalPlayground.cpp',
'''    submarineNodeOverrides.reserve(\n        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +\n        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size());''',
'''    submarineNodeOverrides.reserve(\n        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +\n        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size() + propellerMeshNodeIndices_.size());''')
replace_once('Game/PhysicalPlayground.cpp',
'''    for (std::size_t group = 0; group < depthPlaneMeshNodeIndices_.size(); ++group)\n    {\n        const Assets::ModelTransform postTransform = DepthPlanePostTransform(committedControlSurfaceDeflections_[group]);\n        for (const std::size_t meshNodeIndex : depthPlaneMeshNodeIndices_[group])\n        {\n            submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = postTransform});\n        }\n    }\n    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submarineNodeOverrides);''',
'''    for (std::size_t group = 0; group < depthPlaneMeshNodeIndices_.size(); ++group)\n    {\n        const Assets::ModelTransform postTransform = DepthPlanePostTransform(committedControlSurfaceDeflections_[group]);\n        for (const std::size_t meshNodeIndex : depthPlaneMeshNodeIndices_[group])\n        {\n            submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = postTransform});\n        }\n    }\n    const Assets::ModelTransform propellerPostTransform =\n        PropellerPostTransform(propellerPresentationAngleRadians_);\n    for (const std::size_t meshNodeIndex : propellerMeshNodeIndices_)\n    {\n        submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = propellerPostTransform});\n    }\n    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submarineNodeOverrides);''')

# Combat launch heading/firing gate consumes the same 2.5D facing state; turning around blocks launch.
replace_once('Game/Combat/CombatPlaygroundRuntime.h',
'''            !proxy.halfExtentsMeters.IsFinite() || proxy.halfExtentsMeters.x <= 0.0F ||\n            proxy.halfExtentsMeters.y <= 0.0F || proxy.halfExtentsMeters.z <= 0.0F ||\n            !playerSnapshot.emitter.positionMeters.IsFinite() || !std::isfinite(simulationTimeSeconds) ||''',
'''            !proxy.halfExtentsMeters.IsFinite() || proxy.halfExtentsMeters.x <= 0.0F ||\n            proxy.halfExtentsMeters.y <= 0.0F || proxy.halfExtentsMeters.z <= 0.0F ||\n            (proxy.gameplayLongitudinalFacingSign != 1.0F && proxy.gameplayLongitudinalFacingSign != -1.0F) ||\n            !playerSnapshot.emitter.positionMeters.IsFinite() || !std::isfinite(simulationTimeSeconds) ||''')
replace_once('Game/Combat/CombatPlaygroundRuntime.h',
'''            proxy.body != playerBody_ || !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||\n            !proxy.halfExtentsMeters.IsFinite() ||\n            Distance(proxy.positionMeters, playerSnapshot.emitter.positionMeters) > 0.05 ||''',
'''            proxy.body != playerBody_ || !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||\n            !proxy.halfExtentsMeters.IsFinite() ||\n            (proxy.gameplayLongitudinalFacingSign != 1.0F && proxy.gameplayLongitudinalFacingSign != -1.0F) ||\n            Distance(proxy.positionMeters, playerSnapshot.emitter.positionMeters) > 0.05 ||''')
replace_once('Game/Combat/CombatPlaygroundRuntime.h',
'''        const auto selected = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());''',
'''        if (currentPlayerPhysicalProxy_->turningAround)\n        {\n            return Weapons::WeaponEmploymentAssessment{\n                .allowed = false,\n                .reason = "weapon launch is blocked while the 2.5D carrier is turning around"};\n        }\n\n        const auto selected = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());''')
replace_once('Game/Combat/CombatPlaygroundRuntime.h',
'''        const float launcherHeadingRadians = static_cast<float>(std::atan2(\n            2.0 * (static_cast<double>(orientation.w) * orientation.z +\n                   static_cast<double>(orientation.x) * orientation.y),\n            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +\n                         static_cast<double>(orientation.z) * orientation.z)));''',
'''        float launcherHeadingRadians = static_cast<float>(std::atan2(\n            2.0 * (static_cast<double>(orientation.w) * orientation.z +\n                   static_cast<double>(orientation.x) * orientation.y),\n            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +\n                         static_cast<double>(orientation.z) * orientation.z)));\n        if (currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign < 0.0F)\n        {\n            launcherHeadingRadians = Weapons::WrapEmploymentAngle(launcherHeadingRadians + 3.14159265358979323846F);\n        }''')
replace_once('Game/Combat/CombatPlaygroundRuntime.h',
'''        const float targetDeltaX = targetTrack.estimatedPositionMeters->x - playerSnapshot.emitter.positionMeters.x;\n        if (!std::isfinite(targetDeltaX) || std::abs(targetDeltaX) <= 1.0e-3F)\n        {\n            return std::unexpected("M5-H torpedo launch has no horizontal separation from its perceived track");\n        }\n        playerTorpedoForwardSign_ = targetDeltaX > 0.0F ? 1.0F : -1.0F;\n        const Physics::PhysicsVector3 launchPosition{\n            .x = playerSnapshot.emitter.positionMeters.x +\n                 playerTorpedoForwardSign_ * M5CombatTorpedoLaunchClearanceMeters,\n            .y = playerSnapshot.emitter.positionMeters.y,\n            .z = playerSnapshot.emitter.positionMeters.z};\n        const float launchHeading = playerTorpedoForwardSign_ > 0.0F ? 0.0F : 3.1415927F;''',
'''        if (!currentPlayerPhysicalProxy_.has_value() || currentPlayerPhysicalProxy_->turningAround)\n        {\n            return std::unexpected("M5-H torpedo launch requires a stable 2.5D ownship facing");\n        }\n        const auto& orientation = currentPlayerPhysicalProxy_->orientation;\n        float launchHeading = static_cast<float>(std::atan2(\n            2.0 * (static_cast<double>(orientation.w) * orientation.z +\n                   static_cast<double>(orientation.x) * orientation.y),\n            1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +\n                         static_cast<double>(orientation.z) * orientation.z)));\n        playerTorpedoForwardSign_ = currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign;\n        if (playerTorpedoForwardSign_ < 0.0F)\n        {\n            launchHeading = Weapons::WrapEmploymentAngle(launchHeading + 3.14159265358979323846F);\n        }\n        const Physics::PhysicsVector3 launchPosition{\n            .x = playerSnapshot.emitter.positionMeters.x +\n                 std::cos(launchHeading) * M5CombatTorpedoLaunchClearanceMeters,\n            .y = playerSnapshot.emitter.positionMeters.y +\n                 std::sin(launchHeading) * M5CombatTorpedoLaunchClearanceMeters,\n            .z = playerSnapshot.emitter.positionMeters.z};''')

# P-700 public envelope: surface launch is valid; underwater depth remains capped at 50 m.
replace_once('Simulation/Weapons/WeaponEmploymentEnvelope.h',
'''// P-700/Project 949A public sources consistently describe underwater launch near 30-50 m, a 50 m maximum\n// launch depth, carrier speed up to 5 kt and ~550 km maximum range. A 20 km minimum is reported by a secondary\n// technical compilation. The +/-90 degree horizontal target sector and shallow surface-target band are Game policy.''',
'''// P-700/Project 949A public descriptions allow launch from the surface as well as submerged launch, with\n// underwater depth commonly described around 30-50 m and a 50 m maximum. Deep Run therefore treats 0..50 m\n// as one carrier-depth envelope; sea state/hatch/water-exit constraints belong to the future IG2 launcher\n// lifecycle. A 20 km minimum is reported by a secondary technical compilation. The +/-90 degree horizontal\n// target sector and shallow surface-target band are Game policy.''')
replace_once('Simulation/Weapons/WeaponEmploymentEnvelope.h',
'''    .id = "P-700-Granit",\n    .minimumLaunchDepthMeters = 30.0F,\n    .maximumLaunchDepthMeters = 50.0F,''',
'''    .id = "P-700-Granit",\n    .minimumLaunchDepthMeters = 0.0F,\n    .maximumLaunchDepthMeters = 50.0F,''')
replace_once('Simulation/Weapons/WeaponEmploymentEnvelope.h',
'''static_assert(P700GranitEmploymentEnvelope.minimumTargetRangeMeters == 20'000.0F);''',
'''static_assert(P700GranitEmploymentEnvelope.minimumLaunchDepthMeters == 0.0F);\nstatic_assert(P700GranitEmploymentEnvelope.minimumTargetRangeMeters == 20'000.0F);''')

replace_once('Tests/M5WeaponEmploymentChecks.h',
'''    const auto granitNominal = at(P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 0.0F, 45.0F, 1.0F);\n    const auto granitTooShallow = at(P700GranitEmploymentEnvelope, 29.9F, 120'000.0F, 0.0F, 0.0F, 0.0F);''',
'''    const auto granitNominal = at(P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 0.0F, 45.0F, 1.0F);\n    const auto granitSurfaceLaunch = at(P700GranitEmploymentEnvelope, 0.0F, 120'000.0F, 0.0F, 0.0F, 0.0F);''')
replace_once('Tests/M5WeaponEmploymentChecks.h',
'''    return granitNominal.allowed && !granitTooShallow.allowed && !granitTooDeep.allowed && !granitTooClose.allowed &&\n           !granitTooFar.allowed && !granitWrongSector.allowed && !granitTooFast.allowed && !granitSubmergedTarget.allowed;''',
'''    return granitNominal.allowed && granitSurfaceLaunch.allowed && !granitTooDeep.allowed && !granitTooClose.allowed &&\n           !granitTooFar.allowed && !granitWrongSector.allowed && !granitTooFast.allowed && !granitSubmergedTarget.allowed;''')

# Extend controller/keyboard regression and verify the handling state machine + shaft reversal contract.
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''#include "Engine/Input/InputSystem.h"''',
'''#include "Engine/Input/InputSystem.h"\n#include "Game/Submarine/AnteyHandlingModel.h"\n#include "Simulation/Marine/PropulsionSystem.h"''')
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''        static_cast<std::uint16_t>(GamepadButton::Y) |\n        static_cast<std::uint16_t>(GamepadButton::X) |''',
'''        static_cast<std::uint16_t>(GamepadButton::LeftStick) |\n        static_cast<std::uint16_t>(GamepadButton::Y) |\n        static_cast<std::uint16_t>(GamepadButton::X) |''')
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    if (disconnected.selectContact || disconnected.prepareWeapon || disconnected.fireWeapon ||\n        disconnected.activeSonarPing || disconnected.deployDecoy)''',
'''    if (disconnected.turnAround || disconnected.selectContact || disconnected.prepareWeapon ||\n        disconnected.fireWeapon || disconnected.activeSonarPing || disconnected.deployDecoy)''')
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    if (!controller.selectContact || !controller.prepareWeapon || !controller.fireWeapon ||\n        !controller.activeSonarPing || !controller.deployDecoy)''',
'''    if (!controller.turnAround || !controller.selectContact || !controller.prepareWeapon ||\n        !controller.fireWeapon || !controller.activeSonarPing || !controller.deployDecoy)''')
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    if (legacyFaceButtons.prepareWeapon || legacyFaceButtons.fireWeapon ||\n        legacyFaceButtons.activeSonarPing || legacyFaceButtons.deployDecoy)''',
'''    if (legacyFaceButtons.turnAround || legacyFaceButtons.prepareWeapon || legacyFaceButtons.fireWeapon ||\n        legacyFaceButtons.activeSonarPing || legacyFaceButtons.deployDecoy)''')
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    if (triggerActions.selectContact || triggerActions.prepareWeapon || !triggerActions.fireWeapon ||''',
'''    if (triggerActions.turnAround || triggerActions.selectContact || triggerActions.prepareWeapon || !triggerActions.fireWeapon ||''')

# Insert keyboard turn/fire checks before select-contact checks.
replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    input.BeginFrame();\n    const std::array selectDown{''',
'''    input.BeginFrame();\n    const std::array turnDown{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::T}};\n    input.ProcessEvents(turnDown);\n    if (!input.State().WasPressed(InputAction::TurnAround) ||\n        input.State().PressSequence(InputAction::TurnAround) == 0U)\n    {\n        return false;\n    }\n    const std::array turnUp{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::T}};\n    input.ProcessEvents(turnUp);\n\n    input.BeginFrame();\n    const std::array keyboardFireDown{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Enter}};\n    input.ProcessEvents(keyboardFireDown);\n    if (!input.State().WasPressed(InputAction::FireWeapon) ||\n        input.State().PressSequence(InputAction::FireWeapon) == 0U)\n    {\n        return false;\n    }\n    const std::array keyboardFireUp{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Enter}};\n    input.ProcessEvents(keyboardFireUp);\n\n    input.BeginFrame();\n    const std::array selectDown{''')

replace_once('Tests/M5PlayerCombatInputChecks.h',
'''    return true;\n}\n} // namespace DeepRun::Tests''',
'''    // Canonical Antey handling contract: 24,000 t submerged gameplay mass, weaker astern drive, physical\n    // shaft braking through zero, and a non-instantaneous 180-degree 2.5D facing transition.\n    using namespace Game::Submarine;\n    if (AnteyCanonicalFullSubmergedMassKg != 24'000'000.0F ||\n        AnteyGameplayPropulsion.maxReverseRpm >= AnteyGameplayPropulsion.maxForwardRpm ||\n        AnteyGameplayPropulsion.maxReverseThrustNewtons >= AnteyGameplayPropulsion.maxForwardThrustNewtons)\n    {\n        return false;\n    }\n    const auto braking = Marine::PropulsionSystem::Advance(\n        AnteyGameplayPropulsion,\n        Marine::PropulsionState{.shaftRpm = 120.0F},\n        Marine::PropulsionCommand{.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F},\n        1.0F);\n    if (!braking || braking->nextState.shaftRpm <= 0.0F || braking->nextState.shaftRpm >= 120.0F ||\n        braking->thrustNewtons <= 0.0F)\n    {\n        return false;\n    }\n\n    const auto halfTurn = AdvanceAnteyFacing({}, true, AnteyTurnAroundDurationSeconds * 0.5F);\n    if (!halfTurn || !halfTurn->turnRequestAccepted || !halfTurn->nextState.turningAround ||\n        std::abs(halfTurn->nextState.turnProgress - 0.5F) > 1.0e-4F ||\n        std::abs(AnteyLongitudinalForwardProjection(halfTurn->nextState)) > 1.0e-3F)\n    {\n        return false;\n    }\n    const auto completedTurn = AdvanceAnteyFacing(\n        halfTurn->nextState, false, AnteyTurnAroundDurationSeconds * 0.5F);\n    if (!completedTurn || completedTurn->nextState.turningAround ||\n        completedTurn->nextState.longitudinalSign != -1 ||\n        AnteyLongitudinalForwardProjection(completedTurn->nextState) != -1.0F)\n    {\n        return false;\n    }\n    const auto facingTransform = BuildAnteyFacingPresentationTransform(completedTurn->nextState);\n    if (std::abs(facingTransform.values[0] + 1.0F) > 1.0e-4F ||\n        std::abs(facingTransform.values[10] + 1.0F) > 1.0e-4F)\n    {\n        return false;\n    }\n\n    return true;\n}\n} // namespace DeepRun::Tests''')

# Weapon docs surface/depth correction.
replace_once('docs/design/weapon-employment-envelopes.md',
'''| Minimum launch depth | 30 m | OPEN-SOURCE/CONSERVATIVE: public secondary descriptions commonly state underwater launch around 30-50 m. |\n| Maximum launch depth | 50 m | OPEN-SOURCE Project 949/949A figure. |''',
'''| Minimum launch depth | 0 m (surface) | OPEN-SOURCE/CONSERVATIVE: public descriptions allow surface launch as well as submerged launch. |\n| Maximum launch depth | 50 m | OPEN-SOURCE Project 949/949A figure; secondary descriptions commonly place submerged launch in the 30-50 m band. |''')
replace_once('docs/design/weapon-employment-envelopes.md',
'''The authored SM-225/P-700 launcher geometry remains content authority. The employment sector is only a target-eligibility rule and must never rotate the launcher or rewrite asset geometry.''',
'''Deep Run intentionally treats carrier depth `0..50 m` as one eligibility envelope. The frequently quoted `30..50 m` figure describes submerged launch conditions; it is not used as a fake prohibition on a surfaced carrier. Future IG2 sea-state, hatch, gas-generator and water-exit phases may impose additional stateful constraints without rewriting this depth contract.\n\nThe authored SM-225/P-700 launcher geometry remains content authority. The employment sector is only a target-eligibility rule and must never rotate the launcher or rewrite asset geometry.''')

# Control budget documentation: current mechanics are controller-complete and keyboard-complete.
append_once('docs/design/controls.md', '## Current production control-budget audit (2026-09-12)', r'''## Current production control-budget audit (2026-09-12)

The current playable mechanics have a direct Xbox-controller binding and a keyboard binding; mouse remains an optional duplicate, never the only way to perform a gameplay action.

| Current mechanic | Xbox controller | Keyboard | Notes |
| --- | --- | --- | --- |
| Ahead / astern drive | Left stick X | `D` / `A` | Signed shaft command. Astern first brakes an ahead-turning shaft through zero. |
| Dive / surface planes | Left stick Y | `S` / `W` | Direct normalized depth-plane command. |
| Turn boat 180 degrees in 2.5D | Left-stick click | `T` | Starts one slow 60 s GAME-POLICY presentation/longitudinal-facing turn; repeated input while turning is ignored. |
| Camera pan | Right stick X | Left / Right arrows | Presentation-only. |
| Camera zoom | Right stick Y | `Q` / `E` | Continuous tactical scale. |
| Select next contact | `Y` | `Tab` | Perceived-track selection. |
| Prepare weapon | `LT` | `R` | Right mouse remains optional duplicate. |
| Fire weapon | `RT` | `Enter` | Left mouse remains optional duplicate. |
| Active-sonar ping | `RB` | `Space` | Selected perceived contact bearing. |
| Deploy acoustic decoy | `X` | `F` | One-shot defensive action in the current playground. |
| Debug UI | n/a | `F1` | Development-only. |

Reserved capacity is still sufficient for planned gameplay: `A/B` remain interact/cancel, `LB` remains silent-running, D-pad remains four quick-system slots, View remains tactical/map mode, Menu remains pause, and both right-stick click plus contextual combinations remain unused. P-700 selection/launcher lifecycle must consume this semantic budget deliberately rather than adding device-specific shortcuts.
''')

# Separate handling contract records source status and avoids presenting game policy as historical TTX.
write('docs/design/antey-handling.md', r'''# Project 949A / Antey handling contract

Status: production gameplay contract for the canonical Deep Run Project 949A-inspired player submarine.

## Public-source boundary

The official Rubin public Project 949A page identifies the design and its mission/architecture but does not publish a public displacement table. Open specialist/educational references commonly quote approximately `14,700 t` surfaced and `24,000 t` submerged/full displacement; some specialist compilations instead show `19,400 t (24,000?)`, so the exact public displacement definition is not perfectly consistent.

Deep Run uses **24,000,000 kg** as the canonical fully-submerged gameplay rigid-body mass. This is an explicit open-source convention for the submerged game state, not a claim of access to classified hydrostatic documentation. The buoyancy system derives neutral displaced volume from this mass and authoritative seawater density, so changing the mass cannot silently leave a 12,000 t neutral-buoyancy model behind.

References:
- Rubin Design Bureau, Project 949A public project page: <https://ckb-rubin.ru/proekty/voennoe_korablestroenie/podvodnye_lodki/proekt_949a/>
- Deepstorm Project 949A public compilation (Apalkov-sourced characteristics): <https://www.deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm>

## Ahead / braking / astern

`Throttle` is a signed command. Positive drive spins the aggregate synchronized twin-propeller shaft state ahead; negative drive commands astern. If the shaft is still rotating ahead when astern is requested, the generic propulsion simulation first reduces RPM toward exactly zero and only subsequent fixed ticks build reverse RPM. The same mechanism makes reverse thrust a physical braking command while the boat still has forward inertia.

No reliable public Project 949A maximum-astern speed figure was found. Therefore Deep Run's asymmetry is explicitly **GAME POLICY**: `180 rpm / 12 MN` ahead versus `90 rpm / 3 MN` astern in the current coarse propulsion model. The purpose is to make sustained astern motion materially slower than ahead motion, not to publish a historical performance number.

Both production propeller nodes are hub-centred, local `+X` articulated assets. Their visible angle is derived from signed authoritative shaft RPM, so they spin in the opposite direction under astern command. Presentation never drives thrust.

## 2.5D turn-around

The Jolt body remains a 2.5D XY body: X/Y translation and Z pitch are physical; screen-depth translation/yaw is not promoted to a new simulation DOF. A separate Game-owned `longitudinalSign` tells the rest of gameplay whether the bow points screen-right (`+1`) or screen-left (`-1`).

Pressing `TurnAround` starts a **60 s GAME-POLICY** 180-degree visual turn. During the turn the production model yaws smoothly through the screen-depth axis. Longitudinal thrust is projected by `cos(pi * progress)`: it fades to zero at the visual 90-degree midpoint and grows with the opposite sign during the second half. Existing inertia and hydrodynamic drag remain authoritative, so neither heading nor velocity can snap-reverse.

Astern remains independent of turn-around: negative shaft thrust always acts backward relative to the current/transitioning longitudinal facing. Weapon employment is blocked while the carrier is mid-turn; once stable, launch-sector heading follows the final facing sign. This prevents a target on the old side from being fired upon as though the boat had already completed its turn.

The turn duration, reverse thrust/RPM and 2.5D projection are gameplay abstractions, not measured Project 949A turning-circle data.
''')

print('Antey handling / input / Granit contract patch: PASS')
