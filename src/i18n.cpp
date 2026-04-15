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

// --- French (Français) ---

const LangTable lang_fr = {
    "fr", &lang_en, {
        [STR_APP_NAME]            = "dcmake : débogueur CMake",
        [STR_VERSION_FMT]         = "Version %s",
        [STR_THIRD_PARTY]         = "Licences tierces",
        [STR_ABOUT_DCMAKE]        = "À propos de dcmake",

        [STR_MENU_FILE]           = "Fichier",
        [STR_MENU_EDIT]           = "Édition",
        [STR_MENU_VIEW]           = "Affichage",
        [STR_MENU_HELP]           = "Aide",
        [STR_OPEN_FILE]           = "Ouvrir un fichier...",
        [STR_SET_CWD]             = "Définir le répertoire de travail...",
        [STR_EXIT]                = "Quitter",
        [STR_FIND]                = "Rechercher",
        [STR_GOTO_LINE]           = "Aller à la ligne",
        [STR_RESET_LAYOUT]        = "Réinitialiser la disposition",
        [STR_ABOUT]               = "À propos",

        [STR_CALL_STACK]          = "Pile d'appels",
        [STR_LOCALS]              = "Variables locales",
        [STR_CACHE_VARIABLES]     = "Variables du cache",
        [STR_WATCH]               = "Espion",
        [STR_TARGETS]             = "Cibles",
        [STR_TESTS]               = "Tests",
        [STR_BREAKPOINTS]         = "Points d'arrêt",
        [STR_EXCEPTION_FILTERS]   = "Filtres d'exceptions",
        [STR_DAP_LOG]             = "Journal DAP",
        [STR_OUTPUT]              = "Sortie",

        [STR_TT_START]            = "Démarrer (F5)",
        [STR_TT_PAUSE]            = "Suspendre (F5)",
        [STR_TT_CONTINUE]         = "Continuer (F5)",
        [STR_TT_STOP]             = "Arrêter (Maj+F5)",
        [STR_TT_RESTART]          = "Redémarrer (Ctrl+Maj+F5)",
        [STR_TT_STEP_OVER]        = "Pas à pas principal (F10)",
        [STR_TT_STEP_IN]          = "Pas à pas détaillé (F11)",
        [STR_TT_STEP_OUT]         = "Pas à pas sortant (Maj+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(arguments CMake)",

        [STR_ENABLE_ALL]          = "Tout activer",
        [STR_DISABLE_ALL]         = "Tout désactiver",
        [STR_DELETE_ALL]          = "Tout supprimer",
        [STR_EXPORT]              = "Exporter...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Exécuter jusqu'à la ligne",
        [STR_ADD_BREAKPOINT]      = "Ajouter un point d'arrêt",
        [STR_REMOVE_BREAKPOINT]   = "Supprimer le point d'arrêt",
        [STR_ENABLE_BREAKPOINT]   = "Activer le point d'arrêt",
        [STR_DISABLE_BREAKPOINT]  = "Désactiver le point d'arrêt",
        [STR_REMOVE]              = "Supprimer",

        [STR_HINT_FIND]           = "Rechercher...",
        [STR_HINT_LINE_NUMBER]    = "Numéro de ligne",
        [STR_HINT_FILTER]         = "Filtrer...",

        [STR_NO_RESULTS]          = "Aucun résultat",
        [STR_LOADING]             = "Chargement...",
        [STR_NO_SCOPE_DATA]       = "Aucune donnée de portée.",
        [STR_NO_CACHE_DATA]       = "Aucune donnée de cache.",
        [STR_NO_TARGET_DATA]      = "Aucune donnée de cible.",
        [STR_NO_TEST_DATA]        = "Aucune donnée de test.",
        [STR_NOT_STOPPED]         = "Non arrêté.",
        [STR_NOT_FOUND]           = "<introuvable>",
        [STR_SCOPE_LOCAL]         = "Local",
        [STR_SCOPE_CACHE]         = "Cache",

        [STR_READY]               = "Prêt",
        [STR_RUNNING]             = "En cours",
        [STR_PAUSED]              = "Suspendu",
        [STR_STOPPED]             = "Arrêté",
        [STR_PAUSED_REASON_FMT]   = "Suspendu (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Arrêté (code %d)",
        [STR_ERROR_FMT]           = "Erreur : %s",
        [STR_JSON_PARSE_ERROR]    = "Erreur d'analyse JSON",

        [STR_REASON_PAUSE]        = "pause",
        [STR_REASON_BREAKPOINT]   = "point d'arrêt",
        [STR_REASON_STEP]         = "pas",
        [STR_REASON_EXCEPTION]    = "exception",
        [STR_REASON_ENTRY]        = "entrée",

        [STR_FIND_MATCH_FMT]      = "%d sur %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d messages)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Nom",
        [STR_COL_VALUE]           = "Valeur",
        [STR_COL_SCOPE]           = "Portée",

        [STR_ERR_STDOUT_PIPE]     = "Impossible de créer le pipe stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Impossible de lancer cmake",
        [STR_ERR_SOCKET]          = "Impossible de créer le socket",
        [STR_ERR_CONNECT_CMAKE]   = "Impossible de se connecter au débogueur cmake",
    },
};

