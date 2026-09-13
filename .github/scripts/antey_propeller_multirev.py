from pathlib import Path

path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected exactly one patch match, got {count}: {old[:80]!r}")
    text = text.replace(old, new)


replace_once(
    "        bool capturedLater = false;\n        bool loggedHapticSubmissionFailure = false;",
    "        bool capturedLater = false;\n"
    "        const bool propellerVisualAcceptance = std::getenv(\"DR_PROPELLER_VISUAL_ACCEPTANCE\") != nullptr;\n"
    "        std::size_t propellerVisualCaptureIndex = 0;\n"
    "        constexpr std::array<double, 8> propellerVisualCaptureTimes{\n"
    "            8.00, 8.05, 8.10, 8.15, 8.20, 8.25, 8.30, 8.35};\n"
    "        bool loggedHapticSubmissionFailure = false;",
)

replace_once(
    "             &loggedCombatRuntime, &loggedCombatImpact](const float fixedDeltaSeconds)\n            {\n"
    "                const auto command = (options.smokeTest || options.p700SmokeTest || options.benchmarkM3)\n"
    "                                         ? std::expected<DeepRun::Game::VesselCommandState, std::string>{\n"
    "                                               DeepRun::Game::VesselCommandState{}}\n"
    "                                         : inputState != nullptr\n"
    "                                               ? DeepRun::Game::VesselCommandStateFromInput(*inputState)\n"
    "                                               : std::expected<DeepRun::Game::VesselCommandState, std::string>{\n"
    "                                                     std::unexpected(\"windowed input state is unavailable\")};",
    "             &loggedCombatRuntime, &loggedCombatImpact, &propellerVisualAcceptance](const float fixedDeltaSeconds)\n            {\n"
    "                const auto command = propellerVisualAcceptance\n"
    "                                         ? std::expected<DeepRun::Game::VesselCommandState, std::string>{\n"
    "                                               DeepRun::Game::VesselCommandState{.throttleFraction = 1.0F}}\n"
    "                                         : (options.smokeTest || options.p700SmokeTest || options.benchmarkM3)\n"
    "                                               ? std::expected<DeepRun::Game::VesselCommandState, std::string>{\n"
    "                                                     DeepRun::Game::VesselCommandState{}}\n"
    "                                               : inputState != nullptr\n"
    "                                                     ? DeepRun::Game::VesselCommandStateFromInput(*inputState)\n"
    "                                                     : std::expected<DeepRun::Game::VesselCommandState, std::string>{\n"
    "                                                           std::unexpected(\"windowed input state is unavailable\")};",
)

replace_once(
    "             &inputState, &frameCapture, &captureEnabled, &options,\n"
    "             &renderFrames, &capturedInitial, &capturedLater, &engineServices](DeepRun::Render::D3D12Renderer& renderer)",
    "             &inputState, &frameCapture, &captureEnabled, &options,\n"
    "             &renderFrames, &capturedInitial, &capturedLater, &engineServices,\n"
    "             &propellerVisualAcceptance, &propellerVisualCaptureIndex,\n"
    "             &propellerVisualCaptureTimes](DeepRun::Render::D3D12Renderer& renderer)",
)

replace_once(
    "                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())\n                {\n                    if (options.p700SmokeTest)",
    "                if (combatPlayground.has_value() && combatPlayground->Runtime().has_value())\n                {\n"
    "                    if (propellerVisualAcceptance)\n"
    "                    {\n"
    "                        if (!initialOwnshipNavigationPositionMeters.has_value() ||\n"
    "                            !currentOwnshipNavigationPositionMeters.has_value())\n"
    "                        {\n"
    "                            std::cerr << \"[Game][ERROR] Propeller visual acceptance ownship state unavailable\\n\";\n"
    "                            return false;\n"
    "                        }\n"
    "                        const float ownshipFollowX = currentOwnshipNavigationPositionMeters->x -\n"
    "                            initialOwnshipNavigationPositionMeters->x;\n"
    "                        const float ownshipFollowY = currentOwnshipNavigationPositionMeters->y -\n"
    "                            initialOwnshipNavigationPositionMeters->y;\n"
    "                        const auto closeUp = playground.SetPresentationCameraFraming(\n"
    "                            ownshipFollowX - 68.0F, ownshipFollowY + 1.0F, 52.0F, renderer.AspectRatio());\n"
    "                        if (!closeUp)\n"
    "                        {\n"
    "                            std::cerr << \"[Game][ERROR] Propeller close-up framing failed: \"\n"
    "                                      << closeUp.error() << '\\n';\n"
    "                            return false;\n"
    "                        }\n"
    "                    }\n"
    "                    else if (options.p700SmokeTest)",
)

replace_once(
    "                    if (!options.smokeTest && !options.p700SmokeTest && combatUiSnapshot.has_value())",
    "                    if (!propellerVisualAcceptance && !options.smokeTest && !options.p700SmokeTest && combatUiSnapshot.has_value())",
)

marker = "\n                if (options.benchmarkM3 && renderFrames == 0)\n"
if text.count(marker) != 1:
    raise RuntimeError("benchmark marker for acceptance capture insertion not found uniquely")

capture_block = r'''
                if (propellerVisualAcceptance && captureEnabled &&
                    propellerVisualCaptureIndex < propellerVisualCaptureTimes.size() &&
                    simulationTimeSeconds >= propellerVisualCaptureTimes[propellerVisualCaptureIndex])
                {
                    std::vector<std::byte> pixels;
                    std::uint32_t width = 0;
                    std::uint32_t height = 0;
                    const std::filesystem::path imagePath =
                        "antey-propeller-closeup-" + std::to_string(propellerVisualCaptureIndex) + ".bmp";
                    if (!frameCapture.Capture(pixels, width, height) || !WriteBmp(imagePath, pixels, width, height))
                    {
                        std::cerr << "[Game][ERROR] Propeller close-up frame capture failed at index "
                                  << propellerVisualCaptureIndex << '\n';
                        return false;
                    }
                    std::cout << "[Game][PropellerAcceptance] capture=" << propellerVisualCaptureIndex
                              << " simulation_time_s=" << simulationTimeSeconds
                              << " file=" << imagePath.string() << '\n';
                    ++propellerVisualCaptureIndex;
                    if (propellerVisualCaptureIndex == propellerVisualCaptureTimes.size())
                    {
                        std::cout << "[Game][PropellerAcceptance] PASS: close-up sequence captured after >15 shaft revolutions\n";
                        engineServices->RequestShutdown();
                    }
                }
'''

text = text.replace(marker, capture_block + marker)
path.write_text(text, encoding="utf-8")
