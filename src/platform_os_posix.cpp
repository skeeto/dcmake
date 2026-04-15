#include "dcmake.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

// POSIX OS half of the platform layer.  Paired with either
// platform_gui_glfw.cpp (macOS/Linux) or platform_gui_win32.cpp
// (Cygwin) at link time.  Contains the DAP socket, cmake subprocess
// launch, stdout capture, and portable filesystem helpers.  No
// windowing / dialog code lives here.

// --- Platform pipe implementation ---

struct PosixPlatform {
    int sock_fd = -1;
    int stdout_fd = -1;
    pid_t cmake_pid = -1;
    std::string pipe_path;
};

static int posix_pipe_read(void *ctx, char *buf, int len)
{
    auto *p = (PosixPlatform *)ctx;
    ssize_t n = read(p->sock_fd, buf, (size_t)len);
    return n > 0 ? (int)n : 0;
}

static bool posix_pipe_write(void *ctx, const char *buf, int len)
{
    auto *p = (PosixPlatform *)ctx;
    return write(p->sock_fd, buf, (size_t)len) == len;
}

static void posix_pipe_shutdown(void *ctx)
{
    auto *p = (PosixPlatform *)ctx;
    if (p->sock_fd >= 0) {
        shutdown(p->sock_fd, SHUT_RDWR);
        close(p->sock_fd);
        p->sock_fd = -1;
    }
}

static int posix_stdout_read(void *ctx, char *buf, int len)
{
    auto *p = (PosixPlatform *)ctx;
    struct pollfd pfd = {p->stdout_fd, POLLIN, 0};
    if (poll(&pfd, 1, 100) <= 0) return -1;  // timeout or error
    ssize_t n = read(p->stdout_fd, buf, (size_t)len);
    return n > 0 ? (int)n : 0;
}

static void posix_stdout_shutdown(void *ctx)
{
    auto *p = (PosixPlatform *)ctx;
    if (p->stdout_fd >= 0) {
        close(p->stdout_fd);
        p->stdout_fd = -1;
    }
}

// POSIX shell-quote: wrap in single quotes, escape embedded single quotes.
std::string platform_quote_argv(int argc, char **argv)
{
    std::string result;
    for (int i = 1; i < argc; i++) {
        if (i > 1) result += ' ';
        const char *arg = argv[i];
        // Check if quoting is needed
        bool clean = true;
        for (const char *c = arg; *c; c++) {
            if (!isalnum(*c) && *c != '/' && *c != '.' && *c != '-' &&
                *c != '_' && *c != '=' && *c != ':' && *c != '$') {
                clean = false;
                break;
            }
        }
        if (clean && *arg) {
            result += arg;
        } else {
            result += '\'';
            for (const char *c = arg; *c; c++) {
                if (*c == '\'') {
                    result += "'\\''";
                } else {
                    result += *c;
                }
            }
            result += '\'';
        }
    }
    return result;
}

bool platform_launch(Debugger *dbg, const char *args)
{
    auto *p = new PosixPlatform;
    dbg->platform = p;
    dbg->pipe_read = posix_pipe_read;
    dbg->pipe_write = posix_pipe_write;
    dbg->pipe_shutdown = posix_pipe_shutdown;
    dbg->stdout_read = posix_stdout_read;
    dbg->stdout_shutdown = posix_stdout_shutdown;

    // Build pipe path
    static int launch_count = 0;
    p->pipe_path = "/tmp/dcmake-" + std::to_string(getpid())
                  + "-" + std::to_string(launch_count++) + ".sock";
    unlink(p->pipe_path.c_str());

    // Build shell command
    std::string cmd = "exec cmake --debugger --debugger-pipe=";
    cmd += p->pipe_path;
    cmd += ' ';
    cmd += args;

    // Create pipe for capturing cmake stdout/stderr
    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) {
        dbg->status = "Failed to create stdout pipe";
        return false;
    }

    // Fork cmake subprocess via sh -c (same process group so ctrl-c
    // reaches cmake even if dcmake dies without running cleanup).
    pid_t pid = fork();
    if (pid == 0) {
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        execlp("sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        dbg->status = "Failed to fork cmake";
        return false;
    }
    close(stdout_pipe[1]);
    p->stdout_fd = stdout_pipe[0];
    p->cmake_pid = pid;

    // Connect to cmake's unix domain socket (retry loop)
    p->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (p->sock_fd < 0) {
        dbg->status = "Failed to create socket";
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, p->pipe_path.c_str(), sizeof(addr.sun_path) - 1);

    bool connected = false;
    for (int i = 0; i < 500; i++) {
        if (connect(p->sock_fd, (sockaddr *)&addr, sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        int wstatus;
        if (waitpid(p->cmake_pid, &wstatus, WNOHANG) > 0) {
            p->cmake_pid = -1;
            break;
        }
        usleep(10000);
    }

    if (!connected) {
        dbg->status = "Failed to connect to cmake debugger";
        return false;
    }

    return true;
}

void platform_cleanup(Debugger *dbg)
{
    auto *p = (PosixPlatform *)dbg->platform;
    if (!p) return;

    // Close socket if still open
    if (p->sock_fd >= 0) {
        close(p->sock_fd);
        p->sock_fd = -1;
    }

    // Kill cmake before closing the stdout pipe.  On Linux, close()
    // does not unblock a read() in another thread; the stdout thread
    // needs cmake to die so the write end of the pipe closes and
    // read() returns EOF.
    if (p->cmake_pid > 0) {
        kill(p->cmake_pid, SIGTERM);
        waitpid(p->cmake_pid, nullptr, 0);
        p->cmake_pid = -1;
    }

    // Close stdout pipe (now safe — stdout thread has seen EOF)
    if (p->stdout_fd >= 0) {
        close(p->stdout_fd);
        p->stdout_fd = -1;
    }

    // Clean up socket file
    if (!p->pipe_path.empty()) {
        unlink(p->pipe_path.c_str());
    }

    delete p;
    dbg->platform = nullptr;
}

std::string platform_now_iso8601()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int ms = (int)(ts.tv_nsec / 1000000);
    std::tm local;
    localtime_r(&ts.tv_sec, &local);
    char buf[64];
    int n = (int)strftime(buf, sizeof(buf), "%FT%T", &local);
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, ".%03d", ms);
    char tz[8];
    strftime(tz, sizeof(tz), "%z", &local);
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%.3s:%.2s", tz, tz + 3);
    return std::string(buf, (size_t)n);
}

bool platform_chdir(const char *path)
{
    return chdir(path) == 0;
}

std::string platform_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n <= 0) { fclose(f); return {}; }
    fseek(f, 0, SEEK_SET);
    std::string out((size_t)n, '\0');
    size_t got = fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    out.resize(got);
    return out;
}

bool platform_write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    return wrote == len;
}

std::string platform_config_dir()
{
    std::string dir;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        dir = xdg;
    } else {
        const char *home = getenv("HOME");
        dir = home ? home : ".";
        dir += "/.config";
    }
    dir += "/dcmake";
    mkdir(dir.c_str(), 0755);
    return dir;
}

std::string platform_realpath(const std::string &path)
{
    char *resolved = realpath(path.c_str(), nullptr);
    if (!resolved) return path;
    std::string result(resolved);
    free(resolved);
    return result;
}
