from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8', newline='\n')


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected one match, got {count}: {old[:100]!r}')
    write(path, text.replace(old, new, 1))


def regex_once(path, pattern, repl):
    text = read(path)
    new, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f'{path}: expected one regex match, got {count}: {pattern[:100]!r}')
    write(path, new)

# Normal play uses fog of war; automated/acceptance paths keep historical presentation for regression evidence.
replace_once(
    'Game/Combat/CombatPlaygroundRuntime.h',
    '''        if (!automatedPlayer && !p700AcceptanceMode_)\n        {\n            const auto civilianReady = EnsureCivilianGameplay(playerSnapshot, simulationTimeSeconds);''',
    '''        playerFogOfWarActive_ = !automatedPlayer && !p700AcceptanceMode_;\n        if (playerFogOfWarActive_)\n        {\n            const auto civilianReady = EnsureCivilianGameplay(playerSnapshot, simulationTimeSeconds);''')

replace_once(
    'Game/Combat/CombatPlaygroundRuntime.h',
    '''    [[nodiscard]] const std::optional<DeepRun::Combat::CombatExplosionEvent>& LastExplosion() const noexcept\n    {\n        return lastExplosion_;\n    }\n''',
    '''    [[nodiscard]] const std::optional<DeepRun::Combat::CombatExplosionEvent>& LastExplosion() const noexcept\n    {\n        return lastExplosion_;\n    }\n    [[nodiscard]] bool PlayerFogOfWarActive() const noexcept { return playerFogOfWarActive_; }\n    [[nodiscard]] bool PlayerHasVisualClassification(\n        const Perception::ContactClassification classification) const\n    {\n        return std::ranges::any_of(playerTracks_.Tracks(), [classification](const Perception::Track& track) {\n            return track.lifecycle != Perception::TrackLifecycleState::Lost && track.visuallyIdentified &&\n                   track.classification == classification &&\n                   static_cast<int>(track.opticalIdentificationLevel) >=\n                       static_cast<int>(Perception::OpticalIdentificationLevel::TypeResolved);\n        });\n    }\n''')

# Add member adjacent to mode fields.
replace_once(
    'Game/Combat/CombatPlaygroundRuntime.h',
    '''    bool p700AcceptanceMode_ = false;\n    bool civilianGameplayEnabled_ = false;''',
    '''    bool p700AcceptanceMode_ = false;\n    bool playerFogOfWarActive_ = false;\n    bool civilianGameplayEnabled_ = false;''')

# Presentation snapshot carries whether the physical military contact is known enough to draw.
replace_once(
    'Game/Combat/CombatPlaygroundPresentation.h',
    '''    float destroyerIntegrityFraction = 1.0F;\n    bool destroyerDestroyed = false;''',
    '''    float destroyerIntegrityFraction = 1.0F;\n    bool destroyerDestroyed = false;\n    bool destroyerVisible = true;''')
replace_once(
    'Game/Combat/CombatPlaygroundPresentation.h',
    '''        .destroyerIntegrityFraction = std::clamp(\n            destroyer.integrity.remainingIntegrity / destroyer.integrity.maximumIntegrity, 0.0F, 1.0F),\n        .destroyerDestroyed = destroyer.integrity.destroyed};''',
    '''        .destroyerIntegrityFraction = std::clamp(\n            destroyer.integrity.remainingIntegrity / destroyer.integrity.maximumIntegrity, 0.0F, 1.0F),\n        .destroyerDestroyed = destroyer.integrity.destroyed,\n        .destroyerVisible = !runtime.PlayerFogOfWarActive() ||\n            runtime.PlayerHasVisualClassification(Perception::ContactClassification::MilitarySurfaceCombatant)};''')

# Only materialize the destroyer physical proxy after visual classification in normal play.
replace_once(
    'Game/Combat/CombatPlaygroundPresentation.h',
    '''    draws.push_back(std::move(*hull));\n    draws.push_back(std::move(*superstructure));\n\n    if (snapshot.playerTorpedo''',
    '''    if (snapshot.destroyerVisible)\n    {\n        draws.push_back(std::move(*hull));\n        draws.push_back(std::move(*superstructure));\n    }\n\n    if (snapshot.playerTorpedo''')

# Civilian ground-truth body is likewise hidden until positive type classification in normal play.
replace_once(
    'Game/Combat/CombatPlaygroundView.h',
    '''        const auto civilianDraws = BuildCivilianVesselPresentationDraws(runtime, physicsWorld);\n        if (!civilianDraws)\n        {\n            return std::unexpected("civilian surface-vessel presentation failed: " + civilianDraws.error());\n        }''',
    '''        std::expected<std::vector<Render::ModelDrawInstance>, std::string> civilianDraws =\n            std::vector<Render::ModelDrawInstance>{};\n        if (!runtime.PlayerFogOfWarActive() ||\n            runtime.PlayerHasVisualClassification(Perception::ContactClassification::CivilianSurfaceVessel))\n        {\n            civilianDraws = BuildCivilianVesselPresentationDraws(runtime, physicsWorld);\n        }\n        if (!civilianDraws)\n        {\n            return std::unexpected("civilian surface-vessel presentation failed: " + civilianDraws.error());\n        }''')

# Zero proxy model draws are valid under fog of war. Historical acceptance still takes the old path and stats.
replace_once(
    'Game/Combat/CombatPlaygroundView.h',
    '''        if (proxyDraws.empty())\n        {\n            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");\n        }\n\n        Render::ModelDrawStats totalStats{};''',
    '''        if (proxyDraws.empty() && !runtime.PlayerFogOfWarActive())\n        {\n            return std::unexpected("M5-H.1 combat view must contain at least the destroyer presentation");\n        }\n\n        Render::ModelDrawStats totalStats{};''')
