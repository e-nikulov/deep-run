from pathlib import Path


def rep(text, old, new, label):
    n=text.count(old)
    if n!=1:
        raise RuntimeError(f"{label}: expected 1 match got {n}")
    return text.replace(old,new,1)

# --- Antey production semantic hatch contract ---
p=Path('Game/Submarine/ProductionAnteyAsset.h'); s=p.read_text()
s=rep(s, '''struct ProductionLaunchAnchor final
{
    std::string semanticId;
    ProductionLocalTransform localTransform{};
    Assets::ModelVector3 launchForward{};
};''', '''struct ProductionLaunchAnchor final
{
    std::string semanticId;
    ProductionLocalTransform localTransform{};
    Assets::ModelVector3 launchForward{};
    // Empty for non-P700 launchers. P-700 anchors retain only an opaque semantic group, never a raw GLB node.
    std::string hatchGroupSemanticId;
};

struct ProductionP700Hatch final
{
    std::string semanticId;
    std::size_t presentationNodeBindingIndex = 0;
};''','launch anchor hatch')
s=rep(s, '''    std::vector<ProductionLaunchAnchor> p700LaunchAnchors;
    std::vector<ProductionCompartment> compartments;''', '''    std::vector<ProductionLaunchAnchor> p700LaunchAnchors;
    std::vector<ProductionP700Hatch> p700Hatches;
    std::vector<ProductionCompartment> compartments;''','definition hatch vector')
p.write_text(s)

p=Path('Game/Submarine/ProductionAnteyAsset.cpp'); s=p.read_text()
s=rep(s, '''            .torpedoLaunchAnchors = {},
            .p700LaunchAnchors = {},
            .compartments = {},''', '''            .torpedoLaunchAnchors = {},
            .p700LaunchAnchors = {},
            .p700Hatches = {},
            .compartments = {},''','antey aggregate hatch init')
s=rep(s, '''            definition.torpedoLaunchAnchors.push_back({
                .semanticId = std::format("torpedo.{}.{}", torpedo.at("tubeClass").get<std::string>(), torpedoOrdinal),
                .localTransform = ReadTransform(torpedo.at("transform"), "torpedo transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(torpedo.at("launchForward"), "torpedo launch forward"))});''', '''            definition.torpedoLaunchAnchors.push_back({
                .semanticId = std::format("torpedo.{}.{}", torpedo.at("tubeClass").get<std::string>(), torpedoOrdinal),
                .localTransform = ReadTransform(torpedo.at("transform"), "torpedo transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(torpedo.at("launchForward"), "torpedo launch forward")),
                .hatchGroupSemanticId = {}});''','torpedo hatch empty')
