from pathlib import Path


def rw(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    s = p.read_text(encoding="utf-8")
    count = s.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 match, got {count}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8")


rw(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        const auto slotIndex = p700LauncherInventory_->FirstLoadedSlotIndex();
        if (!slotIndex)
        {
            return std::unexpected("P-700 production launcher inventory is exhausted");
        }
        const auto worldAnchor = p700CarrierLaunchContract_->BuildWorldAnchor(*slotIndex, *currentPlayerPhysicalProxy_);
''',
    '''        const std::size_t requestedCount =
            playerCombat_.P700SalvoMode() == Weapons::P700SalvoMode::Pair ? 2U : 1U;
        const auto selectedSlots = p700LauncherInventory_->LoadedSlotIndices(requestedCount);
        if (!selectedSlots || selectedSlots->empty())
        {
            return std::unexpected("P-700 production launcher inventory cannot satisfy the selected salvo mode");
        }
        // Employment assessment and materialization must resolve the same physical hatch/slot policy. In
        // particular, Single mode advances across complete paired hatches before revisiting half-used groups.
        const std::size_t slotIndex = selectedSlots->front();
        const auto worldAnchor = p700CarrierLaunchContract_->BuildWorldAnchor(slotIndex, *currentPlayerPhysicalProxy_);
''',
    "P700 employment candidate must match ripple slot policy")
rw(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        return PlayerP700LaunchCandidate{
            .slotIndex = *slotIndex,
''',
    '''        return PlayerP700LaunchCandidate{
            .slotIndex = slotIndex,
''',
    "P700 candidate slot value")

p = Path("Tests/M5PlayerControlledCombatChecks.h")
s = p.read_text(encoding="utf-8")
old = '''    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||
        runtime.PlayerTorpedo()->impactedBody)
    {
        return false;
    }

    // Weapon selection remains legal while the previously launched torpedo is still in flight. The projectile
'''
new = '''    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||
        runtime.PlayerTorpedo()->impactedBody || launched->playerCombat.torpedoReadyTubeCount != 3U ||
        launched->playerCombat.playerTorpedoesInFlight != 1U)
    {
        return false;
    }

    // Fire control re-arms independently of the weapon already in the water. Once automatic preparation finishes,
    // a second loaded 533 mm tube can fire while the first torpedo keeps its independent runtime.
    physicsWorld.Step(fixedDeltaSeconds);
    bool readyForSecondShot = false;
    for (int tick = 0; tick < 90; ++tick)
    {
        commandTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || !runtime.PlayerTorpedo())
            return false;
        readyForSecondShot = frame->playerCombat.weaponPhase == Weapons::WeaponPhase::Ready &&
                             frame->playerCombat.canFireWeapon;
        physicsWorld.Step(fixedDeltaSeconds);
        if (readyForSecondShot)
            break;
    }
    if (!readyForSecondShot)
        return false;

    commandTimeSeconds += fixedDeltaSeconds;
    const auto secondLaunch = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!secondLaunch || !secondLaunch->playerCombat.lastCommand ||
        !secondLaunch->playerCombat.lastCommand->accepted ||
        secondLaunch->playerCombat.playerTorpedoesInFlight != 2U ||
        secondLaunch->playerCombat.torpedoReadyTubeCount != 2U ||
        runtime.AdditionalPlayerTorpedoes().size() != 1U || !runtime.PlayerTorpedo())
    {
        return false;
    }

    // Weapon selection remains legal while the previously launched torpedoes are still in flight. The projectiles
'''
if s.count(old) != 1:
    raise RuntimeError("M5 two-torpedo ripple test insertion anchor mismatch")
s = s.replace(old, new, 1)
p.write_text(s, encoding="utf-8")

print("launcher-cycle follow-up applied")
