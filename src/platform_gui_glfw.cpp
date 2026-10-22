#include "dcmake.hpp"
#include "theme.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <atomic>
#include <thread>

#include <dlfcn.h>
#include <poll.h>
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
    // The dialog runs as a separate process, so keep pumping our own
    // window events while waiting on it.  Otherwise the compositor or
    // window manager pings go unanswered and dcmake gets flagged as not
    // responding.
    int fd = fileno(f);
    char buf[4096];
    std::string result;
    for (;;) {
        pollfd pfd = {fd, POLLIN, 0};
        int r = poll(&pfd, 1, 50);
        if (r < 0 && errno != EINTR) break;
        if (r <= 0) {
            glfwPollEvents();
            continue;
        }
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        result.append(buf, n);
    }
    int status = pclose(f);

    // Like a modal dialog, drop any input the main window received while
    // the dialog was up rather than acting on it afterward.
    ImGuiIO &io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();

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

void platform_window_theme(void *, bool)
{
    // Linux: the window manager draws the frame
}

// The system theme comes from the XDG desktop portal's color-scheme
// setting over D-Bus: read once at startup, then followed by a thread
// watching its SettingChanged signal.  libdbus is loaded at run time, so
// it is no build dependency.  Without it, or a portal, the system is dark,
// as dcmake always was.  GNOME reports its light "Default" appearance as
// no preference, so only an explicit dark preference means dark.

// libdbus, as much as is needed here, declared by hand
using DbusInit      = unsigned (*)();
using DbusGet       = void    *(*)(int, void *);
using DbusExit      = void     (*)(void *, unsigned);
using DbusMatch     = void     (*)(void *, const char *, void *);
using DbusRead      = unsigned (*)(void *, int);
using DbusPop       = void    *(*)(void *);
using DbusIsSignal  = unsigned (*)(void *, const char *, const char *);
using DbusNewCall   = void    *(*)(const char *, const char *,
                                   const char *, const char *);
using DbusAppend    = unsigned (*)(void *, int, ...);
using DbusCall      = void    *(*)(void *, void *, int, void *);
using DbusIterInit  = unsigned (*)(void *, void *);
using DbusIterNext  = unsigned (*)(void *);
using DbusIterType  = int      (*)(void *);
using DbusIterSub   = void     (*)(void *, void *);
using DbusIterGet   = void     (*)(void *, void *);
using DbusUnref     = void     (*)(void *);
using DbusFlush     = void     (*)(void *);

struct DbusIter {
    void *opaque[16];  // a DBusMessageIter, 72 bytes on 64-bit hosts
};

static struct {
    DbusGet      get;
    DbusExit     exitondisc;
    DbusMatch    match;
    DbusRead     read;
    DbusPop      pop;
    DbusIsSignal issignal;
    DbusNewCall  newcall;
    DbusAppend   append;
    DbusCall     call;
    DbusIterInit iterinit;
    DbusIterNext iternext;
    DbusIterType itertype;
    DbusIterSub  itersub;
    DbusIterGet  iterget;
    DbusUnref    unref;
    DbusFlush    flush;
} dbus;

// 1 dark, 2 light, 0 no preference, as the portal reports it, or -1
static std::atomic<int> portal_scheme{-1};

// The scheme in a variant at the iterator, unwrapping nested variants
// (the older Read method returns one inside another), or -1.
static int portal_variant(DbusIter *it)
{
    while (dbus.itertype(it) == 'v') {
        DbusIter sub = {};
        dbus.itersub(it, &sub);
        *it = sub;
    }
    if (dbus.itertype(it) != 'u') return -1;
    unsigned scheme = 0;
    dbus.iterget(it, &scheme);
    return (int)scheme;
}

// The color-scheme of a SettingChanged signal, or -1 for other settings.
static int portal_signal_scheme(void *msg)
{
    static const char *const want[] = {
        "org.freedesktop.appearance", "color-scheme",
    };
    DbusIter it = {};
    if (!dbus.iterinit(msg, &it)) return -1;
    for (const char *w : want) {
        const char *s = nullptr;
        if (dbus.itertype(&it) != 's') return -1;
        dbus.iterget(&it, &s);
        if (!s || strcmp(s, w) || !dbus.iternext(&it)) return -1;
    }
    return portal_variant(&it);
}

