#pragma once

#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Acoustics/AcousticEnvironment.h"
#include "Simulation/Acoustics/AcousticWorld.h"
#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game
{
struct AcousticPlaygroundRuntimeFrame final
{
    std::optional<Acoustics::AcousticObservation> passiveObservation{};
    Acoustics::AcousticPropagationModifiers propagationModifiers{};
    std::vector<Perception::Contact> contacts;
    std::vector<Perception::Track> tracks;
};

// Bounded M4 live composition. The representative remote source is simulation-only scenario truth. Its
// identity never crosses into AcousticObservation/SensorObservation/Contact/Track. The production Antey
// snapshot supplied by PhysicalPlayground is the own receiver and carries live Jolt/WaterBody/RPM-derived data.
class AcousticPlaygroundRuntime final
{
public:
    [[nodiscard]] static std::expected<AcousticPlaygroundRuntime, std::string> Create()
    {
        const auto world = Acoustics::AcousticWorld::Create({});
        if (!world)
        {
            return std::unexpected("M4 acoustic world creation failed: " + world.error().message);
        }
        const auto tracks = Perception::TrackManager::Create();
        if (!tracks)
        {
            return std::unexpected("M4 track manager creation failed: " + tracks.error());
        }
        return AcousticPlaygroundRuntime(*world, *tracks);
    }

    [[nodiscard]] static constexpr Acoustics::AcousticSpectrum AmbientNoiseLevelDb() noexcept
    {
        return {.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}};
    }

    [[nodiscard]] std::expected<AcousticPlaygroundRuntimeFrame, std::string> Advance(
        const Submarine::AnteyAcousticSnapshot& ownSnapshot,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
            !ownSnapshot.emitter.positionMeters.IsFinite() ||
            !ownSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !std::isfinite(ownSnapshot.signedDepthMeters))
        {
            return std::unexpected("M4 live acoustic frame inputs must be finite");
        }

        if (!representativeEmitter_.has_value())
        {
            const float referenceSurfaceY =
                ownSnapshot.passiveReceiver.positionMeters.y + ownSnapshot.signedDepthMeters;
            representativeEmitter_ = Acoustics::AcousticEmitter{
                .positionMeters = {
                    ownSnapshot.passiveReceiver.positionMeters.x + 1200.0F,
                    referenceSurfaceY - 220.0F,
                    ownSnapshot.passiveReceiver.positionMeters.z},
                .velocityMetersPerSecond = {},
                .continuousSourceLevelDb = {.levelDb = {154.0F, 151.0F, 147.0F, 142.0F}}};
        }

        const Acoustics::AcousticEmitter& remote = *representativeEmitter_;
        const double dx = static_cast<double>(remote.positionMeters.x) -
                          static_cast<double>(ownSnapshot.passiveReceiver.positionMeters.x);
        const double dy = static_cast<double>(remote.positionMeters.y) -
                          static_cast<double>(ownSnapshot.passiveReceiver.positionMeters.y);
        const double dz = static_cast<double>(remote.positionMeters.z) -
                          static_cast<double>(ownSnapshot.passiveReceiver.positionMeters.z);
        const double distanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(distanceMeters))
        {
            return std::unexpected("M4 representative acoustic distance is non-finite");
        }

        const double travelTimeSeconds =
            distanceMeters / static_cast<double>(world_.Config().effectiveSoundSpeedMetersPerSecond);
        const double emissionTimeSeconds = simulationTimeSeconds > travelTimeSeconds
            ? std::max(0.0, simulationTimeSeconds - travelTimeSeconds - 1.0e-6)
            : 0.0;
        const Acoustics::AcousticEmission emission{
            .positionMeters = remote.positionMeters,
            .sourceLevelDb = remote.continuousSourceLevelDb,
            .emissionTimeSeconds = emissionTimeSeconds};

        const float referenceSurfaceY =
            ownSnapshot.passiveReceiver.positionMeters.y + ownSnapshot.signedDepthMeters;
        const auto modifiers = Acoustics::EvaluateAcousticEnvironmentPath(
            remote.positionMeters,
            ownSnapshot.passiveReceiver.positionMeters,
            referenceSurfaceY,
            0.0F);
        if (!modifiers)
        {
            return std::unexpected("M4 live acoustic environment evaluation failed: " + modifiers.error());
        }

        const auto observed = world_.CollectPassiveDirectObservation(
            emission,
            ownSnapshot.passiveReceiver,
            simulationTimeSeconds,
            *modifiers);
        if (!observed)
        {
            return std::unexpected("M4 live passive propagation failed: " + observed.error().message);
        }

        if (observed->has_value())
        {
            const auto perceived = Perception::FromAcousticObservation(**observed);
            if (!perceived)
            {
                return std::unexpected("M4 live acoustic observation could not cross the perception boundary");
            }
            const auto integrated = tracks_.IntegrateObservation(*perceived);
            if (!integrated)
            {
                return std::unexpected("M4 live track integration failed: " + integrated.error());
            }
        }
        else
        {
            const auto advanced = tracks_.AdvanceTo(simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected("M4 live track time advance failed: " + advanced.error());
            }
        }

        return AcousticPlaygroundRuntimeFrame{
            .passiveObservation = *observed,
            .propagationModifiers = *modifiers,
            .contacts = tracks_.Contacts(),
            .tracks = tracks_.Tracks()};
    }

private:
    AcousticPlaygroundRuntime(Acoustics::AcousticWorld world, Perception::TrackManager tracks)
        : world_(std::move(world)), tracks_(std::move(tracks))
    {
    }

    Acoustics::AcousticWorld world_;
    Perception::TrackManager tracks_;
    std::optional<Acoustics::AcousticEmitter> representativeEmitter_{};
};
}