s=rep(s, '''        const Json& p700Launchers = authoring.at("p700Launchers");
        Require(p700Launchers.is_array() && p700Launchers.size() == 24U, "Antey must have 24 P700 launcher records");
        for (const Json& launcher : p700Launchers)
        {
            const std::string hatchGroup = launcher.at("hatchGroup").get<std::string>();
            const std::size_t pairIndex = ReadCount(launcher.at("pairIndex"), "P700 pair index");
            definition.p700LaunchAnchors.push_back({
                .semanticId = std::format("p700.{}.{}", hatchGroup, pairIndex),
                .localTransform = ReadTransform(launcher.at("transform"), "P700 transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(launcher.at("launchForward"), "P700 launch forward"))});
        }''', '''        const Json& p700Launchers = authoring.at("p700Launchers");
        Require(p700Launchers.is_array() && p700Launchers.size() == 24U, "Antey must have 24 P700 launcher records");
        std::unordered_map<std::string, std::size_t> p700HatchUseCounts;
        for (const Json& launcher : p700Launchers)
        {
            const std::string hatchGroup = launcher.at("hatchGroup").get<std::string>();
            const std::size_t pairIndex = ReadCount(launcher.at("pairIndex"), "P700 pair index");
            Require(pairIndex == 1U || pairIndex == 2U, "P700 paired-hatch launcher index must be 1 or 2");
            ++p700HatchUseCounts[hatchGroup];
            definition.p700LaunchAnchors.push_back({
                .semanticId = std::format("p700.{}.{}", hatchGroup, pairIndex),
                .localTransform = ReadTransform(launcher.at("transform"), "P700 transform"),
                .launchForward = ConvertAnteyAuthoringVector(ReadVector3(launcher.at("launchForward"), "P700 launch forward")),
                .hatchGroupSemanticId = hatchGroup});
        }
        Require(p700HatchUseCounts.size() == 12U, "Antey must expose exactly twelve paired P700 hatch groups");
        for (const auto& [hatchGroup, launcherCount] : p700HatchUseCounts)
        {
            Require(launcherCount == 2U, "each Antey P700 hatch group must own exactly two launchers");
            const bool port = hatchGroup.starts_with("PORT_HATCH_");
            const bool starboard = hatchGroup.starts_with("STBD_HATCH_");
            Require(port || starboard, "P700 hatch group semantic ID must identify port or starboard bank");
            const std::string ordinal = hatchGroup.substr(hatchGroup.size() - 2U);
            const std::string privateNodeReference = std::format(
                "SM_P700_Hatch_{}_{}", port ? "Port" : "Starboard", ordinal);
            const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
            Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value() &&
                    !(**model).nodeBindings.at(bindingIndex).drawableMeshNodeIndices.empty(),
                    "Antey P700 hatch binding must resolve to drawable production geometry");
            definition.p700Hatches.push_back({.semanticId = hatchGroup, .presentationNodeBindingIndex = bindingIndex});
        }
        std::ranges::sort(definition.p700Hatches, {}, &ProductionP700Hatch::semanticId);''','parse hatches')
p.write_text(s)

# --- P700 production booster semantic ---
p=Path('Game/Weapons/ProductionP700Asset.h'); s=p.read_text()
s=rep(s, '''    std::vector<std::size_t> lod0MeshNodeIndices;
    std::vector<ProductionP700MovableSurface> movableSurfaces;''', '''    std::vector<std::size_t> lod0MeshNodeIndices;
    std::vector<std::size_t> boosterLod0MeshNodeIndices;
    std::vector<ProductionP700MovableSurface> movableSurfaces;''','p700 booster def')
s=rep(s, '''[[nodiscard]] bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    std::size_t meshNodeIndex) noexcept;''', '''[[nodiscard]] bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    std::size_t meshNodeIndex) noexcept;

[[nodiscard]] bool IsProductionP700BoosterLod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    std::size_t meshNodeIndex) noexcept;''','booster predicate decl')
p.write_text(s)

p=Path('Game/Weapons/ProductionP700Asset.cpp'); s=p.read_text()
s=rep(s, '''        return ProductionP700AssetDefinition{
            .modelAssetId = model->id,
            .deploymentAnimationName = animation.name,
            .authoredDeploymentDurationSeconds = animation.durationSeconds,
            .lod0MeshNodeIndices = std::move(lod0MeshNodes),
            .movableSurfaces = std::move(surfaces)};''', '''        const auto boosterBindingIndex = FindUniqueBinding(*model, "SM_P700_LOD0_Booster");
        if (!boosterBindingIndex || model->nodeBindings[*boosterBindingIndex].drawableMeshNodeIndices.empty())
        {
            return std::unexpected("staged P-700 LOD0 booster must have one unique drawable semantic binding");
        }
        const std::vector<std::size_t> boosterNodes =
            model->nodeBindings[*boosterBindingIndex].drawableMeshNodeIndices;
        return ProductionP700AssetDefinition{
            .modelAssetId = model->id,
            .deploymentAnimationName = animation.name,
            .authoredDeploymentDurationSeconds = animation.durationSeconds,
            .lod0MeshNodeIndices = std::move(lod0MeshNodes),
            .boosterLod0MeshNodeIndices = boosterNodes,
            .movableSurfaces = std::move(surfaces)};''','booster loader')
