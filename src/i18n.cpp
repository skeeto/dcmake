#include "i18n.hpp"
#include "dcmake.hpp"  // platform_language_code()

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

// --- English (root) ---

const LangTable lang_en = {
    "en", nullptr, {
        // About
        [STR_APP_NAME]            = "dcmake: CMake Debugger",
        [STR_VERSION_FMT]         = "Version %s",
        [STR_THIRD_PARTY]         = "Third-party licenses",
        [STR_ABOUT_DCMAKE]        = "About dcmake",

        // Menus
        [STR_MENU_FILE]           = "File",
        [STR_MENU_EDIT]           = "Edit",
        [STR_MENU_VIEW]           = "View",
        [STR_MENU_HELP]           = "Help",
        [STR_OPEN_FILE]           = "Open File...",
        [STR_SET_CWD]             = "Set Working Directory...",
        [STR_EXIT]                = "Exit",
        [STR_FIND]                = "Find",
        [STR_GOTO_LINE]           = "Go to Line",
        [STR_RESET_LAYOUT]        = "Reset Layout",
        [STR_ABOUT]               = "About",

        // Panels
        [STR_CALL_STACK]          = "Call Stack",
        [STR_LOCALS]              = "Locals",
        [STR_CACHE_VARIABLES]     = "Cache Variables",
        [STR_WATCH]               = "Watch",
        [STR_TARGETS]             = "Targets",
        [STR_TESTS]               = "Tests",
        [STR_BREAKPOINTS]         = "Breakpoints",
        [STR_EXCEPTION_FILTERS]   = "Exception Filters",
        [STR_DAP_LOG]             = "DAP Log",
        [STR_OUTPUT]              = "Output",

        // Toolbar
        [STR_TT_START]            = "Start (F5)",
        [STR_TT_PAUSE]            = "Pause (F5)",
        [STR_TT_CONTINUE]         = "Continue (F5)",
        [STR_TT_STOP]             = "Stop (Shift+F5)",
        [STR_TT_RESTART]          = "Restart (Ctrl+Shift+F5)",
        [STR_TT_STEP_OVER]        = "Step Over (F10)",
        [STR_TT_STEP_IN]          = "Step In (F11)",
        [STR_TT_STEP_OUT]         = "Step Out (Shift+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(CMake arguments)",

        // Buttons
        [STR_ENABLE_ALL]          = "Enable All",
        [STR_DISABLE_ALL]         = "Disable All",
        [STR_DELETE_ALL]          = "Delete All",
        [STR_EXPORT]              = "Export...",
        [STR_OK]                  = "OK",

        // Context menu
        [STR_RUN_TO_LINE]         = "Run to line",
        [STR_ADD_BREAKPOINT]      = "Add Breakpoint",
        [STR_REMOVE_BREAKPOINT]   = "Remove Breakpoint",
        [STR_ENABLE_BREAKPOINT]   = "Enable Breakpoint",
        [STR_DISABLE_BREAKPOINT]  = "Disable Breakpoint",
        [STR_REMOVE]              = "Remove",

        // Hints
        [STR_HINT_FIND]           = "Find...",
        [STR_HINT_LINE_NUMBER]    = "Line number",
        [STR_HINT_FILTER]         = "Filter...",

        // Empty state
        [STR_NO_RESULTS]          = "No results",
        [STR_LOADING]             = "Loading...",
        [STR_NO_SCOPE_DATA]       = "No scope data.",
        [STR_NO_CACHE_DATA]       = "No cache data.",
        [STR_NO_TARGET_DATA]      = "No target data.",
        [STR_NO_TEST_DATA]        = "No test data.",
        [STR_NOT_STOPPED]         = "Not stopped.",
        [STR_NOT_FOUND]           = "<not found>",
        [STR_SCOPE_LOCAL]         = "Local",
        [STR_SCOPE_CACHE]         = "Cache",

        // Status bar
        [STR_READY]               = "Ready",
        [STR_RUNNING]             = "Running",
        [STR_PAUSED]              = "Paused",
        [STR_STOPPED]             = "Stopped",
        [STR_PAUSED_REASON_FMT]   = "Paused (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Stopped (exit %d)",
        [STR_ERROR_FMT]           = "Error: %s",
        [STR_JSON_PARSE_ERROR]    = "JSON parse error",

        // DAP reasons
        [STR_REASON_PAUSE]        = "pause",
        [STR_REASON_BREAKPOINT]   = "breakpoint",
        [STR_REASON_STEP]         = "step",
        [STR_REASON_EXCEPTION]    = "exception",
        [STR_REASON_ENTRY]        = "entry",

        // Formatted
        [STR_FIND_MATCH_FMT]      = "%d of %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d messages)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        // Columns
        [STR_COL_NAME]            = "Name",
        [STR_COL_VALUE]           = "Value",
        [STR_COL_SCOPE]           = "Scope",

        // Platform errors
        [STR_ERR_STDOUT_PIPE]     = "Failed to create stdout pipe",
        [STR_ERR_LAUNCH_CMAKE]    = "Failed to launch cmake",
        [STR_ERR_SOCKET]          = "Failed to create socket",
        [STR_ERR_CONNECT_CMAKE]   = "Failed to connect to cmake debugger",
    },
};

