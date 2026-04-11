#include "dcmake.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

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

#ifndef __APPLE__
// Returns the command's stdout and sets *exit_code.  Returns empty
// string on failure.  Exit 127 = command not found (shell convention).
static std::string run_dialog(const char *cmd, int *exit_code)
{
    *exit_code = 127;
    FILE *f = popen(cmd, "r");
    if (!f) return {};
    char buf[4096];
    std::string result;
    while (fgets(buf, sizeof(buf), f))
        result += buf;
    int status = pclose(f);
    if (WIFEXITED(status))
        *exit_code = WEXITSTATUS(status);
    if (*exit_code != 0) return {};
    while (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

std::string platform_open_file_dialog()
{
    int rc;
    std::string r = run_dialog("kdialog --getopenfilename . 2>/dev/null", &rc);
    if (rc != 127) return r;
    return run_dialog("zenity --file-selection 2>/dev/null", &rc);
}

std::string platform_open_directory_dialog()
{
    int rc;
    std::string r = run_dialog("kdialog --getexistingdirectory . 2>/dev/null", &rc);
    if (rc != 127) return r;
    return run_dialog("zenity --file-selection --directory 2>/dev/null", &rc);
}

std::string platform_save_file_dialog()
{
    int rc;
    std::string r = run_dialog(
        "kdialog --getsavefilename . '*.json|JSON files' 2>/dev/null", &rc);
    if (rc != 127) return r;
    return run_dialog(
        "zenity --file-selection --save --file-filter='*.json' 2>/dev/null",
        &rc);
}

void platform_set_icon(void *)
{
    // Linux: window icon set via .desktop file
}
#endif

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

std::string platform_realpath(const std::string &path)
{
    char *resolved = realpath(path.c_str(), nullptr);
    if (!resolved) return path;
    std::string result(resolved);
    free(resolved);
    return result;
}

static void drop_callback(GLFWwindow *window, int count, const char **paths)
{
    Debugger *dbg = (Debugger *)glfwGetWindowUserPointer(window);
    for (int i = 0; i < count; i++)
        dbg->dropped_files.push_back(paths[i]);
}

// --- GLFW + OpenGL3 entry point ---

int main(int argc, char **argv)
{
    if (!glfwInit()) {
        fprintf(stderr, "dcmake: glfwInit failed\n");
        return 1;
    }

    // Set up config directory and load dcmake config (need window geometry
    // before creating the window).
    Debugger dbg = {};
    std::string initial_args = platform_quote_argv(argc, argv);
    if (initial_args.empty()) initial_args = "-B build";
    snprintf(dbg.cmdline, sizeof(dbg.cmdline), "%s", initial_args.c_str());
    {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) {
            dbg.ini_path = xdg;
        } else {
            const char *home = getenv("HOME");
            dbg.ini_path = home ? home : ".";
            dbg.ini_path += "/.config";
        }
        dbg.ini_path += "/dcmake";
        mkdir(dbg.ini_path.c_str(), 0755);
        dbg.ini_path += "/imgui.ini";
    }
    dcmake_load_config(&dbg);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(dbg.win_w, dbg.win_h,
                                          "dcmake", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "dcmake: failed to create window\n");
        glfwTerminate();
        return 1;
    }
    if (dbg.win_x >= 0 && dbg.win_y >= 0) {
        glfwSetWindowPos(window, dbg.win_x, dbg.win_y);
    } else {
        // Center on primary monitor on first start
        GLFWmonitor *mon = glfwGetPrimaryMonitor();
        if (mon) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
            glfwSetWindowPos(window,
                             mx + (mw - dbg.win_w) / 2,
                             my + (mh - dbg.win_h) / 2);
        }
    }
    if (dbg.win_maximized)
        glfwMaximizeWindow(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    platform_set_icon(window);
    glfwSetWindowUserPointer(window, &dbg);
    glfwSetDropCallback(window, drop_callback);
    glfwGetWindowContentScale(window, &dbg.dpi_scale, nullptr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    dcmake_init(&dbg);
    io.FontGlobalScale = 1.0f / dbg.dpi_scale;

    // Load ImGui layout
    io.IniFilename = nullptr;
    {
        std::string ini = platform_read_file(dbg.ini_path.c_str());
        if (!ini.empty())
            ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    }

    while (!glfwWindowShouldClose(window) && !dbg.want_quit) {
        glfwPollEvents();

        if (dbg.title_dirty) {
            dbg.title_dirty = false;
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                std::string title = std::string("dcmake - ") + cwd;
                glfwSetWindowTitle(window, title.c_str());
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        dcmake_frame(&dbg);

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Capture window geometry before shutdown
    dbg.win_maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
    if (!dbg.win_maximized) {
        glfwGetWindowPos(window, &dbg.win_x, &dbg.win_y);
        glfwGetWindowSize(window, &dbg.win_w, &dbg.win_h);
    }

    dcmake_shutdown(&dbg);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
