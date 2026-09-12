from pathlib import Path

FILES = [
    Path("Game/Weapons/ProductionP700Asset.h"),
    Path("Game/Weapons/ProductionP700Asset.cpp"),
    Path("Game/Weapons/P700LauncherInventory.h"),
    Path("Game/Weapons/P700LaunchGeometry.h"),
    Path("Game/Weapons/P700CarrierLaunchContract.h"),
    Path("Tests/P700ProductionAssetTest.cpp"),
    Path("Tests/P700LauncherInventoryTest.cpp"),
    Path("Game/Combat/CombatPlaygroundWindowedComposition.h"),
]

for path in FILES:
    text = path.read_text(encoding="utf-8")
    text = text.replace("namespace DeepRun::Game::Weapons", "namespace DeepRun::Game::Armament")
    text = text.replace("DeepRun::Game::Weapons::", "DeepRun::Game::Armament::")
    text = text.replace("using namespace DeepRun::Game::Weapons;", "using namespace DeepRun::Game::Armament;")
    text = text.replace("Weapons::P700CarrierLaunchContract", "Armament::P700CarrierLaunchContract")
    text = text.replace("Weapons::P700LauncherInventory", "Armament::P700LauncherInventory")
    text = text.replace("} // namespace DeepRun::Game::Weapons", "} // namespace DeepRun::Game::Armament")
    path.write_text(text, encoding="utf-8")