s += '''\n'''
s=rep(s, '''bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    const std::size_t meshNodeIndex) noexcept
{
    return std::find(definition.lod0MeshNodeIndices.begin(), definition.lod0MeshNodeIndices.end(), meshNodeIndex) !=
           definition.lod0MeshNodeIndices.end();
}
} // namespace DeepRun::Game::Armament''', '''bool IsProductionP700Lod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    const std::size_t meshNodeIndex) noexcept
{
    return std::find(definition.lod0MeshNodeIndices.begin(), definition.lod0MeshNodeIndices.end(), meshNodeIndex) !=
           definition.lod0MeshNodeIndices.end();
}

bool IsProductionP700BoosterLod0MeshNode(
    const ProductionP700AssetDefinition& definition,
    const std::size_t meshNodeIndex) noexcept
{
    return std::find(definition.boosterLod0MeshNodeIndices.begin(), definition.boosterLod0MeshNodeIndices.end(), meshNodeIndex) !=
           definition.boosterLod0MeshNodeIndices.end();
}
} // namespace DeepRun::Game::Armament''','booster predicate impl')
p.write_text(s)

# --- Physical Antey hatch presentation ---
p=Path('Game/PhysicalPlayground.h'); s=p.read_text()
s=rep(s, '''    [[nodiscard]] float PresentationCameraHorizontalSpanMeters() const noexcept
    {
        return M2GameplayCameraHorizontalSpanMeters;
    }
''', '''    [[nodiscard]] float PresentationCameraHorizontalSpanMeters() const noexcept
    {
        return M2GameplayCameraHorizontalSpanMeters;
    }

    // Presentation-only bridge from the P-700 lifecycle. Semantic hatch identity was resolved from production
    // authoring once at Initialize; no raw GLB node name or gameplay launch decision enters the renderer.
    [[nodiscard]] std::expected<void, std::string> SetP700HatchPresentation(
        const std::optional<std::string>& hatchGroupSemanticId,
        const float openProgress)
    {
        if (!std::isfinite(openProgress) || openProgress < 0.0F || openProgress > 1.0F)
            return std::unexpected("P-700 hatch presentation progress must be finite in [0,1]");
        if (hatchGroupSemanticId.has_value())
        {
            const auto found = std::ranges::find_if(p700HatchBindings_, [&](const auto& hatch) {
                return hatch.first == *hatchGroupSemanticId;
            });
            if (found == p700HatchBindings_.end())
                return std::unexpected("P-700 hatch presentation semantic group is not present in production Antey");
        }
        activeP700HatchGroup_ = hatchGroupSemanticId;
        activeP700HatchOpenProgress_ = openProgress;
        return {};
    }
''','physical hatch setter')
s=rep(s, '''    std::vector<std::size_t> propellerNodeBindingIndices_{};
    Submarine::AnteyFacingState facingState_{};''', '''    std::vector<std::size_t> propellerNodeBindingIndices_{};
    std::vector<std::pair<std::string, std::size_t>> p700HatchBindings_{};
    std::optional<std::string> activeP700HatchGroup_{};
    float activeP700HatchOpenProgress_ = 0.0F;
    Submarine::AnteyFacingState facingState_{};''','physical hatch members')
p.write_text(s)

