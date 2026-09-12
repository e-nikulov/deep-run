from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# Stage the two current-main V2 torpedo GLBs into the runtime Content root used by AssetManager.
cmake_path = ROOT / 'CMakeLists.txt'
cmake = cmake_path.read_text(encoding='utf-8')
anchor = '''    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DEEPRUN_ANTEY_RUNTIME_DIR}"
        "$<TARGET_FILE_DIR:DeepRun>/Content/submarines/Antey"
'''
addition = anchor + '''    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DeepRun>/Content/Weapons/Torpedoes/USET80"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Content/Weapons/Torpedoes/USET80/USET80_review.glb"
        "$<TARGET_FILE_DIR:DeepRun>/Content/Weapons/Torpedoes/USET80/USET80_review.glb"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DeepRun>/Content/Weapons/Torpedoes/65-76A"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Content/Weapons/Torpedoes/65-76A/65-76A_Kit_review.glb"
        "$<TARGET_FILE_DIR:DeepRun>/Content/Weapons/Torpedoes/65-76A/65-76A_Kit_review.glb"
'''
if 'USET80_review.glb' not in cmake:
    cmake = replace_once(cmake, anchor, addition, 'CMake torpedo staging')
cmake_path.write_text(cmake, encoding='utf-8')

# The review GLBs are already authored in metres. The old 24 x 5 x 5 scale belonged to the one-metre proxy cube.
presentation_path = ROOT / 'Game/Combat/CombatPlaygroundPresentation.h'
presentation = presentation_path.read_text(encoding='utf-8')
old_scale = '            {.x = 24.0F, .y = 5.0F, .z = 5.0F});'
scale_count = presentation.count(old_scale)
if scale_count != 2:
    raise RuntimeError(f'expected two legacy torpedo proxy scales, found {scale_count}')
presentation = presentation.replace(old_scale, '            {.x = 1.0F, .y = 1.0F, .z = 1.0F});')
presentation_path.write_text(presentation, encoding='utf-8')

# Combat view keeps the proxy cube for destroyer/mine/decoys/VFX but draws both torpedoes with current-main GLBs.
view_path = ROOT / 'Game/Combat/CombatPlaygroundView.h'
view = view_path.read_text(encoding='utf-8')
if '#include "Engine/Assets/AssetManager.h"' not in view:
    view = replace_once(
        view,
        '#include "Engine/Render/D3D12Renderer.h"\n',
        '#include "Engine/Assets/AssetManager.h"\n#include "Engine/Render/D3D12Renderer.h"\n',
        'view AssetManager include')

