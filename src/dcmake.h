#ifndef DCMAKE_H
#define DCMAKE_H

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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
    int next_seq = 1;
    std::mutex queue_mutex;
    std::vector<std::string> inbox;
    std::thread reader_thread;
    std::atomic<bool> reader_running{false};

    std::string status;
    bool want_quit = false;
};

// Platform layer must implement these.
bool platform_launch(Debugger *dbg, int argc, char **argv);
void platform_cleanup(Debugger *dbg);

// Shared logic called by the platform main loop.
void dcmake_init(Debugger *dbg, int argc, char **argv);
void dcmake_frame(Debugger *dbg);
void dcmake_shutdown(Debugger *dbg);

#endif