p=Path('Game/PhysicalPlayground.cpp'); s=p.read_text()
# helper
anchor='''Assets::ModelTransform DepthPlanePostTransform(const float committedDeflectionFraction) noexcept
{'''
idx=s.index(anchor)
end=s.index('\n}\n', idx)+3
helper='''\nAssets::ModelTransform P700HatchOpenPostTransform(const float progress) noexcept
{
    Assets::ModelTransform transform{};
    // The accepted production hatch meshes are distinct but do not yet publish physical hinge pivots. Until
    // authoring adds that semantic, use a bounded vertical lift-open presentation rather than inventing a hinge.
    transform.values[13] = 2.4F * std::clamp(progress, 0.0F, 1.0F);
    return transform;
}\n'''
s=s[:end]+helper+s[end:]
# initialize hatch bindings after propeller block
needle='''    if (propellerNodeBindingIndices.size() != 2U)
    {
        return std::unexpected("physical playground requires two production Antey propeller bindings");
    }
'''
add='''    if (propellerNodeBindingIndices.size() != 2U)
    {
        return std::unexpected("physical playground requires two production Antey propeller bindings");
    }

    std::vector<std::pair<std::string, std::size_t>> p700HatchBindings;
    p700HatchBindings.reserve(productionDefinition->p700Hatches.size());
    for (const Submarine::ProductionP700Hatch& hatch : productionDefinition->p700Hatches)
    {
        if (hatch.semanticId.empty() || hatch.presentationNodeBindingIndex >= (*model)->nodeBindings.size() ||
            (*model)->nodeBindings[hatch.presentationNodeBindingIndex].drawableMeshNodeIndices.empty())
            return std::unexpected("physical playground production P-700 hatch semantic binding is invalid");
        p700HatchBindings.emplace_back(hatch.semanticId, hatch.presentationNodeBindingIndex);
    }
    if (p700HatchBindings.size() != 12U)
        return std::unexpected("physical playground requires twelve production P-700 paired hatch bindings");
'''
s=rep(s,needle,add,'physical init hatch bindings')
s=rep(s, '''    propellerNodeBindingIndices_ = std::move(propellerNodeBindingIndices);
    facingState_ = {};''', '''    propellerNodeBindingIndices_ = std::move(propellerNodeBindingIndices);
    p700HatchBindings_ = std::move(p700HatchBindings);
    activeP700HatchGroup_.reset();
    activeP700HatchOpenProgress_ = 0.0F;
    facingState_ = {};''','store hatch bindings')
s=rep(s, '''    std::vector<Render::ModelBindingTransformOverride> submarineBindingOverrides;
    submarineBindingOverrides.reserve(propellerNodeBindingIndices_.size());
    for (const std::size_t bindingIndex : propellerNodeBindingIndices_)
    {
        submarineBindingOverrides.push_back(
            {.bindingIndex = bindingIndex, .bindingLocalPostTransform = propellerPostTransform});
    }''', '''    std::vector<Render::ModelBindingTransformOverride> submarineBindingOverrides;
    submarineBindingOverrides.reserve(propellerNodeBindingIndices_.size() + 1U);
    for (const std::size_t bindingIndex : propellerNodeBindingIndices_)
    {
        submarineBindingOverrides.push_back(
            {.bindingIndex = bindingIndex, .bindingLocalPostTransform = propellerPostTransform});
    }
    if (activeP700HatchGroup_.has_value() && activeP700HatchOpenProgress_ > 0.0F)
    {
        const auto hatch = std::ranges::find_if(p700HatchBindings_, [&](const auto& value) {
            return value.first == *activeP700HatchGroup_;
        });
        if (hatch == p700HatchBindings_.end())
            return std::unexpected("physical playground active P-700 hatch binding disappeared");
        submarineBindingOverrides.push_back({
            .bindingIndex = hatch->second,
            .bindingLocalPostTransform = P700HatchOpenPostTransform(activeP700HatchOpenProgress_)});
    }''','render hatch override')
p.write_text(s)

