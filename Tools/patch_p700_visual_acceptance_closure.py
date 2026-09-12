from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


runtime = Path("Game/Combat/CombatPlaygroundRuntime.h")
s = runtime.read_text(encoding="utf-8")
s = replace_once(
    s,
    "inline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;\n",
    "inline constexpr double M5CombatIncomingThreatEmissionSampleIntervalSeconds = 0.10;\n"
    "// M5/P-700 fire-control ranging profile. GAME POLICY only: the generic M4 sonar defaults remain unchanged.\n"
    "// At the canonical 20.1 km acceptance range this profile yields ~405 m positional uncertainty, preserving\n"
    "// the P-700 <=500 m Track-quality gate without fabricating target state or weakening weapon requirements.\n"
    "inline constexpr Acoustics::ActiveSonarConfig M5CombatPlayerFireControlActiveSonarConfig{\n"
    "    .bearingUncertaintyRadians = 0.0174532925F, // 1 degree\n"
    "    .minimumRangeUncertaintyMeters = 5.0F,\n"
    "    .fractionalRangeUncertainty = 0.01F};\n",
    "player fire-control sonar profile",
)
s = replace_once(
    s,
    "            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(\n"
    "                acousticWorld_, *activePulse_, *activeReflector_, activeReceiver, simulationTimeSeconds);",
    "            const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(\n"
    "                acousticWorld_, *activePulse_, *activeReflector_, activeReceiver, simulationTimeSeconds,\n"
    "                {}, {}, M5CombatPlayerFireControlActiveSonarConfig);",
    "player active echo fire-control config",
)
runtime.write_text(s, encoding="utf-8")


test = Path("Tests/M5P700Checks.h")
s = test.read_text(encoding="utf-8")
s = replace_once(
    s,
    '#include "Engine/Diagnostics/Logger.h"\n',
    '#include "Engine/Diagnostics/Logger.h"\n#include "Game/Combat/CombatPlaygroundRuntime.h"\n',
    "P700 test combat runtime include",
)
s = replace_once(
    s,
    "    if (!ValidateP700GranitDefinition(definition))\n"
    "    {\n"
    "        return fail(\"definition validation\");\n"
    "    }\n\n",
    "    if (!ValidateP700GranitDefinition(definition))\n"
    "    {\n"
    "        return fail(\"definition validation\");\n"
    "    }\n\n"
    "    constexpr float acceptanceRangeMeters = 20'100.0F;\n"
    "    const auto& fireControl = Game::Combat::M5CombatPlayerFireControlActiveSonarConfig;\n"
    "    const float lateralUncertaintyMeters =\n"
    "        acceptanceRangeMeters * fireControl.bearingUncertaintyRadians;\n"
    "    const float rangeUncertaintyMeters = (std::max)(\n"
    "        fireControl.minimumRangeUncertaintyMeters,\n"
    "        acceptanceRangeMeters * fireControl.fractionalRangeUncertainty);\n"
    "    const float fireControlPositionUncertaintyMeters = static_cast<float>(std::hypot(\n"
    "        static_cast<double>(lateralUncertaintyMeters), static_cast<double>(rangeUncertaintyMeters)));\n"
    "    if (fireControlPositionUncertaintyMeters > definition.weapon.targeting.maximumPositionUncertaintyMeters)\n"
    "    {\n"
    "        return fail(\"20.1 km fire-control sonar must satisfy the P-700 spatial Track-quality gate\");\n"
    "    }\n\n",
    "P700 fire-control sonar uncertainty check",
)
test.write_text(s, encoding="utf-8")


