#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Acoustics
{
enum class AcousticErrorCode
{
    InvalidConfiguration,
    InvalidEmission,
    InvalidReceiver,
    InvalidSimulationTime,
    NonFiniteResult,
};

struct AcousticError final
{
    AcousticErrorCode code = AcousticErrorCode::InvalidConfiguration;
    std::string message;
};

// M4-A tuning is deliberately coarse and gameplay-authored. The four absorption coefficients are not a
// claim about exact real-world ocean acoustics and can be replaced by content/environment tuning later.
struct AcousticWorldConfig final
{
    float effectiveSoundSpeedMetersPerSecond = 1500.0F;
    float referenceDistanceMeters = 1.0F;
    float maxPropagationDistanceMeters = 100000.0F;
    float spreadingLossDbPerDistanceDecade = 20.0F;
    std::array<float, AcousticBandCount> absorptionDbPerKilometer{0.02F, 0.08F, 0.30F, 1.20F};
    float passiveBearingUncertaintyRadians = 0.08726646F;
    float confidenceFullScaleSnrMarginDb = 20.0F;
};

// M4-A authoritative gameplay-acoustic foundation. This class performs one bounded deterministic direct
// passive propagation query. It has no dependency on AudioEngine/miniaudio and never exposes source identity.
class AcousticWorld final
{
public:
    [[nodiscard]] static std::expected<AcousticWorld, AcousticError> Create(const AcousticWorldConfig& config);

    [[nodiscard]] const AcousticWorldConfig& Config() const noexcept;

    // Evaluates a direct passive arrival at one receiver using SimulationTime. A valid but not-yet-arrived,
    // out-of-range, or below-threshold signal returns an empty optional. Invalid runtime data is an error.
    // Passive M4-A observations deliberately contain bearing but no estimated range.
    [[nodiscard]] std::expected<std::optional<AcousticObservation>, AcousticError> CollectPassiveDirectObservation(
        const AcousticEmission& emission,
        const AcousticReceiver& receiver,
        double simulationTimeSeconds) const;

private:
    explicit AcousticWorld(AcousticWorldConfig config);

    AcousticWorldConfig config_;
};
}