# --- Runtime remember launch slot/hatch ---
p=Path('Game/Combat/CombatPlaygroundRuntime.h'); s=p.read_text()
s=rep(s, '''    [[nodiscard]] const std::optional<Weapons::P700GranitRuntimeState>& PlayerP700() const noexcept
    {
        return playerP700_;
    }
''', '''    [[nodiscard]] const std::optional<Weapons::P700GranitRuntimeState>& PlayerP700() const noexcept
    {
        return playerP700_;
    }
    [[nodiscard]] std::optional<std::string> PlayerP700HatchGroupSemanticId() const
    {
        if (!playerP700LaunchSlotIndex_ || !p700LauncherInventory_ ||
            *playerP700LaunchSlotIndex_ >= p700LauncherInventory_->Slots().size())
            return std::nullopt;
        return p700LauncherInventory_->Slots()[*playerP700LaunchSlotIndex_].anchor.hatchGroupSemanticId;
    }
''','runtime hatch accessor')
s=rep(s, '''        playerP700_ = std::move(*missile);
        return {};''', '''        playerP700LaunchSlotIndex_ = launch->slotIndex;
        playerP700_ = std::move(*missile);
        return {};''','store launch slot')
# locate member near playerP700
s=rep(s, '''    std::optional<Weapons::P700GranitRuntimeState> playerP700_{};''', '''    std::optional<Weapons::P700GranitRuntimeState> playerP700_{};
    std::optional<std::size_t> playerP700LaunchSlotIndex_{};''','slot member')
p.write_text(s)

# --- Presentation state and visual proxies ---
p=Path('Game/Combat/CombatPlaygroundPresentation.h'); s=p.read_text()
s=rep(s, '''struct CombatPlaygroundP700Presentation final
{
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    float deploymentProgress = 0.0F;
    Weapons::P700GranitPhase phase = Weapons::P700GranitPhase::Stored;
};''', '''struct CombatPlaygroundP700Presentation final
{
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    float hatchOpenProgress = 0.0F;
    float postExitTransitionProgress = 0.0F;
    float deploymentProgress = 0.0F;
    bool launchBoosterActive = false;
    bool launchBoosterAttached = true;
    bool noseProtectionCapAttached = true;
    bool mainEngineActive = false;
    Weapons::P700TerminalEngagementOutcome terminalOutcome = Weapons::P700TerminalEngagementOutcome::Unresolved;
    Weapons::P700GranitPhase phase = Weapons::P700GranitPhase::Stored;
};''','p700 presentation fields')
s=rep(s, '''    Explosion,
};''', '''    Explosion,
    P700NoseProtectionCap,
    P700DetachedNoseProtectionCap,
    P700LaunchBoosterPlume,
    P700MainEnginePlume,
};''','presentation elements')
s=rep(s, '''        if (!p700->positionMeters.IsFinite() || !std::isfinite(p700->headingRadians) ||
            !std::isfinite(p700->deploymentProgress) || p700->deploymentProgress < 0.0F ||
            p700->deploymentProgress > 1.0F)
        {
            return std::unexpected("M5 P-700 presentation state is invalid");
        }
        snapshot.playerP700 = CombatPlaygroundP700Presentation{
            .positionMeters = p700->positionMeters,
            .headingRadians = p700->headingRadians,
            .deploymentProgress = p700->deploymentProgress,
            .phase = p700->phase};''', '''        if (!p700->positionMeters.IsFinite() || !std::isfinite(p700->headingRadians) ||
            !std::isfinite(p700->hatchOpenProgress) || p700->hatchOpenProgress < 0.0F || p700->hatchOpenProgress > 1.0F ||
            !std::isfinite(p700->postExitTransitionProgress) || p700->postExitTransitionProgress < 0.0F ||
            p700->postExitTransitionProgress > 1.0F || !std::isfinite(p700->deploymentProgress) ||
            p700->deploymentProgress < 0.0F || p700->deploymentProgress > 1.0F)
        {
            return std::unexpected("M5 P-700 presentation state is invalid");
        }
        snapshot.playerP700 = CombatPlaygroundP700Presentation{
            .positionMeters = p700->positionMeters,
            .headingRadians = p700->headingRadians,
            .hatchOpenProgress = p700->hatchOpenProgress,
            .postExitTransitionProgress = p700->postExitTransitionProgress,
            .deploymentProgress = p700->deploymentProgress,
            .launchBoosterActive = p700->launchBoosterActive,
            .launchBoosterAttached = p700->launchBoosterAttached,
            .noseProtectionCapAttached = p700->noseProtectionCapAttached,
            .mainEngineActive = p700->mainEngineActive,
            .terminalOutcome = p700->terminalOutcome,
            .phase = p700->phase};''','snapshot p700 fields')
