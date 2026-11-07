#include "dcmake.hpp"
#include "theme.hpp"
#include "platform_win32_util.hpp"
#include "sched.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <dwmapi.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <atomic>
#include <cstdio>
#include <string>

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

// Win32 + DX11 GUI half of the platform layer.  Paired with either
// platform_os_win32.cpp (native Windows) or platform_os_posix.cpp
// (Cygwin) at link time.  Contains the window/DX11 setup, message
// pump + render loop, file dialogs, and the entry point.
//
// Entry point: under Cygwin the CRT calls int main(), while native
// Windows (/SUBSYSTEM:WINDOWS) calls WinMain.  Both delegate to
// gui_win32_run() so the DX11 code is shared with zero duplication.

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Convert a wide Windows path from a file dialog / drag-and-drop
// into the form the OS half of the platform layer expects.  On
// native Windows that's just UTF-8 with backslashes; on Cygwin and
// MSYS2's msys environment it's a POSIX path under /cygdrive or
// /c, which plays nicely with the POSIX OS half's fopen/chdir and
// with sh -c quoting in platform_launch.
static std::string dialog_path(const wchar_t *wpath)
{
#ifdef __CYGWIN__
    if (!wpath || !*wpath) return {};
    ssize_t n = cygwin_conv_path(CCP_WIN_W_TO_POSIX | CCP_ABSOLUTE,
                                 wpath, nullptr, 0);
    if (n <= 0) return to_utf8(wpath);
    std::string out((size_t)(n - 1), '\0');
    if (cygwin_conv_path(CCP_WIN_W_TO_POSIX | CCP_ABSOLUTE,
                         wpath, out.data(), (size_t)n) != 0)
        return to_utf8(wpath);
    return out;
#else
    return to_utf8(wpath);
#endif
}


std::string platform_open_directory_dialog()
{
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select Working Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        return dialog_path(path);
    }
    return {};
}

std::string platform_open_file_dialog()
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        return dialog_path(path);
    }
    return {};
}

std::string platform_save_file_dialog()
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        return dialog_path(path);
    }
    return {};
}

void platform_set_icon(void *)
{
    // Icon set via .rc resource file
}

// Whether apps should be dark, per Settings > Personalization > Colors.
// Before Windows 10 1809 there is no such value, and apps are light.
static bool system_dark()
{
    DWORD v = 1, n = sizeof(v);
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
    return r == ERROR_SUCCESS && v == 0;
}

static int g_frame_dark = -1;  // title bar as last set, -1 for never

void platform_window_theme(void *window, bool dark)
{
    HWND hwnd = (HWND)window;
    if (g_frame_dark == (int)dark) return;
    BOOL v = dark;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20, or 19 before Windows 10 20H1
    HRESULT r = DwmSetWindowAttribute(hwnd, 20, &v, sizeof(v));
    if (FAILED(r)) r = DwmSetWindowAttribute(hwnd, 19, &v, sizeof(v));
    if (FAILED(r)) return;
    if (g_frame_dark >= 0 && IsWindowVisible(hwnd)) {
        // Windows 10 repaints the title bar only on activation changes
        BOOL active = GetActiveWindow() == hwnd;
        SendMessageW(hwnd, WM_NCACTIVATE, !active, 0);
        SendMessageW(hwnd, WM_NCACTIVATE, active, 0);
    }
    g_frame_dark = dark;
}

// --- Win32 + DX11 entry point ---

static ID3D11Device *g_device = nullptr;
static ID3D11DeviceContext *g_context = nullptr;
static IDXGISwapChain *g_swapchain = nullptr;
static ID3D11RenderTargetView *g_rtv = nullptr;
static D3D_DRIVER_TYPE g_driver = D3D_DRIVER_TYPE_HARDWARE;
static bool g_presented;  // a Present on this device has succeeded

static bool create_rtv()
{
    ID3D11Texture2D *back_buffer = nullptr;
    if (FAILED(g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
        return false;
    HRESULT hr = g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
    back_buffer->Release();
    return SUCCEEDED(hr);
}

static void destroy_device()
{
    if (g_rtv)       { g_rtv->Release();       g_rtv = nullptr; }
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context)   { g_context->Release();   g_context = nullptr; }
    if (g_device)    { g_device->Release();    g_device = nullptr; }
}

// Create the device and swap chain on g_driver, falling back to WARP.
static bool create_device(HWND hwnd)
{
    for (;;) {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, g_driver, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &g_swapchain, &g_device, nullptr, &g_context);
        if (SUCCEEDED(hr) && create_rtv()) {
            g_presented = false;
            return true;
        }
        destroy_device();
        if (g_driver == D3D_DRIVER_TYPE_WARP) return false;
        g_driver = D3D_DRIVER_TYPE_WARP;
    }
}

