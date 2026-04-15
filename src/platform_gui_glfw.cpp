#include "dcmake.hpp"

#include <cstdio>
#include <cstring>
#include <string>

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

// GLFW + OpenGL3 GUI half of the platform layer.  Paired with
// platform_os_posix.cpp on macOS and Linux.  Contains the entry point,
// window/GL context setup, render loop, and non-macOS file dialogs
// (kdialog/zenity) and window icon.  macOS overrides the dialog/icon
// functions via filedialog_macos.mm.

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
        std::string dir = platform_config_dir();
        if (!dir.empty())
            dbg.ini_path = dir + "/imgui.ini";
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

    // Fonts are baked at physical size (size * dpi_scale).  On Wayland and
    // macOS the framebuffer is larger than the window (backing-store
    // scaling), so FontGlobalScale compensates.  On X11 the framebuffer
    // equals the window, so the full dpi_scale must remain to enlarge the
    // UI — with ScaleAllSizes for padding/spacing.
    int win_w_now, fb_w_now;
    glfwGetWindowSize(window, &win_w_now, nullptr);
    glfwGetFramebufferSize(window, &fb_w_now, nullptr);
    float fb_scale = (win_w_now > 0) ? (float)fb_w_now / (float)win_w_now
                                     : 1.0f;
    io.FontGlobalScale = 1.0f / fb_scale;
    float style_scale = dbg.dpi_scale / fb_scale;
    if (style_scale > 1.0f)
        ImGui::GetStyle().ScaleAllSizes(style_scale);

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
