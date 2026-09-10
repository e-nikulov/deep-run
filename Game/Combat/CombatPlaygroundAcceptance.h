#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Combat
{
enum class M5CombatAcceptanceCheckpoint
{
    Initial,
    TorpedoInFlight,
    PreImpact,
    PostImpact,
    Resized,
};

struct M5CombatAcceptanceTorpedoState final
{
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    Weapons::WeaponPhase weaponPhase = Weapons::WeaponPhase::Stored;
    Weapons::MovementDomain movementDomain = Weapons::MovementDomain::Attached;
};

struct M5CombatAcceptanceSnapshot final
{
    double simulationTimeSeconds = 0.0;
    Physics::PhysicsVector3 anteyPositionMeters{};
    Physics::PhysicsBodyState destroyerBody{};
    float destroyerIntegrity = 0.0F;
    std::optional<M5CombatAcceptanceTorpedoState> torpedo{};
    std::optional<Physics::PhysicsVector3> decoyPositionMeters{};
    bool decoyActive = false;
    bool hasImpact = false;
    bool impactBodyHandleValid = false;
    bool impactTargetIsDestroyer = false;
    std::optional<Physics::PhysicsVector3> impactPositionMeters{};
    std::optional<Physics::PhysicsVector3> explosionPositionMeters{};
    std::uint32_t combatDrawCalls = 0U;
    std::uint32_t combatSubmittedPrimitives = 0U;
    std::uint64_t combatSubmittedIndices = 0U;
    float cameraAspectRatio = 0.0F;
    float cameraHorizontalSpanMeters = 0.0F;
    bool gpuPresentationHandleValid = false;
};

struct M5CombatAcceptanceRecord final
{
    M5CombatAcceptanceCheckpoint checkpoint = M5CombatAcceptanceCheckpoint::Initial;
    M5CombatAcceptanceSnapshot state{};
    bool imageCaptured = false;
    std::string imagePath{};
};

// Windowed M5 acceptance is a deterministic state gate, not an image-quality metric. It observes the same
// authoritative snapshots that presentation consumes and never performs collision or distance-based hit
// detection. The only impact transition it accepts is the real ConventionalTorpedo/Jolt result in the frame.
class M5CombatVisualAcceptance final
{
public:
    explicit M5CombatVisualAcceptance(const float surfaceLevelY) noexcept
        : surfaceLevelY_(surfaceLevelY)
    {
    }

    [[nodiscard]] std::expected<void, std::string> ObserveFixed(
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Submarine::AnteyAcousticSnapshot& antey,
        const CombatPlaygroundFrame& frame,
        const double simulationTimeSeconds,
        const bool resized)
    {
        if (!physicsWorld.IsInitialized() || !std::isfinite(surfaceLevelY_) ||
            !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < lastSimulationTimeSeconds_ ||
            !antey.emitter.positionMeters.IsFinite() || !antey.emitter.velocityMetersPerSecond.IsFinite())
        {
            return std::unexpected("M5 visual acceptance received invalid fixed-step input");
        }

        const auto destroyerBody = physicsWorld.GetBodyState(runtime.Destroyer().body);
        if (!destroyerBody || !destroyerBody->position.IsFinite() || !destroyerBody->orientation.IsFinite() ||
            !destroyerBody->linearVelocity.IsFinite() || !destroyerBody->angularVelocity.IsFinite())
        {
            return std::unexpected("M5 visual acceptance could not read the destroyer physics pose");
        }
        if (destroyerBody->position.y > surfaceLevelY_ + 0.5F ||
            destroyerBody->position.y < surfaceLevelY_ - 10.0F)
        {
            return std::unexpected("M5 destroyer left the accepted surface presentation band");
        }
        if (!std::isfinite(runtime.Destroyer().integrity.remainingIntegrity) ||
            runtime.Destroyer().integrity.remainingIntegrity < 0.0F ||
            runtime.Destroyer().integrity.remainingIntegrity > runtime.Destroyer().integrity.maximumIntegrity)
        {
            return std::unexpected("M5 destroyer integrity is invalid");
        }

        M5CombatAcceptanceSnapshot snapshot{
            .simulationTimeSeconds = simulationTimeSeconds,
            .anteyPositionMeters = antey.emitter.positionMeters,
            .destroyerBody = *destroyerBody,
            .destroyerIntegrity = runtime.Destroyer().integrity.remainingIntegrity};

        if (const auto& torpedo = runtime.PlayerTorpedo(); torpedo.has_value())
        {
            if (!torpedo->positionMeters.IsFinite() || !std::isfinite(torpedo->headingRadians) ||
                !std::isfinite(torpedo->speedMetersPerSecond) ||
                !std::isfinite(torpedo->lastUpdateTimeSeconds))
            {
                return std::unexpected("M5 torpedo acceptance state is not finite");
            }
            snapshot.torpedo = M5CombatAcceptanceTorpedoState{
                .positionMeters = torpedo->positionMeters,
                .headingRadians = torpedo->headingRadians,
                .weaponPhase = torpedo->weapon.phase,
                .movementDomain = torpedo->movementDomain};

            if (torpedo->movementDomain == Weapons::MovementDomain::Underwater)
            {
                const auto& launchPosition = runtime.PlayerTorpedoLaunchPosition();
                if (!launchPosition.has_value())
                {
                    return std::unexpected("M5 active torpedo has no authored launch position");
                }
                const float forwardProgressMeters = torpedo->positionMeters.x - launchPosition->x;
                if (!torpedoLaunchValidated_)
                {
                    if (std::abs(launchPosition->x - antey.emitter.positionMeters.x -
                                 M5CombatTorpedoLaunchClearanceMeters) > 0.01F ||
                        std::abs(launchPosition->y - antey.emitter.positionMeters.y) > 0.01F ||
                        std::abs(launchPosition->z - antey.emitter.positionMeters.z) > 0.01F ||
                        std::abs(torpedo->headingRadians) > 0.02F)
                    {
                        return std::unexpected("M5 torpedo did not leave Antey on the horizontal tube-exit profile");
                    }
                    torpedoLaunchValidated_ = true;
                }
                if (torpedo->positionMeters.y >= surfaceLevelY_ - M5CombatTorpedoSurfaceSafetyMarginMeters)
                {
                    return std::unexpected("M5 conventional torpedo crossed the underwater surface safety margin");
                }
                if (std::abs(torpedo->headingRadians) >
                    M5CombatTorpedoMaximumVerticalCourseAngleRadians + 0.001F)
                {
                    return std::unexpected("M5 conventional torpedo exceeded its vertical course limit");
                }
                if (forwardProgressMeters >= 30.0F &&
                    forwardProgressMeters <= M5CombatTorpedoStraightRunMeters - 2.0F)
                {
                    if (std::abs(torpedo->positionMeters.y - launchPosition->y) > 0.25F ||
                        std::abs(torpedo->headingRadians) > 0.02F)
                    {
                        return std::unexpected("M5 torpedo straight run-out changed depth or course too early");
                    }
                    straightRunoutValidated_ = true;
                }
                if (forwardProgressMeters > M5CombatTorpedoStraightRunMeters + 10.0F &&
                    torpedo->positionMeters.y > launchPosition->y + 1.0F)
                {
                    gradualAscentObserved_ = true;
                }

                if (previousTorpedoPosition_.has_value())
                {
                    const double deltaSeconds = simulationTimeSeconds - previousTorpedoTimeSeconds_;
                    const float dx = torpedo->positionMeters.x - previousTorpedoPosition_->x;
                    const float dy = torpedo->positionMeters.y - previousTorpedoPosition_->y;
                    const float dz = torpedo->positionMeters.z - previousTorpedoPosition_->z;
                    const double travelled = std::sqrt(
                        static_cast<double>(dx) * dx + static_cast<double>(dy) * dy + static_cast<double>(dz) * dz);
                    if (deltaSeconds < 0.0 || travelled > static_cast<double>(torpedo->speedMetersPerSecond) *
                        deltaSeconds + 0.05)
                    {
                        return std::unexpected("M5 torpedo movement exceeded its bounded SimulationTime path");
                    }
                    // This scenario is authored as a forward side-view run. A backward X step is a
                    // presentation/simulation desynchronization, not a subjective visual-quality failure.
                    if (torpedo->positionMeters.x + 0.001F < previousTorpedoPosition_->x)
                    {
                        return std::unexpected("M5 torpedo reversed its bounded forward movement path");
                    }
                }
                previousTorpedoPosition_ = torpedo->positionMeters;
                previousTorpedoTimeSeconds_ = simulationTimeSeconds;
            }
            else if (torpedo->movementDomain == Weapons::MovementDomain::Spent)
            {
                if (torpedo->speedMetersPerSecond != 0.0F)
                {
                    return std::unexpected("M5 spent torpedo retained normal movement speed");
                }
                if (spentTorpedoPosition_.has_value() && torpedo->positionMeters != *spentTorpedoPosition_)
                {
                    return std::unexpected("M5 spent torpedo continued moving after impact");
                }
                spentTorpedoPosition_ = torpedo->positionMeters;
            }
        }

        if (const auto& decoy = runtime.Decoy(); decoy.has_value())
        {
            if (!decoy->emitter.positionMeters.IsFinite())
            {
                return std::unexpected("M5 decoy acceptance position is not finite");
            }
            snapshot.decoyPositionMeters = decoy->emitter.positionMeters;
            snapshot.decoyActive = decoy->active;
            if (decoy->active && Distance(*snapshot.decoyPositionMeters, destroyerBody->position) < 0.1F)
            {
                return std::unexpected("M5 decoy shares the destroyer transform");
            }
            if (decoy->active && snapshot.torpedo &&
                snapshot.torpedo->movementDomain == Weapons::MovementDomain::Underwater &&
                Distance(*snapshot.decoyPositionMeters, snapshot.torpedo->positionMeters) < 0.1F)
            {
                return std::unexpected("M5 decoy shares the torpedo transform");
            }
        }

        if (frame.playerTorpedoImpact.has_value())
        {
            const auto& impact = *frame.playerTorpedoImpact;
            if (!impact.physicsHit.body.IsValid() || impact.physicsHit.body != runtime.Destroyer().body ||
                !impact.physicsHit.positionMeters.IsFinite() || !impact.explosion.positionMeters.IsFinite() ||
                Distance(impact.physicsHit.positionMeters, impact.explosion.positionMeters) > 0.01F ||
                impact.explosion.simulationTimeSeconds != simulationTimeSeconds)
            {
                return std::unexpected("M5 impact target or explosion position failed the Jolt authority contract");
            }
            snapshot.hasImpact = true;
            snapshot.impactBodyHandleValid = impact.physicsHit.body.IsValid();
            snapshot.impactTargetIsDestroyer = impact.physicsHit.body == runtime.Destroyer().body;
            snapshot.impactPositionMeters = impact.physicsHit.positionMeters;
            snapshot.explosionPositionMeters = impact.explosion.positionMeters;
            sawImpact_ = true;
        }
        if (const auto& explosion = runtime.LastExplosion(); explosion.has_value())
        {
            if (!explosion->positionMeters.IsFinite() || !std::isfinite(explosion->simulationTimeSeconds) ||
                simulationTimeSeconds < explosion->simulationTimeSeconds)
            {
                return std::unexpected("M5 explosion acceptance state is invalid or time-reversing");
            }
            snapshot.explosionPositionMeters = explosion->positionMeters;
        }

        latestFixedSnapshot_ = snapshot;
        if (!pending_.has_value() && initialSeen_ && !flightSeen_ && snapshot.torpedo &&
                 snapshot.torpedo->movementDomain == Weapons::MovementDomain::Underwater &&
                 runtime.PlayerTorpedoLaunchPosition().has_value() &&
                 snapshot.torpedo->positionMeters.x - runtime.PlayerTorpedoLaunchPosition()->x >= 30.0F &&
                 snapshot.torpedo->positionMeters.x - runtime.PlayerTorpedoLaunchPosition()->x <=
                     M5CombatTorpedoStraightRunMeters - 2.0F &&
                 Distance(snapshot.torpedo->positionMeters, snapshot.destroyerBody.position) > 35.0F)
        {
            pending_ = M5CombatAcceptanceCheckpoint::TorpedoInFlight;
            pendingSnapshot_ = snapshot;
            flightSeen_ = true;
        }
        else if (!pending_.has_value() && flightSeen_ && !preImpactSeen_ && !snapshot.hasImpact && snapshot.torpedo &&
                 snapshot.torpedo->movementDomain == Weapons::MovementDomain::Underwater &&
                 Distance(snapshot.torpedo->positionMeters, snapshot.destroyerBody.position) <= 40.0F)
        {
            pending_ = M5CombatAcceptanceCheckpoint::PreImpact;
            pendingSnapshot_ = snapshot;
            preImpactSeen_ = true;
        }
        else if (!pending_.has_value() && preImpactSeen_ && !postImpactSeen_ && snapshot.hasImpact && snapshot.torpedo &&
                 snapshot.torpedo->movementDomain == Weapons::MovementDomain::Spent &&
                 snapshot.destroyerIntegrity < runtime.Destroyer().integrity.maximumIntegrity)
        {
            pending_ = M5CombatAcceptanceCheckpoint::PostImpact;
            pendingSnapshot_ = snapshot;
            postImpactSeen_ = true;
        }
        else if (!pending_.has_value() && postImpactSeen_ && !resizedSeen_ && resized)
        {
            pending_ = M5CombatAcceptanceCheckpoint::Resized;
            pendingSnapshot_ = snapshot;
            resizedSeen_ = true;
        }
        lastSimulationTimeSeconds_ = simulationTimeSeconds;
        return {};
    }

    [[nodiscard]] std::expected<std::optional<M5CombatAcceptanceRecord>, std::string> ObserveRender(
        const CombatPlaygroundRuntime& runtime,
        const Physics::PhysicsWorld& physicsWorld,
        const Render::OrthographicCamera& camera,
        const Render::ModelDrawStats& combatDrawStats,
        const float rendererAspectRatio,
        const bool gpuPresentationHandleValid)
    {
        // Select the initial checkpoint at render time. Startup can spend enough wall time loading the
        // production scene for several fixed ticks to execute before the first present; choosing here keeps
        // the JSON state and the pixels from the same live frame while still requiring no impact and intact
        // destroyer presentation.
        if (!pending_.has_value() && !initialSeen_ && !latestFixedSnapshot_.hasImpact &&
            latestFixedSnapshot_.destroyerIntegrity >= 99.999F)
        {
            pending_ = M5CombatAcceptanceCheckpoint::Initial;
            pendingSnapshot_ = latestFixedSnapshot_;
            initialSeen_ = true;
        }
        if (!pending_.has_value())
        {
            return std::optional<M5CombatAcceptanceRecord>{};
        }
        if (!Render::IsFinite(camera.view) || !Render::IsFinite(camera.projection) ||
            !Render::IsFinite(camera.viewProjection) || !std::isfinite(camera.width) ||
            !std::isfinite(camera.height) || !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
            !std::isfinite(rendererAspectRatio) || rendererAspectRatio <= 0.0F ||
            !std::isfinite(camera.width) || std::abs(camera.width - 600.0F) > 0.001F ||
            !std::isfinite(camera.height) || camera.height <= 0.0F || camera.nearPlane <= 0.0F ||
            camera.farPlane <= camera.nearPlane ||
            std::abs(camera.target.x - latestFixedSnapshot_.anteyPositionMeters.x -
                     M5CombatCameraTargetOffsetXMeters) > 1.0F ||
            !gpuPresentationHandleValid ||
            combatDrawStats.drawCalls < 2U || combatDrawStats.drawCalls > 5U ||
            combatDrawStats.submittedPrimitives != combatDrawStats.drawCalls ||
            combatDrawStats.submittedIndices != static_cast<std::uint64_t>(combatDrawStats.drawCalls) * 36U)
        {
            return std::unexpected("M5 visual acceptance render/camera/GPU contract failed");
        }
        M5CombatAcceptanceSnapshot renderedSnapshot = pendingSnapshot_.value_or(latestFixedSnapshot_);
        const auto currentDestroyerBody = physicsWorld.GetBodyState(runtime.Destroyer().body);
        if (!currentDestroyerBody || !currentDestroyerBody->position.IsFinite() ||
            !currentDestroyerBody->orientation.IsFinite())
        {
            return std::unexpected("M5 visual acceptance could not synchronize the rendered destroyer pose");
        }
        // FixedUpdate observes the destroyer before the Engine's authoritative PhysicsWorld::Step. Refresh
        // this one presentation field after the step so the report and capture describe the same pose Render
        // consumes; impact/event fields remain from the real fixed-step combat result.
        renderedSnapshot.destroyerBody = *currentDestroyerBody;
        renderedSnapshot.combatDrawCalls = combatDrawStats.drawCalls;
        renderedSnapshot.combatSubmittedPrimitives = combatDrawStats.submittedPrimitives;
        renderedSnapshot.combatSubmittedIndices = combatDrawStats.submittedIndices;
        renderedSnapshot.cameraAspectRatio = rendererAspectRatio;
        renderedSnapshot.cameraHorizontalSpanMeters = camera.width;
        renderedSnapshot.gpuPresentationHandleValid = gpuPresentationHandleValid;

        M5CombatAcceptanceRecord record{
            .checkpoint = *pending_,
            .state = std::move(renderedSnapshot)};
        records_[CheckpointIndex(*pending_)] = record;
        pending_.reset();
        pendingSnapshot_.reset();
        return std::optional<M5CombatAcceptanceRecord>{std::move(record)};
    }

    void MarkImageCaptured(const M5CombatAcceptanceCheckpoint checkpoint, std::string path)
    {
        auto& record = records_[CheckpointIndex(checkpoint)];
        if (record.has_value())
        {
            record->imageCaptured = true;
            record->imagePath = std::move(path);
        }
    }

    [[nodiscard]] const std::array<std::optional<M5CombatAcceptanceRecord>, 5>& Records() const noexcept
    {
        return records_;
    }

    [[nodiscard]] bool AllStateCheckpointsSeen() const noexcept
    {
        return initialSeen_ && flightSeen_ && preImpactSeen_ && postImpactSeen_ && resizedSeen_ && sawImpact_ &&
               torpedoLaunchValidated_ && straightRunoutValidated_ && gradualAscentObserved_;
    }

    [[nodiscard]] static std::size_t CheckpointIndex(const M5CombatAcceptanceCheckpoint checkpoint) noexcept
    {
        return static_cast<std::size_t>(checkpoint);
    }

private:
    [[nodiscard]] static float Distance(
        const Physics::PhysicsVector3& first,
        const Physics::PhysicsVector3& second) noexcept
    {
        const float dx = second.x - first.x;
        const float dy = second.y - first.y;
        const float dz = second.z - first.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float surfaceLevelY_ = 0.0F;
    double lastSimulationTimeSeconds_ = 0.0;
    double previousTorpedoTimeSeconds_ = 0.0;
    std::optional<Physics::PhysicsVector3> previousTorpedoPosition_{};
    std::optional<Physics::PhysicsVector3> spentTorpedoPosition_{};
    M5CombatAcceptanceSnapshot latestFixedSnapshot_{};
    std::optional<M5CombatAcceptanceCheckpoint> pending_{};
    std::optional<M5CombatAcceptanceSnapshot> pendingSnapshot_{};
    std::array<std::optional<M5CombatAcceptanceRecord>, 5> records_{};
    bool initialSeen_ = false;
    bool flightSeen_ = false;
    bool preImpactSeen_ = false;
    bool postImpactSeen_ = false;
    bool resizedSeen_ = false;
    bool sawImpact_ = false;
    bool torpedoLaunchValidated_ = false;
    bool straightRunoutValidated_ = false;
    bool gradualAscentObserved_ = false;
};
} // namespace DeepRun::Game::Combat
