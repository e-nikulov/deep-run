#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Marine/WaterWaveField.h"

#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Marine
{
// Recoverable error for WaterBody configuration and query calls. Programming invariants still use
// assertions (ADR-0002 / A0); invalid runtime/configuration data is reported, never silently corrected.
enum class WaterBodyErrorCode
{
    InvalidConfiguration,
    InvalidQueryPosition,
    InvalidQueryTime,
    NonFiniteResult,
};

struct WaterBodyError final
{
    WaterBodyErrorCode code = WaterBodyErrorCode::InvalidConfiguration;
    std::string message;
};

// Reference-plane configuration with an optional M3-E.1 free-surface definition.
// Values are runtime/configuration data: the generic WaterBody must not hard-code any specific M2
// sea level or ocean scenario. Validation is exact — no silent correction of invalid values.
struct WaterBodyConfig final
{
    // Mean/reference sea level (+Y up), never instantaneous crest/trough height. Must be finite.
    float surfaceLevelY = 0.0F;

    // Bulk density of the water in kg/m^3. Must be finite and strictly positive. Stored for the marine
    // physics systems that consume it (buoyancy is a later slice); D1 performs no force calculation.
    float densityKgPerCubicMeter = 0.0F;
    std::optional<WaterWaveFieldDefinition> waves{};
};

// One coherent surface query result: every field derives from the same queried surface, so
// callers can never combine values that silently disagree with each other.
struct WaterSurfaceSample final
{
    // World-space surface Y (meters): reference plane for Sample, local surface for SampleWaveSurface.
    float surfaceLevelY = 0.0F;

    // Outward surface normal: always +Y for a flat horizontal water body.
    Physics::PhysicsVector3 surfaceNormal{};

    // Signed depth in meters, defined exactly as: signedDepthMeters = surfaceLevelY - worldPosition.y
    //   > 0 -> point below the surface (underwater)
    //   = 0 -> point exactly on the surface
    //   < 0 -> point above the water
    // This sign convention is authoritative for buoyancy, pressure, depth response and later acoustics.
    float signedDepthMeters = 0.0F;
};

// Authoritative reference-plane water body with optional explicit free-surface queries (M3-E.1).
//
// The first marine-environment primitive in Simulation/Marine: the source of truth for future buoyancy,
// depth, pressure and hydrodynamic systems. Rendering is never the source of gameplay state — this type has
// no knowledge of renderers, cameras, physics bodies or submarines; it only exposes environmental queries.
//
// Sample retains the D1 flat hydrostatic contract. Only SampleWaveSurface evaluates waves and time.
// Neither query owns collision bodies or force equations; existing consumers remain reference-plane based.
class WaterBody final
{
public:
    // Constructs the water body from validated configuration. Invalid values (non-finite surface level or
    // non-positive/non-finite density) are recoverable errors, never silently corrected.
    [[nodiscard]] static std::expected<WaterBody, WaterBodyError> Create(const WaterBodyConfig& config);

    // The exact retained configuration: no correction is ever applied after construction.
    [[nodiscard]] const WaterBodyConfig& Config() const noexcept;

    // Samples the water surface at a world-space position (meters). Non-finite positions are recoverable
    // errors instead of propagating NaN. For D1 flat water the result depends only on worldPosition.y:
    // X/Z never affect the surface level, normal or signed depth.
    [[nodiscard]] std::expected<WaterSurfaceSample, WaterBodyError> Sample(
        const Physics::PhysicsVector3& worldPosition) const;

    // Explicit instantaneous free-surface query at WORLD X and fixed-step SimulationTime. Inverts horizontal
    // displacement; returns local surface Y, upward normal and local signed depth. Without waves, equals
    // Sample(). Sample() itself stays reference-plane hydrostatics even when waves are enabled: existing
    // buoyancy and depth consumers are intentionally not migrated in M3-E.1.
    [[nodiscard]] std::expected<WaterSurfaceSample, WaterBodyError> SampleWaveSurface(
        const Physics::PhysicsVector3& worldPosition, double simulationTimeSeconds) const;

private:
    explicit WaterBody(WaterBodyConfig config);

    WaterBodyConfig config_;
};
}
