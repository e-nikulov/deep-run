from pathlib import Path

def rep(s,o,n,l):
    c=s.count(o)
    if c!=1: raise RuntimeError(f'{l}: {c}')
    return s.replace(o,n,1)

# Application option
p=Path('Engine/Core/Application.h'); s=p.read_text()
s=rep(s,'    bool smokeTest = false;\n    bool benchmarkM3 = false;', '    bool smokeTest = false;\n    bool p700SmokeTest = false;\n    bool benchmarkM3 = false;','app option')
p.write_text(s)
p=Path('Engine/Core/Application.cpp'); s=p.read_text()
s=rep(s,'''        else if (argument == "--smoke-test")
        {
            options.smokeTest = true;
        }
        else if (argument == "--benchmark-m3")''','''        else if (argument == "--smoke-test")
        {
            options.smokeTest = true;
        }
        else if (argument == "--smoke-p700")
        {
            options.p700SmokeTest = true;
        }
        else if (argument == "--benchmark-m3")''','parse p700')
s=rep(s,'''    if ((options_.benchmarkM3 && (options_.headless || options_.smokeTest)) ||
        (!options_.benchmarkM3 && (options_.benchmarkHdr || options_.benchmarkStability)))''','''    if ((options_.benchmarkM3 && (options_.headless || options_.smokeTest || options_.p700SmokeTest)) ||
        (options_.p700SmokeTest && (options_.headless || options_.smokeTest || options_.benchmarkHdr || options_.benchmarkStability)) ||
        (!options_.benchmarkM3 && (options_.benchmarkHdr || options_.benchmarkStability)))''','validate p700 combo')
p.write_text(s)

# Physical initial depth parameter
p=Path('Game/PhysicalPlayground.h'); s=p.read_text()
s=rep(s,'''        Physics::PhysicsWorld& physics,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads);''','''        Physics::PhysicsWorld& physics,
        Render::D3D12Renderer& renderer,
        bool verifyDistinctUploads,
        float initialSubmarineDepthMeters = 100.0F);''','init depth signature h')
p.write_text(s)
p=Path('Game/PhysicalPlayground.cpp'); s=p.read_text()
s=rep(s,'''std::expected<void, std::string> PhysicalPlayground::Initialize(
    Assets::AssetManager& assets,
    Physics::PhysicsWorld& physics,
    Render::D3D12Renderer& renderer,
    const bool verifyDistinctUploads)''','''std::expected<void, std::string> PhysicalPlayground::Initialize(
    Assets::AssetManager& assets,
    Physics::PhysicsWorld& physics,
    Render::D3D12Renderer& renderer,
    const bool verifyDistinctUploads,
    const float initialSubmarineDepthMeters)''','init depth signature cpp')
s=rep(s,'''    if (!physics.IsInitialized() || !renderer.IsInitialized())''','''    if (!physics.IsInitialized() || !renderer.IsInitialized() || !std::isfinite(initialSubmarineDepthMeters) ||
        initialSubmarineDepthMeters <= 0.0F || initialSubmarineDepthMeters > NormalGameplayMaximumVisibleDepthMeters)''','init depth validation')
s=s.replace('water->Config().surfaceLevelY, M2InitialSubmarineDepthMeters, collisionCenterModel)', 'water->Config().surfaceLevelY, initialSubmarineDepthMeters, collisionCenterModel)',1)
s=s.replace('std::abs(initialDepthSample->signedDepthMeters - M2InitialSubmarineDepthMeters)', 'std::abs(initialDepthSample->signedDepthMeters - initialSubmarineDepthMeters)',1)
p.write_text(s)

# Combat runtime variable target range + acceptance commander/defense
p=Path('Game/Combat/CombatPlaygroundRuntime.h'); s=p.read_text()
s=rep(s,'''        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds)
    {''','''        Physics::PhysicsWorld& physicsWorld,
        const float surfaceLevelY,
        const double simulationTimeSeconds,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false)
    {''','runtime base create sig')
s=rep(s,'''        if (!physicsWorld.IsInitialized() || !std::isfinite(surfaceLevelY) ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)''','''        if (!physicsWorld.IsInitialized() || !std::isfinite(surfaceLevelY) ||
            !std::isfinite(destroyerInitialXMeters) || destroyerInitialXMeters <= 0.0F ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)''','runtime create validate')
