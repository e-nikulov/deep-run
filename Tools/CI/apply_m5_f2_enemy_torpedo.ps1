Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Replace-Once {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New
    )

    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNormalized = $Old.Replace("`r`n", "`n")
    $newNormalized = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNormalized, [System.StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "Pattern not found in $Path`n--- pattern ---`n$oldNormalized"
    }
    $second = $text.IndexOf($oldNormalized, $first + $oldNormalized.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) {
        throw "Pattern is not unique in $Path"
    }
    $text = $text.Substring(0, $first) + $newNormalized + $text.Substring($first + $oldNormalized.Length)
    [System.IO.File]::WriteAllText($Path, $text, [System.Text.UTF8Encoding]::new($false))
}

$runtime = 'Game/Combat/CombatPlaygroundRuntime.h'
$presentation = 'Game/Combat/CombatPlaygroundPresentation.h'
$checks = 'Tests/M5CombatPlaygroundRuntimeChecks.h'

Replace-Once $runtime @'
inline constexpr float M5CombatDestroyerInitialXMeters = 1800.0F;
inline constexpr float M5CombatTorpedoLaunchClearanceMeters = 85.0F;
'@ @'
inline constexpr float M5CombatDestroyerInitialXMeters = 1800.0F;
inline constexpr float M5CombatTorpedoLaunchClearanceMeters = 85.0F;
inline constexpr float M5CombatDestroyerTorpedoLaunchClearanceMeters = 32.0F;
inline constexpr float M5CombatDestroyerTorpedoLaunchDepthOffsetMeters = 6.0F;
'@

Replace-Once $runtime @'
    std::optional<Weapons::ConventionalTorpedoImpact> playerTorpedoImpact{};
    std::optional<Weapons::NavalMineDetonation> playerMineDetonation{};
'@ @'
    std::optional<Weapons::ConventionalTorpedoImpact> playerTorpedoImpact{};
    std::optional<Weapons::ConventionalTorpedoImpact> destroyerTorpedoImpact{};
    std::optional<Weapons::NavalMineDetonation> playerMineDetonation{};
'@

