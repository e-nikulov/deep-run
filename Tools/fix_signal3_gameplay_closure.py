from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one patch marker, got {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new), encoding="utf-8")


def main() -> None:
    path = "Game/Combat/AnteyElectronicCombatRuntime.h"
    replace_once(
        path,
        """    [[nodiscard]] std::expected<std::string, std::string> OperateSelectedSystem(
        const float ownshipDepthMeters,
        const double simulationTimeSeconds)
    {
        // SIGNAL-3 has a gameplay optics profile, but the production SailDevice node is intentionally unresolved.
        // Do not create a virtual mast that can see while no physical presentation binding exists.
        if (state_.selectedSystem == AnteyElectronicSystem::Signal3NavigationPeriscope)
            return std::string(\"SIGNAL-3 unavailable: production SailDevice mapping is not confirmed yet\");
        return OperateAnteyElectronicSystem(config_, state_, ownshipDepthMeters, simulationTimeSeconds);
    }""",
        """    [[nodiscard]] std::expected<std::string, std::string> OperateSelectedSystem(
        const float ownshipDepthMeters,
        const double simulationTimeSeconds)
    {
        return OperateAnteyElectronicSystem(config_, state_, ownshipDepthMeters, simulationTimeSeconds);
    }""",
    )
    replace_once(
        path,
        """    [[nodiscard]] bool RkpRequested() const noexcept
    {
        return AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::RkpCompressorIntake);
    }

    [[nodiscard]] bool RadioTransmitting""",
        """    [[nodiscard]] bool RkpRequested() const noexcept
    {
        return AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::RkpCompressorIntake);
    }

    [[nodiscard]] bool Signal3Raised() const noexcept
    {
        return AnteyElectronicSystemDeployed(state_, AnteyElectronicSystem::Signal3NavigationPeriscope);
    }

    [[nodiscard]] bool RadioTransmitting""",
    )

    path = "Game/Combat/PeriscopeCombatRuntime.h"
    replace_once(
        path,
        """[[nodiscard]] inline std::expected<PlayerCombatCommandFeedback, std::string> VisualIdentifySelectedTrack(
    PeriscopeState& periscope,""",
        """[[nodiscard]] inline std::expected<PlayerCombatCommandFeedback, std::string> VisualIdentifySelectedTrackWithConfig(
    const PeriscopeObservationConfig& config,
    PeriscopeState& periscope,""",
    )
    replace_once(
        path,
        """    const auto observation = ObserveThroughPeriscope(
        PeriscopeObservationConfig{},
        periscope,""",
        """    const auto observation = ObserveThroughPeriscope(
        config,
        periscope,""",
    )
    replace_once(
        path,
        """            .message = ownshipDepthMeters > PeriscopeObservationConfig{}.maximumOperatingDepthMeters
                ? \"visual identification unavailable: ownship is too deep\"""",
        """            .message = ownshipDepthMeters > config.maximumOperatingDepthMeters
                ? \"visual identification unavailable: ownship is too deep\"""",
    )
    replace_once(
        path,
        """inline void ApplyPeriscopePresentation(
""",
        """[[nodiscard]] inline std::expected<PlayerCombatCommandFeedback, std::string> VisualIdentifySelectedTrack(
    PeriscopeState& periscope,
    Perception::TrackManager& tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float ownshipDepthMeters,
    const float surfaceLevelYMeters,
    const PeriscopeTargetTruth& targetTruth,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& opticalConditions = {})
{
    return VisualIdentifySelectedTrackWithConfig(
        PeriscopeObservationConfig{}, periscope, tracks, selectedTrackId, ownshipPositionMeters,
        ownshipDepthMeters, surfaceLevelYMeters, targetTruth, simulationTimeSeconds, opticalConditions);
}

inline void ApplyPeriscopePresentation(
""",
    )

    path = "Game/Combat/CombatPlaygroundRuntime.h"
    replace_once(
        path,
        """                    const bool unresolvedSignal3 =
                        electronics_.SelectedSystem() == AnteyElectronicSystem::Signal3NavigationPeriscope;
                    lastCombatCommand_ = PlayerCombatCommandFeedback{
                        .command = command.type, .accepted = mastAvailable && !unresolvedSignal3,
                        .trackId = playerCombat_.SelectedTrackId(), .message = *operated};""",
        """                    lastCombatCommand_ = PlayerCombatCommandFeedback{
                        .command = command.type, .accepted = mastAvailable,
                        .trackId = playerCombat_.SelectedTrackId(), .message = *operated};""",
    )
    replace_once(
        path,
        """                    const auto feedback = VisualIdentifySelectedTrack(
                        periscopeState_,
                        playerTracks_,
                        playerCombat_.SelectedTrackId(),
                        playerSnapshot.emitter.positionMeters,
                        playerSnapshot.signedDepthMeters,
                        surfaceLevelYMeters,
                        PeriscopeTargetTruth{
                            .positionMeters = truth->emitter.positionMeters,
                            .visualClassification = truth->visualClassification,
                            .visibleHeightAboveSurfaceMeters = truth->visibleHeightAboveSurfaceMeters},
                        simulationTimeSeconds,
                        periscopeOpticalConditions_);
                    if (!feedback)
                    {
                        return std::unexpected(\"periscope visual-identification command failed: \" + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;""",
        """                    const PeriscopeTargetTruth opticalTruth{
                        .positionMeters = truth->emitter.positionMeters,
                        .visualClassification = truth->visualClassification,
                        .visibleHeightAboveSurfaceMeters = truth->visibleHeightAboveSurfaceMeters};
                    std::expected<PlayerCombatCommandFeedback, std::string> feedback =
                        PlayerCombatCommandFeedback{
                            .command = PlayerCombatCommandType::VisualIdentify,
                            .accepted = false,
                            .trackId = playerCombat_.SelectedTrackId(),
                            .message = \"visual identification requires PZNS-10S or SIGNAL-3 to be raised\"};
                    if (periscopeState_.raised)
                    {
                        feedback = VisualIdentifySelectedTrack(
                            periscopeState_, playerTracks_, playerCombat_.SelectedTrackId(),
                            playerSnapshot.emitter.positionMeters, playerSnapshot.signedDepthMeters,
                            surfaceLevelYMeters, opticalTruth, simulationTimeSeconds, periscopeOpticalConditions_);
                    }
                    else if (electronics_.Signal3Raised())
                    {
                        PeriscopeState signal3{
                            .raised = true,
                            .viewBearingRadians = selectedTrack->estimatedBearingRadians};
                        feedback = VisualIdentifySelectedTrackWithConfig(
                            Signal3NavigationPeriscopeConfig(), signal3, playerTracks_, playerCombat_.SelectedTrackId(),
                            playerSnapshot.emitter.positionMeters, playerSnapshot.signedDepthMeters,
                            surfaceLevelYMeters, opticalTruth, simulationTimeSeconds, periscopeOpticalConditions_);
                        if (feedback)
                            feedback->message = \"SIGNAL-3 / \" + feedback->message;
                    }
                    if (!feedback)
                    {
                        return std::unexpected(\"optical visual-identification command failed: \" + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;""",
    )
    replace_once(
        path,
        """        ApplyPeriscopePresentation(playerCombatPresentation, periscopeState_, playerSnapshot.signedDepthMeters);
        const auto& electronicState = electronics_.State();""",
        """        ApplyPeriscopePresentation(playerCombatPresentation, periscopeState_, playerSnapshot.signedDepthMeters);
        if (electronics_.Signal3Raised() &&
            AnteyElectronicMastsAvailable(electronics_.Config(), playerSnapshot.signedDepthMeters) &&
            selectedPlayerTrack.has_value() &&
            selectedPlayerTrack->lifecycle != Perception::TrackLifecycleState::Lost)
        {
            playerCombatPresentation.canVisualIdentify = true;
        }
        const auto& electronicState = electronics_.State();""",
    )

    path = "DeepRun/Main.cpp"
    replace_once(
        path,
        """                        if (system == DeepRun::Game::Combat::AnteyElectronicSystem::Pzns10AttackPeriscope ||
                            system == DeepRun::Game::Combat::AnteyElectronicSystem::Signal3NavigationPeriscope)
                            continue;""",
        """                        if (system == DeepRun::Game::Combat::AnteyElectronicSystem::Pzns10AttackPeriscope)
                            continue;""",
    )

    path = "Tests/AnteyElectronicSuiteTest.cpp"
    replace_once(
        path,
        '#include "Game/Combat/AnteyElectronicSuite.h"',
        '#include "Game/Combat/AnteyElectronicCombatRuntime.h"\n#include "Game/Combat/AnteyElectronicSuite.h"',
    )
    replace_once(
        path,
        """    const auto navScope = Signal3NavigationPeriscopeConfig();
    Require(navScope.viewHalfAngleRadians > PeriscopeObservationConfig{}.viewHalfAngleRadians &&
            navScope.maximumTypeRecognitionRangeMeters < PeriscopeObservationConfig{}.maximumTypeRecognitionRangeMeters,
            \"SIGNAL-3 gameplay optic should be wider but weaker than the attack optic\");

    Input::GamepadState controls{};""",
        """    const auto navScope = Signal3NavigationPeriscopeConfig();
    Require(navScope.viewHalfAngleRadians > PeriscopeObservationConfig{}.viewHalfAngleRadians &&
            navScope.maximumTypeRecognitionRangeMeters < PeriscopeObservationConfig{}.maximumTypeRecognitionRangeMeters,
            \"SIGNAL-3 gameplay optic should be wider but weaker than the attack optic\");

    AnteyElectronicCombatRuntime electronics{200.0};
    for (std::size_t index = 0; index < AnteyElectronicSystemCount &&
         electronics.SelectedSystem() != AnteyElectronicSystem::Signal3NavigationPeriscope; ++index)
        electronics.CycleSelectedSystem();
    Require(electronics.SelectedSystem() == AnteyElectronicSystem::Signal3NavigationPeriscope,
            \"electronic-suite selection should reach SIGNAL-3\");
    const auto signal3Raised = electronics.OperateSelectedSystem(10.0F, 200.0);
    Require(signal3Raised.has_value() && electronics.Signal3Raised(),
            \"confirmed SailDevice11 SIGNAL-3 must raise inside the mast operating zone\");
    const auto signal3Stowed = electronics.OperateSelectedSystem(10.0F, 200.0);
    Require(signal3Stowed.has_value() && !electronics.Signal3Raised(),
            \"SIGNAL-3 second operation should stow the mast\");

    Input::GamepadState controls{};""",
    )


if __name__ == "__main__":
    main()