// --- Spanish (Español) ---

const LangTable lang_es = {
    "es", &lang_en, {
        [STR_APP_NAME]            = "dcmake: depurador de CMake",
        [STR_VERSION_FMT]         = "Versión %s",
        [STR_THIRD_PARTY]         = "Licencias de terceros",
        [STR_ABOUT_DCMAKE]        = "Acerca de dcmake",

        [STR_MENU_FILE]           = "Archivo",
        [STR_MENU_EDIT]           = "Editar",
        [STR_MENU_VIEW]           = "Ver",
        [STR_MENU_HELP]           = "Ayuda",
        [STR_OPEN_FILE]           = "Abrir archivo...",
        [STR_SET_CWD]             = "Establecer directorio de trabajo...",
        [STR_EXIT]                = "Salir",
        [STR_FIND]                = "Buscar",
        [STR_GOTO_LINE]           = "Ir a la línea",
        [STR_RESET_LAYOUT]        = "Restablecer diseño",
        [STR_ABOUT]               = "Acerca de",

        [STR_CALL_STACK]          = "Pila de llamadas",
        [STR_LOCALS]              = "Variables locales",
        [STR_CACHE_VARIABLES]     = "Variables de caché",
        [STR_WATCH]               = "Inspección",
        [STR_TARGETS]             = "Objetivos",
        [STR_TESTS]               = "Pruebas",
        [STR_BREAKPOINTS]         = "Puntos de interrupción",
        [STR_EXCEPTION_FILTERS]   = "Filtros de excepciones",
        [STR_DAP_LOG]             = "Registro DAP",
        [STR_OUTPUT]              = "Salida",

        [STR_TT_START]            = "Iniciar (F5)",
        [STR_TT_PAUSE]            = "Pausar (F5)",
        [STR_TT_CONTINUE]         = "Continuar (F5)",
        [STR_TT_STOP]             = "Detener (Mayús+F5)",
        [STR_TT_RESTART]          = "Reiniciar (Ctrl+Mayús+F5)",
        [STR_TT_STEP_OVER]        = "Paso a paso por procedimientos (F10)",
        [STR_TT_STEP_IN]          = "Paso a paso por instrucciones (F11)",
        [STR_TT_STEP_OUT]         = "Paso a paso para salir (Mayús+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(argumentos de CMake)",

        [STR_ENABLE_ALL]          = "Activar todo",
        [STR_DISABLE_ALL]         = "Desactivar todo",
        [STR_DELETE_ALL]          = "Eliminar todo",
        [STR_EXPORT]              = "Exportar...",
        [STR_OK]                  = "Aceptar",

        [STR_RUN_TO_LINE]         = "Ejecutar hasta la línea",
        [STR_ADD_BREAKPOINT]      = "Agregar punto de interrupción",
        [STR_REMOVE_BREAKPOINT]   = "Quitar punto de interrupción",
        [STR_ENABLE_BREAKPOINT]   = "Activar punto de interrupción",
        [STR_DISABLE_BREAKPOINT]  = "Desactivar punto de interrupción",
        [STR_REMOVE]              = "Quitar",

        [STR_HINT_FIND]           = "Buscar...",
        [STR_HINT_LINE_NUMBER]    = "Número de línea",
        [STR_HINT_FILTER]         = "Filtrar...",

        [STR_NO_RESULTS]          = "Sin resultados",
        [STR_LOADING]             = "Cargando...",
        [STR_NO_SCOPE_DATA]       = "Sin datos de ámbito.",
        [STR_NO_CACHE_DATA]       = "Sin datos de caché.",
        [STR_NO_TARGET_DATA]      = "Sin datos de objetivos.",
        [STR_NO_TEST_DATA]        = "Sin datos de pruebas.",
        [STR_NOT_STOPPED]         = "No detenido.",
        [STR_NOT_FOUND]           = "<no encontrado>",
        [STR_SCOPE_LOCAL]         = "Local",
        [STR_SCOPE_CACHE]         = "Caché",

        [STR_READY]               = "Listo",
        [STR_RUNNING]             = "En ejecución",
        [STR_PAUSED]              = "Pausado",
        [STR_STOPPED]             = "Detenido",
        [STR_PAUSED_REASON_FMT]   = "Pausado (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Detenido (salida %d)",
        [STR_ERROR_FMT]           = "Error: %s",
        [STR_JSON_PARSE_ERROR]    = "Error de análisis JSON",

        [STR_REASON_PAUSE]        = "pausa",
        [STR_REASON_BREAKPOINT]   = "punto de interrupción",
        [STR_REASON_STEP]         = "paso",
        [STR_REASON_EXCEPTION]    = "excepción",
        [STR_REASON_ENTRY]        = "entrada",

        [STR_FIND_MATCH_FMT]      = "%d de %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d mensajes)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Nombre",
        [STR_COL_VALUE]           = "Valor",
        [STR_COL_SCOPE]           = "Ámbito",

        [STR_ERR_STDOUT_PIPE]     = "No se pudo crear la tubería stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "No se pudo iniciar cmake",
        [STR_ERR_SOCKET]          = "No se pudo crear el socket",
        [STR_ERR_CONNECT_CMAKE]   = "No se pudo conectar al depurador de cmake",
    },
};

