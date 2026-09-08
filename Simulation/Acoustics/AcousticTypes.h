#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace DeepRun::Acoustics
{
enum class AcousticBand : std::size_t
{
    VeryLow = 0,
    Low,
    Medium,
    High,
    Count,
};

inline constexpr std::size_t AcousticBandCount = static_cast<std::size_t>(AcousticBand::Count);

// Coarse gameplay spectrum. Values are authored relative dB-like tuning levels; they are not intended
// to encode classified or exact real-world military signatures.
struct AcousticSpectrum final
{
    std::array<float, AcousticBandCount> levelDb{};

    [[nodiscard]] bool IsFinite() const noexcept
    {
        for (const float level : levelDb)
        {
            if (!std::isfinite(level))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] float& operator[](const AcousticBand band) noexcept
    {
        return levelDb[static_cast<std::size_t>(band)];
    }

    [[nodiscard]] const float& operator[](const AcousticBand band) const noexcept
    {
        return levelDb[static_cast<std::size_t>(band)];
    }

    [[nodiscard]] bool operator==(const AcousticSpectrum&) const noexcept = default;
};

// Persistent simulation-facing source state. It may carry authoritative position/velocity because it remains
// inside simulation; normal observations never receive a source/entity identifier or this structure itself.
struct AcousticEmitter final
{
    Physics::PhysicsVector3 positionMeters{};
    Physics::PhysicsVector3 velocityMetersPerSecond{};
    AcousticSpectrum continuousSourceLevelDb{};
};

// One gameplay acoustic emission sample. Source identity is intentionally absent: authoritative entity
// identity belongs inside simulation ownership and must never leak into normal sensor observations.
struct AcousticEmission final
{
    Physics::PhysicsVector3 positionMeters{};
    AcousticSpectrum sourceLevelDb{};
    double emissionTimeSeconds = 0.0;
};

struct AcousticReceiver final
{
    // Sensor identity is safe perceived-world provenance. It identifies which own sensor produced evidence,
    // not the ground-truth source that generated the signal.
    std::string sensorId;
    Physics::PhysicsVector3 positionMeters{};
    AcousticSpectrum ambientNoiseLevelDb{};
    AcousticSpectrum selfNoiseLevelDb{};
    AcousticSpectrum sensitivityDb{};
    float minimumPeakSnrDb = 0.0F;
};

enum class AcousticPathClass
{
    Direct,
};

// M4 observations are evidence, not targets. Passive M4-A therefore publishes bearing/SNR/uncertainty
// and deliberately leaves range unset; there is no source/entity identifier in this value.
struct AcousticObservation final
{
    std::string sensorId;
    double observationTimeSeconds = 0.0;
    double arrivalTimeSeconds = 0.0;
    float measuredBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    std::optional<float> estimatedRangeMeters{};
    std::optional<float> rangeUncertaintyMeters{};
    AcousticSpectrum receivedLevelDb{};
    AcousticSpectrum signalToNoiseDb{};
    float peakSignalToNoiseDb = 0.0F;
    float confidence = 0.0F;
    AcousticPathClass pathClass = AcousticPathClass::Direct;

    [[nodiscard]] bool operator==(const AcousticObservation&) const noexcept = default;
};
}
