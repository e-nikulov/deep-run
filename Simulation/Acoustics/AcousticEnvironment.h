#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Acoustics
{
// Authored simplified layer. Depth is positive downward from an externally supplied mean/reference surface.
// This is gameplay tuning, not a full sound-speed profile or physical refraction model.
struct AcousticThermoclineLayer final
{
    float upperDepthMeters = 70.0F;
    float lowerDepthMeters = 150.0F;
    AcousticSpectrum additionalTransmissionLossDb{.levelDb = {1.0F, 2.5F, 4.5F, 7.0F}};
    float confidenceMultiplier = 0.85F;
};

struct AcousticEnvironmentConfig final
{
    AcousticThermoclineLayer thermocline{};

    // Full terrain obstruction remains attenuation rather than binary silence. Higher bands are affected more
    // strongly, while sufficiently loud events may remain observable through coarse terrain masking.
    AcousticSpectrum fullTerrainOcclusionLossDb{.levelDb = {8.0F, 12.0F, 18.0F, 24.0F}};
    float fullTerrainOcclusionConfidenceMultiplier = 0.55F;
};

[[nodiscard]] inline std::expected<AcousticPropagationModifiers, std::string> EvaluateAcousticEnvironmentPath(
    const Physics::PhysicsVector3& sourcePositionMeters,
    const Physics::PhysicsVector3& receiverPositionMeters,
    const float referenceSurfaceYMeters,
    const float terrainOcclusionFraction,
    const AcousticEnvironmentConfig& config = {})
{
    if (!sourcePositionMeters.IsFinite() || !receiverPositionMeters.IsFinite() ||
        !std::isfinite(referenceSurfaceYMeters) || !std::isfinite(terrainOcclusionFraction) ||
        terrainOcclusionFraction < 0.0F || terrainOcclusionFraction > 1.0F)
    {
        return std::unexpected("acoustic environment path inputs must be finite and terrain occlusion must be [0,1]");
    }

    const AcousticThermoclineLayer& layer = config.thermocline;
    if (!std::isfinite(layer.upperDepthMeters) || !std::isfinite(layer.lowerDepthMeters) ||
        layer.upperDepthMeters < 0.0F || layer.lowerDepthMeters <= layer.upperDepthMeters ||
        !layer.additionalTransmissionLossDb.IsFinite() || !std::isfinite(layer.confidenceMultiplier) ||
        layer.confidenceMultiplier < 0.0F || layer.confidenceMultiplier > 1.0F ||
        !config.fullTerrainOcclusionLossDb.IsFinite() ||
        !std::isfinite(config.fullTerrainOcclusionConfidenceMultiplier) ||
        config.fullTerrainOcclusionConfidenceMultiplier < 0.0F ||
        config.fullTerrainOcclusionConfidenceMultiplier > 1.0F)
    {
        return std::unexpected("invalid acoustic environment configuration");
    }
    for (std::size_t band = 0; band < AcousticBandCount; ++band)
    {
        if (layer.additionalTransmissionLossDb.levelDb[band] < 0.0F ||
            config.fullTerrainOcclusionLossDb.levelDb[band] < 0.0F)
        {
            return std::unexpected("acoustic environment losses must be non-negative");
        }
    }

    const float sourceDepthMeters = referenceSurfaceYMeters - sourcePositionMeters.y;
    const float receiverDepthMeters = referenceSurfaceYMeters - receiverPositionMeters.y;
    if (!std::isfinite(sourceDepthMeters) || !std::isfinite(receiverDepthMeters))
    {
        return std::unexpected("acoustic environment depths must be finite");
    }

    const float shallowDepth = std::min(sourceDepthMeters, receiverDepthMeters);
    const float deepDepth = std::max(sourceDepthMeters, receiverDepthMeters);
    const bool intersectsThermocline =
        shallowDepth <= layer.lowerDepthMeters && deepDepth >= layer.upperDepthMeters;

    AcousticPropagationModifiers result{};
    result.terrainAttenuated = terrainOcclusionFraction > 0.0F;
    result.crossedThermocline = intersectsThermocline;

    for (std::size_t band = 0; band < AcousticBandCount; ++band)
    {
        const float terrainLoss = config.fullTerrainOcclusionLossDb.levelDb[band] * terrainOcclusionFraction;
        const float layerLoss = intersectsThermocline ? layer.additionalTransmissionLossDb.levelDb[band] : 0.0F;
        result.additionalTransmissionLossDb.levelDb[band] = terrainLoss + layerLoss;
    }

    const float terrainConfidenceMultiplier = std::lerp(
        1.0F, config.fullTerrainOcclusionConfidenceMultiplier, terrainOcclusionFraction);
    result.confidenceMultiplier = terrainConfidenceMultiplier *
        (intersectsThermocline ? layer.confidenceMultiplier : 1.0F);
    return result;
}
}