// --- Italian (Italiano) ---

const LangTable lang_it = {
    "it", &lang_en, {
        [STR_APP_NAME]            = "dcmake: debugger CMake",
        [STR_VERSION_FMT]         = "Versione %s",
        [STR_THIRD_PARTY]         = "Licenze di terze parti",
        [STR_ABOUT_DCMAKE]        = "Informazioni su dcmake",

        [STR_MENU_FILE]           = "File",
        [STR_MENU_EDIT]           = "Modifica",
        [STR_MENU_VIEW]           = "Visualizza",
        [STR_MENU_HELP]           = "?",
        [STR_OPEN_FILE]           = "Apri file...",
        [STR_SET_CWD]             = "Imposta directory di lavoro...",
        [STR_EXIT]                = "Esci",
        [STR_FIND]                = "Trova",
        [STR_GOTO_LINE]           = "Vai alla riga",
        [STR_RESET_LAYOUT]        = "Ripristina layout",
        [STR_ABOUT]               = "Informazioni",

        [STR_CALL_STACK]          = "Stack di chiamate",
        [STR_LOCALS]              = "Variabili locali",
        [STR_CACHE_VARIABLES]     = "Variabili cache",
        [STR_WATCH]               = "Espressioni di controllo",
        [STR_TARGETS]             = "Target",
        [STR_TESTS]               = "Test",
        [STR_BREAKPOINTS]         = "Punti di interruzione",
        [STR_EXCEPTION_FILTERS]   = "Filtri eccezioni",
        [STR_DAP_LOG]             = "Log DAP",
        [STR_OUTPUT]              = "Output",

        [STR_TT_START]            = "Avvia (F5)",
        [STR_TT_PAUSE]            = "Sospendi (F5)",
        [STR_TT_CONTINUE]         = "Continua (F5)",
        [STR_TT_STOP]             = "Arresta (Maiusc+F5)",
        [STR_TT_RESTART]          = "Riavvia (Ctrl+Maiusc+F5)",
        [STR_TT_STEP_OVER]        = "Esegui istruzione/routine (F10)",
        [STR_TT_STEP_IN]          = "Esegui istruzione (F11)",
        [STR_TT_STEP_OUT]         = "Esci da istruzione (Maiusc+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(argomenti CMake)",

        [STR_ENABLE_ALL]          = "Abilita tutto",
        [STR_DISABLE_ALL]         = "Disabilita tutto",
        [STR_DELETE_ALL]          = "Elimina tutto",
        [STR_EXPORT]              = "Esporta...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Esegui fino alla riga",
        [STR_ADD_BREAKPOINT]      = "Aggiungi punto di interruzione",
        [STR_REMOVE_BREAKPOINT]   = "Rimuovi punto di interruzione",
        [STR_ENABLE_BREAKPOINT]   = "Abilita punto di interruzione",
        [STR_DISABLE_BREAKPOINT]  = "Disabilita punto di interruzione",
        [STR_REMOVE]              = "Rimuovi",

        [STR_HINT_FIND]           = "Trova...",
        [STR_HINT_LINE_NUMBER]    = "Numero di riga",
        [STR_HINT_FILTER]         = "Filtra...",

        [STR_NO_RESULTS]          = "Nessun risultato",
        [STR_LOADING]             = "Caricamento...",
        [STR_NO_SCOPE_DATA]       = "Nessun dato di ambito.",
        [STR_NO_CACHE_DATA]       = "Nessun dato di cache.",
        [STR_NO_TARGET_DATA]      = "Nessun dato dei target.",
        [STR_NO_TEST_DATA]        = "Nessun dato dei test.",
        [STR_NOT_STOPPED]         = "Non arrestato.",
        [STR_NOT_FOUND]           = "<non trovato>",
        [STR_SCOPE_LOCAL]         = "Locale",
        [STR_SCOPE_CACHE]         = "Cache",

        [STR_READY]               = "Pronto",
        [STR_RUNNING]             = "In esecuzione",
        [STR_PAUSED]              = "Sospeso",
        [STR_STOPPED]             = "Arrestato",
        [STR_PAUSED_REASON_FMT]   = "Sospeso (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Arrestato (uscita %d)",
        [STR_ERROR_FMT]           = "Errore: %s",
        [STR_JSON_PARSE_ERROR]    = "Errore analisi JSON",

        [STR_REASON_PAUSE]        = "pausa",
        [STR_REASON_BREAKPOINT]   = "punto di interruzione",
        [STR_REASON_STEP]         = "passo",
        [STR_REASON_EXCEPTION]    = "eccezione",
        [STR_REASON_ENTRY]        = "ingresso",

        [STR_FIND_MATCH_FMT]      = "%d di %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d messaggi)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Nome",
        [STR_COL_VALUE]           = "Valore",
        [STR_COL_SCOPE]           = "Ambito",

        [STR_ERR_STDOUT_PIPE]     = "Impossibile creare la pipe stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Impossibile avviare cmake",
        [STR_ERR_SOCKET]          = "Impossibile creare il socket",
        [STR_ERR_CONNECT_CMAKE]   = "Impossibile connettersi al debugger di cmake",
    },
};