Replace-Once $runtime @'
            .directImpactDamage = 60.0F,
            .explosionRadiusMeters = 8.0F};
        const Weapons::AcousticDecoyDefinition decoyDefinition{
'@ @'
            .directImpactDamage = 60.0F,
            .explosionRadiusMeters = 8.0F};
        const Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition{
            .weapon = destroyerDefinition.weapon,
            .underwaterSpeedMetersPerSecond = 44.0F,
            .maximumTurnRateRadiansPerSecond = 0.35F,
            .maximumVerticalCourseAngleRadians = M5CombatTorpedoMaximumVerticalCourseAngleRadians,
            .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
            .directImpactDamage = 55.0F,
            .explosionRadiusMeters = 8.0F};
        const Weapons::AcousticDecoyDefinition decoyDefinition{
'@

Replace-Once $runtime @'
            destroyerDefinition,
            *destroyer,
            playerTorpedoDefinition,
            std::move(*playerCombat),
'@ @'
            destroyerDefinition,
            *destroyer,
            destroyerTorpedoDefinition,
            playerTorpedoDefinition,
            std::move(*playerCombat),
'@

Replace-Once $runtime @'
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept
    {
        return playerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
'@ @'
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& PlayerTorpedoLaunchPosition() const noexcept
    {
        return playerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::ConventionalTorpedoRuntimeState>& DestroyerTorpedo() const noexcept
    {
        return destroyerTorpedo_;
    }
    [[nodiscard]] const std::optional<Physics::PhysicsVector3>& DestroyerTorpedoLaunchPosition() const noexcept
    {
        return destroyerTorpedoLaunchPosition_;
    }
    [[nodiscard]] const std::optional<Weapons::AcousticDecoyRuntimeState>& Decoy() const noexcept { return decoy_; }
'@

Replace-Once $runtime @'
        SimpleDestroyerDefinition destroyerDefinition,
        SimpleDestroyerRuntimeState destroyer,
        Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition,
        PlayerCombatCommandRuntime playerCombat,
'@ @'
        SimpleDestroyerDefinition destroyerDefinition,
        SimpleDestroyerRuntimeState destroyer,
        Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition,
        Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition,
        PlayerCombatCommandRuntime playerCombat,
'@

Replace-Once $runtime @'
          destroyerDefinition_(std::move(destroyerDefinition)),
          destroyer_(std::move(destroyer)),
          playerTorpedoDefinition_(std::move(playerTorpedoDefinition)),
'@ @'
          destroyerDefinition_(std::move(destroyerDefinition)),
          destroyer_(std::move(destroyer)),
          destroyerTorpedoDefinition_(std::move(destroyerTorpedoDefinition)),
          playerTorpedoDefinition_(std::move(playerTorpedoDefinition)),
'@

Replace-Once $runtime @'
        if (!destroyerActivePulse_ && destroyer_.weapon.phase != Weapons::WeaponPhase::Launched &&
            simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
'@ @'
        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
'@

Replace-Once $runtime @'
        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
'@ @'
        const auto destroyerDecision = AdvanceSimpleDestroyerCombatRuntime(
            destroyerDefinition_, destroyer_, destroyerTracks_.Tracks(), simulationTimeSeconds);
        if (!destroyerDecision)
        {
            return std::unexpected("M5-H destroyer combat AI failed: " + destroyerDecision.error());
        }

        // M5-F.2 materializes hostile weapon state only from the Track that the existing F.1 controller actually
        // accepted for LaunchWeapon. No player body handle or authoritative player Transform is used to create,
        // target, or steer the torpedo.
        if (destroyerDecision->action == SimpleDestroyerCombatAction::LaunchWeapon && !destroyerTorpedo_)
        {
            const auto targetTrack = FindTrack(destroyerTracks_.Tracks(), destroyer_.weapon.targetTrackId);
            if (!targetTrack)
            {
                return std::unexpected("M5-F.2 destroyer launch lost its perceived fire-control track");
            }
            const auto materialized = MaterializeDestroyerLaunch(
                *destroyerAcoustics, *targetTrack, simulationTimeSeconds);
            if (!materialized)
            {
                return std::unexpected(materialized.error());
            }
        }

        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
'@

Replace-Once $runtime @'
        std::optional<Weapons::NavalMineDetonation> mineDetonation{};
'@ @'
        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};
        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const auto perceivedTrack = FindTrack(destroyerTracks_.Tracks(), destroyerTorpedo_->guidanceTrackId);
            const auto advanced = Weapons::AdvanceConventionalTorpedoWithCollision(
                destroyerTorpedoDefinition_,
                *destroyerTorpedo_,
                perceivedTrack,
                *physicsWorld_,
                simulationTimeSeconds,
                destroyer_.body);
            if (!advanced)
            {
                return std::unexpected("M5-F.2 destroyer torpedo fixed-step advance failed: " + advanced.error());
            }
            if (advanced->has_value())
            {
                destroyerImpact = **advanced;
                lastExplosion_ = destroyerImpact->explosion;
                if (playerIntegrity_ && destroyerImpact->physicsHit.body == playerBody_)
                {
                    const auto damaged = DeepRun::Combat::ApplyCombatDamage(*playerIntegrity_, destroyerImpact->damage);
                    if (!damaged)
                    {
                        return std::unexpected("M5-F.2 player torpedo-damage application failed: " + damaged.error());
                    }
                }
            }
        }

        std::optional<Weapons::NavalMineDetonation> mineDetonation{};
'@

Replace-Once $runtime @'
            .destroyerDecision = *destroyerDecision,
            .playerTorpedoImpact = impact,
            .playerMineDetonation = mineDetonation,
'@ @'
            .destroyerDecision = *destroyerDecision,
            .playerTorpedoImpact = impact,
            .destroyerTorpedoImpact = destroyerImpact,
            .playerMineDetonation = mineDetonation,
'@

Replace-Once $runtime @'
    [[nodiscard]] std::expected<void, std::string> MaterializePlayerLaunch(
'@ @'
    [[nodiscard]] std::expected<void, std::string> MaterializeDestroyerLaunch(
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const Perception::Track& targetTrack,
        const double simulationTimeSeconds)
    {
        if (destroyer_.weapon.phase != Weapons::WeaponPhase::Launched ||
            destroyer_.weapon.targetTrackId != std::optional<std::uint64_t>{targetTrack.trackId} ||
            !targetTrack.estimatedPositionMeters ||
            !Weapons::ValidateTrackForWeapon(destroyerTorpedoDefinition_.weapon, targetTrack))
        {
            return std::unexpected("M5-F.2 destroyer torpedo materialization requires its accepted ranged Track");
        }
        if (!destroyerAcoustics.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-F.2 destroyer torpedo launch origin is invalid");
        }

        const float deltaX = targetTrack.estimatedPositionMeters->x - destroyerAcoustics.emitter.positionMeters.x;
        if (!std::isfinite(deltaX) || std::abs(deltaX) <= 1.0e-3F)
        {
            return std::unexpected("M5-F.2 destroyer torpedo launch has no horizontal target separation");
        }
        const float forwardSign = deltaX > 0.0F ? 1.0F : -1.0F;
        const Physics::PhysicsVector3 launchPosition{
            .x = destroyerAcoustics.emitter.positionMeters.x +
                 forwardSign * M5CombatDestroyerTorpedoLaunchClearanceMeters,
            .y = destroyerAcoustics.emitter.positionMeters.y - M5CombatDestroyerTorpedoLaunchDepthOffsetMeters,
            .z = destroyerAcoustics.emitter.positionMeters.z};
        const float launchHeading = static_cast<float>(std::atan2(
            static_cast<double>(targetTrack.estimatedPositionMeters->y) - launchPosition.y,
            static_cast<double>(targetTrack.estimatedPositionMeters->x) - launchPosition.x));
        const auto launched = Weapons::CreateLaunchedConventionalTorpedo(
            destroyerTorpedoDefinition_,
            destroyer_.weapon,
            launchPosition,
            launchHeading,
            targetTrack,
            simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("M5-F.2 destroyer torpedo runtime creation failed: " + launched.error());
        }
        destroyerTorpedo_ = *launched;
        destroyerTorpedoLaunchPosition_ = launchPosition;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> MaterializePlayerLaunch(
'@

Replace-Once $runtime @'
    SimpleDestroyerDefinition destroyerDefinition_;
    SimpleDestroyerRuntimeState destroyer_;
    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
'@ @'
    SimpleDestroyerDefinition destroyerDefinition_;
    SimpleDestroyerRuntimeState destroyer_;
    Weapons::ConventionalTorpedoDefinition destroyerTorpedoDefinition_;
    std::optional<Weapons::ConventionalTorpedoRuntimeState> destroyerTorpedo_{};
    std::optional<Physics::PhysicsVector3> destroyerTorpedoLaunchPosition_{};
    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;
'@

Replace-Once $presentation @'
    bool destroyerDestroyed = false;
    std::optional<CombatPlaygroundTorpedoPresentation> playerTorpedo{};
    std::optional<CombatPlaygroundDecoyPresentation> decoy{};
'@ @'
    bool destroyerDestroyed = false;
    std::optional<CombatPlaygroundTorpedoPresentation> playerTorpedo{};
    std::optional<CombatPlaygroundTorpedoPresentation> destroyerTorpedo{};
    std::optional<CombatPlaygroundDecoyPresentation> decoy{};
'@

Replace-Once $presentation @'
    DestroyerSuperstructure,
    PlayerTorpedo,
    AcousticDecoy,
'@ @'
    DestroyerSuperstructure,
    PlayerTorpedo,
    DestroyerTorpedo,
    AcousticDecoy,
'@

Replace-Once $presentation @'
    if (const auto& decoy = runtime.Decoy(); decoy.has_value())
'@ @'
    if (const auto& torpedo = runtime.DestroyerTorpedo(); torpedo.has_value())
    {
        if (!torpedo->positionMeters.IsFinite() || !std::isfinite(torpedo->headingRadians))
        {
            return std::unexpected("M5-F.2 destroyer torpedo presentation state is invalid");
        }
        snapshot.destroyerTorpedo = CombatPlaygroundTorpedoPresentation{
            .positionMeters = torpedo->positionMeters,
            .headingRadians = torpedo->headingRadians,
            .movementDomain = torpedo->movementDomain};
    }

    if (const auto& decoy = runtime.Decoy(); decoy.has_value())
'@

Replace-Once $presentation @'
    draws.reserve(6U);
'@ @'
    draws.reserve(7U);
'@

Replace-Once $presentation @'
    if (snapshot.decoy && snapshot.decoy->active)
'@ @'
    if (snapshot.destroyerTorpedo && snapshot.destroyerTorpedo->movementDomain == Weapons::MovementDomain::Underwater)
    {
        const auto transform = PoseScaleTransform(
            snapshot.destroyerTorpedo->positionMeters,
            Weapons::WeaponHeadingQuaternion(snapshot.destroyerTorpedo->headingRadians),
            {.x = 4.0F, .y = 0.65F, .z = 0.65F});
        if (!transform)
        {
            return std::unexpected(transform.error());
        }
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::DestroyerTorpedo,
            *transform,
            Material("M5DestroyerTorpedo", {0.64F, 0.42F, 0.24F, 1.0F}, 0.32F, 0.52F));
        if (!draw)
        {
            return std::unexpected(draw.error());
        }
        draws.push_back(std::move(*draw));
    }

    if (snapshot.decoy && snapshot.decoy->active)
'@

Replace-Once $checks @'
    const auto playerSnapshot = *playerSnapshotResult;

    Game::Combat::CombatPlaygroundCameraDirector cameraDirector;
'@ @'
    const auto playerSnapshot = *playerSnapshotResult;

    // M5-F.2 uses a real PhysicsWorld body and the same I.2 physical-proxy bridge as the windowed production
    // composition. The hostile torpedo never receives this handle; it can discover it only through swept collision.
    const Physics::PhysicsVector3 playerHalfExtentsMeters{.x = 75.0F, .y = 8.0F, .z = 8.0F};
    const auto playerBody = physicsWorld.CreateDynamicBoxBody(Physics::DynamicBoxBodyCreateInfo{
        .halfExtents = playerHalfExtentsMeters,
        .mass = 12'000'000.0F,
        .position = playerSnapshot.emitter.positionMeters,
        .orientation = {},
        .gravityEnabled = false,
        .linearDamping = 0.0F,
        .angularDamping = 0.0F,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {}});
    if (!playerBody.IsValid())
    {
        return false;
    }
    const auto boundPlayer = runtime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = playerBody,
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .orientation = {},
            .halfExtentsMeters = playerHalfExtentsMeters},
        playerSnapshot,
        0.0);
    if (!boundPlayer || !runtime.Mine().has_value())
    {
        return false;
    }

    Game::Combat::CombatPlaygroundCameraDirector cameraDirector;
'@

Replace-Once $checks @'
    bool sawDestroyerLaunch = false;
    bool sawTorpedo = false;
'@ @'
    bool sawDestroyerLaunch = false;
    bool sawDestroyerTorpedoMaterialized = false;
    bool sawDestroyerTorpedoImpact = false;
    bool sawDestroyerTorpedoUnderwaterWithoutBodyIdentity = false;
    bool sawDestroyerTorpedoHiddenAfterImpact = false;
    bool sawTorpedo = false;
'@

Replace-Once $checks @'
    bool sawPresentationTorpedo = false;
    bool sawPresentationDecoy = false;
'@ @'
    bool sawPresentationTorpedo = false;
    bool sawPresentationDestroyerTorpedo = false;
    bool sawPresentationMine = false;
    bool sawPresentationDecoy = false;
'@

Replace-Once $checks @'
            sawDestroyerLaunch = true;
        }
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
'@ @'
            if (!runtime.DestroyerTorpedo() || !runtime.DestroyerTorpedoLaunchPosition() ||
                runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Underwater ||
                runtime.DestroyerTorpedo()->impactedBody.has_value() ||
                runtime.DestroyerTorpedo()->guidanceTrackId != frame->destroyerDecision.perceivedTrackId ||
                runtime.DestroyerTorpedo()->weapon.targetTrackId != frame->destroyerDecision.perceivedTrackId)
            {
                return false;
            }
            sawDestroyerLaunch = true;
            sawDestroyerTorpedoMaterialized = true;
        }
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
'@

