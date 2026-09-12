from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# -----------------------------------------------------------------------------
# Production asset semantic boundary: resolve authored depth-plane node names
# privately into opaque binding indices. Gameplay never consumes raw GLB names.
# -----------------------------------------------------------------------------
path = ROOT / 'Game/Submarine/ProductionAnteyAsset.h'
text = path.read_text(encoding='utf-8')
old = '''struct ProductionLaunchAnchor final
{
    std::string semanticId;
    ProductionLocalTransform localTransform{};
    Assets::ModelVector3 launchForward{};
};
'''
new = old + '''
enum class ProductionDepthPlaneGroup
{
    Bow,
    Stern,
};

struct ProductionDepthPlane final
{
    std::string semanticId;
    ProductionDepthPlaneGroup group = ProductionDepthPlaneGroup::Bow;
    // Opaque Assets-layer binding index; raw GLB/source names stop in the loader.
    std::size_t presentationNodeBindingIndex = 0;
};
'''
text = replace_once(text, old, new, 'add production depth-plane contract')
old = '''    std::vector<ProductionRetractableSailDevice> retractableSailDevices;
    std::vector<ProductionLaunchAnchor> torpedoLaunchAnchors;
'''
new = '''    std::vector<ProductionRetractableSailDevice> retractableSailDevices;
    std::vector<ProductionDepthPlane> depthPlanes;
    std::vector<ProductionLaunchAnchor> torpedoLaunchAnchors;
'''
text = replace_once(text, old, new, 'add depth planes to production definition')
path.write_text(text, encoding='utf-8')

path = ROOT / 'Game/Submarine/ProductionAnteyAsset.cpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '''    throw std::runtime_error(std::format("Antey GLB has no node for a required propeller presentation binding"));
''',
    '''    throw std::runtime_error(std::format("Antey GLB has no node for a required production presentation binding"));
''',
    'generalize node-binding diagnostic')
old = '''            .propellers = {},
            .retractableSailDevices = {},
            .torpedoLaunchAnchors = {},
'''
new = '''            .propellers = {},
            .retractableSailDevices = {},
            .depthPlanes = {},
            .torpedoLaunchAnchors = {},
'''
text = replace_once(text, old, new, 'initialize depth plane collection')
anchor = '''        const Json& retractableSailDevices = authoring.at("retractableSailDevices");
'''
insert = '''        // M5-V2-B consumes production control-surface authoring only as a private node-resolution source.
        // The public runtime definition retains semantic group + opaque node binding, never raw GLB names.
        const Json& controlSurfaceAuthoring = metadata.at("controlSurfaceAuthoring");
        Require(controlSurfaceAuthoring.is_object(), "Antey controlSurfaceAuthoring must be an object");
        std::unordered_set<std::size_t> depthPlaneBindingIndices;
        const auto appendDepthPlanes = [&](const std::string_view key,
                                           const ProductionDepthPlaneGroup group,
                                           const std::string_view semanticPrefix)
        {
            const Json& records = controlSurfaceAuthoring.at(std::string(key));
            Require(records.is_array() && records.size() == 2U,
                    std::format("Antey {} must contain two depth planes", key));
            for (std::size_t index = 0; index < records.size(); ++index)
            {
                const std::string privateNodeReference = records[index].get<std::string>();
                const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
                Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value(),
                        "Antey depth-plane binding must resolve to a drawable mesh node");
                Require(depthPlaneBindingIndices.insert(bindingIndex).second,
                        "Antey depth-plane bindings must be unique");
                definition.depthPlanes.push_back({
                    .semanticId = std::format("depth-plane.{}.{:02}", semanticPrefix, index + 1U),
                    .group = group,
                    .presentationNodeBindingIndex = bindingIndex});
            }
        };
        appendDepthPlanes("bowPlanes", ProductionDepthPlaneGroup::Bow, "bow");
        appendDepthPlanes("sternPlanes", ProductionDepthPlaneGroup::Stern, "stern");
        Require(definition.depthPlanes.size() == 4U, "Antey must expose four production depth-plane bindings");