// --- Portuguese (Português) ---

const LangTable lang_pt = {
    "pt", &lang_en, {
        [STR_APP_NAME]            = "dcmake: depurador do CMake",
        [STR_VERSION_FMT]         = "Versão %s",
        [STR_THIRD_PARTY]         = "Licenças de terceiros",
        [STR_ABOUT_DCMAKE]        = "Sobre o dcmake",

        [STR_MENU_FILE]           = "Arquivo",
        [STR_MENU_EDIT]           = "Editar",
        [STR_MENU_VIEW]           = "Exibir",
        [STR_MENU_HELP]           = "Ajuda",
        [STR_OPEN_FILE]           = "Abrir arquivo...",
        [STR_SET_CWD]             = "Definir diretório de trabalho...",
        [STR_EXIT]                = "Sair",
        [STR_FIND]                = "Localizar",
        [STR_GOTO_LINE]           = "Ir para a linha",
        [STR_RESET_LAYOUT]        = "Redefinir layout",
        [STR_ABOUT]               = "Sobre",

        [STR_CALL_STACK]          = "Pilha de chamadas",
        [STR_LOCALS]              = "Locais",
        [STR_CACHE_VARIABLES]     = "Variáveis de cache",
        [STR_WATCH]               = "Inspeção",
        [STR_TARGETS]             = "Alvos",
        [STR_TESTS]               = "Testes",
        [STR_BREAKPOINTS]         = "Pontos de interrupção",
        [STR_EXCEPTION_FILTERS]   = "Filtros de exceção",
        [STR_DAP_LOG]             = "Log do DAP",
        [STR_OUTPUT]              = "Saída",

        [STR_TT_START]            = "Iniciar (F5)",
        [STR_TT_PAUSE]            = "Pausar (F5)",
        [STR_TT_CONTINUE]         = "Continuar (F5)",
        [STR_TT_STOP]             = "Parar (Shift+F5)",
        [STR_TT_RESTART]          = "Reiniciar (Ctrl+Shift+F5)",
        [STR_TT_STEP_OVER]        = "Depurar parcialmente (F10)",
        [STR_TT_STEP_IN]          = "Depurar (F11)",
        [STR_TT_STEP_OUT]         = "Sair de depuração (Shift+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(argumentos do CMake)",

        [STR_ENABLE_ALL]          = "Habilitar tudo",
        [STR_DISABLE_ALL]         = "Desabilitar tudo",
        [STR_DELETE_ALL]          = "Excluir tudo",
        [STR_EXPORT]              = "Exportar...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Executar até a linha",
        [STR_ADD_BREAKPOINT]      = "Adicionar ponto de interrupção",
        [STR_REMOVE_BREAKPOINT]   = "Remover ponto de interrupção",
        [STR_ENABLE_BREAKPOINT]   = "Habilitar ponto de interrupção",
        [STR_DISABLE_BREAKPOINT]  = "Desabilitar ponto de interrupção",
        [STR_REMOVE]              = "Remover",

        [STR_HINT_FIND]           = "Localizar...",
        [STR_HINT_LINE_NUMBER]    = "Número da linha",
        [STR_HINT_FILTER]         = "Filtrar...",

        [STR_NO_RESULTS]          = "Nenhum resultado",
        [STR_LOADING]             = "Carregando...",
        [STR_NO_SCOPE_DATA]       = "Sem dados de escopo.",
        [STR_NO_CACHE_DATA]       = "Sem dados de cache.",
        [STR_NO_TARGET_DATA]      = "Sem dados de alvos.",
        [STR_NO_TEST_DATA]        = "Sem dados de testes.",
        [STR_NOT_STOPPED]         = "Não interrompido.",
        [STR_NOT_FOUND]           = "<não encontrado>",
        [STR_SCOPE_LOCAL]         = "Local",
        [STR_SCOPE_CACHE]         = "Cache",

        [STR_READY]               = "Pronto",
        [STR_RUNNING]             = "Em execução",
        [STR_PAUSED]              = "Pausado",
        [STR_STOPPED]             = "Parado",
        [STR_PAUSED_REASON_FMT]   = "Pausado (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Parado (saída %d)",
        [STR_ERROR_FMT]           = "Erro: %s",
        [STR_JSON_PARSE_ERROR]    = "Erro ao analisar JSON",

        [STR_REASON_PAUSE]        = "pausa",
        [STR_REASON_BREAKPOINT]   = "ponto de interrupção",
        [STR_REASON_STEP]         = "passo",
        [STR_REASON_EXCEPTION]    = "exceção",
        [STR_REASON_ENTRY]        = "entrada",

        [STR_FIND_MATCH_FMT]      = "%d de %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d mensagens)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Nome",
        [STR_COL_VALUE]           = "Valor",
        [STR_COL_SCOPE]           = "Escopo",

        [STR_ERR_STDOUT_PIPE]     = "Falha ao criar pipe de stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Falha ao iniciar cmake",
        [STR_ERR_SOCKET]          = "Falha ao criar socket",
        [STR_ERR_CONNECT_CMAKE]   = "Falha ao conectar ao depurador do cmake",
    },
};