acceptance = Path("Game/Combat/P700VisualAcceptance.h")
acceptance.write_text(r'''#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Combat
{
enum class M5P700AcceptanceCheckpoint
{
    Launch,
    WaterExit,
    Deploy,
    CruiseTerminal,
    Impact,
};

[[nodiscard]] inline const char* M5P700AcceptanceCheckpointName(const M5P700AcceptanceCheckpoint checkpoint) noexcept
{
    switch (checkpoint)
    {
    case M5P700AcceptanceCheckpoint::Launch: return "M5_P700_LAUNCH";
    case M5P700AcceptanceCheckpoint::WaterExit: return "M5_P700_WATER_EXIT";
    case M5P700AcceptanceCheckpoint::Deploy: return "M5_P700_DEPLOY";
    case M5P700AcceptanceCheckpoint::CruiseTerminal: return "M5_P700_CRUISE_TERMINAL";
    case M5P700AcceptanceCheckpoint::Impact: return "M5_P700_IMPACT";
    }
    return "M5_P700_UNKNOWN";
}

[[nodiscard]] inline const char* M5P700PhaseName(const Weapons::P700GranitPhase phase) noexcept
{
    switch (phase)
    {
    case Weapons::P700GranitPhase::Stored: return "Stored";
    case Weapons::P700GranitPhase::HatchOpening: return "HatchOpening";
    case Weapons::P700GranitPhase::UnderwaterLaunch: return "UnderwaterLaunch";
    case Weapons::P700GranitPhase::WaterExit: return "WaterExit";
    case Weapons::P700GranitPhase::PostExitTransition: return "PostExitTransition";
    case Weapons::P700GranitPhase::AirborneDeploying: return "AirborneDeploying";
    case Weapons::P700GranitPhase::Cruise: return "Cruise";
    case Weapons::P700GranitPhase::Terminal: return "Terminal";
    case Weapons::P700GranitPhase::Impact: return "Impact";
    case Weapons::P700GranitPhase::Spent: return "Spent";
    }
    return "Unknown";
}

struct M5P700AcceptanceSnapshot final
{
    double simulationTimeSeconds = 0.0;
    Physics::PhysicsVector3 missilePositionMeters{};
    Weapons::P700GranitPhase phase = Weapons::P700GranitPhase::Stored;
    float hatchOpenProgress = 0.0F;
    float deploymentProgress = 0.0F;
    std::size_t p700LoadedCount = 0U;
    float destroyerIntegrity = 0.0F;
    bool hasImpact = false;
    bool impactBodyHandleValid = false;
    bool impactTargetIsDestroyer = false;
    float impactDamage = 0.0F;
    std::optional<Physics::PhysicsVector3> impactPositionMeters{};
    std::optional<Physics::PhysicsVector3> explosionPositionMeters{};
    std::uint32_t combatDrawCalls = 0U;
    std::uint32_t combatSubmittedPrimitives = 0U;
    std::uint64_t combatSubmittedIndices = 0U;
    float cameraAspectRatio = 0.0F;
    float cameraHorizontalSpanMeters = 0.0F;
    bool gpuPresentationHandleValid = false;
};

struct M5P700AcceptanceRecord final
{
    M5P700AcceptanceCheckpoint checkpoint = M5P700AcceptanceCheckpoint::Launch;
    M5P700AcceptanceSnapshot state{};
    bool imageCaptured = false;
    std::string imagePath{};
};

class M5P700VisualAcceptance final
{
public:
    [[nodiscard]] std::expected<std::optional<M5P700AcceptanceCheckpoint>, std::string> ObserveFixed(
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const CombatPlaygroundFrame& frame,
        const double simulationTimeSeconds)
    {
        if (!physicsWorld.IsInitialized() || !std::isfinite(simulationTimeSeconds) ||
            simulationTimeSeconds < lastSimulationTimeSeconds_)
        {
            return std::unexpected("P-700 visual acceptance received invalid fixed-step input");
        }

        M5P700AcceptanceSnapshot snapshot{.simulationTimeSeconds = simulationTimeSeconds};
        const auto destroyerBody = physicsWorld.GetBodyState(runtime.Destroyer().body);
        if (!destroyerBody || !destroyerBody->position.IsFinite() ||
            !std::isfinite(runtime.Destroyer().integrity.remainingIntegrity))
        {
            return std::unexpected("P-700 visual acceptance could not read destroyer authority");
        }
        snapshot.destroyerIntegrity = runtime.Destroyer().integrity.remainingIntegrity;
        snapshot.p700LoadedCount = runtime.P700Launchers() ? runtime.P700Launchers()->LoadedCount() : 0U;

        if (runtime.PlayerP700().has_value())
        {
            const auto& missile = *runtime.PlayerP700();
            if (!missile.positionMeters.IsFinite() || !std::isfinite(missile.hatchOpenProgress) ||
                !std::isfinite(missile.deploymentProgress))
            {
                return std::unexpected("P-700 visual acceptance missile state is invalid");
            }
            snapshot.missilePositionMeters = missile.positionMeters;
            snapshot.phase = missile.phase;
            snapshot.hatchOpenProgress = missile.hatchOpenProgress;
            snapshot.deploymentProgress = missile.deploymentProgress;
        }

        if (frame.playerP700Impact.has_value())
        {
            const auto& impact = *frame.playerP700Impact;
            if (!impact.physicsHit.body.IsValid() || impact.physicsHit.body != runtime.Destroyer().body ||
                !impact.physicsHit.positionMeters.IsFinite() || !impact.explosion.positionMeters.IsFinite() ||
                std::abs(impact.damage.damage - 100.0F) > 0.001F)
            {
                return std::unexpected("P-700 visual acceptance impact failed physical authority contract");
            }
            snapshot.hasImpact = true;
            snapshot.impactBodyHandleValid = impact.physicsHit.body.IsValid();
            snapshot.impactTargetIsDestroyer = impact.physicsHit.body == runtime.Destroyer().body;
            snapshot.impactDamage = impact.damage.damage;
            snapshot.impactPositionMeters = impact.physicsHit.positionMeters;
            snapshot.explosionPositionMeters = impact.explosion.positionMeters;
        }
        else if (runtime.LastExplosion().has_value())
        {
            snapshot.explosionPositionMeters = runtime.LastExplosion()->positionMeters;
        }

        std::optional<M5P700AcceptanceCheckpoint> newlyObserved{};
        const auto mark = [this, &snapshot, &newlyObserved](const M5P700AcceptanceCheckpoint checkpoint)
        {
            const std::size_t index = CheckpointIndex(checkpoint);
            if (!records_[index].has_value())
            {
                records_[index] = M5P700AcceptanceRecord{.checkpoint = checkpoint, .state = snapshot};
                newlyObserved = checkpoint;
            }
        };

        if (runtime.PlayerP700().has_value())
        {
            const auto& missile = *runtime.PlayerP700();
            if ((missile.phase == Weapons::P700GranitPhase::HatchOpening && missile.hatchOpenProgress > 0.0F) ||
                missile.phase == Weapons::P700GranitPhase::UnderwaterLaunch)
            {
                mark(M5P700AcceptanceCheckpoint::Launch);
            }
            if (missile.phase == Weapons::P700GranitPhase::WaterExit)
            {
                mark(M5P700AcceptanceCheckpoint::WaterExit);
            }
            if (missile.phase == Weapons::P700GranitPhase::AirborneDeploying &&
                missile.deploymentProgress >= 0.25F && missile.deploymentProgress <= 0.75F)
            {
                mark(M5P700AcceptanceCheckpoint::Deploy);
            }
            if ((missile.phase == Weapons::P700GranitPhase::Cruise ||
                 missile.phase == Weapons::P700GranitPhase::Terminal) &&
                missile.deploymentProgress >= 0.999F)
            {
                mark(M5P700AcceptanceCheckpoint::CruiseTerminal);
            }
        }
        if (snapshot.hasImpact)
        {
            if (snapshot.p700LoadedCount != 23U ||
                snapshot.destroyerIntegrity >= runtime.Destroyer().integrity.maximumIntegrity)
            {
                return std::unexpected("P-700 impact did not consume exactly one launcher or reduce destroyer integrity");
            }
            mark(M5P700AcceptanceCheckpoint::Impact);
        }
        lastSimulationTimeSeconds_ = simulationTimeSeconds;
        return newlyObserved;
    }

    [[nodiscard]] std::expected<std::optional<M5P700AcceptanceCheckpoint>, std::string> ObserveRender(
        const CombatPlaygroundPresentationSnapshot& presentation,
        const Render::Camera& camera,
        const Render::ModelDrawStatistics& combatDrawStats,
        const float rendererAspectRatio,
        const bool gpuPresentationHandleValid,
        const bool explosionDrawn)
    {
        if (!Render::IsFinite(camera.view) || !Render::IsFinite(camera.projection) ||
            !Render::IsFinite(camera.viewProjection) || !std::isfinite(camera.width) || camera.width <= 0.0F ||
            !std::isfinite(rendererAspectRatio) || rendererAspectRatio <= 0.0F || !gpuPresentationHandleValid ||
            combatDrawStats.drawCalls < 2U || combatDrawStats.submittedPrimitives != combatDrawStats.drawCalls ||
            combatDrawStats.submittedIndices < 72U)
        {
            return std::unexpected("P-700 visual acceptance render/camera/GPU contract failed");
        }
        if (camera.width < 850.0F || camera.width > 1'850.0F)
        {
            return std::unexpected("P-700 acceptance camera left the bounded 0.9-1.8 km presentation scale");
        }

        for (std::size_t index = 0; index < records_.size(); ++index)
        {
            if (!records_[index].has_value() || captureIssued_[index])
                continue;
            const auto checkpoint = records_[index]->checkpoint;
            if (!PresentationMatches(checkpoint, presentation, explosionDrawn))
                continue;

            if (presentation.playerP700.has_value() && checkpoint != M5P700AcceptanceCheckpoint::Impact &&
                std::abs(camera.target.x - presentation.playerP700->positionMeters.x) > 5.0F)
            {
                return std::unexpected("P-700 acceptance camera is not following the production missile presentation");
            }

            auto& state = records_[index]->state;
            state.combatDrawCalls = combatDrawStats.drawCalls;
            state.combatSubmittedPrimitives = combatDrawStats.submittedPrimitives;
            state.combatSubmittedIndices = combatDrawStats.submittedIndices;
            state.cameraAspectRatio = rendererAspectRatio;
            state.cameraHorizontalSpanMeters = camera.width;
            state.gpuPresentationHandleValid = gpuPresentationHandleValid;

            if (!presentedFrameArmed_[index])
            {
                presentedFrameArmed_[index] = true;
                return std::optional<M5P700AcceptanceCheckpoint>{};
            }
            captureIssued_[index] = true;
            return std::optional<M5P700AcceptanceCheckpoint>{checkpoint};
        }
        return std::optional<M5P700AcceptanceCheckpoint>{};
    }

    void MarkImageCaptured(const M5P700AcceptanceCheckpoint checkpoint, std::string path)
    {
        auto& record = records_[CheckpointIndex(checkpoint)];
        if (record.has_value())
        {
            record->imageCaptured = true;
            record->imagePath = std::move(path);
        }
    }

    [[nodiscard]] bool AllStateCheckpointsSeen() const noexcept
    {
        for (const auto& record : records_)
        {
            if (!record.has_value())
                return false;
        }
        const auto& impact = records_[CheckpointIndex(M5P700AcceptanceCheckpoint::Impact)]->state;
        return impact.hasImpact && impact.impactBodyHandleValid && impact.impactTargetIsDestroyer &&
               std::abs(impact.impactDamage - 100.0F) <= 0.001F && impact.p700LoadedCount == 23U;
    }

    [[nodiscard]] const std::array<std::optional<M5P700AcceptanceRecord>, 5>& Records() const noexcept
    {
        return records_;
    }

    [[nodiscard]] static constexpr std::size_t CheckpointIndex(const M5P700AcceptanceCheckpoint checkpoint) noexcept
    {
        return static_cast<std::size_t>(checkpoint);
    }

private:
    [[nodiscard]] static bool PresentationMatches(
        const M5P700AcceptanceCheckpoint checkpoint,
        const CombatPlaygroundPresentationSnapshot& presentation,
        const bool explosionDrawn) noexcept
    {
        const auto& missile = presentation.playerP700;
        switch (checkpoint)
        {
        case M5P700AcceptanceCheckpoint::Launch:
            return missile.has_value() &&
                (missile->phase == Weapons::P700GranitPhase::HatchOpening ||
                 missile->phase == Weapons::P700GranitPhase::UnderwaterLaunch);
        case M5P700AcceptanceCheckpoint::WaterExit:
            return missile.has_value() &&
                (missile->phase == Weapons::P700GranitPhase::WaterExit ||
                 missile->phase == Weapons::P700GranitPhase::PostExitTransition);
        case M5P700AcceptanceCheckpoint::Deploy:
            return missile.has_value() && missile->phase == Weapons::P700GranitPhase::AirborneDeploying;
        case M5P700AcceptanceCheckpoint::CruiseTerminal:
            return missile.has_value() &&
                (missile->phase == Weapons::P700GranitPhase::Cruise ||
                 missile->phase == Weapons::P700GranitPhase::Terminal);
        case M5P700AcceptanceCheckpoint::Impact:
            return presentation.explosion.has_value() && explosionDrawn && presentation.destroyerIntegrityFraction < 1.0F;
        }
        return false;
    }

    double lastSimulationTimeSeconds_ = 0.0;
    std::array<std::optional<M5P700AcceptanceRecord>, 5> records_{};
    std::array<bool, 5> presentedFrameArmed_{};
    std::array<bool, 5> captureIssued_{};
};
} // namespace DeepRun::Game::Combat
''', encoding="utf-8")


