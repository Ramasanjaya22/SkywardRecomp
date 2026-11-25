// The one colour palette of the launcher and the menus: blue, from the launcher backdrop's sky (runtime/ui/assets/
// backdrop.jpg). Status colours (ok / warn / error) are muted and only for status text and icons.
#pragma once
#include "imgui.h"

#define PAL_A(c, a) (((c) & 0x00FFFFFFu) | ((ImU32)(a) << 24))
// deep navy: panels (translucent over the backdrop), the text on them is white
#define PAL_PANEL_V4 ImVec4(0.035f, 0.09f, 0.21f, 0.94f)
#define PAL_PANEL_DARK_V4(a) ImVec4(0.02f, 0.06f, 0.15f, a)
#define PAL_NAVY IM_COL32(8, 22, 52, 255)
#define PAL_NAVY_HI IM_COL32(18, 44, 92, 255)
// light cyan: primary, focus, checkmarks, headings
#define PAL_ACCENT IM_COL32(140, 215, 255, 255)
#define PAL_ACCENT_A(a) PAL_A(PAL_ACCENT, a)
#define PAL_ACCENT_V4 ImVec4(0.55f, 0.84f, 1.0f, 1)
#define PAL_ACCENT_DIM_V4 ImVec4(0.36f, 0.62f, 0.86f, 1)
// sky blue: buttons, hover
#define PAL_SKY IM_COL32(47, 128, 218, 235)
#define PAL_SKY_HI IM_COL32(86, 164, 240, 245)
#define PAL_SKY_OFF IM_COL32(40, 64, 104, 200)
// text
#define PAL_TEXT IM_COL32(236, 244, 255, 255)
#define PAL_TEXT_V4 ImVec4(0.93f, 0.96f, 1.0f, 1)
#define PAL_TEXT2_V4 ImVec4(0.80f, 0.88f, 0.98f, 1)
#define PAL_DIM IM_COL32(150, 176, 212, 255)
#define PAL_DIM_V4 ImVec4(0.59f, 0.69f, 0.83f, 1)
#define PAL_OFF IM_COL32(104, 122, 150, 255)
// status
#define PAL_OK_V4 ImVec4(0.55f, 0.88f, 0.70f, 1)
#define PAL_OK IM_COL32(130, 225, 170, 255)
#define PAL_WARN_V4 ImVec4(0.98f, 0.80f, 0.50f, 1)
#define PAL_ERR_V4 ImVec4(1.0f, 0.55f, 0.50f, 1)
#define PAL_ERR IM_COL32(245, 120, 110, 255)

// ImGui's widget colours from the palette (menu.mm and the launcher both call it)
static inline void pal_apply_style(ImGuiStyle &s) {
    ImVec4 *c = s.Colors;
    const ImVec4 hover(0.18f, 0.42f, 0.76f, 1), press(0.25f, 0.52f, 0.88f, 1), frame(0.08f, 0.20f, 0.42f, 1);
    c[ImGuiCol_WindowBg] = PAL_PANEL_V4;
    c[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.13f, 0.28f, 0.85f);
    c[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.10f, 0.23f, 0.98f);
    c[ImGuiCol_Text] = PAL_TEXT_V4;
    c[ImGuiCol_TextDisabled] = PAL_DIM_V4;
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.30f, 0.58f, 1);
    c[ImGuiCol_FrameBgActive] = hover;
    c[ImGuiCol_Button] = ImVec4(0.10f, 0.27f, 0.52f, 1);
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = press;
    c[ImGuiCol_Header] = ImVec4(0.12f, 0.30f, 0.58f, 1);
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = press;
    c[ImGuiCol_Tab] = ImVec4(0.07f, 0.18f, 0.38f, 1);
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.38f, 0.70f, 1);
    c[ImGuiCol_TabSelectedOverline] = PAL_ACCENT_V4;
    c[ImGuiCol_CheckMark] = PAL_ACCENT_V4;
    c[ImGuiCol_SliderGrab] = PAL_ACCENT_DIM_V4;
    c[ImGuiCol_SliderGrabActive] = PAL_ACCENT_V4;
    c[ImGuiCol_NavCursor] = PAL_ACCENT_V4;
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.36f, 0.60f, 1);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
}
