#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

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
    case Weapons::P700GranitPhase::Defeated: return "Defeated";
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
        const Render::OrthographicCamera& camera,
        const Render::ModelDrawStats& combatDrawStats,
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