// --- German (Deutsch) ---

const LangTable lang_de = {
    "de", &lang_en, {
        [STR_APP_NAME]            = "dcmake: CMake-Debugger",
        [STR_VERSION_FMT]         = "Version %s",
        [STR_THIRD_PARTY]         = "Lizenzen von Drittanbietern",
        [STR_ABOUT_DCMAKE]        = "Über dcmake",

        [STR_MENU_FILE]           = "Datei",
        [STR_MENU_EDIT]           = "Bearbeiten",
        [STR_MENU_VIEW]           = "Ansicht",
        [STR_MENU_HELP]           = "Hilfe",
        [STR_OPEN_FILE]           = "Datei öffnen...",
        [STR_SET_CWD]             = "Arbeitsverzeichnis festlegen...",
        [STR_EXIT]                = "Beenden",
        [STR_FIND]                = "Suchen",
        [STR_GOTO_LINE]           = "Gehe zu Zeile",
        [STR_RESET_LAYOUT]        = "Layout zurücksetzen",
        [STR_ABOUT]               = "Über",

        [STR_CALL_STACK]          = "Aufrufliste",
        [STR_LOCALS]              = "Lokale Variablen",
        [STR_CACHE_VARIABLES]     = "Cache-Variablen",
        [STR_WATCH]               = "Überwachung",
        [STR_TARGETS]             = "Targets",
        [STR_TESTS]               = "Tests",
        [STR_BREAKPOINTS]         = "Haltepunkte",
        [STR_EXCEPTION_FILTERS]   = "Ausnahmefilter",
        [STR_DAP_LOG]             = "DAP-Protokoll",
        [STR_OUTPUT]              = "Ausgabe",

        [STR_TT_START]            = "Starten (F5)",
        [STR_TT_PAUSE]            = "Anhalten (F5)",
        [STR_TT_CONTINUE]         = "Fortsetzen (F5)",
        [STR_TT_STOP]             = "Stoppen (Umschalt+F5)",
        [STR_TT_RESTART]          = "Neu starten (Strg+Umschalt+F5)",
        [STR_TT_STEP_OVER]        = "Prozedurschritt (F10)",
        [STR_TT_STEP_IN]          = "Einzelschritt (F11)",
        [STR_TT_STEP_OUT]         = "Rücksprung (Umschalt+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(CMake-Argumente)",

        [STR_ENABLE_ALL]          = "Alle aktivieren",
        [STR_DISABLE_ALL]         = "Alle deaktivieren",
        [STR_DELETE_ALL]          = "Alle löschen",
        [STR_EXPORT]              = "Exportieren...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Bis Zeile ausführen",
        [STR_ADD_BREAKPOINT]      = "Haltepunkt hinzufügen",
        [STR_REMOVE_BREAKPOINT]   = "Haltepunkt entfernen",
        [STR_ENABLE_BREAKPOINT]   = "Haltepunkt aktivieren",
        [STR_DISABLE_BREAKPOINT]  = "Haltepunkt deaktivieren",
        [STR_REMOVE]              = "Entfernen",

        [STR_HINT_FIND]           = "Suchen...",
        [STR_HINT_LINE_NUMBER]    = "Zeilennummer",
        [STR_HINT_FILTER]         = "Filtern...",

        [STR_NO_RESULTS]          = "Keine Ergebnisse",
        [STR_LOADING]             = "Lädt...",
        [STR_NO_SCOPE_DATA]       = "Keine Bereichsdaten.",
        [STR_NO_CACHE_DATA]       = "Keine Cache-Daten.",
        [STR_NO_TARGET_DATA]      = "Keine Target-Daten.",
        [STR_NO_TEST_DATA]        = "Keine Testdaten.",
        [STR_NOT_STOPPED]         = "Nicht angehalten.",
        [STR_NOT_FOUND]           = "<nicht gefunden>",
        [STR_SCOPE_LOCAL]         = "Lokal",
        [STR_SCOPE_CACHE]         = "Cache",

        [STR_READY]               = "Bereit",
        [STR_RUNNING]             = "Läuft",
        [STR_PAUSED]              = "Angehalten",
        [STR_STOPPED]             = "Gestoppt",
        [STR_PAUSED_REASON_FMT]   = "Angehalten (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Gestoppt (Exit %d)",
        [STR_ERROR_FMT]           = "Fehler: %s",
        [STR_JSON_PARSE_ERROR]    = "JSON-Parsefehler",

        [STR_REASON_PAUSE]        = "Pause",
        [STR_REASON_BREAKPOINT]   = "Haltepunkt",
        [STR_REASON_STEP]         = "Schritt",
        [STR_REASON_EXCEPTION]    = "Ausnahme",
        [STR_REASON_ENTRY]        = "Eintritt",

        [STR_FIND_MATCH_FMT]      = "%d von %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d Nachrichten)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Name",
        [STR_COL_VALUE]           = "Wert",
        [STR_COL_SCOPE]           = "Bereich",

        [STR_ERR_STDOUT_PIPE]     = "stdout-Pipe konnte nicht erstellt werden",
        [STR_ERR_LAUNCH_CMAKE]    = "cmake konnte nicht gestartet werden",
        [STR_ERR_SOCKET]          = "Socket konnte nicht erstellt werden",
        [STR_ERR_CONNECT_CMAKE]   = "Verbindung zum cmake-Debugger fehlgeschlagen",
    },
};