old_create = '''    [[nodiscard]] static std::expected<CombatPlaygroundView, std::string> Create(
        Render::D3D12Renderer& renderer)
    {
        if (!renderer.IsInitialized() || !renderer.IsModelPipelineReady())
        {
            return std::unexpected("M5-H.1 combat view requires an initialized model renderer");
        }

        const auto model = BuildCombatPlaygroundPresentationModel();
        if (!model)
        {
            return std::unexpected("M5-H.1 combat view model creation failed: " + model.error());
        }
        const auto uploaded = renderer.UploadModel(*model);
        if (!uploaded || !uploaded->handle.IsValid() || !uploaded->stats.uploadCompleted ||
            uploaded->stats.primitiveCount != 1U || uploaded->stats.indexCount != 36U)
        {
            return std::unexpected(uploaded
                ? "M5-H.1 combat view upload produced an invalid GPU model contract"
                : "M5-H.1 combat view upload failed: " + uploaded.error());
        }
        return CombatPlaygroundView(uploaded->handle);
    }
'''
new_create = '''    [[nodiscard]] static std::expected<CombatPlaygroundView, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets)
    {
        if (!renderer.IsInitialized() || !renderer.IsModelPipelineReady())
        {
            return std::unexpected("M5-H.1 combat view requires an initialized model renderer");
        }

        const auto proxyModel = BuildCombatPlaygroundPresentationModel();
        if (!proxyModel)
        {
            return std::unexpected("M5-H.1 combat view model creation failed: " + proxyModel.error());
        }
        const auto proxyUploaded = renderer.UploadModel(*proxyModel);
        if (!proxyUploaded || !proxyUploaded->handle.IsValid() || !proxyUploaded->stats.uploadCompleted ||
            proxyUploaded->stats.primitiveCount != 1U || proxyUploaded->stats.indexCount != 36U)
        {
            return std::unexpected(proxyUploaded
                ? "M5-H.1 combat view upload produced an invalid GPU model contract"
                : "M5-H.1 combat view upload failed: " + proxyUploaded.error());
        }

        // M5-V2 playground visual binding only. These are the current-main V2 review GLBs; simulation,
        // collision, seeker, damage and launch authority remain the existing ConventionalTorpedo runtime.
        const auto uset80 = assets.LoadModel("Weapons/Torpedoes/USET80/USET80_review.glb");
        if (!uset80 || !uset80->IsValid() || uset80->Get() == nullptr || (*uset80)->primitives.empty())
        {
            return std::unexpected(uset80
                ? "M5-V2 USET-80 playground model is invalid"
                : "M5-V2 USET-80 playground model load failed: " + uset80.error().message);
        }
        const auto kit6576 = assets.LoadModel("Weapons/Torpedoes/65-76A/65-76A_Kit_review.glb");
        if (!kit6576 || !kit6576->IsValid() || kit6576->Get() == nullptr || (*kit6576)->primitives.empty())
        {
            return std::unexpected(kit6576
                ? "M5-V2 65-76A playground model is invalid"
                : "M5-V2 65-76A playground model load failed: " + kit6576.error().message);
        }
        const auto uset80Uploaded = renderer.UploadModel(**uset80);
        const auto kit6576Uploaded = renderer.UploadModel(**kit6576);
        if (!uset80Uploaded || !uset80Uploaded->handle.IsValid() || !uset80Uploaded->stats.uploadCompleted ||
            uset80Uploaded->stats.primitiveCount == 0U || !kit6576Uploaded ||
            !kit6576Uploaded->handle.IsValid() || !kit6576Uploaded->stats.uploadCompleted ||
            kit6576Uploaded->stats.primitiveCount == 0U)
        {
            return std::unexpected("M5-V2 production torpedo playground GPU upload failed");
        }

        return CombatPlaygroundView(
            proxyUploaded->handle,
            *uset80,
            uset80Uploaded->handle,
            *kit6576,
            kit6576Uploaded->handle);
    }
'''
view = replace_once(view, old_create, new_create, 'view Create')