Replace-Once $checks @'
        if (frame->playerTorpedoImpact)
        {
            if (frame->playerTorpedoImpact->physicsHit.body != runtime.Destroyer().body ||
                !runtime.LastExplosion().has_value())
            {
                return false;
            }
            sawImpact = true;
        }

        // M5-H.1-A presentation is a pure read-only projection of already-authoritative combat/physics state.
'@ @'
        if (runtime.DestroyerTorpedo() &&
            runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater)
        {
            if (runtime.DestroyerTorpedo()->impactedBody.has_value())
            {
                return false;
            }
            sawDestroyerTorpedoUnderwaterWithoutBodyIdentity = true;
        }

        if (frame->playerTorpedoImpact)
        {
            if (frame->playerTorpedoImpact->physicsHit.body != runtime.Destroyer().body ||
                !runtime.LastExplosion().has_value())
            {
                return false;
            }
            sawImpact = true;
        }
        if (frame->destroyerTorpedoImpact)
        {
            if (frame->destroyerTorpedoImpact->physicsHit.body != playerBody ||
                frame->destroyerTorpedoImpact->damage.targetBody != playerBody ||
                !runtime.PlayerIntegrity().has_value() || runtime.PlayerIntegrity()->destroyed)
            {
                return false;
            }
            sawDestroyerTorpedoImpact = true;
        }

        // M5-H.1-A presentation is a pure read-only projection of already-authoritative combat/physics state.