main = Path("DeepRun/Main.cpp")
s = main.read_text(encoding="utf-8")
s = replace_once(
    s,
    '#include "Game/Combat/CombatPlaygroundWindowedComposition.h"\n',
    '#include "Game/Combat/CombatPlaygroundWindowedComposition.h"\n#include "Game/Combat/P700VisualAcceptance.h"\n',
    "Main P700 acceptance include",
)
report_fn = r'''

bool WriteM5P700AcceptanceReport(
    const std::filesystem::path& path,
    const DeepRun::Game::Combat::M5P700VisualAcceptance& acceptance)
{
    nlohmann::json report;
    report["schema"] = "deeprun.m5.p700.visual-acceptance.v1";
    report["all_state_checkpoints_seen"] = acceptance.AllStateCheckpointsSeen();
    report["checkpoints"] = nlohmann::json::array();
    for (const auto& record : acceptance.Records())
    {
        if (!record.has_value())
            continue;
        const auto& state = record->state;
        nlohmann::json entry{
            {"name", DeepRun::Game::Combat::M5P700AcceptanceCheckpointName(record->checkpoint)},
            {"simulation_time_seconds", state.simulationTimeSeconds},
            {"phase", DeepRun::Game::Combat::M5P700PhaseName(state.phase)},
            {"missile_position_meters", JsonVector(state.missilePositionMeters)},
            {"hatch_open_progress", state.hatchOpenProgress},
            {"deployment_progress", state.deploymentProgress},
            {"p700_loaded_count", state.p700LoadedCount},
            {"destroyer_integrity", state.destroyerIntegrity},
            {"combat_draw_calls", state.combatDrawCalls},
            {"combat_submitted_primitives", state.combatSubmittedPrimitives},
            {"combat_submitted_indices", state.combatSubmittedIndices},
            {"camera_aspect_ratio", state.cameraAspectRatio},
            {"camera_horizontal_span_meters", state.cameraHorizontalSpanMeters},
            {"gpu_presentation_handle_valid", state.gpuPresentationHandleValid},
            {"image_captured", record->imageCaptured},
            {"image_path", record->imagePath}};
        entry["impact"] = {
            {"present", state.hasImpact},
            {"body_handle_valid", state.impactBodyHandleValid},
            {"target_is_destroyer_body", state.impactTargetIsDestroyer},
            {"damage", state.impactDamage}};
        if (state.impactPositionMeters.has_value())
            entry["impact"]["position_meters"] = JsonVector(*state.impactPositionMeters);
        if (state.explosionPositionMeters.has_value())
            entry["explosion_position_meters"] = JsonVector(*state.explosionPositionMeters);
        else
            entry["explosion_position_meters"] = nullptr;
        report["checkpoints"].push_back(std::move(entry));
    }
    std::ofstream output(path);
    if (!output)
        return false;
    output << report.dump(2) << '\n';
    return static_cast<bool>(output);
}
'''
s = replace_once(s, "}\n} // namespace\n\nint main", "}" + report_fn + "\n} // namespace\n\nint main", "P700 report writer")
s = replace_once(
    s,
    "        std::optional<DeepRun::Game::Combat::M5CombatVisualAcceptance> combatAcceptance;\n",
    "        std::optional<DeepRun::Game::Combat::M5CombatVisualAcceptance> combatAcceptance;\n"
    "        std::optional<DeepRun::Game::Combat::M5P700VisualAcceptance> p700Acceptance;\n"
    "        std::array<bool, 8> p700LifecycleLogged{};\n",
    "P700 acceptance state",
)
s = replace_once(
    s,
    "             &combatAcceptance, &combatUiSnapshot, &currentOwnshipNavigationPositionMeters, &inputState, &engineServices,\n",
    "             &combatAcceptance, &p700Acceptance, &p700LifecycleLogged, &combatUiSnapshot,\n"
    "             &currentOwnshipNavigationPositionMeters, &inputState, &engineServices,\n",
    "fixed lambda P700 captures",
)
old_block = '''                    if (options.p700SmokeTest && combatPlayground->Runtime().has_value())
                    {
                        const auto& runtime = *combatPlayground->Runtime();
                        if (runtime.PlayerP700().has_value())
                        {
                            const auto& missile = *runtime.PlayerP700();
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::HatchOpening && missile.hatchOpenProgress > 0.0F)
                                std::cout << "[Game][P700] HATCH_OPENING progress=" << missile.hatchOpenProgress << '\\n';
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::UnderwaterLaunch)
                                std::cout << "[Game][P700] UNDERWATER_BOOSTER_EXIT\\n";
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::WaterExit)
                                std::cout << "[Game][P700] WATER_EXIT\\n";
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
                                      << combatFrame->playerP700Impact->explosion.radiusMeters << "\\n";
                            engineServices->RequestShutdown();
                        }
                        if (simulationTimeSeconds > 75.0)
                        {
                            std::cerr << "[Game][ERROR] P-700 acceptance exceeded 75 s SimulationTime without impact\\n";
                            return false;
                        }
                    }
'''
new_block = '''                    if (options.p700SmokeTest && combatPlayground->Runtime().has_value())
                    {
                        const auto& runtime = *combatPlayground->Runtime();
                        if (!p700Acceptance.has_value())
                            p700Acceptance.emplace();
                        const auto accepted = p700Acceptance->ObserveFixed(
                            runtime, *physics, *combatFrame, simulationTimeSeconds);
                        if (!accepted)
                        {
                            std::cerr << "[Game][ERROR] " << accepted.error() << '\\n';
                            return false;
                        }
                        if (runtime.PlayerP700().has_value())
                        {
                            const auto& missile = *runtime.PlayerP700();
                            const auto logOnce = [&p700LifecycleLogged](const std::size_t index, const std::string& text)
                            {
                                if (!p700LifecycleLogged[index])
                                {
                                    p700LifecycleLogged[index] = true;
                                    std::cout << text << '\\n';
                                }
                            };
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::HatchOpening && missile.hatchOpenProgress > 0.0F)
                                logOnce(0U, "[Game][P700] HATCH_OPENING progress=" + std::to_string(missile.hatchOpenProgress));
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::UnderwaterLaunch)
                                logOnce(1U, "[Game][P700] UNDERWATER_BOOSTER_EXIT");
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::WaterExit)
                                logOnce(2U, "[Game][P700] WATER_EXIT");
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::PostExitTransition)
                                logOnce(3U, "[Game][P700] HARDWARE_SEPARATION progress=" + std::to_string(missile.postExitTransitionProgress));
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::AirborneDeploying)
                                logOnce(4U, "[Game][P700] AERODYNAMIC_DEPLOY progress=" + std::to_string(missile.deploymentProgress));
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Cruise)
                                logOnce(5U, "[Game][P700] CRUISE speed_mps=" + std::to_string(missile.speedMetersPerSecond));
                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal)
                                logOnce(6U, "[Game][P700] TERMINAL speed_mps=" + std::to_string(missile.speedMetersPerSecond));
                        }
                        if (combatFrame->playerP700Impact.has_value() && !p700LifecycleLogged[7])
                        {
                            p700LifecycleLogged[7] = true;
                            std::cout << "[Game][P700] PHYSICAL_IMPACT damage="
                                      << combatFrame->playerP700Impact->damage.damage << " radius="
                                      << combatFrame->playerP700Impact->explosion.radiusMeters << "\\n";
                        }
                        if (simulationTimeSeconds > 95.0)
                        {
                            std::cerr << "[Game][ERROR] P-700 acceptance exceeded 95 s SimulationTime without completed impact capture\\n";
                            return false;
                        }
                    }
'''
s = replace_once(s, old_block, new_block, "P700 fixed acceptance block")
s = replace_once(
    s,
    "             &playground, &combatPlayground, &combatAcceptance, &combatUiSnapshot, &smokeCombatCameraDirector,\n",
    "             &playground, &combatPlayground, &combatAcceptance, &p700Acceptance, &combatUiSnapshot, &smokeCombatCameraDirector,\n",
    "render lambda P700 acceptance capture",
)
s = replace_once(
    s,
    "                        float spanMeters = 900.0F;\n"
    "                        if (combatPlayground->Runtime()->PlayerP700().has_value() && initialOwnshipNavigationPositionMeters.has_value())\n"
    "                        {\n"
    "                            const auto& missile = *combatPlayground->Runtime()->PlayerP700();\n"
    "                            targetOffsetXMeters = missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x;\n"
    "                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Cruise) spanMeters = 4'000.0F;\n"
    "                            if (missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal) spanMeters = 7'000.0F;\n"
    "                        }",
    "                        float spanMeters = 900.0F;\n"
    "                        if (combatPlayground->Runtime()->PlayerP700().has_value() && initialOwnshipNavigationPositionMeters.has_value())\n"
    "                        {\n"
    "                            const auto& missile = *combatPlayground->Runtime()->PlayerP700();\n"
    "                            targetOffsetXMeters = missile.positionMeters.x - initialOwnshipNavigationPositionMeters->x;\n"
    "                            spanMeters = missile.phase == DeepRun::Weapons::P700GranitPhase::Terminal ? 1'800.0F : 1'200.0F;\n"
    "                        }",
    "P700 visual follow camera scale",
)
s = replace_once(
    s,
    "                    if (!options.smokeTest && combatUiSnapshot.has_value())\n",
    "                    if (!options.smokeTest && !options.p700SmokeTest && combatUiSnapshot.has_value())\n",
    "suppress normal combat HUD during P700 acceptance",
)
insert_after = '''                    if (combatAcceptance.has_value())
                    {
                        const auto acceptanceRendered = combatAcceptance->ObserveRender(
                            *combatPlayground->Runtime(), *physics, *camera, combatRendered->stats,
                            renderer.AspectRatio(), combatPlayground->PresentationModelValid(renderer),
                            combatRendered->presentation, combatRendered->explosionDrawn);
                        if (!acceptanceRendered)
                        {
                            std::cerr << "[Game][ERROR] " << acceptanceRendered.error() << '\\n';
                            return false;
                        }
                        if (acceptanceRendered->has_value())
                        {
                            const auto checkpoint = acceptanceRendered->value().checkpoint;
                            const std::filesystem::path imagePath = [&checkpoint]() {
                                switch (checkpoint)
                                {
                                case DeepRun::Game::Combat::M5CombatAcceptanceCheckpoint::Initial:
                                    return std::filesystem::path("m5-combat-initial.bmp");
                                case DeepRun::Game::Combat::M5CombatAcceptanceCheckpoint::TorpedoInFlight:
                                    return std::filesystem::path("m5-torpedo-flight.bmp");
                                case DeepRun::Game::Combat::M5CombatAcceptanceCheckpoint::PreImpact:
                                    return std::filesystem::path("m5-pre-impact.bmp");
                                case DeepRun::Game::Combat::M5CombatAcceptanceCheckpoint::PostImpact:
                                    return std::filesystem::path("m5-post-impact.bmp");
                                case DeepRun::Game::Combat::M5CombatAcceptanceCheckpoint::Resized:
                                    return std::filesystem::path("m5-resized.bmp");
                                }
                                return std::filesystem::path("m5-unknown.bmp");
                            }();
                            if (captureEnabled)
                            {
                                std::vector<std::byte> pixels;
                                std::uint32_t width = 0;
                                std::uint32_t height = 0;
                                if (frameCapture.Capture(pixels, width, height) &&
                                    WriteBmp(imagePath, pixels, width, height))
                                {
                                    combatAcceptance->MarkImageCaptured(checkpoint, imagePath.string());
                                    std::cout << "[Game][M5] Captured " << M5CheckpointName(checkpoint)
                                              << " to " << imagePath.string() << '\\n';
                                }
                            }
                            static_cast<void>(WriteM5AcceptanceReport(
                                "m5-combat-acceptance.json", *combatAcceptance));
                        }
                    }
'''
p700_render = insert_after + '''                    if (p700Acceptance.has_value())
                    {
                        const auto acceptanceRendered = p700Acceptance->ObserveRender(
                            combatRendered->presentation, *camera, combatRendered->stats,
                            renderer.AspectRatio(), combatPlayground->PresentationModelValid(renderer),
                            combatRendered->explosionDrawn);
                        if (!acceptanceRendered)
                        {
                            std::cerr << "[Game][ERROR] " << acceptanceRendered.error() << '\\n';
                            return false;
                        }
                        if (acceptanceRendered->has_value())
                        {
                            const auto checkpoint = **acceptanceRendered;
                            const std::filesystem::path imagePath = [&checkpoint]() {
                                using Checkpoint = DeepRun::Game::Combat::M5P700AcceptanceCheckpoint;
                                switch (checkpoint)
                                {
                                case Checkpoint::Launch: return std::filesystem::path("m5-p700-launch.bmp");
                                case Checkpoint::WaterExit: return std::filesystem::path("m5-p700-water-exit.bmp");
                                case Checkpoint::Deploy: return std::filesystem::path("m5-p700-deploy.bmp");
                                case Checkpoint::CruiseTerminal: return std::filesystem::path("m5-p700-cruise-terminal.bmp");
                                case Checkpoint::Impact: return std::filesystem::path("m5-p700-impact.bmp");
                                }
                                return std::filesystem::path("m5-p700-unknown.bmp");
                            }();
                            if (captureEnabled)
                            {
                                std::vector<std::byte> pixels;
                                std::uint32_t width = 0;
                                std::uint32_t height = 0;
                                if (frameCapture.Capture(pixels, width, height) && WriteBmp(imagePath, pixels, width, height))
                                {
                                    p700Acceptance->MarkImageCaptured(checkpoint, imagePath.string());
                                    std::cout << "[Game][P700] Captured "
                                              << DeepRun::Game::Combat::M5P700AcceptanceCheckpointName(checkpoint)
                                              << " to " << imagePath.string() << '\\n';
                                }
                            }
                            static_cast<void>(WriteM5P700AcceptanceReport("m5-p700-acceptance.json", *p700Acceptance));
                            if (checkpoint == DeepRun::Game::Combat::M5P700AcceptanceCheckpoint::Impact &&
                                p700Acceptance->AllStateCheckpointsSeen())
                            {
                                engineServices->RequestShutdown();
                            }
                        }
                    }
'''
s = replace_once(s, insert_after, p700_render, "P700 render acceptance block")
s = replace_once(
    s,
    "                if (captureEnabled && !options.headless && !capturedInitial && renderFrames == 3)\n",
    "                if (captureEnabled && !options.headless && !options.p700SmokeTest && !capturedInitial && renderFrames == 3)\n",
    "skip generic initial capture in P700 acceptance",
)
s = replace_once(
    s,
    "                if (captureEnabled && !options.headless && !capturedLater && renderFrames == 90)\n",
    "                if (captureEnabled && !options.headless && !options.p700SmokeTest && !capturedLater && renderFrames == 90)\n",
    "skip generic later capture in P700 acceptance",
)
s = replace_once(
    s,
    "        if (combatAcceptance.has_value())\n"
    "        {\n"
    "            static_cast<void>(WriteM5AcceptanceReport(\"m5-combat-acceptance.json\", *combatAcceptance));\n"
    "            if (applicationExitCode == 0 && options.smokeTest && !combatAcceptance->AllStateCheckpointsSeen())\n"
    "            {\n"
    "                std::cerr << \"[Game][ERROR] M5 visual acceptance did not observe every required checkpoint\\n\";\n"
    "                return 13;\n"
    "            }\n"
    "        }\n"
    "        return applicationExitCode;",
    "        if (combatAcceptance.has_value())\n"
    "        {\n"
    "            static_cast<void>(WriteM5AcceptanceReport(\"m5-combat-acceptance.json\", *combatAcceptance));\n"
    "            if (applicationExitCode == 0 && options.smokeTest && !combatAcceptance->AllStateCheckpointsSeen())\n"
    "            {\n"
    "                std::cerr << \"[Game][ERROR] M5 visual acceptance did not observe every required checkpoint\\n\";\n"
    "                return 13;\n"
    "            }\n"
    "        }\n"
    "        if (p700Acceptance.has_value())\n"
    "        {\n"
    "            static_cast<void>(WriteM5P700AcceptanceReport(\"m5-p700-acceptance.json\", *p700Acceptance));\n"
    "            if (applicationExitCode == 0 && options.p700SmokeTest && !p700Acceptance->AllStateCheckpointsSeen())\n"
    "            {\n"
    "                std::cerr << \"[Game][ERROR] P-700 visual acceptance did not observe every required checkpoint\\n\";\n"
    "                return 14;\n"
    "            }\n"
    "        }\n"
    "        return applicationExitCode;",
    "P700 final acceptance gate",
)
main.write_text(s, encoding="utf-8")


