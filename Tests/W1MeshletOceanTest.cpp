#include "Engine/Render/GerstnerSurface.h"

#include <cstdint>
#include <iostream>

namespace
{
using namespace DeepRun::Render;

GerstnerSurfacePresentationParameters MakeBaseSurface()
{
    return GerstnerSurfacePresentationParameters{
        .minimumX = -300.0F,
        .maximumX = 300.0F,
        .referenceLevelY = 0.0F,
        .bottomFillY = -600.0F,
        .horizontalSampleCount = 257U,
        .activeComponentCount = 0U,
        .deepFillRgb = {0.02F, 0.075F, 0.12F},
        .surfaceTintRgb = {0.0065F, 0.075F, 0.18F}};
}

bool Require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "W1-J meshlet ocean regression failed: " << message << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    using namespace DeepRun::Render;

    GerstnerSurfacePresentationParameters flat = MakeBaseSurface();
    const auto flatPlan = BuildGerstnerMeshletDispatchPlan(flat, 600.0F, 2560U);
    if (!Require(flatPlan.has_value(), "flat dispatch plan rejected") ||
        !Require(flatPlan->cellCount == 256U, "flat water must keep the minimum cell budget") ||
        !Require(flatPlan->meshletCount == 9U, "flat water meshlet count changed") ||
        !Require(flatPlan->emittedVertexCount == 530U, "flat emitted vertex count changed") ||
        !Require(flatPlan->emittedPrimitiveCount == 512U, "flat primitive count changed"))
    {
        return 1;
    }

    GerstnerSurfacePresentationParameters rough = MakeBaseSurface();
    rough.activeComponentCount = 1U;
    rough.components[0] = GerstnerWaveComponent{
        .amplitudeMeters = 2.5F,
        .wavelengthMeters = 45.0F,
        .angularFrequencyRadiansPerSecond = 0.8F,
        .phaseOffsetRadians = 0.2F,
        .horizontalSteepness = 0.05F};

    const auto targetPlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 2560U);
    if (!Require(targetPlan.has_value(), "target-resolution rough plan rejected") ||
        !Require(targetPlan->cellCount == 4096U, "2560 px rough sea must select 4096 procedural cells") ||
        !Require(targetPlan->meshletCount == 133U, "4096-cell sea must dispatch 133 meshlets") ||
        !Require(targetPlan->emittedVertexCount == 8458U, "4096-cell emitted vertex count changed") ||
        !Require(targetPlan->emittedPrimitiveCount == 8192U, "4096-cell primitive count changed"))
    {
        return 1;
    }

    const auto acceptancePlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 1028U);
    if (!Require(acceptancePlan.has_value(), "visual-acceptance rough plan rejected") ||
        !Require(acceptancePlan->cellCount == 1645U, "1028 px rough sea density changed") ||
        !Require(acceptancePlan->meshletCount == 54U, "1028 px rough sea meshlet count changed") ||
        !Require(acceptancePlan->emittedVertexCount == 3398U, "1028 px emitted vertex count changed") ||
        !Require(acceptancePlan->emittedPrimitiveCount == 3290U, "1028 px primitive count changed"))
    {
        return 1;
    }

    const auto clampedPlan = BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 20000U);
    if (!Require(clampedPlan.has_value(), "maximum-density plan rejected") ||
        !Require(clampedPlan->cellCount == GerstnerMeshletMaximumCellCount, "maximum cell clamp changed") ||
        !Require(clampedPlan->meshletCount == 265U, "maximum meshlet count changed") ||
        !Require(clampedPlan->emittedVertexCount == 16914U, "maximum emitted vertex count changed") ||
        !Require(clampedPlan->emittedPrimitiveCount == 16384U, "maximum primitive count changed"))
    {
        return 1;
    }

    if (!Require(!BuildGerstnerMeshletDispatchPlan(rough, 0.0F, 2560U).has_value(),
                 "zero camera span must be rejected") ||
        !Require(!BuildGerstnerMeshletDispatchPlan(rough, 600.0F, 0U).has_value(),
                 "zero viewport width must be rejected"))
    {
        return 1;
    }

    GerstnerSurfacePresentationParameters invalidCrest = rough;
    invalidCrest.crestWhiteningStrength = 1.01F;
    if (!Require(!ValidateGerstnerSurfacePresentationParameters(invalidCrest).has_value(),
                 "crest whitening above normalized range must be rejected"))
    {
        return 1;
    }
    rough.crestWhiteningStrength = 0.82F;
    if (!Require(ValidateGerstnerSurfacePresentationParameters(rough).has_value(),
                 "normalized crest whitening must remain a valid renderer-neutral surface snapshot"))
    {
        return 1;
    }

    std::cout << "W1-J meshlet ocean regression PASS\n";
    return 0;
}