static Debugger *g_dbg = nullptr;
static WINDOWPLACEMENT g_last_placement = { sizeof(g_last_placement) };
static RECT g_last_rect;

// --- Passive rendering ---
//
// The loop waits for messages, a wake, or the scheduler's next deadline,
// then draws only if the scheduler says a frame is due (sched.cpp).
// Wakes from other threads are coalesced with an atomic flag and arrive
// as a posted WM_APP_WAKE, whose handler clears the flag before the frame
// drains, so every post after a drain began wakes again.  Windows' modal
// loops (a move or resize, a file dialog) dispatch it too, so the request
// survives them.
//
// The window procedure renders from inside a move/resize modal loop: a
// resize draws at once from WM_SIZE, and a timer armed for the loop's
// duration keeps owed frames, deadlines and wakes going during a drag.

enum {
    WM_APP_WAKE = WM_APP + 1,
    TIMER_MODAL = 1,
};

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static Sched g_sched;
static std::atomic<bool> g_wake{false};
static HWND g_hwnd;
static HANDLE g_timer;     // the loop's wait timeout
static bool g_ready;       // ImGui is up: frames may be drawn
static bool g_occluded;    // Present said nothing shows

void platform_wake()
{
    if (!g_wake.exchange(true) && g_hwnd)
        PostMessageW(g_hwnd, WM_APP_WAKE, 0, 0);
}

static void take_wake()
{
    if (g_wake.exchange(false))
        scheddirty(&g_sched);
}

static int64_t refresh_pace(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    int hz = GetDeviceCaps(dc, VREFRESH);
    ReleaseDC(hwnd, dc);
    return 1000000 / (hz > 1 ? hz : 60);
}

static void no_device()
{
    MessageBoxW(g_hwnd, L"Could not create a Direct3D 11 device.",
                L"dcmake", MB_ICONERROR);
    ExitProcess(1);
}

// Replace a removed or reset device.  Some drivers (e.g. VirtualBox's
// on Windows 7) hand out a hardware device whose first Present already
// fails, so a device lost before it ever presented falls back to WARP.
static void reset_device()
{
    if (!g_presented) g_driver = D3D_DRIVER_TYPE_WARP;
    // The DX11 backend's shutdown destroys every viewport's platform
    // data, the main window's included, so cycle both backends.
    if (g_ready) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
    }
    destroy_device();
    if (!create_device(g_hwnd)) no_device();
    if (g_ready) {
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX11_Init(g_device, g_context);
    }
    schedforce(&g_sched);
}