// Ask the portal for color-scheme, or -1 if it does not answer.
static int portal_read(void *conn, const char *method)
{
    const char *ns = "org.freedesktop.appearance";
    const char *key = "color-scheme";
    void *msg = dbus.newcall("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.Settings", method);
    if (!msg) return -1;
    int scheme = -1;
    if (dbus.append(msg, 's', &ns, 's', &key, 0)) {
        void *reply = dbus.call(conn, msg, 500, nullptr);
        if (reply) {
            DbusIter it = {};
            if (dbus.iterinit(reply, &it))
                scheme = portal_variant(&it);
            dbus.unref(reply);
        }
    }
    dbus.unref(msg);
    return scheme;
}

static void portal_thread(void *conn)
{
    while (dbus.read(conn, -1)) {
        for (void *m; (m = dbus.pop(conn));) {
            int scheme = -1;
            if (dbus.issignal(m, "org.freedesktop.portal.Settings",
                              "SettingChanged"))
                scheme = portal_signal_scheme(m);
            dbus.unref(m);
            if (scheme >= 0)
                portal_scheme.store(scheme);
        }
    }
}

static void system_theme_watch()
{
    void *h = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
    auto init = (DbusInit)dlsym(h, "dbus_threads_init_default");
    dbus.get        = (DbusGet)dlsym(h, "dbus_bus_get_private");
    dbus.exitondisc = (DbusExit)dlsym(h, "dbus_connection_set_exit_on_disconnect");
    dbus.match      = (DbusMatch)dlsym(h, "dbus_bus_add_match");
    dbus.read       = (DbusRead)dlsym(h, "dbus_connection_read_write");
    dbus.pop        = (DbusPop)dlsym(h, "dbus_connection_pop_message");
    dbus.issignal   = (DbusIsSignal)dlsym(h, "dbus_message_is_signal");
    dbus.newcall    = (DbusNewCall)dlsym(h, "dbus_message_new_method_call");
    dbus.append     = (DbusAppend)dlsym(h, "dbus_message_append_args");
    dbus.call       = (DbusCall)dlsym(h, "dbus_connection_send_with_reply_and_block");
    dbus.iterinit   = (DbusIterInit)dlsym(h, "dbus_message_iter_init");
    dbus.iternext   = (DbusIterNext)dlsym(h, "dbus_message_iter_next");
    dbus.itertype   = (DbusIterType)dlsym(h, "dbus_message_iter_get_arg_type");
    dbus.itersub    = (DbusIterSub)dlsym(h, "dbus_message_iter_recurse");
    dbus.iterget    = (DbusIterGet)dlsym(h, "dbus_message_iter_get_basic");
    dbus.unref      = (DbusUnref)dlsym(h, "dbus_message_unref");
    dbus.flush      = (DbusFlush)dlsym(h, "dbus_connection_flush");
    bool ok = init && dbus.get && dbus.exitondisc && dbus.match &&
              dbus.read && dbus.pop && dbus.issignal && dbus.newcall &&
              dbus.append && dbus.call && dbus.iterinit && dbus.iternext &&
              dbus.itertype && dbus.itersub && dbus.iterget && dbus.unref &&
              dbus.flush;
    if (!ok || !init()) return;

    void *conn = dbus.get(0, nullptr);  // the session bus, privately
    if (!conn) return;
    dbus.exitondisc(conn, 0);
    dbus.match(conn, "type='signal',interface='org.freedesktop.portal.Settings',"
                     "member='SettingChanged',arg0='org.freedesktop.appearance',"
                     "arg1='color-scheme'", nullptr);
    dbus.flush(conn);

    int scheme = portal_read(conn, "ReadOne");
    if (scheme < 0) scheme = portal_read(conn, "Read");  // older portals
    if (scheme >= 0) portal_scheme.store(scheme);

    std::thread(portal_thread, conn).detach();
}

static bool system_dark()
{
    int scheme = portal_scheme.load();
    return scheme < 0 || scheme == 1;
}
#else
// filedialog_macos.mm
void system_theme_watch();
bool system_dark();
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
    system_theme_watch();
    dbg.sys_dark = system_dark();

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

        bool dark = system_dark();
        if (dark != dbg.sys_dark) {
            dbg.sys_dark = dark;
            dbg.theme_dirty = true;
        }
        if (dbg.theme_dirty) {
            dbg.theme_dirty = false;
            theme_apply(&dbg);
            platform_window_theme(window, dbg.pal->dark);
        }

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
        ImVec4 clear = ImGui::ColorConvertU32ToFloat4(dbg.pal->clear);
        glClearColor(clear.x, clear.y, clear.z, 1.0f);
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