// --- Polish (Polski) ---

const LangTable lang_pl = {
    "pl", &lang_en, {
        [STR_APP_NAME]            = "dcmake: debuger CMake",
        [STR_VERSION_FMT]         = "Wersja %s",
        [STR_THIRD_PARTY]         = "Licencje stron trzecich",
        [STR_ABOUT_DCMAKE]        = "O programie dcmake",

        [STR_MENU_FILE]           = "Plik",
        [STR_MENU_EDIT]           = "Edycja",
        [STR_MENU_VIEW]           = "Widok",
        [STR_MENU_HELP]           = "Pomoc",
        [STR_OPEN_FILE]           = "Otwórz plik...",
        [STR_SET_CWD]             = "Ustaw katalog roboczy...",
        [STR_EXIT]                = "Zakończ",
        [STR_FIND]                = "Znajdź",
        [STR_GOTO_LINE]           = "Przejdź do wiersza",
        [STR_RESET_LAYOUT]        = "Resetuj układ",
        [STR_ABOUT]               = "O programie",

        [STR_CALL_STACK]          = "Stos wywołań",
        [STR_LOCALS]              = "Zmienne lokalne",
        [STR_CACHE_VARIABLES]     = "Zmienne cache",
        [STR_WATCH]               = "Czujka",
        [STR_TARGETS]             = "Cele",
        [STR_TESTS]               = "Testy",
        [STR_BREAKPOINTS]         = "Punkty przerwania",
        [STR_EXCEPTION_FILTERS]   = "Filtry wyjątków",
        [STR_DAP_LOG]             = "Dziennik DAP",
        [STR_OUTPUT]              = "Dane wyjściowe",

        [STR_TT_START]            = "Uruchom (F5)",
        [STR_TT_PAUSE]            = "Wstrzymaj (F5)",
        [STR_TT_CONTINUE]         = "Kontynuuj (F5)",
        [STR_TT_STOP]             = "Zatrzymaj (Shift+F5)",
        [STR_TT_RESTART]          = "Uruchom ponownie (Ctrl+Shift+F5)",
        [STR_TT_STEP_OVER]        = "Krok przez (F10)",
        [STR_TT_STEP_IN]          = "Wkrocz do (F11)",
        [STR_TT_STEP_OUT]         = "Wyjdź z (Shift+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(argumenty CMake)",

        [STR_ENABLE_ALL]          = "Włącz wszystko",
        [STR_DISABLE_ALL]         = "Wyłącz wszystko",
        [STR_DELETE_ALL]          = "Usuń wszystko",
        [STR_EXPORT]              = "Eksportuj...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Uruchom do wiersza",
        [STR_ADD_BREAKPOINT]      = "Dodaj punkt przerwania",
        [STR_REMOVE_BREAKPOINT]   = "Usuń punkt przerwania",
        [STR_ENABLE_BREAKPOINT]   = "Włącz punkt przerwania",
        [STR_DISABLE_BREAKPOINT]  = "Wyłącz punkt przerwania",
        [STR_REMOVE]              = "Usuń",

        [STR_HINT_FIND]           = "Znajdź...",
        [STR_HINT_LINE_NUMBER]    = "Numer wiersza",
        [STR_HINT_FILTER]         = "Filtruj...",

        [STR_NO_RESULTS]          = "Brak wyników",
        [STR_LOADING]             = "Ładowanie...",
        [STR_NO_SCOPE_DATA]       = "Brak danych zakresu.",
        [STR_NO_CACHE_DATA]       = "Brak danych cache.",
        [STR_NO_TARGET_DATA]      = "Brak danych celów.",
        [STR_NO_TEST_DATA]        = "Brak danych testów.",
        [STR_NOT_STOPPED]         = "Nie zatrzymano.",
        [STR_NOT_FOUND]           = "<nie znaleziono>",
        [STR_SCOPE_LOCAL]         = "Lokalny",
        [STR_SCOPE_CACHE]         = "Cache",

        [STR_READY]               = "Gotowy",
        [STR_RUNNING]             = "Działa",
        [STR_PAUSED]              = "Wstrzymano",
        [STR_STOPPED]             = "Zatrzymano",
        [STR_PAUSED_REASON_FMT]   = "Wstrzymano (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Zatrzymano (kod %d)",
        [STR_ERROR_FMT]           = "Błąd: %s",
        [STR_JSON_PARSE_ERROR]    = "Błąd analizy JSON",

        [STR_REASON_PAUSE]        = "pauza",
        [STR_REASON_BREAKPOINT]   = "punkt przerwania",
        [STR_REASON_STEP]         = "krok",
        [STR_REASON_EXCEPTION]    = "wyjątek",
        [STR_REASON_ENTRY]        = "wejście",

        [STR_FIND_MATCH_FMT]      = "%d z %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d wiadomości)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Nazwa",
        [STR_COL_VALUE]           = "Wartość",
        [STR_COL_SCOPE]           = "Zakres",

        [STR_ERR_STDOUT_PIPE]     = "Nie można utworzyć potoku stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Nie można uruchomić cmake",
        [STR_ERR_SOCKET]          = "Nie można utworzyć gniazda",
        [STR_ERR_CONNECT_CMAKE]   = "Nie można połączyć się z debugerem cmake",
    },
};

