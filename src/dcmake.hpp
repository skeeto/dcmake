#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct ImFont;

enum struct DapState {
    IDLE,
    CONNECTING,
    INITIALIZING,
    RUNNING,
    STOPPED,
    TERMINATED,
};

struct StackFrame {
    int id;
    std::string name;
    std::string source_path;
    int line;
};

struct SourceFile {
    std::string path;
    std::vector<std::string> lines;
};

struct DapVariable {
    std::string name;
    std::string value;
    std::string type;
    int64_t variables_ref = 0;
    std::vector<DapVariable> children;
    bool fetched = false;
    bool changed = false;
};

struct DapScope {
    std::string name;
    int64_t variables_ref = 0;
    std::vector<DapVariable> variables;
    bool fetched = false;
};

struct WatchEntry {
    char buf[256] = {};
};

struct OpenSource {
    std::string path;
    bool open = true;
    bool focus = false;
    bool needs_dock = true;

    // Find bar
    bool find_open = false;
    bool find_focus = false;
    char find_buf[256] = {};
    int find_match_idx = -1;
    int find_match_count = 0;
    bool find_scroll = false;

    // Go to Line
    bool goto_open = false;
    bool goto_focus = false;
    char goto_buf[16] = {};
    int goto_line = 0;

    // Smooth scrolling
    float scroll_target = -1.0f;  // <0 = inactive

    // Flash highlight
    int flash_line = 0;
    float flash_time = 0;
};

struct LineBreakpoint {
    std::string path;
    int line;
    std::string line_text;  // content when set, for relocation after edits
    int id = 0;
    bool enabled = true;
};

struct ExceptionFilter {
    std::string filter;
    std::string label;
    bool enabled;
    bool default_enabled;   // server-reported default, for "Reset to defaults"
};

struct Debugger {
    DapState state = DapState::IDLE;
    int thread_id = 0;
    std::vector<StackFrame> stack;
    std::deque<SourceFile> sources;
    SourceFile *current_source = nullptr;
    int current_line = 0;
    bool scroll_to_line = false;

    // Platform pipe I/O (set by platform_launch)
    void *platform = nullptr;
    int  (*pipe_read)(void *ctx, char *buf, int len) = nullptr;
    bool (*pipe_write)(void *ctx, const char *buf, int len) = nullptr;
    void (*pipe_shutdown)(void *ctx) = nullptr;

    // Platform stdout capture (set by platform_launch, may be null)
    int  (*stdout_read)(void *ctx, char *buf, int len) = nullptr;
    void (*stdout_shutdown)(void *ctx) = nullptr;
    std::thread stdout_thread;
    std::atomic<bool> stdout_running{false};
    std::string stdout_pending;  // guarded by queue_mutex

    // DAP protocol state
    bool pause_at_entry = false;
    int next_seq = 1;
    std::mutex queue_mutex;
    std::vector<std::string> inbox;
    std::thread reader_thread;
    std::atomic<bool> reader_running{false};

    // Variable/scope data (refreshed each time we stop)
    std::vector<DapScope> scopes;
    std::vector<DapScope> pending_scopes;  // buffered until variables arrive
    int pending_scope_reqs = 0;            // outstanding variable requests
    std::unordered_map<int, int64_t> pending_vars;  // request seq → variablesReference

    // Breakpoints
    std::vector<LineBreakpoint> breakpoints;
    std::string run_to_path;  // temporary invisible breakpoint for "Run to line"
    int run_to_line = 0;
    std::vector<ExceptionFilter> exception_filters;
    std::unordered_map<int, std::string> pending_bps;  // request seq → file path
    std::unordered_map<std::string, std::string> cmake_paths;  // canonical → cmake name

    // Source tabs
    std::vector<OpenSource> open_sources;
    std::string focused_source;  // path of last focused source tab
    unsigned int source_dock_id = 0;
    unsigned int dockspace_id = 0;

    // UI state
    std::string ini_path;
    char cmdline[4096] = {};
    char filter_locals[256] = {};
    char filter_cache[256] = {};
    char filter_targets[256] = {};
    char filter_tests[256] = {};
    std::string status;
    int win_x = -1;           // -1 = use default position
    int win_y = -1;
    int win_w = 1280;
    int win_h = 720;
    bool win_maximized = false;
    bool want_quit = false;
    bool title_dirty = true;
    bool first_layout = true;
    bool reset_layout = false;
    bool show_stack = true;
    bool show_locals = true;
    bool show_cache = true;
    bool show_targets = true;
    bool show_tests = true;
    bool show_breakpoints = true;
    bool show_filters = true;
    bool show_output = true;
    bool show_watch = true;
    bool show_dap_log = false;
    std::vector<WatchEntry> watches;
    std::string output;

    // DAP message log (survives stop for post-mortem, cleared on start)
    struct DapMessage {
        bool sent;             // true = to CMake, false = from CMake
        std::string summary;   // "→ request initialize" etc.
        std::string raw;       // raw JSON string
        std::string timestamp; // ISO 8601 with ms and timezone
    };
    std::vector<DapMessage> dap_log;
    std::vector<std::string> dropped_files;
    ImFont *mono_font = nullptr;
    float dpi_scale = 1.0f;
};

// Platform layer must implement these.
std::string platform_quote_argv(int argc, char **argv);
bool platform_launch(Debugger *dbg, const char *args);
void platform_cleanup(Debugger *dbg);
std::string platform_open_file_dialog();
std::string platform_open_directory_dialog();
bool platform_chdir(const char *path);
std::string platform_read_file(const char *path);
bool platform_write_file(const char *path, const char *data, size_t len);
void platform_set_icon(void *window);
std::string platform_realpath(const std::string &path);
std::string platform_save_file_dialog();
std::string platform_now_iso8601();
std::string platform_config_dir();

// Shared logic called by the platform main loop.
void dcmake_init(Debugger *dbg);
void dcmake_load_config(Debugger *dbg);
void dcmake_start(Debugger *dbg);
void dcmake_stop(Debugger *dbg);
void dcmake_frame(Debugger *dbg);
void dcmake_shutdown(Debugger *dbg);
