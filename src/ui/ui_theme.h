#pragma once

// One look for our Dear ImGui pages (the launcher and the in-game PC menu): dark grey with the
// launcher front page's red for what is hovered, active or chosen. Header only (Dear ImGui is
// all it needs), so the renderer's menu can use it without the settings widgets.

#include "imgui.h"

namespace ui {

inline void apply_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.GrabRounding = 2.0f;
    ImVec4* c = style.Colors;
    const ImVec4 red(0.55f, 0.12f, 0.10f, 1.0f);         // hovered (the front page's buttons)
    const ImVec4 red_bright(0.70f, 0.15f, 0.12f, 1.0f);  // pressed, chosen
    const ImVec4 red_dim(0.38f, 0.09f, 0.08f, 1.0f);     // chosen but not hovered
    const ImVec4 grey(0.16f, 0.16f, 0.16f, 1.0f);
    const ImVec4 grey_light(0.22f, 0.22f, 0.22f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.09f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.30f, 0.5f);
    c[ImGuiCol_FrameBg] = grey;
    c[ImGuiCol_FrameBgHovered] = grey_light;
    c[ImGuiCol_FrameBgActive] = red_dim;
    c[ImGuiCol_TitleBg] = grey;
    c[ImGuiCol_TitleBgActive] = red_dim;
    c[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.25f, 0.20f, 1.0f);
    c[ImGuiCol_SliderGrab] = red;
    c[ImGuiCol_SliderGrabActive] = red_bright;
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    c[ImGuiCol_ButtonHovered] = red;
    c[ImGuiCol_ButtonActive] = red_bright;
    c[ImGuiCol_Header] = red_dim;
    c[ImGuiCol_HeaderHovered] = red;
    c[ImGuiCol_HeaderActive] = red_bright;
    c[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 0.6f);
    c[ImGuiCol_SeparatorHovered] = red;
    c[ImGuiCol_SeparatorActive] = red_bright;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = red;
    c[ImGuiCol_ResizeGripActive] = red_bright;
    c[ImGuiCol_Tab] = grey;
    c[ImGuiCol_TabHovered] = red;
    c[ImGuiCol_TabSelected] = red_dim;
    c[ImGuiCol_TabSelectedOverline] = red_bright;
    c[ImGuiCol_TabDimmed] = grey;
    c[ImGuiCol_TabDimmedSelected] = red_dim;
    c[ImGuiCol_TableHeaderBg] = grey;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.70f, 0.15f, 0.12f, 0.45f);
    c[ImGuiCol_NavCursor] = red_bright;
    c[ImGuiCol_ScrollbarGrab] = grey_light;
    c[ImGuiCol_ScrollbarGrabHovered] = red;
    c[ImGuiCol_ScrollbarGrabActive] = red_bright;
}

}  // namespace ui