// --- Russian (Русский) ---

const LangTable lang_ru = {
    "ru", &lang_en, {
        [STR_APP_NAME]            = "dcmake: отладчик CMake",
        [STR_VERSION_FMT]         = "Версия %s",
        [STR_THIRD_PARTY]         = "Сторонние лицензии",
        [STR_ABOUT_DCMAKE]        = "О программе dcmake",

        [STR_MENU_FILE]           = "Файл",
        [STR_MENU_EDIT]           = "Правка",
        [STR_MENU_VIEW]           = "Вид",
        [STR_MENU_HELP]           = "Справка",
        [STR_OPEN_FILE]           = "Открыть файл...",
        [STR_SET_CWD]             = "Задать рабочий каталог...",
        [STR_EXIT]                = "Выход",
        [STR_FIND]                = "Поиск",
        [STR_GOTO_LINE]           = "Перейти к строке",
        [STR_RESET_LAYOUT]        = "Сбросить раскладку",
        [STR_ABOUT]               = "О программе",

        [STR_CALL_STACK]          = "Стек вызовов",
        [STR_LOCALS]              = "Локальные",
        [STR_CACHE_VARIABLES]     = "Переменные кэша",
        [STR_WATCH]               = "Контрольные значения",
        [STR_TARGETS]             = "Цели",
        [STR_TESTS]               = "Тесты",
        [STR_BREAKPOINTS]         = "Точки останова",
        [STR_EXCEPTION_FILTERS]   = "Фильтры исключений",
        [STR_DAP_LOG]             = "Журнал DAP",
        [STR_OUTPUT]              = "Вывод",

        [STR_TT_START]            = "Запуск (F5)",
        [STR_TT_PAUSE]            = "Пауза (F5)",
        [STR_TT_CONTINUE]         = "Продолжить (F5)",
        [STR_TT_STOP]             = "Остановить (Shift+F5)",
        [STR_TT_RESTART]          = "Перезапустить (Ctrl+Shift+F5)",
        [STR_TT_STEP_OVER]        = "Шаг с обходом (F10)",
        [STR_TT_STEP_IN]          = "Шаг с заходом (F11)",
        [STR_TT_STEP_OUT]         = "Шаг с выходом (Shift+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(аргументы CMake)",

        [STR_ENABLE_ALL]          = "Включить все",
        [STR_DISABLE_ALL]         = "Выключить все",
        [STR_DELETE_ALL]          = "Удалить все",
        [STR_EXPORT]              = "Экспорт...",
        [STR_OK]                  = "ОК",

        [STR_RUN_TO_LINE]         = "Выполнить до строки",
        [STR_ADD_BREAKPOINT]      = "Добавить точку останова",
        [STR_REMOVE_BREAKPOINT]   = "Удалить точку останова",
        [STR_ENABLE_BREAKPOINT]   = "Включить точку останова",
        [STR_DISABLE_BREAKPOINT]  = "Выключить точку останова",
        [STR_REMOVE]              = "Удалить",

        [STR_HINT_FIND]           = "Поиск...",
        [STR_HINT_LINE_NUMBER]    = "Номер строки",
        [STR_HINT_FILTER]         = "Фильтр...",

        [STR_NO_RESULTS]          = "Нет результатов",
        [STR_LOADING]             = "Загрузка...",
        [STR_NO_SCOPE_DATA]       = "Нет данных области.",
        [STR_NO_CACHE_DATA]       = "Нет данных кэша.",
        [STR_NO_TARGET_DATA]      = "Нет данных целей.",
        [STR_NO_TEST_DATA]        = "Нет данных тестов.",
        [STR_NOT_STOPPED]         = "Не остановлено.",
        [STR_NOT_FOUND]           = "<не найдено>",
        [STR_SCOPE_LOCAL]         = "Локальная",
        [STR_SCOPE_CACHE]         = "Кэш",

        [STR_READY]               = "Готов",
        [STR_RUNNING]             = "Выполняется",
        [STR_PAUSED]              = "Приостановлено",
        [STR_STOPPED]             = "Остановлено",
        [STR_PAUSED_REASON_FMT]   = "Приостановлено (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Остановлено (код %d)",
        [STR_ERROR_FMT]           = "Ошибка: %s",
        [STR_JSON_PARSE_ERROR]    = "Ошибка разбора JSON",

        [STR_REASON_PAUSE]        = "пауза",
        [STR_REASON_BREAKPOINT]   = "точка останова",
        [STR_REASON_STEP]         = "шаг",
        [STR_REASON_EXCEPTION]    = "исключение",
        [STR_REASON_ENTRY]        = "вход",

        [STR_FIND_MATCH_FMT]      = "%d из %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d сообщений)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Имя",
        [STR_COL_VALUE]           = "Значение",
        [STR_COL_SCOPE]           = "Область",

        [STR_ERR_STDOUT_PIPE]     = "Не удалось создать канал stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Не удалось запустить cmake",
        [STR_ERR_SOCKET]          = "Не удалось создать сокет",
        [STR_ERR_CONNECT_CMAKE]   = "Не удалось подключиться к отладчику cmake",
    },
};

