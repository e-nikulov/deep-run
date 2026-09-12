from pathlib import Path

path = Path("Tests/M5PlayerControlledCombatChecks.h")
text = path.read_text(encoding="utf-8")

anchor = '''    const auto playerSnapshot = *playerSnapshotResult;\n\n    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;\n'''
insert = '''    const auto playerSnapshot = *playerSnapshotResult;\n\n    // Weapon-employment gating consumes the same authoritative ownship physical snapshot as windowed play.\n    // Keep this direct-runtime test honest by binding a real PhysicsWorld body instead of bypassing the gate.\n    const Physics::PhysicsVector3 playerHalfExtentsMeters{.x = 75.0F, .y = 8.0F, .z = 8.0F};\n    const auto playerBody = physicsWorld.CreateDynamicBoxBody(Physics::DynamicBoxBodyCreateInfo{\n        .halfExtents = playerHalfExtentsMeters,\n        .mass = 12'000'000.0F,\n        .position = playerSnapshot.emitter.positionMeters,\n        .orientation = {},\n        .gravityEnabled = false,\n        .linearDamping = 0.0F,\n        .angularDamping = 0.0F,\n        .initialLinearVelocity = {},\n        .initialAngularVelocity = {}});\n    if (!playerBody.IsValid())\n    {\n        return false;\n    }\n    const auto boundPlayer = runtime.BindPlayerPhysicalProxy(\n        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{\n            .body = playerBody,\n            .positionMeters = playerSnapshot.emitter.positionMeters,\n            .orientation = {},\n            .halfExtentsMeters = playerHalfExtentsMeters},\n        playerSnapshot,\n        0.0);\n    if (!boundPlayer)\n    {\n        (void)physicsWorld.DestroyBody(playerBody);\n        return false;\n    }\n\n    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;\n'''
if text.count(anchor) != 1:
    raise RuntimeError(f"fixture anchor count={text.count(anchor)}")
text = text.replace(anchor, insert, 1)

old = '''    return physicsWorld.DestroyBody(runtime.Destroyer().body);\n'''
new = '''    const auto mineBody = runtime.Mine() ? runtime.Mine()->body : Physics::PhysicsBodyHandle{};\n    const bool destroyedMine = !mineBody.IsValid() || physicsWorld.DestroyBody(mineBody);\n    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);\n    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);\n    return destroyedMine && destroyedPlayer && destroyedDestroyer;\n'''
if text.count(old) != 1:
    raise RuntimeError(f"cleanup anchor count={text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
print("M5 player employment fixture repair: PASS")
