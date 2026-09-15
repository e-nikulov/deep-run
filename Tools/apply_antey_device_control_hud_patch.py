from pathlib import Path

path = Path("Game/Combat/CombatCommandUi.cpp")
text = path.read_text(encoding="utf-8")
start_marker = '    ImGui::Separator();\n    ImGui::TextUnformatted("ELECTRONICS / MASTS");\n'
end_marker = '    ImGui::Separator();\n    ImGui::TextUnformatted("THREAT");\n'
start = text.index(start_marker)
end = text.index(end_marker, start)

replacement = r'''    ImGui::Separator();
    ImGui::TextUnformatted("DEVICE CONTROL");
    ImGui::TextDisabled("LB / R: select device     B / =: operate selected");

    const auto deviceStateLabel = [&snapshot](const AnteyElectronicSystem system) -> const char*
    {
        const bool deployed = snapshot.electronicSystemDeployed[AnteyElectronicSystemIndex(system)];
        if (!deployed)
            return "STOWED";
        switch (system)
        {
        case AnteyElectronicSystem::SynthesisSatNav: return "SAT FIX";
        case AnteyElectronicSystem::ZonaRadioDirectionFinder: return "PASSIVE ESM";
        case AnteyElectronicSystem::AnisRadio: return "RX READY";
        case AnteyElectronicSystem::Mrsc2TargetingReceiver: return "CU RECEIVE";
        case AnteyElectronicSystem::RadianSurfaceRadar:
            return snapshot.radianTransmitting ? "RADAR TX" : "STANDBY";
        case AnteyElectronicSystem::KoraCommunications: return "RX READY";
        case AnteyElectronicSystem::RkpCompressorIntake:
            return snapshot.rkpCompressorRunning ? "RUNNING" :
                   (snapshot.rkpCompressorRequested ? "REQUESTED" : "RAISED");
        case AnteyElectronicSystem::SelenaSatelliteTargeting: return "CU RECEIVE";
        case AnteyElectronicSystem::Signal3NavigationPeriscope: return "OPTICS";
        case AnteyElectronicSystem::Pzns10AttackPeriscope: return "OPTICS";
        case AnteyElectronicSystem::Count: break;
        }
        return "UNKNOWN";
    };

    if (ImGui::BeginChild("##ANTEY_DEVICE_CONTROL_MENU", ImVec2(0.0F, 225.0F), true,
                          ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        for (std::size_t index = 0; index < AnteyElectronicSystemCount; ++index)
        {
            const auto system = static_cast<AnteyElectronicSystem>(index);
            const std::string_view name = AnteyElectronicSystemName(system);
            const bool selected = system == snapshot.selectedElectronicSystem;
            const char* state = deviceStateLabel(system);
            if (selected)
            {
                ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.36F, 1.0F),
                                   "> %02zu  %.*s", index + 1U,
                                   static_cast<int>(name.size()), name.data());
                ImGui::SameLine();
                ImGui::TextColored(
                    snapshot.radianTransmitting && system == AnteyElectronicSystem::RadianSurfaceRadar
                        ? ImVec4(1.0F, 0.42F, 0.25F, 1.0F)
                        : ImVec4(0.72F, 0.90F, 1.0F, 1.0F),
                    "[%s]", state);
            }
            else
            {
                ImGui::Text("  %02zu  %.*s", index + 1U,
                            static_cast<int>(name.size()), name.data());
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", state);
            }
        }
    }
    ImGui::EndChild();

    const std::string_view selectedElectronicName = AnteyElectronicSystemName(snapshot.selectedElectronicSystem);
    const bool selectedElectronicRaised = snapshot.electronicSystemDeployed[
        AnteyElectronicSystemIndex(snapshot.selectedElectronicSystem)];
    const char* selectedAction = selectedElectronicRaised ? "STOW" : "RAISE";
    if (snapshot.selectedElectronicSystem == AnteyElectronicSystem::RadianSurfaceRadar && selectedElectronicRaised)
        selectedAction = snapshot.radianTransmitting ? "STOP RADAR TX" : "START RADAR TX";
    else if ((snapshot.selectedElectronicSystem == AnteyElectronicSystem::AnisRadio ||
              snapshot.selectedElectronicSystem == AnteyElectronicSystem::KoraCommunications) && selectedElectronicRaised)
        selectedAction = "BURST TRANSMIT";
    else if (snapshot.selectedElectronicSystem == AnteyElectronicSystem::Pzns10AttackPeriscope)
        selectedAction = snapshot.periscopeRaised ? "STOW WITH P / D-PAD UP" : "RAISE WITH P / D-PAD UP";

    ImGui::Text("Selected: %.*s", static_cast<int>(selectedElectronicName.size()), selectedElectronicName.data());
    ImGui::Text("Action: %s", selectedAction);
    ImGui::TextDisabled("Shared hardware: ZONA + RADIAN | KORA + RKP");

    if (snapshot.radianTransmitting)
        ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.25F, 1.0F), "RADIAN: TRANSMITTING / ESM INTERCEPT RISK");
    else
        ImGui::TextUnformatted("RADIAN: EMCON");
    if (snapshot.radioTransmitting)
        ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.25F, 1.0F), "Radio: BURST TX / ESM INTERCEPT RISK");
    else
        ImGui::TextUnformatted("Radio: SILENT");

    ImGui::Text("INS position error: +/- %.0f m", snapshot.navigationErrorMeters);
    if (snapshot.externalTargetReportSource)
    {
        const std::string_view reportSource = ExternalTargetReportSourceName(*snapshot.externalTargetReportSource);
        ImGui::Text("External CU: %.*s | age %.0f s | unc +/- %.1f km",
                    static_cast<int>(reportSource.size()), reportSource.data(),
                    snapshot.externalTargetReportAgeSeconds.value_or(0.0F),
                    snapshot.externalTargetReportUncertaintyMeters.value_or(0.0F) / 1000.0F);
    }
    else
        ImGui::TextUnformatted("External CU: NONE");
    ImGui::Text("HP air: %.0f%% | RKP: %s", snapshot.highPressureAirFraction * 100.0F,
                snapshot.rkpCompressorRunning ? "RUNNING" :
                (snapshot.rkpCompressorRequested ? "REQUESTED" : "OFF"));

'''

path.write_text(text[:start] + replacement + text[end:], encoding="utf-8")