# reserve and insert proxy P700 VFX before decoy
s=s.replace('draws.reserve(8U);','draws.reserve(14U);')
needle='''    if (snapshot.decoy && snapshot.decoy->active)
    {'''
block='''    if (snapshot.playerP700)
    {
        const auto missilePose = BodyPoseTransform(
            snapshot.playerP700->positionMeters,
            Weapons::P700HeadingQuaternion(snapshot.playerP700->headingRadians));
        if (!missilePose) return std::unexpected(missilePose.error());
        const auto addP700Proxy = [&](const CombatPlaygroundPresentationElement element,
                                     const Physics::PhysicsVector3& scale,
                                     const Physics::PhysicsVector3& localOffset,
                                     const std::array<float,4>& color) -> std::expected<void,std::string>
        {
            const Assets::ModelTransform transform = Render::Multiply(*missilePose, LocalScaleTranslation(scale, localOffset));
            auto draw = MakeDraw(element, transform, Material("M5P700Transient", color, 0.02F, 0.20F));
            if (!draw) return std::unexpected(draw.error());
            draws.push_back(std::move(*draw));
            return {};
        };
        // Canonical GLB has no separately authored nose protection cap. This small fairing proxy is therefore
        // presentation-only and deliberately never participates in missile bounds, collision or damage.
        if (snapshot.playerP700->noseProtectionCapAttached)
        {
            if (auto r = addP700Proxy(CombatPlaygroundPresentationElement::P700NoseProtectionCap,
                    {0.65F, 0.95F, 0.95F}, {5.25F, 0.0F, 0.0F}, {0.72F,0.74F,0.72F,1.0F}); !r) return std::unexpected(r.error());
        }
        else if (snapshot.playerP700->phase == Weapons::P700GranitPhase::PostExitTransition &&
                 snapshot.playerP700->postExitTransitionProgress < 0.80F)
        {
            const float p = snapshot.playerP700->postExitTransitionProgress;
            if (auto r = addP700Proxy(CombatPlaygroundPresentationElement::P700DetachedNoseProtectionCap,
                    {0.65F, 0.95F, 0.95F}, {5.25F + 3.0F*p, 1.5F*p, 0.0F}, {0.72F,0.74F,0.72F,1.0F}); !r) return std::unexpected(r.error());
        }
        if (snapshot.playerP700->launchBoosterActive)
        {
            if (auto r = addP700Proxy(CombatPlaygroundPresentationElement::P700LaunchBoosterPlume,
                    {3.4F, 0.32F, 0.32F}, {-6.0F, 0.0F, 0.0F}, {1.0F,0.72F,0.18F,1.0F}); !r) return std::unexpected(r.error());
        }
        if (snapshot.playerP700->mainEngineActive)
        {
            if (auto r = addP700Proxy(CombatPlaygroundPresentationElement::P700MainEnginePlume,
                    {4.8F, 0.28F, 0.28F}, {-6.8F, 0.0F, 0.0F}, {1.0F,0.46F,0.08F,1.0F}); !r) return std::unexpected(r.error());
        }
    }

    if (snapshot.decoy && snapshot.decoy->active)
    {'''
s=rep(s,needle,block,'p700 vfx proxies')
p.write_text(s)