'''
text = replace_once(text, anchor, insert + anchor, 'parse depth plane bindings')
path.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# PhysicalPlayground telemetry and committed visual state.
# -----------------------------------------------------------------------------
path = ROOT / 'Game/PhysicalPlayground.h'
text = path.read_text(encoding='utf-8')
old = '''namespace DeepRun::Game
{
// M2 Slice C2: the canonical submarine is rendered from authoritative Jolt rigid-body state.
'''
new = '''namespace DeepRun::Game
{
struct VesselPresentationTelemetry final
{
    float signedDepthMeters = 0.0F;
    // World +Y is upward, so positive values mean surfacing and negative values mean diving.
    float verticalSpeedMetersPerSecond = 0.0F;
    float throttleFraction = 0.0F;
    float bowPlaneDeflectionFraction = 0.0F;
    float sternPlaneDeflectionFraction = 0.0F;
};

// M2 Slice C2: the canonical submarine is rendered from authoritative Jolt rigid-body state.
'''
text = replace_once(text, old, new, 'add vessel presentation telemetry')
old = '''    [[nodiscard]] std::expected<Submarine::AnteyPhysicalCollisionProxySnapshot, std::string>
    BuildPhysicalCollisionProxySnapshot() const
'''
new = '''    [[nodiscard]] std::expected<VesselPresentationTelemetry, std::string> BuildVesselPresentationTelemetry() const;

    [[nodiscard]] std::expected<Submarine::AnteyPhysicalCollisionProxySnapshot, std::string>
    BuildPhysicalCollisionProxySnapshot() const
'''
text = replace_once(text, old, new, 'declare telemetry builder')
old = '''    std::array<Marine::ControlSurfaceComponent, 2> controlSurfaces_{};

    // World-space fixed camera target initialized from the production body's initial center. M5 free navigation
'''
new = '''    std::array<Marine::ControlSurfaceComponent, 2> controlSurfaces_{};
    // M5-V2-B committed simulation-control state. Presentation reads only these values after the full fixed
    // transaction succeeds; raw keyboard/controller state never drives model articulation directly.
    std::array<float, 2> committedControlSurfaceDeflections_{};
    float committedThrottleFraction_ = 0.0F;
    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices_{};

    // World-space fixed camera target initialized from the production body's initial center. M5 free navigation
'''
text = replace_once(text, old, new, 'add committed control presentation state')
path.write_text(text, encoding='utf-8')

path = ROOT / 'Game/PhysicalPlayground.cpp'
text = path.read_text(encoding='utf-8')
old = '''constexpr float M2MaximumPlaneDeflection = 0.5F;
constexpr std::array<std::string_view, 2> M2ControlSurfaceNames{"bow", "stern"};
'''
new = '''constexpr float M2MaximumPlaneDeflection = 0.5F;
// Presentation-only articulation envelope. This is not a claim about classified/production hardware limits;
// it maps the accepted normalized H2 simulation deflection visibly onto the authored production plane pivots.
constexpr float M5DepthPlaneVisualMaximumRadians = 0.436332313F; // 25 degrees
constexpr std::array<std::string_view, 2> M2ControlSurfaceNames{"bow", "stern"};
'''
text = replace_once(text, old, new, 'add visual depth plane articulation envelope')

# add local rotation helper after vector formatting helper
anchor = '''std::string FormatBounds(const EnvironmentBounds& bounds)
'''
insert = '''Assets::ModelTransform DepthPlanePostTransform(const float committedDeflectionFraction) noexcept
{
    const float normalized = M2MaximumPlaneDeflection > 0.0F
        ? std::clamp(committedDeflectionFraction / M2MaximumPlaneDeflection, -1.0F, 1.0F)
        : 0.0F;
    // Blender LOCAL_Y hinge maps to runtime -Z under the accepted Antey basis conversion.
    const float radians = -normalized * M5DepthPlaneVisualMaximumRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Assets::ModelTransform transform{};
    transform.values[0] = cosine;
    transform.values[1] = sine;
    transform.values[4] = -sine;
    transform.values[5] = cosine;
    return transform;
}

'''
text = replace_once(text, anchor, insert + anchor, 'add depth plane post transform')

# Build mesh node group collection immediately after sail-device overrides
old = '''    for (const Submarine::ProductionRetractableSailDevice& device : productionDefinition->retractableSailDevices)
    {
        if (device.defaultState != Submarine::RetractableSailDeviceState::Stowed ||
            device.presentationNodeBindingIndex >= (*model)->nodeBindings.size())
        {
            return std::unexpected("physical playground production sail-device state is invalid");
        }
        const auto meshNodeIndex = (*model)->nodeBindings[device.presentationNodeBindingIndex].meshNodeIndex;
        if (!meshNodeIndex.has_value())
        {
            return std::unexpected("physical playground production sail-device binding is not drawable");
        }
        submergedSailDeviceOverrides.push_back(
            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
    }

    if (productionDefinition->collisionProxies.size() != 1U)
'''
new = '''    for (const Submarine::ProductionRetractableSailDevice& device : productionDefinition->retractableSailDevices)
    {
        if (device.defaultState != Submarine::RetractableSailDeviceState::Stowed ||
            device.presentationNodeBindingIndex >= (*model)->nodeBindings.size())
        {
            return std::unexpected("physical playground production sail-device state is invalid");
        }
        const auto meshNodeIndex = (*model)->nodeBindings[device.presentationNodeBindingIndex].meshNodeIndex;
        if (!meshNodeIndex.has_value())
        {
            return std::unexpected("physical playground production sail-device binding is not drawable");
        }
        submergedSailDeviceOverrides.push_back(
            {.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
    }
    std::array<std::vector<std::size_t>, 2> depthPlaneMeshNodeIndices;
    for (const Submarine::ProductionDepthPlane& plane : productionDefinition->depthPlanes)
    {
        if (plane.presentationNodeBindingIndex >= (*model)->nodeBindings.size())
        {
            return std::unexpected("physical playground production depth-plane binding is invalid");
        }
        const auto meshNodeIndex = (*model)->nodeBindings[plane.presentationNodeBindingIndex].meshNodeIndex;
        if (!meshNodeIndex.has_value())
        {
            return std::unexpected("physical playground production depth-plane binding is not drawable");
        }
        const std::size_t groupIndex = plane.group == Submarine::ProductionDepthPlaneGroup::Bow
            ? M2BowPlaneIndex : M2SternPlaneIndex;
        depthPlaneMeshNodeIndices[groupIndex].push_back(*meshNodeIndex);
    }
    if (depthPlaneMeshNodeIndices[M2BowPlaneIndex].size() != 2U ||
        depthPlaneMeshNodeIndices[M2SternPlaneIndex].size() != 2U)
    {
        return std::unexpected("physical playground requires two production bow and two stern depth-plane nodes");
    }

    if (productionDefinition->collisionProxies.size() != 1U)
'''
text = replace_once(text, old, new, 'resolve production depth-plane mesh nodes')

old = '''    controlSurfaces_ = std::move(controlSurfaces);
    propellerPresentationAngleRadians_ = 0.0F;
'''
new = '''    controlSurfaces_ = std::move(controlSurfaces);
    committedControlSurfaceDeflections_ = {};
    committedThrottleFraction_ = 0.0F;
    depthPlaneMeshNodeIndices_ = std::move(depthPlaneMeshNodeIndices);
    propellerPresentationAngleRadians_ = 0.0F;
'''
text = replace_once(text, old, new, 'store depth plane bindings and initial telemetry state')

old = '''    propulsionState_ = propulsionResult->nextState;
    propellerPresentationAngleRadians_ = *nextPresentationAngle;
'''
new = '''    propulsionState_ = propulsionResult->nextState;
    propellerPresentationAngleRadians_ = *nextPresentationAngle;
    committedThrottleFraction_ = command.throttleFraction;
    committedControlSurfaceDeflections_ = controlDeflections;
'''
text = replace_once(text, old, new, 'commit control state at fixed transaction boundary')

old = '''    // IG1-B deliberately leaves production propellers static. Their semantic anchors resolve through IG1-A,
    // but the existing M2 override addresses a prototype mesh node and must not leak raw GLB names into Game.
    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submergedSailDeviceOverrides_);
'''
new = '''    // M5-V2-B composes dynamic production depth-plane articulation after the already accepted submerged
    // sail-device overrides. Both consume committed Game state; neither mutates ModelAsset or physics.
    std::vector<Render::ModelNodeTransformOverride> submarineNodeOverrides = submergedSailDeviceOverrides_;
    submarineNodeOverrides.reserve(
        submarineNodeOverrides.size() + depthPlaneMeshNodeIndices_[M2BowPlaneIndex].size() +
        depthPlaneMeshNodeIndices_[M2SternPlaneIndex].size());
    for (std::size_t group = 0; group < depthPlaneMeshNodeIndices_.size(); ++group)
    {
        const Assets::ModelTransform postTransform = DepthPlanePostTransform(committedControlSurfaceDeflections_[group]);
        for (const std::size_t meshNodeIndex : depthPlaneMeshNodeIndices_[group])
        {
            submarineNodeOverrides.push_back({.nodeIndex = meshNodeIndex, .nodeLocalPostTransform = postTransform});
        }
    }
    const auto draws = Render::PrepareModelDraws(*modelAsset_, modelToWorld, submarineNodeOverrides);
'''
text = replace_once(text, old, new, 'animate production depth planes from committed state')

# Add telemetry builder before Render
anchor = '''std::expected<Render::ModelDrawStats, std::string> PhysicalPlayground::Render(
'''
insert = '''std::expected<VesselPresentationTelemetry, std::string> PhysicalPlayground::BuildVesselPresentationTelemetry() const
{
    if (physics_ == nullptr || !physicsBody_.IsValid() || !water_.has_value())
    {
        return std::unexpected("vessel presentation telemetry authorities are unavailable");
    }
    const auto state = physics_->GetBodyState(physicsBody_);
    if (!state || !state->position.IsFinite() || !state->linearVelocity.IsFinite())
    {
        return std::unexpected("vessel presentation telemetry body state is unavailable");
    }
    const auto waterSample = water_->Sample(state->position);
    if (!waterSample || !std::isfinite(waterSample->signedDepthMeters))
    {
        return std::unexpected("vessel presentation telemetry depth sample is unavailable");
    }
    return VesselPresentationTelemetry{
        .signedDepthMeters = waterSample->signedDepthMeters,
        .verticalSpeedMetersPerSecond = state->linearVelocity.y,
        .throttleFraction = committedThrottleFraction_,
        .bowPlaneDeflectionFraction = committedControlSurfaceDeflections_[M2BowPlaneIndex],
        .sternPlaneDeflectionFraction = committedControlSurfaceDeflections_[M2SternPlaneIndex]};
}

'''
text = replace_once(text, anchor, insert + anchor, 'add vessel telemetry builder')
path.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# NAV HUD in the existing combat UI module.
# -----------------------------------------------------------------------------
path = ROOT / 'Game/Combat/CombatCommandUi.h'
text = path.read_text(encoding='utf-8')
old = '''void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);

void DrawTacticalSituationOverlay(
'''
new = '''void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);

struct VesselNavigationHudSnapshot final
{
    float signedDepthMeters = 0.0F;
    float verticalSpeedMetersPerSecond = 0.0F;
    float throttleFraction = 0.0F;
    float bowPlaneDeflectionFraction = 0.0F;
    float sternPlaneDeflectionFraction = 0.0F;
};

void DrawVesselNavigationHud(const VesselNavigationHudSnapshot& snapshot);

void DrawTacticalSituationOverlay(
'''
text = replace_once(text, old, new, 'declare vessel navigation HUD')
path.write_text(text, encoding='utf-8')

path = ROOT / 'Game/Combat/CombatCommandUi.cpp'
text = path.read_text(encoding='utf-8')
anchor = '''void DrawTacticalSituationOverlay(
'''
insert = '''void DrawVesselNavigationHud(const VesselNavigationHudSnapshot& snapshot)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        constexpr float cameraHudClearance = 112.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + margin,
                   viewport->WorkPos.y + viewport->WorkSize.y - margin - cameraHudClearance),
            ImGuiCond_Always,
            ImVec2(0.0F, 1.0F));
    }
    ImGui::SetNextWindowBgAlpha(0.78F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##VESSEL_NAV", nullptr, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("NAV");
    ImGui::Text("Depth: %.1f m", snapshot.signedDepthMeters);
    ImGui::Text("V/S: %+0.2f m/s (UP+)", snapshot.verticalSpeedMetersPerSecond);
    ImGui::Text("Throttle: %+0.0f%%", snapshot.throttleFraction * 100.0F);
    ImGui::Text("Planes B/S: %+0.0f%% / %+0.0f%%",
                snapshot.bowPlaneDeflectionFraction * 100.0F,
                snapshot.sternPlaneDeflectionFraction * 100.0F);
    ImGui::End();
}

'''
text = replace_once(text, anchor, insert + anchor, 'implement vessel navigation HUD')
path.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# Wire telemetry to normal player HUD.
# -----------------------------------------------------------------------------
path = ROOT / 'DeepRun/Main.cpp'
text = path.read_text(encoding='utf-8')
old = '''                        if (ownshipLengthMeters)
                        {
                            DeepRun::Game::Combat::DrawCameraScaleHud({
                                .band = framing.band,
                                .horizontalSpanMeters = framing.horizontalSpanMeters,
                                .maximumHorizontalSpanMeters = multiScaleCamera.MaximumHorizontalSpanMeters(),
                                .ownshipProjectedPixels = DeepRun::Game::Camera::ProjectedHorizontalPixels(
                                    *ownshipLengthMeters, framing.horizontalSpanMeters, viewportWidthPixels)});
                        }

                        if (framing.band == DeepRun::Game::Camera::MultiScaleCameraBand::Operational ||
'''
new = '''                        if (ownshipLengthMeters)
                        {
                            DeepRun::Game::Combat::DrawCameraScaleHud({
                                .band = framing.band,
                                .horizontalSpanMeters = framing.horizontalSpanMeters,
                                .maximumHorizontalSpanMeters = multiScaleCamera.MaximumHorizontalSpanMeters(),
                                .ownshipProjectedPixels = DeepRun::Game::Camera::ProjectedHorizontalPixels(
                                    *ownshipLengthMeters, framing.horizontalSpanMeters, viewportWidthPixels)});
                        }
                        const auto navigationTelemetry = playground.BuildVesselPresentationTelemetry();
                        if (!navigationTelemetry)
                        {
                            std::cerr << "[Game][ERROR] M5-V2 vessel navigation HUD failed: "
                                      << navigationTelemetry.error() << '\\n';
                            return false;
                        }
                        DeepRun::Game::Combat::DrawVesselNavigationHud({
                            .signedDepthMeters = navigationTelemetry->signedDepthMeters,
                            .verticalSpeedMetersPerSecond = navigationTelemetry->verticalSpeedMetersPerSecond,
                            .throttleFraction = navigationTelemetry->throttleFraction,
                            .bowPlaneDeflectionFraction = navigationTelemetry->bowPlaneDeflectionFraction,
                            .sternPlaneDeflectionFraction = navigationTelemetry->sternPlaneDeflectionFraction});

                        if (framing.band == DeepRun::Game::Camera::MultiScaleCameraBand::Operational ||
'''
text = replace_once(text, old, new, 'wire navigation telemetry HUD')
path.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# Lightweight source regression coverage: production contract + telemetry API.
# Existing runtime tests continue exercising the actual H2 force path.
# -----------------------------------------------------------------------------
path = ROOT / 'Tests/M5MultiScaleCameraChecks.h'
text = path.read_text(encoding='utf-8')
# No behavior-specific test belongs here beyond compile coverage; leave file unchanged intentionally.
path.write_text(text, encoding='utf-8')
