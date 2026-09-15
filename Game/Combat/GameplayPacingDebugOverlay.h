#pragma once

#include "Game/Combat/GameplayPacingMetrics.h"

#include <imgui.h>

namespace DeepRun::Game::Combat
{
namespace Detail
{
inline bool HasGameplayPacingDebugData(const GameplayPacingMetricsSnapshot& snapshot) noexcept
{
    return snapshot.playerElapsedSeconds > 0.0 ||
           snapshot.timeToFirstContactSeconds.has_value() ||
           snapshot.timeToClassificationSeconds.has_value() ||
           snapshot.timeToFirstWeaponLaunchSeconds.has_value() ||
           snapshot.playerDecisionCount > 0U;
}

inline void DrawPacingMilestone(const char* label, const std::optional<double>& seconds)
{
    if (seconds)
    {
        ImGui::Text("%-18s %6.1f s", label, *seconds);
    }
    else
    {
        ImGui::Text("%-18s %s", label, "PENDING");
    }
}
} // namespace Detail

// Debug-only pacing telemetry. This window has no input path back into gameplay and is intentionally kept out of
// smoke/acceptance runs: those paths never call GameplayPacingMetrics::Observe, so the default snapshot renders
// nothing. Normal play gets the overlay as part of the existing debug HUD composition.
inline void DrawGameplayPacingDebugOverlay(const GameplayPacingMetricsSnapshot& snapshot)
{
    if (!Detail::HasGameplayPacingDebugData(snapshot) || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        constexpr float engineDebugClearance = 170.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + engineDebugClearance),
            ImGuiCond_Always,
            ImVec2(0.0F, 0.0F));
    }

    ImGui::SetNextWindowBgAlpha(0.82F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##GAMEPLAY_PACING_DEBUG", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("GAMEPLAY PACING [DEBUG]");
    ImGui::Separator();
    ImGui::Text("Player time:        %6.1f s", snapshot.playerElapsedSeconds);
    Detail::DrawPacingMilestone("First contact:", snapshot.timeToFirstContactSeconds);
    Detail::DrawPacingMilestone("Classification:", snapshot.timeToClassificationSeconds);
    Detail::DrawPacingMilestone("First launch:", snapshot.timeToFirstWeaponLaunchSeconds);
    ImGui::Separator();
    ImGui::Text("No-decision now:    %6.1f s", snapshot.currentNoDecisionIntervalSeconds);
    ImGui::Text("No-decision longest:%6.1f s", snapshot.longestNoDecisionIntervalSeconds);
    ImGui::Text("Player decisions:   %6llu", static_cast<unsigned long long>(snapshot.playerDecisionCount));

    if (snapshot.playerElapsedSeconds > 0.0)
    {
        const double decisionsPerMinute =
            static_cast<double>(snapshot.playerDecisionCount) * 60.0 / snapshot.playerElapsedSeconds;
        ImGui::Text("Decision rate:      %6.1f /min", decisionsPerMinute);
    }

    if (snapshot.currentNoDecisionIntervalSeconds >= 45.0)
    {
        ImGui::TextUnformatted("PACING: LONG IDLE INTERVAL");
    }
    else if (snapshot.currentNoDecisionIntervalSeconds >= 20.0)
    {
        ImGui::TextUnformatted("PACING: IDLE INTERVAL GROWING");
    }

    ImGui::End();
}
} // namespace DeepRun::Game::Combat