ci = Path(".github/workflows/ci.yml")
s = ci.read_text(encoding="utf-8")
s = replace_once(s, "        if: matrix.preset == 'windows-debug'\n", "", "run P700 smoke in both configs")
s = replace_once(
    s,
    "          if ($text -notmatch \"PHYSICAL_IMPACT damage=100\") {\n"
    "            throw \"P-700 smoke did not apply the accepted 100 HP direct hit\"\n"
    "          }\n\n"
    "      - name: Retain M5 visual acceptance artifacts\n",
    "          if ($text -notmatch \"PHYSICAL_IMPACT damage=100\") {\n"
    "            throw \"P-700 smoke did not apply the accepted 100 HP direct hit\"\n"
    "          }\n"
    "          $p700ReportPath = \"m5-p700-acceptance.json\"\n"
    "          if (-not (Test-Path $p700ReportPath)) {\n"
    "            throw \"P-700 visual acceptance did not produce its machine-readable report\"\n"
    "          }\n"
    "          $p700Report = Get-Content -Raw $p700ReportPath | ConvertFrom-Json\n"
    "          if (-not $p700Report.all_state_checkpoints_seen -or $p700Report.checkpoints.Count -ne 5) {\n"
    "            throw \"P-700 visual acceptance did not observe all five required checkpoints\"\n"
    "          }\n"
    "          $badInventory = @($p700Report.checkpoints | Where-Object { $_.p700_loaded_count -ne 23 })\n"
    "          if ($badInventory.Count -ne 0) {\n"
    "            throw \"P-700 visual acceptance did not retain the expected 23/24 launcher inventory after launch\"\n"
    "          }\n"
    "          $impact = @($p700Report.checkpoints | Where-Object { $_.name -eq 'M5_P700_IMPACT' })\n"
    "          if ($impact.Count -ne 1 -or -not $impact[0].impact.target_is_destroyer_body -or $impact[0].impact.damage -ne 100) {\n"
    "            throw \"P-700 visual acceptance report did not confirm the physical 100 HP destroyer impact\"\n"
    "          }\n\n"
    "      - name: Retain P-700 visual acceptance artifacts\n"
    "        if: always()\n"
    "        continue-on-error: true\n"
    "        uses: actions/upload-artifact@v4\n"
    "        with:\n"
    "          name: m5-p700-visual-${{ matrix.preset }}\n"
    "          if-no-files-found: warn\n"
    "          path: |\n"
    "            m5-p700-acceptance.json\n"
    "            m5-p700-launch.bmp\n"
    "            m5-p700-water-exit.bmp\n"
    "            m5-p700-deploy.bmp\n"
    "            m5-p700-cruise-terminal.bmp\n"
    "            m5-p700-impact.bmp\n\n"
    "      - name: Retain M5 visual acceptance artifacts\n",
    "P700 CI report and artifacts",
)
ci.write_text(s, encoding="utf-8")

print("P-700 visual acceptance closure patch applied")
