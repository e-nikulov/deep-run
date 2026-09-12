from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# 1) Pure composition helper belongs to camera presentation, not simulation.
path = ROOT / 'Game/Camera/MultiScaleTacticalCamera.h'
text = path.read_text(encoding='utf-8')
old = '''[[nodiscard]] inline float ProjectedHorizontalPixels(
    const float worldWidthMeters,
    const float horizontalSpanMeters,
    const std::uint32_t viewportWidthPixels) noexcept
{
    if (!std::isfinite(worldWidthMeters) || !(worldWidthMeters > 0.0F) ||
        !std::isfinite(horizontalSpanMeters) || !(horizontalSpanMeters > 0.0F) || viewportWidthPixels == 0U)
    {
        return 0.0F;
    }
    return worldWidthMeters / horizontalSpanMeters * static_cast<float>(viewportWidthPixels);
}
'''
new = old + '''
// M5-V2-A: normal navigation follows the authoritative ownship position without moving simulation state.
// multiScaleRelativeOffsetMeters remains the player's presentation pan around ownship.
[[nodiscard]] inline std::expected<float, std::string> ComposeOwnshipFollowOffsetMeters(
    const float initialOwnshipXMeters,
    const float currentOwnshipXMeters,
    const float multiScaleRelativeOffsetMeters)
{
    if (!std::isfinite(initialOwnshipXMeters) || !std::isfinite(currentOwnshipXMeters) ||
        !std::isfinite(multiScaleRelativeOffsetMeters))
    {
        return std::unexpected("ownship-follow camera composition received non-finite input");
    }
    const float composed = currentOwnshipXMeters - initialOwnshipXMeters + multiScaleRelativeOffsetMeters;
    if (!std::isfinite(composed))
    {
        return std::unexpected("ownship-follow camera composition overflowed");
    }
    return composed;
}
'''
text = replace_once(text, old, new, 'add ownship follow composition helper')
path.write_text(text, encoding='utf-8')

# 2) Integrate live ownship position into normal windowed camera framing.
path = ROOT / 'DeepRun/Main.cpp'
text = path.read_text(encoding='utf-8')
old = '''        std::optional<DeepRun::Game::Combat::M5CombatVisualAcceptance> combatAcceptance;
        std::optional<DeepRun::Game::Combat::PlayerCombatPresentationSnapshot> combatUiSnapshot;
        DeepRun::Game::Combat::CombatPlaygroundCameraDirector smokeCombatCameraDirector;
'''
new = '''        std::optional<DeepRun::Game::Combat::M5CombatVisualAcceptance> combatAcceptance;
        std::optional<DeepRun::Game::Combat::PlayerCombatPresentationSnapshot> combatUiSnapshot;
        std::optional<DeepRun::Physics::PhysicsVector3> initialOwnshipNavigationPositionMeters;
        std::optional<DeepRun::Physics::PhysicsVector3> currentOwnshipNavigationPositionMeters;
        DeepRun::Game::Combat::CombatPlaygroundCameraDirector smokeCombatCameraDirector;
'''
text = replace_once(text, old, new, 'add navigation position state')

old = '''            [&options, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,
             &inputState, &engineServices](DeepRun::Core::Engine& engine)
'''
new = '''            [&options, &playground, &acousticPlaygroundRuntime, &combatPlayground, &multiScaleCamera,
             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,
             &inputState, &engineServices](DeepRun::Core::Engine& engine)
'''
text = replace_once(text, old, new, 'capture navigation state in initialize callback')

old = '''                const auto initialized = playground.Initialize(engine.Assets(), *physics, *renderer, options.smokeTest);
                if (!initialized)
                {
                    std::cerr << "[Game][ERROR] " << initialized.error() << '\\n';
                    return false;
                }

                const auto acousticRuntime = DeepRun::Game::AcousticPlaygroundRuntime::Create();
'''
new = '''                const auto initialized = playground.Initialize(engine.Assets(), *physics, *renderer, options.smokeTest);
                if (!initialized)
                {
                    std::cerr << "[Game][ERROR] " << initialized.error() << '\\n';
                    return false;
                }
                if (!options.benchmarkM3)
                {
                    const auto initialOwnship = playground.BuildPhysicalCollisionProxySnapshot();
                    if (!initialOwnship)
                    {
                        std::cerr << "[Game][ERROR] M5-V2 initial ownship navigation snapshot failed: "
                                  << initialOwnship.error() << '\\n';
                        return false;
                    }
                    initialOwnshipNavigationPositionMeters = initialOwnship->positionMeters;
                    currentOwnshipNavigationPositionMeters = initialOwnship->positionMeters;
                }

                const auto acousticRuntime = DeepRun::Game::AcousticPlaygroundRuntime::Create();
'''
text = replace_once(text, old, new, 'capture initial ownship position')

