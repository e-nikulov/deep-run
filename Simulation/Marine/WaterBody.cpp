#include "Simulation/Marine/WaterBody.h"

#include <cmath>

namespace DeepRun::Marine
{
namespace
{
// The flat water surface is horizontal with its outward normal along +Y (DeepRun world: +X right, +Y up,
// +Z toward camera). Water occupies the half-space y < surfaceLevelY.
constexpr Physics::PhysicsVector3 kSurfaceNormal = {0.0F, 1.0F, 0.0F};

WaterBodyError MakeConfigError(const std::string& message)
{
    return WaterBodyError{WaterBodyErrorCode::InvalidConfiguration, message};
}
} // namespace

std::expected<WaterBody, WaterBodyError> WaterBody::Create(const WaterBodyConfig& config)
{
    if (!std::isfinite(config.surfaceLevelY))
    {
        return std::unexpected(MakeConfigError("surfaceLevelY must be finite"));
    }
    if (!std::isfinite(config.densityKgPerCubicMeter) || config.densityKgPerCubicMeter <= 0.0F)
    {
        return std::unexpected(
            MakeConfigError("densityKgPerCubicMeter must be finite and strictly positive"));
    }
    return WaterBody{config};
}

WaterBody::WaterBody(const WaterBodyConfig config) : config_(config) {}

const WaterBodyConfig& WaterBody::Config() const noexcept
{
    return config_;
}

std::expected<WaterSurfaceSample, WaterBodyError> WaterBody::Sample(
    const Physics::PhysicsVector3& worldPosition) const
{
    if (!worldPosition.IsFinite())
    {
        return std::unexpected(WaterBodyError{
            WaterBodyErrorCode::InvalidQueryPosition, "world position must be finite"});
    }

    // The single canonical depth definition for this water body. Every other derived value (IsUnderwater,
    // submersion depth) must come from this expression — never a duplicated calculation.
    const float signedDepthMeters = config_.surfaceLevelY - worldPosition.y;

    return WaterSurfaceSample{
        .surfaceLevelY = config_.surfaceLevelY,
        .surfaceNormal = kSurfaceNormal,
        .signedDepthMeters = signedDepthMeters};
}
}