// --- Registry of available languages (for lookup) ---

static const LangTable *const all_tables[] = {
    &lang_en,
    &lang_de,
};

// --- Active language and lookup ---

const LangTable *g_lang = &lang_en;

const char *tr(Str id)
{
    for (const LangTable *t = g_lang; t; t = t->fallback)
        if (t->strings[id])
            return t->strings[id];
    return lang_en.strings[id];  // always populated
}

// Cache of "Translated###English" strings for window titles.  Rebuilt
// when the active language changes.
static const LangTable *cached_lang = nullptr;
static std::string tr_win_cache[STR_COUNT];

const char *tr_win(Str id)
{
    if (cached_lang != g_lang) {
        for (auto &s : tr_win_cache) s.clear();
        cached_lang = g_lang;
    }
    if (tr_win_cache[id].empty()) {
        tr_win_cache[id] = tr(id);
        tr_win_cache[id] += "###";
        tr_win_cache[id] += lang_en.strings[id];  // stable English ID
    }
    return tr_win_cache[id].c_str();
}

const LangTable *lang_lookup(const char *code)
{
    if (!code || !*code) return &lang_en;

    // Exact match first ("fr_CA" beats "fr").
    for (auto *t : all_tables)
        if (strcmp(t->code, code) == 0)
            return t;

    // Parse down to base language: "de_DE.UTF-8" -> "de", "fr-CA" -> "fr".
    char base[16] = {};
    size_t n = 0;
    for (; code[n] && n < sizeof(base) - 1; n++) {
        char c = code[n];
        if (c == '_' || c == '.' || c == '@' || c == '-') break;
        base[n] = (char)tolower((unsigned char)c);
    }
    base[n] = '\0';
    if (!base[0]) return &lang_en;

    for (auto *t : all_tables)
        if (strcmp(t->code, base) == 0)
            return t;

    return &lang_en;
}

void lang_init()
{
    // POSIX locale env vars win on every platform — this is the escape
    // hatch users expect ("LANG=de dcmake ...").
    const char *env = getenv("LC_ALL");
    if (!env || !*env) env = getenv("LC_MESSAGES");
    if (!env || !*env) env = getenv("LANG");
    if (env && *env && strcmp(env, "C") != 0 && strcmp(env, "POSIX") != 0) {
        g_lang = lang_lookup(env);
        return;
    }

    // Fall back to platform's UI language.
    std::string code = platform_language_code();
    g_lang = lang_lookup(code.c_str());
}
