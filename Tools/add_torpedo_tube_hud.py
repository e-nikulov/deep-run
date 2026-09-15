from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "#include <cstddef>\n",
    "#include <array>\n#include <cstddef>\n",
)

replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "// Read-only state intended for controller-first combat UI. It deliberately exposes perceived quality and\n"
    "// perceived visual classification rather than hostile Transform/entity truth. A weapon-quality acoustic track\n"
    "// may remain visually unconfirmed; that is an intentional civilian-identification risk, not a missing field.\n"
    "struct PlayerCombatPresentationSnapshot final\n"
    "{\n",
    "// Read-only state intended for controller-first combat UI. It deliberately exposes perceived quality and\n"
    "// perceived visual classification rather than hostile Transform/entity truth. A weapon-quality acoustic track\n"
    "// may remain visually unconfirmed; that is an intentional civilian-identification risk, not a missing field.\n"
    "struct TorpedoTubePresentationSnapshot final\n"
    "{\n"
    "    std::size_t tubeNumber = 0U;\n"
    "    std::uint16_t calibreMillimeters = 533U;\n"
    "    bool ready = true;\n"
    "    double reloadSecondsRemaining = 0.0;\n"
    "};\n\n"
    "struct PlayerCombatPresentationSnapshot final\n"
    "{\n",
)

replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "    std::size_t torpedoRoundsRemaining = 0U;\n"
    "    std::size_t torpedoReadyTubeCount = 0U;\n"
    "    std::size_t torpedoTubeCount = 0U;\n"
    "    std::optional<double> torpedoNextTubeReadySeconds{};\n",
    "    std::size_t torpedoRoundsRemaining = 0U;\n"
    "    std::size_t torpedo533RoundsRemaining = 0U;\n"
    "    std::size_t torpedo650RoundsRemaining = 0U;\n"
    "    std::size_t torpedoReadyTubeCount = 0U;\n"
    "    std::size_t torpedoTubeCount = 0U;\n"
    "    std::optional<double> torpedoNextTubeReadySeconds{};\n"
    "    std::array<TorpedoTubePresentationSnapshot, 6U> torpedoTubes{};\n",
)

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    "        playerCombatPresentation.torpedoRoundsRemaining = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoInventory_.LoadedCount(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedoReadyTubeCount = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoTubeBank_.ReadyTubeCount(selectedPlayerWeapon_, simulationTimeSeconds);\n"
    "        const auto selectedTubeCalibre = Armament::AnteyTorpedoTubeCalibreForWeapon(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedoTubeCount = !selectedTubeCalibre ? 0U :\n"
    "            (*selectedTubeCalibre == Armament::AnteyTorpedoTubeCalibre::Mm533\n"
    "                ? Armament::Antey533MmTorpedoTubeCount : Armament::Antey650MmTorpedoTubeCount);\n",
    "        playerCombatPresentation.torpedoRoundsRemaining = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoInventory_.LoadedCount(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedo533RoundsRemaining =\n"
    "            playerTorpedoInventory_.LoadedCount(Armament::PlayerWeaponType::HeavyweightTorpedo);\n"
    "        playerCombatPresentation.torpedo650RoundsRemaining =\n"
    "            playerTorpedoInventory_.LoadedCount(Armament::PlayerWeaponType::Type6576AFast);\n"
    "        playerCombatPresentation.torpedoReadyTubeCount = selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit\n"
    "            ? 0U : playerTorpedoTubeBank_.ReadyTubeCount(selectedPlayerWeapon_, simulationTimeSeconds);\n"
    "        const auto selectedTubeCalibre = Armament::AnteyTorpedoTubeCalibreForWeapon(selectedPlayerWeapon_);\n"
    "        playerCombatPresentation.torpedoTubeCount = !selectedTubeCalibre ? 0U :\n"
    "            (*selectedTubeCalibre == Armament::AnteyTorpedoTubeCalibre::Mm533\n"
    "                ? Armament::Antey533MmTorpedoTubeCount : Armament::Antey650MmTorpedoTubeCount);\n"
    "        const auto& torpedoTubes = playerTorpedoTubeBank_.Tubes();\n"
    "        static_assert(std::tuple_size_v<decltype(torpedoTubes)> == 6U);\n"
    "        for (std::size_t index = 0U; index < torpedoTubes.size(); ++index)\n"
    "        {\n"
    "            const auto& tube = torpedoTubes[index];\n"
    "            const double reloadSecondsRemaining =\n"
    "                std::max(0.0, tube.nextReadyTimeSeconds - simulationTimeSeconds);\n"
    "            playerCombatPresentation.torpedoTubes[index] = TorpedoTubePresentationSnapshot{\n"
    "                .tubeNumber = index + 1U,\n"
    "                .calibreMillimeters = tube.calibre == Armament::AnteyTorpedoTubeCalibre::Mm533 ? 533U : 650U,\n"
    "                .ready = reloadSecondsRemaining <= 1.0e-9,\n"
    "                .reloadSecondsRemaining = reloadSecondsRemaining};\n"
    "        }\n",
)

replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    "    if (snapshot.selectedWeapon != Armament::PlayerWeaponType::P700Granit)\n"
    "    {\n"
    "        ImGui::Text(\"Torpedo ammo: %zu | ready tubes: %zu / %zu\",\n"
    "                    snapshot.torpedoRoundsRemaining, snapshot.torpedoReadyTubeCount, snapshot.torpedoTubeCount);\n"
    "        if (snapshot.torpedoNextTubeReadySeconds)\n"
    "            ImGui::Text(\"Next compatible tube reload: %.1f s\", *snapshot.torpedoNextTubeReadySeconds);\n"
    "    }\n",
    "    ImGui::TextUnformatted(\"Torpedo tubes:\");\n"
    "    const auto drawTorpedoTubeCalibre = [&](const std::uint16_t calibreMillimeters, const std::size_t roundsRemaining)\n"
    "    {\n"
    "        ImGui::Text(\"%u mm / ammo %zu:\", static_cast<unsigned int>(calibreMillimeters), roundsRemaining);\n"
    "        for (const auto& tube : snapshot.torpedoTubes)\n"
    "        {\n"
    "            if (tube.calibreMillimeters != calibreMillimeters)\n"
    "                continue;\n"
    "            ImGui::SameLine(0.0F, 8.0F);\n"
    "            if (tube.ready)\n"
    "                ImGui::Text(\"#%zu READY\", tube.tubeNumber);\n"
    "            else\n"
    "                ImGui::Text(\"#%zu RLD %.0fs\", tube.tubeNumber, tube.reloadSecondsRemaining);\n"
    "        }\n"
    "    };\n"
    "    drawTorpedoTubeCalibre(533U, snapshot.torpedo533RoundsRemaining);\n"
    "    drawTorpedoTubeCalibre(650U, snapshot.torpedo650RoundsRemaining);\n"
    "    if (snapshot.selectedWeapon != Armament::PlayerWeaponType::P700Granit)\n"
    "    {\n"
    "        ImGui::Text(\"Selected torpedo pool: %zu | ready tubes: %zu / %zu\",\n"
    "                    snapshot.torpedoRoundsRemaining, snapshot.torpedoReadyTubeCount, snapshot.torpedoTubeCount);\n"
    "        if (snapshot.torpedoNextTubeReadySeconds)\n"
    "            ImGui::Text(\"Next compatible tube reload: %.1f s\", *snapshot.torpedoNextTubeReadySeconds);\n"
    "    }\n",
)

replace_once(
    "Tests/M5PlayerControlledCombatChecks.h",
    "    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||\n"
    "        runtime.PlayerTorpedo()->impactedBody || launched->playerCombat.torpedoReadyTubeCount != 3U ||\n"
    "        launched->playerCombat.playerTorpedoesInFlight != 1U)\n"
    "    {\n"
    "        return false;\n"
    "    }\n",
    "    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||\n"
    "        runtime.PlayerTorpedo()->impactedBody || launched->playerCombat.torpedoReadyTubeCount != 3U ||\n"
    "        launched->playerCombat.playerTorpedoesInFlight != 1U ||\n"
    "        launched->playerCombat.torpedo533RoundsRemaining != 17U ||\n"
    "        launched->playerCombat.torpedo650RoundsRemaining != 10U)\n"
    "    {\n"
    "        return false;\n"
    "    }\n"
    "    const auto& tubeHud = launched->playerCombat.torpedoTubes;\n"
    "    if (tubeHud[0].tubeNumber != 1U || tubeHud[0].calibreMillimeters != 533U || tubeHud[0].ready ||\n"
    "        std::abs(tubeHud[0].reloadSecondsRemaining - Game::Armament::Antey533MmTorpedoTubeReloadSeconds) > 0.01 ||\n"
    "        tubeHud[1].tubeNumber != 2U || tubeHud[1].calibreMillimeters != 533U || !tubeHud[1].ready ||\n"
    "        tubeHud[4].tubeNumber != 5U || tubeHud[4].calibreMillimeters != 650U || !tubeHud[4].ready ||\n"
    "        tubeHud[5].tubeNumber != 6U || tubeHud[5].calibreMillimeters != 650U || !tubeHud[5].ready)\n"
    "    {\n"
    "        return false;\n"
    "    }\n",
)

print("Torpedo tube HUD patch applied")
