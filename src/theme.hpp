#pragma once

#include <imgui.h>

struct Debugger;

// The theme setting: follow the operating system, or force one.
enum {
    THEME_SYSTEM,
    THEME_DARK,
    THEME_LIGHT,
    THEME_COUNT,
};

extern const char *const theme_names[THEME_COUNT];

// Every color dcmake draws, packed as IM_COL32.  The ImGui overrides are
// applied over its stock dark or light style.
struct Palette {
    bool dark;

    // ImGui overrides, plus the window clear color between docked windows
    ImU32 clear;
    ImU32 panel;      // window background
    ImU32 paneltext;
    ImU32 paneldim;   // disabled text and hints
    ImU32 frame;      // fields
    ImU32 selrow;     // a selected row, hovered, pressed
    ImU32 rowhover;
    ImU32 rowpress;
    ImU32 btn;        // buttons: at rest, hovered, pressed
    ImU32 btnhover;
    ImU32 btnpress;
    ImU32 link;

    // Syntax highlighting, on panel
    ImU32 text;
    ImU32 comment;
    ImU32 string;
    ImU32 variable;
    ImU32 genex;
    ImU32 keyword;
    ImU32 boolean;

    // Source view
    ImU32 curline;    // current line background
    ImU32 flash;      // go-to-line flash, alpha scaled as it fades
    ImU32 findcur;    // current find match
    ImU32 findother;  // other find matches
    ImU32 breakpoint;

    // Variables that changed since the last stop
    ImU32 changed;

    // States, as text on panel
    ImU32 ok;
    ImU32 bad;
    ImU32 amber;
};

int theme_parse(const char *name);
void theme_apply(Debugger *dbg);