s=s.replace('            M5CombatDestroyerInitialXMeters,\n            0.0F,', '            destroyerInitialXMeters,\n            0.0F,',1)
s=rep(s,'''            playerDecoyDefinition,
            simulationTimeSeconds);''','''            playerDecoyDefinition,
            simulationTimeSeconds,
            p700AcceptanceMode);''','runtime ctor call flag')
s=rep(s,'''        Armament::P700CarrierLaunchContract p700CarrierLaunchContract,
        Armament::P700LauncherInventory p700LauncherInventory)''','''        Armament::P700CarrierLaunchContract p700CarrierLaunchContract,
        Armament::P700LauncherInventory p700LauncherInventory,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false)''','runtime prod create sig')
s=rep(s,'''        auto runtime = Create(physicsWorld, surfaceLevelY, simulationTimeSeconds);''','''        auto runtime = Create(physicsWorld, surfaceLevelY, simulationTimeSeconds, destroyerInitialXMeters, p700AcceptanceMode);''','runtime prod call')
# method acceptance after AdvancePlayerControlled
needle='''    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvancePlayerControlled(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {
        return AdvanceImpl(playerSnapshot, commands, simulationTimeSeconds, false);
    }
'''
add=needle+'''
    // Dedicated windowed acceptance driver. It issues only the same semantic commands normal input can issue;
    // perception/ranging/readiness/employment/materialization all stay on the production path.
    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvanceP700Acceptance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const double simulationTimeSeconds)
    {
        std::array<PlayerCombatCommand, 2> commands{};
        std::size_t count = 0U;
        if (selectedPlayerWeapon_ != Armament::PlayerWeaponType::P700Granit &&
            playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
        {
            commands[count++] = {.type = PlayerCombatCommandType::NextWeapon};
        }
        const auto tracks = playerTracks_.Tracks();
        if (!playerCombat_.SelectedTrackId().has_value() && !tracks.empty())
        {
            commands[count++] = {.type = PlayerCombatCommandType::SelectNextTrack};
        }
        else if (playerCombat_.SelectedTrackId().has_value() && selectedPlayerWeapon_ == Armament::PlayerWeaponType::P700Granit)
        {
            const auto selected = FindTrack(tracks, playerCombat_.SelectedTrackId());
            if (selected && !selected->estimatedPositionMeters.has_value() && !activePulse_.has_value() &&
                simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_)
            {
                commands[count++] = {.type = PlayerCombatCommandType::ActiveSonarPing};
            }
            else if (selected && selected->estimatedPositionMeters.has_value())
            {
                if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)
                    commands[count++] = {.type = PlayerCombatCommandType::PrepareWeapon};
                else if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready)
                    commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};
            }
        }
        return AdvanceImpl(playerSnapshot, std::span<const PlayerCombatCommand>{commands.data(), count}, simulationTimeSeconds, false);
    }
'''
s=rep(s,needle,add,'acceptance advance method')
# ctor flag
s=rep(s,'''        Weapons::AcousticDecoyDefinition playerDecoyDefinition,
        const double simulationTimeSeconds)''','''        Weapons::AcousticDecoyDefinition playerDecoyDefinition,
        const double simulationTimeSeconds,
        const bool p700AcceptanceMode)''','runtime ctor sig')
s=rep(s,'''          nextDestroyerActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds)''','''          nextDestroyerActivePulseTimeSeconds_(simulationTimeSeconds),
          lastUpdateTimeSeconds_(simulationTimeSeconds),
          p700AcceptanceMode_(p700AcceptanceMode)''','runtime ctor init')
# defense at advance
s=rep(s,'''            const auto advanced = Weapons::AdvanceP700GranitWithCollision(
                playerP700Definition_, *playerP700_, perceivedTrack, *physicsWorld_, simulationTimeSeconds, playerBody_);''','''            const std::optional<Weapons::P700TerminalDefenseProfile> targetDefense = p700AcceptanceMode_
                ? std::nullopt
                : std::optional<Weapons::P700TerminalDefenseProfile>{Weapons::P700TerminalDefenseProfile{}};
            const auto advanced = Weapons::AdvanceP700GranitWithCollision(
                playerP700Definition_, *playerP700_, perceivedTrack, *physicsWorld_, simulationTimeSeconds, playerBody_, targetDefense);''','runtime defense profile')