start_marker = '''        std::vector<Render::ModelDrawInstance> draws;
        draws.reserve(presentationDraws->size());
'''
end_marker = '''        const bool explosionDrawn = std::ranges::any_of(
'''
start = view.find(start_marker)
end = view.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError('view render composition markers not found')
new_render = '''        std::vector<Render::ModelDrawInstance> proxyDraws;
        proxyDraws.reserve(presentationDraws->size());
        std::optional<Assets::ModelTransform> playerTorpedoTransform{};
        std::optional<Assets::ModelTransform> destroyerTorpedoTransform{};
        for (const auto& presentationDraw : *presentationDraws)
        {
            if (presentationDraw.element == CombatPlaygroundPresentationElement::PlayerTorpedo)
            {
                playerTorpedoTransform = presentationDraw.draw.modelToWorld;
            }
            else if (presentationDraw.element == CombatPlaygroundPresentationElement::DestroyerTorpedo)
            {
                destroyerTorpedoTransform = presentationDraw.draw.modelToWorld;
            }
            else
            {
                proxyDraws.push_back(presentationDraw.draw);
            }
        }
        if (proxyDraws.empty())
        {
            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");
        }

        Render::ModelDrawStats totalStats{};
        const auto accumulate = [&totalStats](const Render::ModelDrawStats& stats)
        {
            totalStats.drawCalls += stats.drawCalls;
            totalStats.submittedPrimitives += stats.submittedPrimitives;
            totalStats.submittedIndices += stats.submittedIndices;
        };

        const auto proxyStats = renderer.DrawModel(
            proxyGpuModel_, std::span<const Render::ModelDrawInstance>(proxyDraws.data(), proxyDraws.size()), camera);
        if (!proxyStats)
        {
            return std::unexpected("M5-H.1 combat proxy view draw failed: " + proxyStats.error());
        }
        accumulate(*proxyStats);

        const auto drawTorpedoModel = [&](const std::optional<Assets::ModelTransform>& transform,
                                          const Assets::AssetHandle<Assets::ModelAsset>& asset,
                                          const Render::GpuModelHandle gpuModel,
                                          const std::string_view label) -> std::expected<void, std::string>
        {
            if (!transform)
            {
                return {};
            }
            const Assets::ModelAsset* model = asset.Get();
            if (model == nullptr || !gpuModel.IsValid() || !renderer.IsGpuModelValid(gpuModel))
            {
                return std::unexpected(std::string(label) + " playground model is unavailable");
            }
            const auto draws = Render::PrepareModelDraws(*model, *transform);
            if (!draws || draws->empty())
            {
                return std::unexpected(draws
                    ? std::string(label) + " produced no presentation draws"
                    : std::string(label) + " draw preparation failed: " + draws.error());
            }
            const auto stats = renderer.DrawModel(
                gpuModel, std::span<const Render::ModelDrawInstance>(draws->data(), draws->size()), camera);
            if (!stats)
            {
                return std::unexpected(std::string(label) + " draw failed: " + stats.error());
            }
            if (stats->drawCalls != draws->size() || stats->submittedPrimitives != draws->size() ||
                stats->submittedIndices == 0U)
            {
                return std::unexpected(std::string(label) + " draw statistics are invalid");
            }
            accumulate(*stats);
            return {};
        };

        // Deliberate playground review pairing: the player visual exercises the 533 mm USET-80 candidate and
        // the hostile visual exercises the 650 mm 65-76A candidate. This is not a destroyer-loadout assertion.
        if (const auto playerDraw = drawTorpedoModel(
                playerTorpedoTransform, uset80Asset_, uset80GpuModel_, "M5-V2 USET-80"); !playerDraw)
        {
            return std::unexpected(playerDraw.error());
        }
        if (const auto hostileDraw = drawTorpedoModel(
                destroyerTorpedoTransform, kit6576Asset_, kit6576GpuModel_, "M5-V2 65-76A"); !hostileDraw)
        {
            return std::unexpected(hostileDraw.error());
        }
        if (totalStats.drawCalls < proxyDraws.size() ||
            totalStats.submittedPrimitives != totalStats.drawCalls || totalStats.submittedIndices < 72U)
        {
            return std::unexpected("M5-V2 combat view aggregate draw statistics are invalid");
        }
'''
view = view[:start] + new_render + view[end:]
view = view.replace('.stats = *stats,', '.stats = totalStats,', 1)

old_model_method = '''    [[nodiscard]] Render::GpuModelHandle Model() const noexcept
    {
        return gpuModel_;
    }

private:
    explicit CombatPlaygroundView(const Render::GpuModelHandle model) noexcept
        : gpuModel_(model)
    {
    }

    Render::GpuModelHandle gpuModel_{};
'''
new_model_method = '''    [[nodiscard]] Render::GpuModelHandle Model() const noexcept
    {
        return proxyGpuModel_;
    }

    [[nodiscard]] bool ModelsValid(const Render::D3D12Renderer& renderer) const noexcept
    {
        return proxyGpuModel_.IsValid() && renderer.IsGpuModelValid(proxyGpuModel_) &&
               uset80Asset_.IsValid() && uset80GpuModel_.IsValid() && renderer.IsGpuModelValid(uset80GpuModel_) &&
               kit6576Asset_.IsValid() && kit6576GpuModel_.IsValid() && renderer.IsGpuModelValid(kit6576GpuModel_);
    }

private:
    CombatPlaygroundView(
        const Render::GpuModelHandle proxyModel,
        Assets::AssetHandle<Assets::ModelAsset> uset80Asset,
        const Render::GpuModelHandle uset80Model,
        Assets::AssetHandle<Assets::ModelAsset> kit6576Asset,
        const Render::GpuModelHandle kit6576Model) noexcept
        : proxyGpuModel_(proxyModel),
          uset80Asset_(std::move(uset80Asset)),
          uset80GpuModel_(uset80Model),
          kit6576Asset_(std::move(kit6576Asset)),
          kit6576GpuModel_(kit6576Model)
    {
    }

    Render::GpuModelHandle proxyGpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> uset80Asset_{};
    Render::GpuModelHandle uset80GpuModel_{};
    Assets::AssetHandle<Assets::ModelAsset> kit6576Asset_{};
    Render::GpuModelHandle kit6576GpuModel_{};
'''
view = replace_once(view, old_model_method, new_model_method, 'view model storage')
view = view.replace('if (!gpuModel_.IsValid() || !renderer.IsGpuModelValid(gpuModel_))',
                    'if (!ModelsValid(renderer))', 1)
