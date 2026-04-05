#include "dcmake.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <errno.h>
#include <signal.h>
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
    std::string cmd = "cmake --debugger --debugger-pipe=";
    cmd += p->pipe_path;
    cmd += ' ';
    cmd += args;

    // Create pipe for capturing cmake stdout/stderr
    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) {
        dbg->status = "Failed to create stdout pipe";
        return false;
    }

    // Fork cmake subprocess via sh -c in its own process group
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
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

    // Close stdout pipe if still open
    if (p->stdout_fd >= 0) {
        close(p->stdout_fd);
        p->stdout_fd = -1;
    }

    // Clean up socket file
    if (!p->pipe_path.empty()) {
        unlink(p->pipe_path.c_str());
    }

    // Kill cmake process group (sh + cmake + any children)
    if (p->cmake_pid > 0) {
        kill(-p->cmake_pid, SIGTERM);
        waitpid(p->cmake_pid, nullptr, 0);
        p->cmake_pid = -1;
    }

    delete p;
    dbg->platform = nullptr;
}

#ifndef __APPLE__
std::string platform_open_file_dialog()
{
    return {};
}

std::string platform_open_directory_dialog()
{
    return {};
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

// --- GLFW + OpenGL3 entry point ---

int main(int argc, char **argv)
{
    if (!glfwInit()) {
        fprintf(stderr, "dcmake: glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "dcmake", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "dcmake: failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    platform_set_icon(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    Debugger dbg = {};
    std::string initial_args = platform_quote_argv(argc, argv);
    if (initial_args.empty()) initial_args = "-B build";
    snprintf(dbg.cmdline, sizeof(dbg.cmdline), "%s", initial_args.c_str());
    dcmake_init(&dbg);

    // Set up config directory for imgui.ini
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
        io.IniFilename = nullptr;
        std::string ini = platform_read_file(dbg.ini_path.c_str());
        if (!ini.empty())
            ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    }
    dcmake_load_config(&dbg);

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

    dcmake_shutdown(&dbg);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