# seed variability
s=rep(s,'''        playerP700LaunchSlotIndex_ = launch->slotIndex;
        playerP700_ = std::move(*missile);''','''        missile->terminalRandomSeed = Weapons::P700SplitMix64(
            missile->terminalRandomSeed ^ static_cast<std::uint64_t>(launch->slotIndex + 1U) ^
            static_cast<std::uint64_t>(std::llround(simulationTimeSeconds * 60.0)));
        playerP700LaunchSlotIndex_ = launch->slotIndex;
        playerP700_ = std::move(*missile);''','seed launcher/time')
# member at bottom by lastUpdateTime
s=rep(s,'''    double lastUpdateTimeSeconds_ = 0.0;''','''    double lastUpdateTimeSeconds_ = 0.0;
    bool p700AcceptanceMode_ = false;''','acceptance member')
p.write_text(s)

# Windowed composition configurable target + advance
p=Path('Game/Combat/CombatPlaygroundWindowedComposition.h'); s=p.read_text()
s=rep(s,'''    [[nodiscard]] static std::expected<CombatPlaygroundWindowedComposition, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets)''','''    [[nodiscard]] static std::expected<CombatPlaygroundWindowedComposition, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets,
        const float destroyerInitialXMeters = M5CombatDestroyerInitialXMeters,
        const bool p700AcceptanceMode = false)''','composition create sig')
s=rep(s,'''        return CombatPlaygroundWindowedComposition(std::move(*view), std::move(*p700Carrier));''','''        return CombatPlaygroundWindowedComposition(
            std::move(*view), std::move(*p700Carrier), destroyerInitialXMeters, p700AcceptanceMode);''','composition ctor call')
needle='''    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvancePlayerControlled(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
        Physics::PhysicsWorld& physicsWorld,
        const std::span<const PlayerCombatCommand> commands,
        const double simulationTimeSeconds)
    {'''
idx=s.index(needle)
# add separate method before RenderWithPresentation by replacing end chunk marker
marker='''    [[nodiscard]] std::expected<CombatPlaygroundRenderFrame, std::string> RenderWithPresentation('''
method='''    [[nodiscard]] std::expected<CombatPlaygroundFrame, std::string> AdvanceP700Acceptance(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerCollisionProxy,
        Physics::PhysicsWorld& physicsWorld,
        const double simulationTimeSeconds)
    {
        const auto ready = EnsureRuntime(playerSnapshot, playerCollisionProxy, physicsWorld, simulationTimeSeconds);
        if (!ready) return std::unexpected(ready.error());
        const auto synced = runtime_->UpdatePlayerPhysicalProxy(playerCollisionProxy, playerSnapshot);
        if (!synced) return std::unexpected("M5 P-700 acceptance physical proxy update failed: " + synced.error());
        const auto frame = runtime_->AdvanceP700Acceptance(playerSnapshot, simulationTimeSeconds);
        if (!frame) return std::unexpected("M5 P-700 acceptance advance failed: " + frame.error());
        return *frame;
    }

'''+marker
s=rep(s,marker,method,'composition acceptance method')
s=rep(s,'''    CombatPlaygroundWindowedComposition(
        CombatPlaygroundView view,
        Armament::P700CarrierLaunchContract p700CarrierLaunchContract)
        : view_(std::move(view)),
          p700CarrierLaunchContract_(std::move(p700CarrierLaunchContract))''','''    CombatPlaygroundWindowedComposition(
        CombatPlaygroundView view,
        Armament::P700CarrierLaunchContract p700CarrierLaunchContract,
        const float destroyerInitialXMeters,
        const bool p700AcceptanceMode)
        : view_(std::move(view)),
          p700CarrierLaunchContract_(std::move(p700CarrierLaunchContract)),
          destroyerInitialXMeters_(destroyerInitialXMeters),
          p700AcceptanceMode_(p700AcceptanceMode)''','composition ctor')
s=rep(s,'''            p700CarrierLaunchContract_,
            std::move(*p700Inventory));''','''            p700CarrierLaunchContract_,
            std::move(*p700Inventory),
            destroyerInitialXMeters_,
            p700AcceptanceMode_);''','composition runtime args')
s=rep(s,'''    Armament::P700CarrierLaunchContract p700CarrierLaunchContract_;
    std::optional<CombatPlaygroundRuntime> runtime_{};''','''    Armament::P700CarrierLaunchContract p700CarrierLaunchContract_;
    float destroyerInitialXMeters_ = M5CombatDestroyerInitialXMeters;
    bool p700AcceptanceMode_ = false;
    std::optional<CombatPlaygroundRuntime> runtime_{};''','composition members')