replace_once(
    'Game/Combat/CombatPlaygroundView.h',
    '''        const auto proxyStats = renderer.DrawModel(\n            proxyGpuModel_, std::span<const Render::ModelDrawInstance>(proxyDraws.data(), proxyDraws.size()), camera);\n        if (!proxyStats)\n        {\n            return std::unexpected("M5-H.1 combat proxy view draw failed: " + proxyStats.error());\n        }\n        accumulate(*proxyStats);''',
    '''        if (!proxyDraws.empty())\n        {\n            const auto proxyStats = renderer.DrawModel(\n                proxyGpuModel_, std::span<const Render::ModelDrawInstance>(proxyDraws.data(), proxyDraws.size()), camera);\n            if (!proxyStats)\n            {\n                return std::unexpected("M5-H.1 combat proxy view draw failed: " + proxyStats.error());\n            }\n            accumulate(*proxyStats);\n        }''')
replace_once(
    'Game/Combat/CombatPlaygroundView.h',
    '''        if (totalStats.drawCalls < proxyDraws.size() ||\n            totalStats.submittedPrimitives != totalStats.drawCalls || totalStats.submittedIndices < 72U)\n        {''',
    '''        const bool historicalPresentationGate = !runtime.PlayerFogOfWarActive();\n        if (totalStats.drawCalls < proxyDraws.size() ||\n            totalStats.submittedPrimitives != totalStats.drawCalls ||\n            (historicalPresentationGate && totalStats.submittedIndices < 72U))\n        {''')

# UI command name/help for new semantic input.
replace_once(
    'Game/Combat/CombatCommandUi.cpp',
    '''    case PlayerCombatCommandType::VisualIdentify: return "VISUAL ID";\n    }''',
    '''    case PlayerCombatCommandType::VisualIdentify: return "VISUAL ID";\n    case PlayerCombatCommandType::ToggleP700SalvoMode: return "P-700 SALVO MODE";\n    }''')
replace_once(
    'Game/Combat/CombatCommandUi.cpp',
    '''    ImGui::TextUnformatted("D-pad L/R / Z/C  Select weapon");\n    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");''',
    '''    ImGui::TextUnformatted("D-pad L/R / Z/C  Select weapon");\n    ImGui::TextUnformatted("D-pad Down / G    P-700 single/pair");\n    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");''')

# Sonar labels explicitly expose hypothesis state without claiming truth.
replace_once(
    'Game/Combat/CombatCommandUi.cpp',
    '''        const std::string label = "#" + std::to_string(static_cast<unsigned long long>(contact.trackId));\n        drawList->AddText(ImVec2(labelPoint.x + 6.0F, labelPoint.y - 7.0F), color, label.c_str());''',
    '''        const char* knowledge = "BRG";\n        switch (contact.knowledge)\n        {\n        case ContactKnowledgeLevel::BearingOnly: knowledge = "BRG"; break;\n        case ContactKnowledgeLevel::AreaEstimate: knowledge = "AREA"; break;\n        case ContactKnowledgeLevel::Classified: knowledge = "CLASS"; break;\n        case ContactKnowledgeLevel::PositiveIdentification: knowledge = "ID"; break;\n        case ContactKnowledgeLevel::Coasting: knowledge = "COAST"; break;\n        }\n        const std::string label = "#" + std::to_string(static_cast<unsigned long long>(contact.trackId)) +\n                                  " " + knowledge;\n        drawList->AddText(ImVec2(labelPoint.x + 6.0F, labelPoint.y - 7.0F), color, label.c_str());''')
replace_once(
    'Game/Combat/CombatCommandUi.cpp',
    '    ImGui::TextUnformatted("Bearing-only = radial line | ranged = contact point");\n',
    '    ImGui::TextUnformatted("BRG=line | AREA=estimated region | CLASS/ID=optical evidence | COAST=stale");\n')

# Launcher fixture now mirrors 12 authored paired hatch groups and tests pair/transaction semantics.
replace_once(
    'Tests/P700LauncherInventoryTest.cpp',
    '''        anchors[index].semanticId = "p700.fixture." + std::to_string(index + 1U);\n        anchors[index].localTransform.values[12] = 10.0F + static_cast<float>(index);''',
    '''        anchors[index].semanticId = "p700.fixture." + std::to_string(index + 1U);\n        anchors[index].hatchGroupSemanticId = "FIXTURE_HATCH_" + std::to_string(index / 2U + 1U);\n        anchors[index].localTransform.values[12] = 10.0F + static_cast<float>(index);''')
replace_once(
    'Tests/P700LauncherInventoryTest.cpp',
    '''    const auto consumed = inventory.Consume(0U);\n    if (!consumed || consumed->semanticId != anchors.front().semanticId ||''',
    '''    const auto firstPair = inventory.LoadedSlotIndices(2U);\n    if (!firstPair || firstPair->size() != 2U || (*firstPair)[0] != 0U || (*firstPair)[1] != 1U ||\n        inventory.Slots()[0].anchor.hatchGroupSemanticId != inventory.Slots()[1].anchor.hatchGroupSemanticId)\n    {\n        std::cerr << "P-700 pair selection did not use one authored hatch group\\n";\n        return false;\n    }\n    const std::array<std::size_t, 2> duplicatePair{0U, 0U};\n    if (inventory.ConsumeMany(duplicatePair) || inventory.LoadedCount() != AnteyP700LauncherSlotCount)\n    {\n        std::cerr << "P-700 transactional pair consumption mutated inventory after invalid request\\n";\n        return false;\n    }\n\n    const auto consumed = inventory.Consume(0U);\n    if (!consumed || consumed->semanticId != anchors.front().semanticId ||''')

print('D2 fog closure patch applied')