'@

Replace-Once $checks @'
        if (!presentationDraws || presentationDraws->size() < 2U || presentationDraws->size() > 5U ||
'@ @'
        if (!presentationDraws || presentationDraws->size() < 2U || presentationDraws->size() > 7U ||
'@

Replace-Once $checks @'
        if (tick == 0 && presentationDraws->size() != 2U)
        {
            return false;
        }
'@ @'
        if (tick == 0 && (presentationDraws->size() != 3U ||
                          (*presentationDraws)[2].element != CombatPlaygroundPresentationElement::NavalMine))
        {
            return false;
        }
'@

Replace-Once $checks @'
        sawPresentationTorpedo = sawPresentationTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        sawPresentationDecoy = sawPresentationDecoy ||
'@ @'
        sawPresentationTorpedo = sawPresentationTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        sawPresentationDestroyerTorpedo = sawPresentationDestroyerTorpedo ||
            hasElement(CombatPlaygroundPresentationElement::DestroyerTorpedo);
        sawPresentationMine = sawPresentationMine ||
            hasElement(CombatPlaygroundPresentationElement::NavalMine);
        sawPresentationDecoy = sawPresentationDecoy ||
'@

Replace-Once $checks @'
        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawPostImpactTorpedoHidden = sawPostImpactTorpedoHidden ||
                !hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        }

        physicsWorld.Step(fixedDeltaSeconds);
'@ @'
        if (runtime.PlayerTorpedo() && runtime.PlayerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawPostImpactTorpedoHidden = sawPostImpactTorpedoHidden ||
                !hasElement(CombatPlaygroundPresentationElement::PlayerTorpedo);
        }
        if (runtime.DestroyerTorpedo() && runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Spent)
        {
            sawDestroyerTorpedoHiddenAfterImpact = sawDestroyerTorpedoHiddenAfterImpact ||
                !hasElement(CombatPlaygroundPresentationElement::DestroyerTorpedo);
        }

        physicsWorld.Step(fixedDeltaSeconds);
'@

Replace-Once $checks @'
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerBearingOnlyAwareness ||
        !sawDestroyerSpatialFireControlTrack || !sawDestroyerPreparation || !sawDestroyerLaunch || !sawTorpedo ||
        !sawDecoy || !sawLiveSeekerSelection || !sawDecoyDiversion || !sawPostDecoyRecovery ||
        !sawImpact || !sawPresentationTorpedo || !sawPresentationDecoy ||
        !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
        !sawStraightRunout || !sawGradualAscent || !sawCameraTransition || !sawTacticalCamera ||
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase != Weapons::WeaponPhase::Launched ||
        !runtime.Destroyer().weapon.targetTrackId.has_value() ||
        runtime.Destroyer().integrity.destroyed ||
        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||
        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent)
