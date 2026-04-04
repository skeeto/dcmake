#ifndef DCMAKE_H
#define DCMAKE_H

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
};

struct DapScope {
    std::string name;
    int64_t variables_ref = 0;
    std::vector<DapVariable> variables;
    bool fetched = false;
};

struct LineBreakpoint {
    std::string path;
    int line;
    int id = 0;
    bool verified = false;
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

    // DAP protocol state
    bool pause_at_entry = false;
    int next_seq = 1;
    std::mutex queue_mutex;
    std::vector<std::string> inbox;
    std::thread reader_thread;
    std::atomic<bool> reader_running{false};

    // Variable/scope data (refreshed each time we stop)
    std::vector<DapScope> scopes;
    std::unordered_map<int, int64_t> pending_vars;  // request seq → variablesReference

    // Breakpoints
    std::vector<LineBreakpoint> breakpoints;
    std::vector<ExceptionFilter> exception_filters;

    // UI state
    std::string ini_path;
    char cmdline[4096] = {};
    std::string status;
    bool want_quit = false;
    bool first_layout = true;
    bool show_source = true;
    bool show_stack = true;
    bool show_locals = true;
    bool show_cache = false;
    bool show_targets = false;
    bool show_tests = false;
    bool show_breakpoints = true;
};

// Platform layer must implement these.
std::string platform_quote_argv(int argc, char **argv);
bool platform_launch(Debugger *dbg, const char *args);
void platform_cleanup(Debugger *dbg);

// Shared logic called by the platform main loop.
void dcmake_init(Debugger *dbg);
void dcmake_start(Debugger *dbg);
void dcmake_stop(Debugger *dbg);
void dcmake_frame(Debugger *dbg);
void dcmake_shutdown(Debugger *dbg);

#endif
