from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        previousPlayerPositionMeters_ = playerSnapshot.emitter.positionMeters;\n        playerIntegrity_ = *integrity;''',
    '''        previousPlayerPositionMeters_ = proxy.positionMeters;\n        currentPlayerPhysicalProxy_ = proxy;\n        playerIntegrity_ = *integrity;''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''    [[nodiscard]] const SimpleDestroyerRuntimeState& Destroyer() const noexcept { return destroyer_; }''',
    '''    // Refreshes the read-only production physical snapshot for the upcoming fixed combat tick. The acoustic\n    // body-reference position must agree with the physical bridge, but the physical snapshot owns sweep geometry.\n    [[nodiscard]] std::expected<void, std::string> UpdatePlayerPhysicalProxy(\n        const Submarine::AnteyPhysicalCollisionProxySnapshot& proxy,\n        const Submarine::AnteyAcousticSnapshot& playerSnapshot)\n    {\n        if (!playerIntegrity_ || !mine_ || !previousPlayerPositionMeters_ || !playerBody_.IsValid() ||\n            proxy.body != playerBody_ || !proxy.positionMeters.IsFinite() || !proxy.orientation.IsFinite() ||\n            !proxy.halfExtentsMeters.IsFinite() ||\n            Distance(proxy.positionMeters, playerSnapshot.emitter.positionMeters) > 0.05 ||\n            Distance(proxy.halfExtentsMeters, playerCollisionHalfExtentsMeters_) > 1.0e-4 ||\n            physicsWorld_ == nullptr || !physicsWorld_->GetBodyState(proxy.body).has_value())\n        {\n            return std::unexpected("M5-I.2 player physical proxy update is invalid or disagrees with acoustic authority");\n        }\n        currentPlayerPhysicalProxy_ = proxy;\n        return {};\n    }\n\n    [[nodiscard]] const SimpleDestroyerRuntimeState& Destroyer() const noexcept { return destroyer_; }''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        if (mineDefinition_ && mine_ && playerIntegrity_ && previousPlayerPositionMeters_)\n        {\n            if (playerIntegrity_->body != playerBody_ || !playerCollisionHalfExtentsMeters_.IsFinite())\n            {\n                return std::unexpected("M5-I.2 bound player combat/physical identity is inconsistent");\n            }\n            const auto playerBodyState = physicsWorld_->GetBodyState(playerBody_);\n            if (!playerBodyState || !playerBodyState->orientation.IsFinite())\n            {\n                return std::unexpected("M5-I.2 live player body orientation is unavailable");\n            }\n            const Physics::PhysicsVector3 displacement = Difference(\n                playerSnapshot.emitter.positionMeters, *previousPlayerPositionMeters_);''',
    '''        if (mineDefinition_ && mine_ && playerIntegrity_ && previousPlayerPositionMeters_)\n        {\n            if (playerIntegrity_->body != playerBody_ || !playerCollisionHalfExtentsMeters_.IsFinite() ||\n                !currentPlayerPhysicalProxy_ || currentPlayerPhysicalProxy_->body != playerBody_)\n            {\n                return std::unexpected("M5-I.2 bound player combat/physical identity is inconsistent");\n            }\n            const Physics::PhysicsVector3 displacement = Difference(\n                currentPlayerPhysicalProxy_->positionMeters, *previousPlayerPositionMeters_);''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''                        .startPositionMeters = *previousPlayerPositionMeters_,\n                        .orientation = playerBodyState->orientation,\n                        .displacementMeters = displacement},''',
    '''                        .startPositionMeters = *previousPlayerPositionMeters_,\n                        .orientation = currentPlayerPhysicalProxy_->orientation,\n                        .displacementMeters = displacement},''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''            previousPlayerPositionMeters_ = playerSnapshot.emitter.positionMeters;\n        }''',
    '''            previousPlayerPositionMeters_ = currentPlayerPhysicalProxy_->positionMeters;\n        }''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''    std::optional<Physics::PhysicsVector3> previousPlayerPositionMeters_{};\n    std::optional<DeepRun::Combat::CombatIntegrityState> playerIntegrity_{};''',
    '''    std::optional<Physics::PhysicsVector3> previousPlayerPositionMeters_{};\n    std::optional<Submarine::AnteyPhysicalCollisionProxySnapshot> currentPlayerPhysicalProxy_{};\n    std::optional<DeepRun::Combat::CombatIntegrityState> playerIntegrity_{};''')

# Both windowed paths must refresh the physical snapshot before the runtime evaluates hazards.
replace_once(
    "Game/Combat/CombatPlaygroundWindowedComposition.h",
    '''        const auto frame = runtime_->Advance(playerSnapshot, simulationTimeSeconds);''',
    '''        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);\n        if (!synced)\n        {\n            return std::unexpected("M5-I.2 windowed player physical proxy update failed: " + synced.error());\n        }\n        const auto frame = runtime_->Advance(playerSnapshot, simulationTimeSeconds);''')
replace_once(
    "Game/Combat/CombatPlaygroundWindowedComposition.h",
    '''        const auto frame = runtime_->AdvancePlayerControlled(playerSnapshot, commands, simulationTimeSeconds);''',
    '''        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);\n        if (!synced)\n        {\n            return std::unexpected("M5-I.2 windowed player physical proxy update failed: " + synced.error());\n        }\n        const auto frame = runtime_->AdvancePlayerControlled(playerSnapshot, commands, simulationTimeSeconds);''')

# Focused live test updates the semantic physical bridge explicitly before the fixed combat tick.
replace_once(
    "Tests/M5NavalMineChecks.h",
    '''    const auto liveRuntimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 10.0);''',
    '''    auto liveRuntimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 10.0);''')
replace_once(
    "Tests/M5NavalMineChecks.h",
    '''    auto liveRuntime = *liveRuntimeResult;''',
    '''    auto liveRuntime = std::move(*liveRuntimeResult);''')
replace_once(
    "Tests/M5NavalMineChecks.h",
    '''    const auto liveFrame = movedAcoustic ? liveRuntime.Advance(*movedAcoustic, 11.0)\n                                         : std::expected<Game::Combat::CombatPlaygroundFrame, std::string>{\n                                               std::unexpected("fixture acoustic snapshot failed")};''',
    '''    const auto movedProxy = Game::Submarine::AnteyPhysicalCollisionProxySnapshot{\n        .body = livePlayerBody,\n        .positionMeters = liveEnd,\n        .orientation = {},\n        .halfExtentsMeters = liveHalfExtents};\n    const auto physicalUpdate = movedAcoustic\n        ? liveRuntime.UpdatePlayerPhysicalProxy(movedProxy, *movedAcoustic)\n        : std::expected<void, std::string>{std::unexpected("fixture acoustic snapshot failed")};\n    const auto liveFrame = physicalUpdate ? liveRuntime.Advance(*movedAcoustic, 11.0)\n                                          : std::expected<Game::Combat::CombatPlaygroundFrame, std::string>{\n                                                std::unexpected("fixture physical proxy update failed")};''')

print("M5-I.2 physical trajectory authority fix applied")