'@ @'
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerBearingOnlyAwareness ||
        !sawDestroyerSpatialFireControlTrack || !sawDestroyerPreparation || !sawDestroyerLaunch ||
        !sawDestroyerTorpedoMaterialized || !sawDestroyerTorpedoImpact ||
        !sawDestroyerTorpedoUnderwaterWithoutBodyIdentity || !sawDestroyerTorpedoHiddenAfterImpact || !sawTorpedo ||
        !sawDecoy || !sawLiveSeekerSelection || !sawDecoyDiversion || !sawPostDecoyRecovery ||
        !sawImpact || !sawPresentationTorpedo || !sawPresentationDestroyerTorpedo || !sawPresentationMine ||
        !sawPresentationDecoy || !sawPresentationExplosion || !sawPostImpactTorpedoHidden || !sawHorizontalLaunch ||
        !sawStraightRunout || !sawGradualAscent || !sawCameraTransition || !sawTacticalCamera ||
        stableTacticalTicks < 60U || !destroyerState ||
        runtime.Destroyer().weapon.phase != Weapons::WeaponPhase::Launched ||
        !runtime.Destroyer().weapon.targetTrackId.has_value() ||
        runtime.Destroyer().integrity.destroyed ||
        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||
        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||
        !runtime.DestroyerTorpedo() || runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||
        runtime.DestroyerTorpedo()->impactedBody != std::optional<Physics::PhysicsBodyHandle>{playerBody} ||
        !runtime.PlayerIntegrity() || runtime.PlayerIntegrity()->destroyed ||
        std::abs(runtime.PlayerIntegrity()->remainingIntegrity - 45.0F) > 0.001F ||
        !runtime.Mine() || runtime.Mine()->detonated)
'@

Replace-Once $checks @'
    // M5-F.1 closes the previous bearing-only boundary deliberately: launch is now legal only because a
    // monostatic active echo produced ranged perceived evidence. The controller still receives no player body,
    // Transform or scenario-ground-truth position.
    return physicsWorld.DestroyBody(runtime.Destroyer().body);
'@ @'
    // M5-F.2 now proves the reciprocal physical consequence as well: target identity enters the hostile torpedo
    // only at PhysicsWorld impact, then the existing combat-integrity authority applies damage to the bound body.
    const auto mineBody = runtime.Mine()->body;
    const bool destroyedMine = physicsWorld.DestroyBody(mineBody);
    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);
    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);
    return destroyedMine && destroyedPlayer && destroyedDestroyer;
'@

Write-Host 'M5-F.2 source patch applied successfully.'
