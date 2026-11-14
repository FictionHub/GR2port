// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <cmrc/cmrc.hpp>
#include <fmt/chrono.h>
#include <imgui.h>

#include "core/game_check/trophy_list.h"
#include "core/language/language.h"
#include "imgui/imgui_layer.h"
#include "imgui/imgui_texture.h"
#include "imgui/renderer/imgui_core.h"
#include "imgui/trophy_list_layer.h"

CMRC_DECLARE(res);

namespace Gr2Port::TrophyList {

namespace {

constexpr std::array<char, 4> Grades{'B', 'S', 'G', 'P'};
constexpr std::array<const char*, Grades.size()> GradeFiles{
    "src/resources/bronze.png", "src/resources/silver.png", "src/resources/gold.png",
    "src/resources/platinum.png"};

// The launcher's trophy viewer filters: the earned trophies, the others, and the hidden ones
// among the others.
constexpr std::array<const char*, 3> Filters{"Show Earned Trophies", "Show Not Earned Trophies",
                                             "Show Hidden Trophies"};
constexpr size_t EarnedFilter = 0;
constexpr size_t NotEarnedFilter = 1;
constexpr size_t HiddenFilter = 2;

// Canvas pixels.
constexpr float PanelWidth = 880.0f;
constexpr float PanelMargin = 32.0f;
constexpr float IconSize = 56.0f;
constexpr float GradeSize = 24.0f;
constexpr float TimeWidth = 150.0f;
constexpr float TitleSize = 48.0f;
constexpr float NotEarnedAlpha = 0.45f;

class ListLayer final : public ImGui::Layer {
public:
    ListLayer();

    void Draw() override;

private:
    void DrawHeader();
    void DrawRow(size_t index, bool first);
    void Finish();

    std::vector<Trophies::Trophy> trophies;
    std::vector<ImGui::RefCountedTexture> icons;
    std::array<ImGui::RefCountedTexture, Grades.size()> grade_icons;
    // The earned trophies and all trophies of each grade.
    std::array<std::array<size_t, 2>, Grades.size()> grade_counts{};
    size_t earned = 0;
    std::array<bool, Filters.size()> filters{true, true, false};
    // The list is closed and the layer stays until no key or button is held, so the game never
    // reads the press that closed it.
    bool closing = false;
};

std::optional<ListLayer> g_layer;
std::atomic_bool g_open{false};
// The trophy list closes on its next frame.
std::atomic_bool g_close{false};

size_t GradeIndex(char grade) {
    return static_cast<size_t>(std::ranges::find(Grades, grade) - Grades.begin());
}

// Whether a key or a pad button is held; the sticks and the mouse do not count.
bool KeyHeld() {
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadLStickLeft; ++key) {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) {
            return true;
        }
    }
    return false;
}

// The time a trophy was earned in the player's time zone.
std::string TimeText(u64 seconds) {
    const std::time_t time = static_cast<std::time_t>(seconds);
    return fmt::format("{:%Y-%m-%d %H:%M}", *std::localtime(&time));
}

ListLayer::ListLayer() {
    for (Trophies::List& list : Trophies::Read(Language::Current())) {
        for (Trophies::Trophy& trophy : list.trophies) {
            trophies.push_back(std::move(trophy));
        }
    }
    for (const Trophies::Trophy& trophy : trophies) {
        icons.push_back(ImGui::RefCountedTexture::DecodePngFile(trophy.icon));
        earned += trophy.earned ? 1 : 0;
        const size_t grade = GradeIndex(trophy.grade);
        if (grade < Grades.size()) {
            grade_counts[grade][0] += trophy.earned ? 1 : 0;
            ++grade_counts[grade][1];
        }
    }
    const cmrc::embedded_filesystem files = cmrc::res::get_filesystem();
    for (size_t g = 0; g < Grades.size(); ++g) {
        const cmrc::file file = files.open(GradeFiles[g]);
        grade_icons[g] = ImGui::RefCountedTexture::DecodePngTexture({file.begin(), file.end()});
    }
    ImGui::Core::AcquireGamepadInputCapture();
    AddLayer(this);
}

