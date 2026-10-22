#include "theme.hpp"
#include "dcmake.hpp"

#include <cstring>

// Light and dark palettes, after Prospero's.  The setting, theme= in
// dcmake.ini, is system (the default), dark, or light.  The platform's
// GUI half reports the system's preference in Debugger::sys_dark.

const char *const theme_names[THEME_COUNT] = {"system", "dark", "light"};

static constexpr ImU32 rgb(int r, int g, int b, int a = 255)
{
    return IM_COL32(r, g, b, a);
}

static const Palette theme_dark = {
    .dark       = true,
    .clear      = rgb( 20,  20,  23),
    .panel      = rgb( 15,  15,  16),
    .paneltext  = rgb(255, 255, 255),
    .paneldim   = rgb(153, 153, 153),
    .frame      = rgb( 30,  47,  74),
    .selrow     = rgb( 32,  58,  89),
    .rowhover   = rgb( 28,  46,  70),
    .rowpress   = rgb( 36,  54,  80),
    .btn        = rgb( 36,  69, 110),
    .btnhover   = rgb( 46,  88, 140),
    .btnpress   = rgb( 30,  62, 104),
    .link       = rgb(100, 160, 240),
    .text       = rgb(212, 212, 212),
    .comment    = rgb(118, 160, 115),
    .string     = rgb(214, 157, 133),
    .variable   = rgb(156, 220, 254),
    .genex      = rgb(197, 134, 192),
    .keyword    = rgb(198, 140, 228),
    .boolean    = rgb(181, 206, 168),
    .curline    = rgb( 48,  46,  20),
    .flash      = rgb(220, 180,  50, 120),
    .findcur    = rgb(220, 180,  50, 180),
    .findother  = rgb(180, 140,  30,  80),
    .breakpoint = rgb(220,  50,  50),
    .changed    = rgb(102, 102,  26,  89),
    .ok         = rgb(110, 200, 120),
    .bad        = rgb(250, 132, 132),
    .amber      = rgb(235, 180,  80),
};

static const Palette theme_light = {
    .dark       = false,
    .clear      = rgb(214, 214, 218),
    .panel      = rgb(240, 240, 240),
    .paneltext  = rgb(  0,   0,   0),
    .paneldim   = rgb( 98,  98,  98),
    .frame      = rgb(255, 255, 255),
    .selrow     = rgb(186, 212, 243),
    .rowhover   = rgb(206, 224, 246),
    .rowpress   = rgb(190, 212, 240),
    .btn        = rgb(176, 206, 244),
    .btnhover   = rgb(150, 190, 244),
    .btnpress   = rgb(124, 172, 236),
    .link       = rgb(  0,  88, 190),
    .text       = rgb( 36,  41,  47),
    .comment    = rgb( 89,  99, 110),
    .string     = rgb( 10,  48, 105),
    .variable   = rgb(  0,  92, 130),
    .genex      = rgb(110,  60, 190),
    .keyword    = rgb(176,  18,  40),
    .boolean    = rgb(  5,  80, 174),
    .curline    = rgb(252, 236, 180),
    .flash      = rgb(240, 190,  40, 120),
    .findcur    = rgb(240, 190,  40, 180),
    .findother  = rgb(240, 200,  80,  90),
    .breakpoint = rgb(220,  50,  50),
    .changed    = rgb(252, 236, 180),
    .ok         = rgb( 16, 100,  42),
    .bad        = rgb(168,  24,  34),
    .amber      = rgb(125,  78,   0),
};

// The setting's value from its name, or -1 if it names none.
int theme_parse(const char *name)
{
    for (int i = 0; i < THEME_COUNT; i++)
        if (!strcmp(theme_names[i], name)) return i;
    return -1;
}

static void theme_style(ImGuiStyle *st, const Palette *p)
{
    if (p->dark)
        ImGui::StyleColorsDark(st);
    else
        ImGui::StyleColorsLight(st);
    ImVec4 *c = st->Colors;
    c[ImGuiCol_WindowBg]      = ImGui::ColorConvertU32ToFloat4(p->panel);
    c[ImGuiCol_Text]          = ImGui::ColorConvertU32ToFloat4(p->paneltext);
    c[ImGuiCol_TextDisabled]  = ImGui::ColorConvertU32ToFloat4(p->paneldim);
    c[ImGuiCol_FrameBg]       = ImGui::ColorConvertU32ToFloat4(p->frame);
    c[ImGuiCol_Header]        = ImGui::ColorConvertU32ToFloat4(p->selrow);
    c[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(p->rowhover);
    c[ImGuiCol_HeaderActive]  = ImGui::ColorConvertU32ToFloat4(p->rowpress);
    c[ImGuiCol_TabHovered]    = ImGui::ColorConvertU32ToFloat4(p->rowhover);
    c[ImGuiCol_Button]        = ImGui::ColorConvertU32ToFloat4(p->btn);
    c[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(p->btnhover);
    c[ImGuiCol_ButtonActive]  = ImGui::ColorConvertU32ToFloat4(p->btnpress);
    c[ImGuiCol_TextLink]      = ImGui::ColorConvertU32ToFloat4(p->link);
    c[ImGuiCol_PopupBg]       = ImGui::ColorConvertU32ToFloat4(p->panel);
}

// Put the theme in effect: the configured one, or the system's.  Only
// style colors change, so sizes scaled at init are kept.
void theme_apply(Debugger *dbg)
{
    const Palette *p;
    switch (dbg->theme) {
    case THEME_DARK:  p = &theme_dark;  break;
    case THEME_LIGHT: p = &theme_light; break;
    default:          p = dbg->sys_dark ? &theme_dark : &theme_light;
    }
    dbg->pal = p;
    theme_style(&ImGui::GetStyle(), p);
}
