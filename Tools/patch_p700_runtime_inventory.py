from pathlib import Path

runtime_path = Path("Game/Combat/CombatPlaygroundRuntime.h")
text = runtime_path.read_text(encoding="utf-8")

include_anchor = '#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"\n'
include_line = '#include "Game/Weapons/P700LauncherInventory.h"\n'
if include_line not in text:
    if include_anchor not in text:
        raise SystemExit("runtime include anchor missing")
    text = text.replace(include_anchor, include_anchor + include_line, 1)

create_end = '''            playerDecoyDefinition,
            simulationTimeSeconds);
    }

    // Accepted H/H.1 smoke and headless regression path.'''
overload = '''            playerDecoyDefinition,
            simulationTimeSeconds);
    }

    // Production/windowed overload. Existing headless M5 tests keep the three-argument factory and therefore
    // retain the accepted torpedo-only fixture. Normal play injects one validated 24-slot P-700 inventory;
    // this is carrier load state only and does not grant a target, launch solution or renderer authority.
    [[nodiscard]] static std::expected<CombatPlaygroundRuntime, std::string> Create(
        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds,
        Armament::P700LauncherInventory p700LauncherInventory)
    {
        if (p700LauncherInventory.Slots().size() != Armament::AnteyP700LauncherSlotCount ||
            p700LauncherInventory.LoadedCount() != Armament::AnteyP700LauncherSlotCount ||
            p700LauncherInventory.SpentCount() != 0U)
        {
            return std::unexpected("M5 P-700 production runtime requires a fresh 24-slot launcher inventory");
        }
        auto runtime = Create(physicsWorld, surfaceLevelY, simulationTimeSeconds);
        if (!runtime)
        {
            return runtime;
        }
        runtime->p700LauncherInventory_ = std::move(p700LauncherInventory);
        return std::move(*runtime);
    }

    // Accepted H/H.1 smoke and headless regression path.'''
if 'Armament::P700LauncherInventory p700LauncherInventory' not in text:
    if create_end not in text:
        raise SystemExit("runtime Create tail anchor missing")
    text = text.replace(create_end, overload, 1)

accessor_anchor = '''    [[nodiscard]] const std::optional<DeepRun::Combat::CombatExplosionEvent>& LastExplosion() const noexcept
    {
        return lastExplosion_;
    }
'''
accessor_replacement = accessor_anchor + '''    [[nodiscard]] const std::optional<Armament::P700LauncherInventory>& P700Launchers() const noexcept
    {
        return p700LauncherInventory_;
    }
'''
if 'P700Launchers() const noexcept' not in text:
    if accessor_anchor not in text:
        raise SystemExit("runtime accessor anchor missing")
    text = text.replace(accessor_anchor, accessor_replacement, 1)

member_anchor = '''    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
    PlayerCombatCommandRuntime playerCombat_;
'''
member_replacement = '''    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
    // Present only in the production/windowed composition until Weapon Selector materializes a P-700 launch.
    // Inventory is Game authority for 24 Loaded/Spent carrier slots; simulation missile state remains separate.
    std::optional<Armament::P700LauncherInventory> p700LauncherInventory_{};
    PlayerCombatCommandRuntime playerCombat_;
'''
if 'std::optional<Armament::P700LauncherInventory> p700LauncherInventory_' not in text:
    if member_anchor not in text:
        raise SystemExit("runtime member anchor missing")
    text = text.replace(member_anchor, member_replacement, 1)

runtime_path.write_text(text, encoding="utf-8")

window_path = Path("Game/Combat/CombatPlaygroundWindowedComposition.h")
window = window_path.read_text(encoding="utf-8")
old_create = '''        auto runtime = CombatPlaygroundRuntime::Create(
            physicsWorld, static_cast<float>(surfaceLevel), simulationTimeSeconds);
'''
new_create = '''        auto p700Inventory = p700CarrierLaunchContract_.CreateInventory();
        if (!p700Inventory)
        {
            return std::unexpected("M5 P-700 windowed launcher inventory creation failed: " + p700Inventory.error());
        }
        auto runtime = CombatPlaygroundRuntime::Create(
            physicsWorld,
            static_cast<float>(surfaceLevel),
            simulationTimeSeconds,
            std::move(*p700Inventory));
'''
if 'M5 P-700 windowed launcher inventory creation failed' not in window:
    if old_create not in window:
        raise SystemExit("windowed runtime Create anchor missing")
    window = window.replace(old_create, new_create, 1)
window_path.write_text(window, encoding="utf-8")