// Draw a frame, unless one is already being drawn.
static void gui_frame()
{
    Debugger *dbg = g_dbg;
    Sched *s = &g_sched;
    if (!g_ready || !sched_frame_start(s)) return;

    if (dbg->title_dirty) {
        dbg->title_dirty = false;
        wchar_t cwd[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
            std::wstring title = L"dcmake - ";
            title += cwd;
            SetWindowTextW(g_hwnd, title.c_str());
        }
    }
    if (dbg->theme_dirty) {
        dbg->theme_dirty = false;
        theme_apply(dbg);
        platform_window_theme(g_hwnd, dbg->pal->dark);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    dcmake_frame(dbg);
    ImGui::Render();
    Frame f = sched_frame_end(s, dcmake_animating(dbg) ? BUSY_APP : 0);

    ImDrawData *dd = ImGui::GetDrawData();
    if (f.present) {
        ImVec4 clear = ImGui::ColorConvertU32ToFloat4(dbg->pal->clear);
        float clear_color[] = {clear.x, clear.y, clear.z, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear_color);
        ImGui_ImplDX11_RenderDrawData(dd);
        HRESULT hr = g_swapchain->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) {
            // Nothing shows: rest until painted or activated again.
            g_occluded = true;
            schedminimize(s, true);
        } else if (FAILED(hr)) {
            reset_device();
        } else {
            g_presented = true;
        }
    } else if (dd->Textures) {
        // Glyphs may have been added to the atlas even though the
        // picture is unchanged, and must be uploaded all the same.
        for (ImTextureData *tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplDX11_UpdateTexture(tex);
    }
}

// Wait up to us microseconds (-1: indefinitely) for a message, then
// dispatch every pending one.  Returns false on WM_QUIT.
static bool gui_wait(int64_t us)
{
    if (us < 0) {
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    } else if (us > 0) {
        LARGE_INTEGER due;
        due.QuadPart = -us * 10;  // relative, in 100 ns units
        SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE);
        MsgWaitForMultipleObjectsEx(1, &g_timer, INFINITE, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    // ImGui drops the OS's auto-repeats from its queue, but a held key's
    // repeats show the scheduler it is still held.
    switch (msg) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        if (g_ready) sched_keyevent(&g_sched);
        break;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SYSKEYDOWN:
        // Let Alt+F4 through to DefWindowProcW for window close
        if (wParam == VK_F4) break;
        // Prevent F10/Alt+key from activating Win32 menu loop
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            schedminimize(&g_sched, true);
        } else {
            if (g_device) {
                if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
                HRESULT hr = g_swapchain->ResizeBuffers(0,
                    (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                    DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(hr) || !create_rtv()) reset_device();
            }
            g_occluded = false;
            schedminimize(&g_sched, false);
            schedforce(&g_sched);
            gui_frame();  // at once, even inside a modal resize
        }
        GetWindowPlacement(hWnd, &g_last_placement);
        GetWindowRect(hWnd, &g_last_rect);
        return 0;
    case WM_ENTERSIZEMOVE:
        SetTimer(hWnd, TIMER_MODAL, 10, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(hWnd, TIMER_MODAL);
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_MODAL) {
            // Inside the move/resize loop: draw what is due
            take_wake();
            g_sched.now = sched_ticks();
            if (scheddue(&g_sched)) gui_frame();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        g_occluded = false;
        schedminimize(&g_sched, false);
        schedforce(&g_sched);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && g_occluded) {
            g_occluded = false;
            schedminimize(&g_sched, false);
        }
        break;
    case WM_APP_WAKE:
        g_wake.store(false);  // cleared before the frame drains
        scheddirty(&g_sched);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; i++) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring wpath(len, L'\0');
            DragQueryFileW(drop, i, wpath.data(), len + 1);
            g_dbg->dropped_files.push_back(dialog_path(wpath.c_str()));
        }
        DragFinish(drop);
        scheddirty(&g_sched);
        return 0;
    }
    case WM_CLOSE:
        // Capture placement while the window is still valid
        GetWindowPlacement(hWnd, &g_last_placement);
        GetWindowRect(hWnd, &g_last_rect);
        break;
    case WM_SETTINGCHANGE:
        if (g_dbg && lParam &&
            !lstrcmpW((LPCWSTR)lParam, L"ImmersiveColorSet")) {
            g_dbg->sys_dark = system_dark();
            g_dbg->theme_dirty = true;
            schedforce(&g_sched);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// argc/argv are only meaningful on Cygwin, where the POSIX OS half's
// platform_quote_argv walks argv.  Native Windows uses WinMain, has
// no argv, and the Win32 OS half's platform_quote_argv reads
// GetCommandLineW() directly — so WinMain passes (0, nullptr) and
// the values are ignored at the bottom.
static int gui_win32_run(HINSTANCE hInstance, int nCmdShow,
                         int argc, char **argv)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"dcmake";
    RegisterClassExW(&wc);

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

    // Query DPI scale for the primary monitor (need this before window
    // creation to convert saved logical geometry to physical pixels).
    HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    dbg.dpi_scale = ImGui_ImplWin32_GetDpiScaleForMonitor((void *)primary);
    float ds = dbg.dpi_scale;

    // Center on primary monitor on first start.  Saved geometry is in
    // logical pixels; monitor work area is in physical pixels.
    int phys_w = (int)((float)dbg.win_w * ds);
    int phys_h = (int)((float)dbg.win_h * ds);
    int init_x = dbg.win_x, init_y = dbg.win_y;
    if (init_x < 0 || init_y < 0) {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(primary, &mi);
        int mw = mi.rcWork.right - mi.rcWork.left;
        int mh = mi.rcWork.bottom - mi.rcWork.top;
        init_x = mi.rcWork.left + (mw - phys_w) / 2;
        init_y = mi.rcWork.top + (mh - phys_h) / 2;
    } else {
        init_x = (int)((float)init_x * ds);
        init_y = (int)((float)init_y * ds);
    }

    HWND hwnd = CreateWindowExW(
        0, L"dcmake", L"dcmake",
        WS_OVERLAPPEDWINDOW,
        init_x, init_y, phys_w, phys_h,
        nullptr, nullptr, hInstance, nullptr);
    g_hwnd = hwnd;

    // Theme the title bar before the window is first shown
    dbg.sys_dark = system_dark();
    platform_window_theme(hwnd, dbg.theme == THEME_DARK ||
                                (dbg.theme == THEME_SYSTEM && dbg.sys_dark));

    if (!create_device(hwnd)) no_device();

    ShowWindow(hwnd, dbg.win_maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);
    g_dbg = &dbg;
    DragAcceptFiles(hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    dcmake_init(&dbg);
    ImGui::GetStyle().ScaleAllSizes(dbg.dpi_scale);

    // Load ImGui layout
    io.IniFilename = nullptr;
    {
        std::string ini = platform_read_file(dbg.ini_path.c_str());
        if (!ini.empty())
            ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    }

    // The loop: wait, dispatch everything, then decide.
    g_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    if (!g_timer)  // before Windows 10 1803
        g_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    Sched *s = &g_sched;
    *s = newsched(sched_ticks(), refresh_pace(hwnd));
    g_ready = true;  // gui_frame may run from the window procedure now
    while (!dbg.want_quit) {
        take_wake();
        s->now = sched_ticks();
        int64_t timeout = schedtimeout(s);
        int queued = sched_queued();
        if (!gui_wait(timeout)) break;
        sched_takeinput(s, queued);
        take_wake();
        if (dbg.theme_dirty) schedforce(s);
        if (dbg.title_dirty) scheddirty(s);
        s->now = sched_ticks();
        if (scheddue(s)) gui_frame();
    }
    g_ready = false;

    // Read window geometry captured in WM_CLOSE/WM_SIZE.  When fully
    // maximized, save the restored rect so un-maximizing works.  Otherwise
    // save the actual rect (preserves vertical maximize, snap, etc.).
    {
        dbg.win_maximized = (g_last_placement.showCmd == SW_SHOWMAXIMIZED);
        RECT r = dbg.win_maximized ? g_last_placement.rcNormalPosition
                                   : g_last_rect;
        dbg.win_x = (int)((float)r.left / ds);
        dbg.win_y = (int)((float)r.top / ds);
        dbg.win_w = (int)((float)(r.right - r.left) / ds);
        dbg.win_h = (int)((float)(r.bottom - r.top) / ds);
    }

    dcmake_shutdown(&dbg);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    destroy_device();

    DestroyWindow(hwnd);
    UnregisterClassW(L"dcmake", hInstance);
    return 0;
}

#ifdef __CYGWIN__
// Cygwin's CRT calls main() regardless of subsystem.  -mwindows on
// the link line gives us a GUI subsystem executable with no console.
// argc/argv flow through to platform_quote_argv in the POSIX OS
// half, which actually uses them.
int main(int argc, char **argv)
{
    // No console means fds 0/1/2 start closed.  The next pipe() or
    // socket() call in platform_launch then hands out fds 0 and 1,
    // which alias STDIN_FILENO / STDOUT_FILENO.  In the forked child,
    // close(pipe[1]) would then also close stdout, silently dropping
    // everything cmake writes there (message(STATUS ...), etc.) while
    // stderr — already dup'd off a higher slot — still works.  Plug
    // /dev/null into any closed low fd so pipe() always lands >= 3.
    for (int fd = 0; fd < 3; fd++) {
        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) continue;
        int nf = open("/dev/null", fd == 0 ? O_RDONLY : O_WRONLY);
        if (nf < 0) continue;
        if (nf != fd) { dup2(nf, fd); close(nf); }
    }
    return gui_win32_run(GetModuleHandleW(nullptr), SW_SHOWDEFAULT,
                         argc, argv);
}
#else
// Native Windows: /SUBSYSTEM:WINDOWS (set by add_executable(... WIN32))
// calls WinMain.  The raw command line is read via GetCommandLineW in
// platform_quote_argv, so lpCmdLine / argc / argv are unused here.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    return gui_win32_run(hInstance, nCmdShow, 0, nullptr);
}
#endif