p.write_text(s)

# Main wire p700 smoke, evidence/log/camera/shutdown
p=Path('DeepRun/Main.cpp'); s=p.read_text()
# startup depth and combat config
s=rep(s,'''                const auto initialized = playground.Initialize(engine.Assets(), *physics, *renderer, options.smokeTest);''','''                const auto initialized = playground.Initialize(
                    engine.Assets(), *physics, *renderer, options.smokeTest || options.p700SmokeTest,
                    options.p700SmokeTest ? 30.0F : 100.0F);''','main init depth')
s=rep(s,'''                    const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(*renderer, engine.Assets());''','''                    const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(
                        *renderer,
                        engine.Assets(),
                        options.p700SmokeTest ? 20'100.0F : DeepRun::Game::Combat::M5CombatDestroyerInitialXMeters,
                        options.p700SmokeTest);''','main combat scenario')
# neutral vessel command p700 smoke
s=rep(s,'''                const auto command = (options.smokeTest || options.benchmarkM3)''','''                const auto command = (options.smokeTest || options.p700SmokeTest || options.benchmarkM3)''','main neutral command')
# combat advance choice
s=rep(s,'''                    const auto combatFrame = options.smokeTest
                        ? combatPlayground->Advance(
                              *acousticSnapshot, *playerCollisionProxy, *physics, simulationTimeSeconds)
                        : combatPlayground->AdvancePlayerControlled(''','''                    const auto combatFrame = options.smokeTest
                        ? combatPlayground->Advance(
                              *acousticSnapshot, *playerCollisionProxy, *physics, simulationTimeSeconds)
                        : options.p700SmokeTest
                            ? combatPlayground->AdvanceP700Acceptance(
                                  *acousticSnapshot, *playerCollisionProxy, *physics, simulationTimeSeconds)
                            : combatPlayground->AdvancePlayerControlled(''','main acceptance advance')
# log/shutdown after frame; insert before loggedCombatRuntime
needle='''                    if (!loggedCombatRuntime)
                    {'''
block='''                    if (options.p700SmokeTest && combatPlayground->Runtime().has_value())
                    {
                        const auto& runtime = *combatPlayground->Runtime();
                        if (runtime.PlayerP700().has_value())
                        {
                            const auto& missile = *runtime.PlayerP700();
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::HatchOpening && missile.hatchOpenProgress > 0.0F)
                                std::cout << "[Game][P700] HATCH_OPENING progress=" << missile.hatchOpenProgress << '\\n';
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::UnderwaterLaunch)
                                std::cout << "[Game][P700] UNDERWATER_BOOSTER_EXIT\n";
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::WaterExit)
                                std::cout << "[Game][P700] WATER_EXIT\n";
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::PostExitTransition)
                                std::cout << "[Game][P700] HARDWARE_SEPARATION progress=" << missile.postExitTransitionProgress << '\\n';
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::AirborneDeploying)
                                std::cout << "[Game][P700] AERODYNAMIC_DEPLOY progress=" << missile.deploymentProgress << '\\n';
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Cruise)
                                std::cout << "[Game][P700] CRUISE speed_mps=" << missile.speedMetersPerSecond << '\\n';
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal)
                                std::cout << "[Game][P700] TERMINAL speed_mps=" << missile.speedMetersPerSecond << '\\n';
                        }
                        if (combatFrame->playerP700Impact.has_value())
                        {
                            std::cout << "[Game][P700] PHYSICAL_IMPACT damage="
                                      << combatFrame->playerP700Impact->damage.damage << " radius="
                                      << combatFrame->playerP700Impact->explosion.radiusMeters << "\n";
                            engineServices->RequestShutdown();
                        }
                        if (simulationTimeSeconds > 75.0)
                        {
                            std::cerr << "[Game][ERROR] P-700 acceptance exceeded 75 s SimulationTime without impact\n";
                            return false;
                        }
                    }
                    if (!loggedCombatRuntime)
                    {'''