view_path.write_text(view, encoding='utf-8')

composition_path = ROOT / 'Game/Combat/CombatPlaygroundWindowedComposition.h'
composition = composition_path.read_text(encoding='utf-8')
composition = replace_once(
    composition,
    '''    [[nodiscard]] static std::expected<CombatPlaygroundWindowedComposition, std::string> Create(
        Render::D3D12Renderer& renderer)
    {
        auto view = CombatPlaygroundView::Create(renderer);
''',
    '''    [[nodiscard]] static std::expected<CombatPlaygroundWindowedComposition, std::string> Create(
        Render::D3D12Renderer& renderer,
        Assets::AssetManager& assets)
    {
        auto view = CombatPlaygroundView::Create(renderer, assets);
''',
    'composition Create')
composition = replace_once(
    composition,
    '        return view_.Model().IsValid() && renderer.IsGpuModelValid(view_.Model());\n',
    '        return view_.ModelsValid(renderer);\n',
    'composition model validity')
composition_path.write_text(composition, encoding='utf-8')

main_path = ROOT / 'DeepRun/Main.cpp'
main = main_path.read_text(encoding='utf-8')
main = replace_once(
    main,
    '                    const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(*renderer);\n',
    '                    const auto combat = DeepRun::Game::Combat::CombatPlaygroundWindowedComposition::Create(*renderer, engine.Assets());\n',
    'Main combat Create')
old_stats = '''                    if (combatRendered->stats.drawCalls < 2U || combatRendered->stats.drawCalls > 8U ||
                        combatRendered->stats.submittedPrimitives != combatRendered->stats.drawCalls ||
                        combatRendered->stats.submittedIndices != combatRendered->stats.drawCalls * 36U)
'''
new_stats = '''                    if (combatRendered->stats.drawCalls < 2U || combatRendered->stats.drawCalls > 20U ||
                        combatRendered->stats.submittedPrimitives != combatRendered->stats.drawCalls ||
                        combatRendered->stats.submittedIndices < 72U)
'''
main = replace_once(main, old_stats, new_stats, 'Main combat stats')
main_path.write_text(main, encoding='utf-8')

# Record the deliberate scope boundary so later production promotion does not accidentally inherit this mapping.
doc_path = ROOT / 'docs/development/m5-v2-player-navigation-sonar.md'
doc = doc_path.read_text(encoding='utf-8')
section = '''
## Main asset sync / torpedo playground binding

The V2 branch is merged with current `main` Antey content before further navigation/sonar work. The current
Project 949A compartment contract and staged production Antey GLB are authoritative on this branch.

Windowed combat playground presentation also loads the current-main V2 torpedo review GLBs directly from
`Content/Weapons/Torpedoes`: USET-80-inspired for the player torpedo and 65-76A-inspired for the hostile test
torpedo. This pairing exists only to exercise both visual candidates in the playground. It does not change
weapon simulation, tube/loadout truth, collision, guidance, seeker, damage, acoustic authority, or production
promotion status. Both GLBs render at authored metre scale; the old 24 x 5 x 5 proxy-cube torpedo visual is not
used when a live underwater torpedo is rendered.
'''
if '## Main asset sync / torpedo playground binding' not in doc:
    doc = doc.rstrip() + '\n' + section
    doc_path.write_text(doc, encoding='utf-8')

print('TORPEDO_PLAYGROUND_PATCH_OK')
