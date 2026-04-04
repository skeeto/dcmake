#include "dcmake.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <errno.h>
#include <signal.h>
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
    pid_t cmake_pid = -1;
    std::string pipe_path;
};

static int posix_pipe_read(void *ctx, char *buf, int len)
{
    auto *p = (PosixPlatform *)ctx;
    ssize_t n = read(p->sock_fd, buf, len);
    return n > 0 ? (int)n : 0;
}

static bool posix_pipe_write(void *ctx, const char *buf, int len)
{
    auto *p = (PosixPlatform *)ctx;
    return write(p->sock_fd, buf, len) == len;
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

bool platform_launch(Debugger *dbg, int argc, char **argv)
{
    auto *p = new PosixPlatform;
    dbg->platform = p;
    dbg->pipe_read = posix_pipe_read;
    dbg->pipe_write = posix_pipe_write;
    dbg->pipe_shutdown = posix_pipe_shutdown;

    // Build pipe path
    p->pipe_path = "/tmp/dcmake-" + std::to_string(getpid()) + ".sock";
    unlink(p->pipe_path.c_str());

    // Build cmake argument list
    std::vector<const char *> args;
    args.push_back("cmake");
    args.push_back("--debugger");
    std::string pipe_arg = "--debugger-pipe=" + p->pipe_path;
    args.push_back(pipe_arg.c_str());
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);

    // Fork cmake subprocess
    pid_t pid = fork();
    if (pid == 0) {
        execvp("cmake", const_cast<char *const *>(args.data()));
        _exit(127);
    }
    if (pid < 0) {
        dbg->status = "Failed to fork cmake";
        return false;
    }
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

    // Clean up socket file
    if (!p->pipe_path.empty()) {
        unlink(p->pipe_path.c_str());
    }

    // Kill cmake if still running
    if (p->cmake_pid > 0) {
        kill(p->cmake_pid, SIGTERM);
        waitpid(p->cmake_pid, nullptr, 0);
        p->cmake_pid = -1;
    }

    delete p;
    dbg->platform = nullptr;
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    Debugger dbg = {};
    dcmake_init(&dbg, argc, argv);

    while (!glfwWindowShouldClose(window) && !dbg.want_quit) {
        glfwPollEvents();

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