// --- Vietnamese (Tiếng Việt) ---

const LangTable lang_vi = {
    "vi", &lang_en, {
        [STR_APP_NAME]            = "dcmake: trình gỡ lỗi CMake",
        [STR_VERSION_FMT]         = "Phiên bản %s",
        [STR_THIRD_PARTY]         = "Giấy phép của bên thứ ba",
        [STR_ABOUT_DCMAKE]        = "Giới thiệu về dcmake",

        [STR_MENU_FILE]           = "Tệp",
        [STR_MENU_EDIT]           = "Chỉnh sửa",
        [STR_MENU_VIEW]           = "Xem",
        [STR_MENU_HELP]           = "Trợ giúp",
        [STR_OPEN_FILE]           = "Mở tệp...",
        [STR_SET_CWD]             = "Đặt thư mục làm việc...",
        [STR_EXIT]                = "Thoát",
        [STR_FIND]                = "Tìm kiếm",
        [STR_GOTO_LINE]           = "Đến dòng",
        [STR_RESET_LAYOUT]        = "Đặt lại bố cục",
        [STR_ABOUT]               = "Giới thiệu",

        [STR_CALL_STACK]          = "Ngăn xếp lời gọi",
        [STR_LOCALS]              = "Biến cục bộ",
        [STR_CACHE_VARIABLES]     = "Biến bộ đệm",
        [STR_WATCH]               = "Theo dõi",
        [STR_TARGETS]             = "Mục tiêu",
        [STR_TESTS]               = "Kiểm thử",
        [STR_BREAKPOINTS]         = "Điểm dừng",
        [STR_EXCEPTION_FILTERS]   = "Bộ lọc ngoại lệ",
        [STR_DAP_LOG]             = "Nhật ký DAP",
        [STR_OUTPUT]              = "Đầu ra",

        [STR_TT_START]            = "Bắt đầu (F5)",
        [STR_TT_PAUSE]            = "Tạm dừng (F5)",
        [STR_TT_CONTINUE]         = "Tiếp tục (F5)",
        [STR_TT_STOP]             = "Dừng (Shift+F5)",
        [STR_TT_RESTART]          = "Khởi động lại (Ctrl+Shift+F5)",
        [STR_TT_STEP_OVER]        = "Bước qua (F10)",
        [STR_TT_STEP_IN]          = "Bước vào (F11)",
        [STR_TT_STEP_OUT]         = "Bước ra (Shift+F11)",
        [STR_CMAKE_ARGS_HINT]     = "(đối số CMake)",

        [STR_ENABLE_ALL]          = "Bật tất cả",
        [STR_DISABLE_ALL]         = "Tắt tất cả",
        [STR_DELETE_ALL]          = "Xóa tất cả",
        [STR_EXPORT]              = "Xuất...",
        [STR_OK]                  = "OK",

        [STR_RUN_TO_LINE]         = "Chạy đến dòng",
        [STR_ADD_BREAKPOINT]      = "Thêm điểm dừng",
        [STR_REMOVE_BREAKPOINT]   = "Xóa điểm dừng",
        [STR_ENABLE_BREAKPOINT]   = "Bật điểm dừng",
        [STR_DISABLE_BREAKPOINT]  = "Tắt điểm dừng",
        [STR_REMOVE]              = "Xóa",

        [STR_HINT_FIND]           = "Tìm kiếm...",
        [STR_HINT_LINE_NUMBER]    = "Số dòng",
        [STR_HINT_FILTER]         = "Lọc...",

        [STR_NO_RESULTS]          = "Không có kết quả",
        [STR_LOADING]             = "Đang tải...",
        [STR_NO_SCOPE_DATA]       = "Không có dữ liệu phạm vi.",
        [STR_NO_CACHE_DATA]       = "Không có dữ liệu bộ đệm.",
        [STR_NO_TARGET_DATA]      = "Không có dữ liệu mục tiêu.",
        [STR_NO_TEST_DATA]        = "Không có dữ liệu kiểm thử.",
        [STR_NOT_STOPPED]         = "Chưa dừng.",
        [STR_NOT_FOUND]           = "<không tìm thấy>",
        [STR_SCOPE_LOCAL]         = "Cục bộ",
        [STR_SCOPE_CACHE]         = "Bộ đệm",

        [STR_READY]               = "Sẵn sàng",
        [STR_RUNNING]             = "Đang chạy",
        [STR_PAUSED]              = "Đã tạm dừng",
        [STR_STOPPED]             = "Đã dừng",
        [STR_PAUSED_REASON_FMT]   = "Đã tạm dừng (%s)",
        [STR_STOPPED_EXIT_FMT]    = "Đã dừng (thoát %d)",
        [STR_ERROR_FMT]           = "Lỗi: %s",
        [STR_JSON_PARSE_ERROR]    = "Lỗi phân tích JSON",

        [STR_REASON_PAUSE]        = "tạm dừng",
        [STR_REASON_BREAKPOINT]   = "điểm dừng",
        [STR_REASON_STEP]         = "bước",
        [STR_REASON_EXCEPTION]    = "ngoại lệ",
        [STR_REASON_ENTRY]        = "vào",

        [STR_FIND_MATCH_FMT]      = "%d trên %d",
        [STR_DAP_LOG_COUNT_FMT]   = "(%d tin nhắn)",
        [STR_VAR_TYPE_FMT]        = "%s : %s",
        [STR_VAR_VALUE_FMT]       = "%s = %s",

        [STR_COL_NAME]            = "Tên",
        [STR_COL_VALUE]           = "Giá trị",
        [STR_COL_SCOPE]           = "Phạm vi",

        [STR_ERR_STDOUT_PIPE]     = "Không tạo được pipe stdout",
        [STR_ERR_LAUNCH_CMAKE]    = "Không khởi chạy được cmake",
        [STR_ERR_SOCKET]          = "Không tạo được socket",
        [STR_ERR_CONNECT_CMAKE]   = "Không kết nối được với trình gỡ lỗi cmake",
    },
};

// --- Registry of available languages (for lookup) ---

static const LangTable *const all_tables[] = {
    &lang_en,
    &lang_de,
    &lang_es,
    &lang_fr,
    &lang_it,
    &lang_pl,
    &lang_pt,
    &lang_ru,
    &lang_vi,
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