void ListLayer::DrawHeader() {
    ImGui::PushFont(nullptr, TitleSize);
    ImGui::TextUnformatted(Language::Text("Trophies").c_str());
    ImGui::PopFont();

    const size_t total = trophies.size();
    if (total == 0) {
        ImGui::TextUnformatted(Language::Text("No trophies found.").c_str());
        return;
    }
    const std::array<std::string, 3> values{std::to_string(earned * 100 / total),
                                            std::to_string(earned), std::to_string(total)};
    ImGui::TextUnformatted(Language::Text("Progress: {0}% ({1}/{2})", values).c_str());
    ImGui::ProgressBar(static_cast<float>(earned) / static_cast<float>(total), ImVec2(-1.0f, 0.0f),
                       "");

    for (size_t g = 0; g < Grades.size(); ++g) {
        if (g > 0) {
            ImGui::SameLine(0.0f, 24.0f);
        }
        if (grade_icons[g]) {
            ImGui::Image(grade_icons[g].GetTexture().im_id, ImVec2(GradeSize, GradeSize));
        } else {
            ImGui::Dummy(ImVec2(GradeSize, GradeSize));
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu/%zu", grade_counts[g][0], grade_counts[g][1]);
    }

    for (size_t i = 0; i < Filters.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 24.0f);
        }
        ImGui::Checkbox(Language::Text(Filters[i]).c_str(), &filters[i]);
    }
    ImGui::Separator();
}

// A row of the list: the trophy's picture, name and description, its grade and the time it was
// earned. A trophy not earned yet is drawn faded.
void ListLayer::DrawRow(size_t index, bool first) {
    const Trophies::Trophy& trophy = trophies[index];
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float text_width = ImGui::GetContentRegionAvail().x - IconSize - GradeSize - TimeWidth -
                             style.ItemSpacing.x * 3.0f;
    const float height = std::max(
        IconSize, ImGui::GetTextLineHeightWithSpacing() +
                      ImGui::CalcTextSize(trophy.detail.c_str(), nullptr, false, text_width).y);

    ImGui::PushID(static_cast<int>(index));
    const ImVec2 start = ImGui::GetCursorPos();
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, height));
    if (first && ImGui::IsWindowAppearing()) {
        ImGui::SetItemDefaultFocus();
    }
    const ImVec2 end = ImGui::GetCursorPos();

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, trophy.earned ? 1.0f : NotEarnedAlpha);
    ImGui::SetCursorPos(start);
    if (icons[index]) {
        ImGui::Image(icons[index].GetTexture().im_id, ImVec2(IconSize, IconSize));
    } else {
        ImGui::Dummy(ImVec2(IconSize, IconSize));
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(trophy.name.c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_width);
    ImGui::TextDisabled("%s", trophy.detail.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(right - GradeSize - TimeWidth - style.ItemSpacing.x);
    const size_t grade = GradeIndex(trophy.grade);
    if (grade < Grades.size() && grade_icons[grade]) {
        ImGui::Image(grade_icons[grade].GetTexture().im_id, ImVec2(GradeSize, GradeSize));
    } else {
        ImGui::Dummy(ImVec2(GradeSize, GradeSize));
    }
    if (trophy.earned && trophy.earned_at != 0) {
        ImGui::SameLine();
        ImGui::TextUnformatted(TimeText(trophy.earned_at).c_str());
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(end);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

void ListLayer::Draw() {
    if (g_close.exchange(false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        closing = true;
    }
    if (closing) {
        if (!KeyHeld()) {
            Finish();
        }
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##TrophyList", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }
    const float width = std::min(PanelWidth, viewport->WorkSize.x - PanelMargin * 2.0f);
    ImGui::SetCursorPos(ImVec2((viewport->WorkSize.x - width) / 2.0f, PanelMargin));
    ImGui::BeginChild("##Panel", ImVec2(width, viewport->WorkSize.y - PanelMargin * 2.0f),
                      ImGuiChildFlags_NavFlattened);
    DrawHeader();
    ImGui::BeginChild("##Trophies", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened);
    bool first = true;
    for (size_t i = 0; i < trophies.size(); ++i) {
        const Trophies::Trophy& trophy = trophies[i];
        if (!filters[trophy.earned ? EarnedFilter : NotEarnedFilter] ||
            (!trophy.earned && trophy.hidden && !filters[HiddenFilter])) {
            continue;
        }
        DrawRow(i, first);
        first = false;
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}

// Destroys the layer: nothing of it is used after.
void ListLayer::Finish() {
    ImGui::Core::ReleaseGamepadInputCapture();
    RemoveLayer(this);
    g_layer.reset();
    g_open = false;
}

} // namespace

void Toggle() {
    if (g_open) {
        g_close = true;
        return;
    }
    g_close = false;
    g_layer.emplace();
    g_open = true;
}

bool IsOpen() {
    return g_open;
}

} // namespace Gr2Port::TrophyList