old = '''            [&options, &playground, &hapticFeedback, &acousticPlaygroundRuntime, &combatPlayground,
             &combatAcceptance, &combatUiSnapshot, &inputState, &engineServices,
'''
new = '''            [&options, &playground, &hapticFeedback, &acousticPlaygroundRuntime, &combatPlayground,
             &combatAcceptance, &combatUiSnapshot, &currentOwnshipNavigationPositionMeters, &inputState, &engineServices,
'''
text = replace_once(text, old, new, 'capture current ownship in fixed callback')

old = '''                    const auto playerCollisionProxy = playground.BuildPhysicalCollisionProxySnapshot();
                    if (!playerCollisionProxy)
                    {
                        std::cerr << "[Game][ERROR] M5-I.2 player collision proxy snapshot failed: "
                                  << playerCollisionProxy.error() << '\\n';
                        return false;
                    }

                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 5> playerCommands{};
'''
new = '''                    const auto playerCollisionProxy = playground.BuildPhysicalCollisionProxySnapshot();
                    if (!playerCollisionProxy)
                    {
                        std::cerr << "[Game][ERROR] M5-I.2 player collision proxy snapshot failed: "
                                  << playerCollisionProxy.error() << '\\n';
                        return false;
                    }
                    currentOwnshipNavigationPositionMeters = playerCollisionProxy->positionMeters;

                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 5> playerCommands{};
'''
text = replace_once(text, old, new, 'refresh current ownship position')

old = '''            [&playground, &combatPlayground, &combatAcceptance, &combatUiSnapshot, &smokeCombatCameraDirector,
             &calmLaunchCameraAssist, &multiScaleCamera, &inputState, &frameCapture, &captureEnabled, &options,
'''
new = '''            [&playground, &combatPlayground, &combatAcceptance, &combatUiSnapshot, &smokeCombatCameraDirector,
             &calmLaunchCameraAssist, &multiScaleCamera,
             &initialOwnshipNavigationPositionMeters, &currentOwnshipNavigationPositionMeters,
             &inputState, &frameCapture, &captureEnabled, &options,
'''
text = replace_once(text, old, new, 'capture navigation positions in render callback')

old = '''                        const auto appliedFraming = playground.SetPresentationCameraFraming(
                            cameraFraming->targetOffsetXMeters,
                            cameraFraming->targetOffsetYMeters,
                            cameraFraming->horizontalSpanMeters, renderer.AspectRatio());
'''
new = '''                        if (!initialOwnshipNavigationPositionMeters.has_value() ||
                            !currentOwnshipNavigationPositionMeters.has_value())
                        {
                            std::cerr << "[Game][ERROR] M5-V2 ownship navigation state is unavailable\\n";
                            return false;
                        }
                        const auto ownshipFollowOffset = DeepRun::Game::Camera::ComposeOwnshipFollowOffsetMeters(
                            initialOwnshipNavigationPositionMeters->x,
                            currentOwnshipNavigationPositionMeters->x,
                            cameraFraming->targetOffsetXMeters);
                        if (!ownshipFollowOffset)
                        {
                            std::cerr << "[Game][ERROR] M5-V2 ownship camera follow failed: "
                                      << ownshipFollowOffset.error() << '\\n';
                            return false;
                        }
                        const auto appliedFraming = playground.SetPresentationCameraFraming(
                            *ownshipFollowOffset,
                            cameraFraming->targetOffsetYMeters,
                            cameraFraming->horizontalSpanMeters, renderer.AspectRatio());
'''
text = replace_once(text, old, new, 'compose ownship follow into camera framing')
path.write_text(text, encoding='utf-8')

# 3) Regression: camera pan remains relative to physical ownship displacement.
path = ROOT / 'Tests/M5MultiScaleCameraChecks.h'
text = path.read_text(encoding='utf-8')
old = '''    MultiScaleTacticalCamera camera;
'''
new = '''    const auto followForward = ComposeOwnshipFollowOffsetMeters(100.0F, 750.0F, 25.0F);
    const auto followAstern = ComposeOwnshipFollowOffsetMeters(100.0F, -50.0F, -20.0F);
    if (!followForward || !followAstern || std::abs(*followForward - 675.0F) > 0.001F ||
        std::abs(*followAstern + 170.0F) > 0.001F)
    {
        return false;
    }

    MultiScaleTacticalCamera camera;
'''
text = replace_once(text, old, new, 'add ownship follow regression')
path.write_text(text, encoding='utf-8')
