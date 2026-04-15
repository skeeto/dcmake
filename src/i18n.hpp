#pragma once

// Internationalization support.  All user-visible UI strings are looked
// up through tr(Str) against a statically-linked table for the active
// language.  LangTable supports a fallback chain (regional variant ->
// base language -> English) so partial translations degrade gracefully.

enum Str {
    // About dialog
    STR_APP_NAME,            // "dcmake: CMake Debugger"
    STR_VERSION_FMT,         // "Version %s"
    STR_THIRD_PARTY,         // "Third-party licenses"
    STR_ABOUT_DCMAKE,        // "About dcmake" (modal title)

    // Menus
    STR_MENU_FILE,
    STR_MENU_EDIT,
    STR_MENU_VIEW,
    STR_MENU_HELP,
    STR_OPEN_FILE,           // "Open File..."
    STR_SET_CWD,             // "Set Working Directory..."
    STR_EXIT,
    STR_FIND,
    STR_GOTO_LINE,
    STR_RESET_LAYOUT,
    STR_ABOUT,

    // Panel / window titles (used with tr_win for ### stable IDs)
    STR_CALL_STACK,
    STR_LOCALS,
    STR_CACHE_VARIABLES,
    STR_WATCH,
    STR_TARGETS,
    STR_TESTS,
    STR_BREAKPOINTS,
    STR_EXCEPTION_FILTERS,
    STR_DAP_LOG,
    STR_OUTPUT,

    // Toolbar tooltips
    STR_TT_START,            // "Start (F5)"
    STR_TT_PAUSE,            // "Pause (F5)"
    STR_TT_CONTINUE,         // "Continue (F5)"
    STR_TT_STOP,             // "Stop (Shift+F5)"
    STR_TT_RESTART,          // "Restart (Ctrl+Shift+F5)"
    STR_TT_STEP_OVER,        // "Step Over (F10)"
    STR_TT_STEP_IN,          // "Step In (F11)"
    STR_TT_STEP_OUT,         // "Step Out (Shift+F11)"
    STR_CMAKE_ARGS_HINT,     // "(CMake arguments)"

    // Buttons
    STR_ENABLE_ALL,
    STR_DISABLE_ALL,
    STR_DELETE_ALL,
    STR_EXPORT,              // "Export..."
    STR_OK,

    // Context menu (source editor)
    STR_RUN_TO_LINE,
    STR_ADD_BREAKPOINT,
    STR_REMOVE_BREAKPOINT,
    STR_ENABLE_BREAKPOINT,
    STR_DISABLE_BREAKPOINT,
    STR_REMOVE,              // Watch panel context menu

    // Input placeholders / hints
    STR_HINT_FIND,           // "Find..."
    STR_HINT_LINE_NUMBER,    // "Line number"
    STR_HINT_FILTER,         // "Filter..."

    // Status / empty state
    STR_NO_RESULTS,
    STR_LOADING,
    STR_NO_SCOPE_DATA,
    STR_NO_CACHE_DATA,
    STR_NO_TARGET_DATA,
    STR_NO_TEST_DATA,
    STR_NOT_STOPPED,
    STR_NOT_FOUND,
    STR_SCOPE_LOCAL,
    STR_SCOPE_CACHE,

    // Status bar
    STR_READY,
    STR_RUNNING,
    STR_PAUSED,
    STR_STOPPED,
    STR_PAUSED_REASON_FMT,   // "Paused (%s)"
    STR_STOPPED_EXIT_FMT,    // "Stopped (exit %d)"
    STR_ERROR_FMT,           // "Error: %s"
    STR_JSON_PARSE_ERROR,

    // DAP reasons (filled into STR_PAUSED_REASON_FMT)
    STR_REASON_PAUSE,
    STR_REASON_BREAKPOINT,
    STR_REASON_STEP,
    STR_REASON_EXCEPTION,
    STR_REASON_ENTRY,

    // Formatted text
    STR_FIND_MATCH_FMT,      // "%d of %d"
    STR_DAP_LOG_COUNT_FMT,   // "(%d messages)"
    STR_VAR_TYPE_FMT,        // "%s : %s"
    STR_VAR_VALUE_FMT,       // "%s = %s"

    // Column headers
    STR_COL_NAME,
    STR_COL_VALUE,
    STR_COL_SCOPE,

    // Platform error messages
    STR_ERR_STDOUT_PIPE,
    STR_ERR_LAUNCH_CMAKE,
    STR_ERR_SOCKET,
    STR_ERR_CONNECT_CMAKE,

    STR_COUNT
};

struct LangTable {
    const char *code;                    // "en", "de", "fr_CA", ...
    const LangTable *fallback;           // nullptr for English root
    const char *strings[STR_COUNT];      // nullptr entries fall through
};

extern const LangTable lang_en;
extern const LangTable lang_de;
extern const LangTable lang_es;
extern const LangTable lang_fr;
extern const LangTable lang_it;
extern const LangTable lang_pl;
extern const LangTable lang_pt;
extern const LangTable lang_ru;
extern const LangTable lang_vi;

// Active language table.  Defaults to &lang_en until lang_init() runs.
extern const LangTable *g_lang;

// Look up a translated string.  Walks g_lang->fallback chain, with a
// final guarantee of lang_en.  Never returns nullptr.
const char *tr(Str id);

// Returns "Translated###StableEnglishID" for ImGui::Begin.  The `###`
// suffix keeps docking/ini persistence stable across languages.
// Cached per id, regenerated on language change.
const char *tr_win(Str id);

// Parse a locale code like "de_DE.UTF-8" or "fr_CA" down to the
// matching table.  Falls back to English when no match.  Never returns
// nullptr.
const LangTable *lang_lookup(const char *code);

// Initialize g_lang.  Checks $LANG / $LC_MESSAGES / $LC_ALL first (they
// override on every platform), then falls back to platform_language_code().
void lang_init();