# --- View: remove attached booster after separation and render detached production booster ---
p=Path('Game/Combat/CombatPlaygroundView.h'); s=p.read_text()
s=rep(s, '''                if (Armament::IsProductionP700Lod0MeshNode(p700Definition_, draw.nodeIndex))
                {
                    p700Draws.push_back(draw);
                }''', '''                if (Armament::IsProductionP700Lod0MeshNode(p700Definition_, draw.nodeIndex) &&
                    (snapshot->playerP700->launchBoosterAttached ||
                     !Armament::IsProductionP700BoosterLod0MeshNode(p700Definition_, draw.nodeIndex)))
                {
                    p700Draws.push_back(draw);
                }''','filter booster')
needle='''            accumulate(*p700Stats);
        }
        if (totalStats.drawCalls < proxyDraws.size() ||'''
block='''            accumulate(*p700Stats);

            // During the bounded separation phase draw the canonical booster mesh once more as a detached
            // presentation object. It is no longer part of missile collision/authority; this is visual evidence
            // of the lifecycle state only.
            if (!snapshot->playerP700->launchBoosterAttached &&
                snapshot->playerP700->phase == Weapons::P700GranitPhase::PostExitTransition &&
                snapshot->playerP700->postExitTransitionProgress < 0.95F)
            {
                const float p = snapshot->playerP700->postExitTransitionProgress;
                Assets::ModelTransform separation{};
                separation.values[12] = -4.0F * p;
                separation.values[13] = -2.0F * p;
                const Assets::ModelTransform detachedToWorld = Render::Multiply(*modelToWorld, separation);
                const auto detachedPrepared = Render::PrepareModelDraws(*p700Asset_, detachedToWorld);
                if (!detachedPrepared) return std::unexpected("M5 detached P-700 booster draw preparation failed: " + detachedPrepared.error());
                std::vector<Render::ModelDrawInstance> detachedBoosterDraws;
                for (const auto& draw : *detachedPrepared)
                    if (Armament::IsProductionP700BoosterLod0MeshNode(p700Definition_, draw.nodeIndex)) detachedBoosterDraws.push_back(draw);
                if (detachedBoosterDraws.empty()) return std::unexpected("M5 detached P-700 booster has no LOD0 draw");
                const auto detachedStats = renderer.DrawModel(p700GpuModel_,
                    std::span<const Render::ModelDrawInstance>(detachedBoosterDraws.data(), detachedBoosterDraws.size()), camera);
                if (!detachedStats) return std::unexpected("M5 detached P-700 booster draw failed: " + detachedStats.error());
                accumulate(*detachedStats);
            }
        }
        if (totalStats.drawCalls < proxyDraws.size() ||'''
s=rep(s,needle,block,'detached booster draw')
p.write_text(s)

# --- Main: send lifecycle hatch state into production Antey presentation ---
p=Path('DeepRun/Main.cpp'); s=p.read_text()
needle='''                    combatUiSnapshot = combatFrame->playerCombat;
                    if (options.smokeTest)'''
block='''                    combatUiSnapshot = combatFrame->playerCombat;
                    if (combatPlayground->Runtime().has_value())
                    {
                        const auto& runtime = *combatPlayground->Runtime();
                        std::optional<std::string> hatchGroup{};
                        float hatchProgress = 0.0F;
                        if (runtime.PlayerP700().has_value())
                        {
                            hatchGroup = runtime.PlayerP700HatchGroupSemanticId();
                            hatchProgress = runtime.PlayerP700()->hatchOpenProgress;
                        }
                        const auto hatchPresentation = playground.SetP700HatchPresentation(hatchGroup, hatchProgress);
                        if (!hatchPresentation)
                        {
                            std::cerr << "[Game][ERROR] M5 P-700 production hatch presentation failed: "
                                      << hatchPresentation.error() << '\\n';
                            return false;
                        }
                    }
                    if (options.smokeTest)'''
s=rep(s,needle,block,'main hatch bridge')
p.write_text(s)

print('full P700 production presentation patch applied')
