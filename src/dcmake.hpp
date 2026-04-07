#ifndef DCMAKE_HPP
#define DCMAKE_HPP

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

struct OpenSource {
    std::string path;
    bool open = true;
    bool focus = false;
    bool needs_dock = true;
};

struct LineBreakpoint {
    std::string path;
    int line;
    std::string line_text;  // content when set, for relocation after edits
    int id = 0;
    bool verified = false;
    bool enabled = true;
};

struct ExceptionFilter {
    std::string filter;
    std::string label;
    bool enabled;
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

    // Source tabs
    std::vector<OpenSource> open_sources;
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
    bool want_quit = false;
    bool title_dirty = true;
    bool first_layout = true;
    bool show_stack = true;
    bool show_locals = true;
    bool show_cache = true;
    bool show_targets = true;
    bool show_tests = true;
    bool show_breakpoints = true;
    bool show_filters = true;
    bool show_output = true;
    std::string output;
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

// Shared logic called by the platform main loop.
void dcmake_init(Debugger *dbg);
void dcmake_load_config(Debugger *dbg);
void dcmake_start(Debugger *dbg);
void dcmake_stop(Debugger *dbg);
void dcmake_frame(Debugger *dbg);
void dcmake_shutdown(Debugger *dbg);

#endif