s=rep(s,needle,block,'main p700 logging shutdown')
# camera p700 branch before smoke
s=rep(s,'''                    if (options.smokeTest)
                    {
                        const auto cameraFraming = smokeCombatCameraDirector.Evaluate(''','''                    if (options.p700SmokeTest)
                    {
                        float targetOffsetXMeters = 0.0F;
                        float spanMeters = 900.0F;
                        if (combatPlayground->Runtime()->PlayerP700().has_value() && initialOwnshipNavigationPositionMeters.has_value())
                        {
                            const auto& missile = *combatPlayground->Runtime()->PlayerP700();
                            targetOffsetXMeters = missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x;
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Cruise) spanMeters = 4'000.0F;
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal) spanMeters = 7'000.0F;
                        }
                        const auto appliedFraming = playground.SetPresentationCameraFraming(
                            targetOffsetXMeters, 0.0F, spanMeters, renderer.AspectRatio());
                        if (!appliedFraming)
                        {
                            std::cerr << "[Game][ERROR] P-700 acceptance camera failed: " << appliedFraming.error() << '\\n';
                            return false;
                        }
                    }
                    else if (options.smokeTest)
                    {
                        const auto cameraFraming = smokeCombatCameraDirector.Evaluate(''','main p700 camera')
# no torpedo acceptance final check applies only smokeTest already. capture enabled okay.
p.write_text(s)

# CI add separate Debug-only P700 smoke, using log markers; do not require BMP gate yet.
p=Path('.github/workflows/ci.yml'); s=p.read_text()
needle='''      - name: Retain M5 visual acceptance artifacts
        if: always()'''
step='''      - name: P-700 production launch smoke
        if: matrix.preset == 'windows-debug'
        shell: pwsh
        run: |
          $executable = ".\\build\\${{ matrix.preset }}\\${{ matrix.configuration }}\\DeepRun.exe"
          $output = & $executable --smoke-p700 2>&1 | Tee-Object -Variable p700Output
          $exitCode = $LASTEXITCODE
          $text = ($p700Output | Out-String)
          if ($exitCode -ne 0) { Write-Host $text; throw "P-700 smoke failed with exit code $exitCode" }
          foreach ($marker in @("HATCH_OPENING", "UNDERWATER_BOOSTER_EXIT", "WATER_EXIT", "HARDWARE_SEPARATION", "AERODYNAMIC_DEPLOY", "CRUISE", "TERMINAL", "PHYSICAL_IMPACT")) {
            if ($text -notmatch "\\[Game\\]\\[P700\\] $marker") { throw "P-700 smoke missed lifecycle marker $marker" }
          }
          if ($text -notmatch "PHYSICAL_IMPACT damage=100") { throw "P-700 smoke did not apply the accepted 100 HP direct hit" }

'''+needle
s=rep(s,needle,step,'ci p700 smoke')
p.write_text(s)

# Docs update concise contract
p=Path('docs/development/m5-p700-runtime.md'); s=p.read_text()
s=s.replace('Status: PRODUCTION RUNTIME INTEGRATED, CI PENDING.', 'Status: FULL LAUNCH/FLIGHT/TERMINAL SEQUENCE IMPLEMENTED, CI PENDING.')
s += '''\n## Final gameplay contract\n\nThe player launch sequence is now explicit and simulation-owned: selected paired production hatch opening -> attached launch-booster underwater exit -> physical water crossing -> bounded water-exit climb -> protective nose-cap and booster separation -> main-engine ignition -> `P700_Deploy` wing/tail articulation -> cruise -> terminal -> physical impact or terminal defeat. The canonical P-700 GLB owns the missile and booster geometry; the current source asset has no separately authored nose protection cap, so that cap is a clearly presentation-only fairing proxy until art authoring publishes a dedicated mesh. The selected Antey hatch is one of the twelve actual production hatch meshes, resolved through the launcher's semantic `hatchGroup`.\n\nCurrent explicit GAME POLICY values are `680 m/s` cruise and `750 m/s` terminal speed, `20..550 km` employment range, and `100 HP` direct impact with a `30 m` coarse explosion radius. These are gameplay tuning, not historical/classified exact performance claims.\n\nNormal-play terminal effectiveness uses deterministic-seeded independent failure opportunities: 4% base seeker failure plus up to 20 percentage points from perceived position uncertainty, 10% soft-kill, 16% hard-kill, and 4% maneuver defeat. A failure enters `Defeated` and cannot fabricate collision damage. A clean terminal solution still requires the real PhysicsWorld sweep to produce `Impact`. The dedicated acceptance scenario disables defensive defeat only so CI can prove the complete production launch-to-physical-impact path; a separate headless regression forces a 100% hard-kill profile and proves defeat-without-impact.\n'''
p.write_text(s)
print('P700 acceptance patch applied')
