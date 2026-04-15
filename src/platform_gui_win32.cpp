#include "dcmake.hpp"
#include "platform_win32_util.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

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

// --- Win32 + DX11 entry point ---

static ID3D11Device *g_device = nullptr;
static ID3D11DeviceContext *g_context = nullptr;
static IDXGISwapChain *g_swapchain = nullptr;
static ID3D11RenderTargetView *g_rtv = nullptr;

static void create_rtv()
{
    ID3D11Texture2D *back_buffer = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
    back_buffer->Release();
}

static Debugger *g_dbg = nullptr;
static WINDOWPLACEMENT g_last_placement = { sizeof(g_last_placement) };
static RECT g_last_rect;

static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SYSKEYDOWN:
        // Let Alt+F4 through to DefWindowProcW for window close
        if (wParam == VK_F4) break;
        // Prevent F10/Alt+key from activating Win32 menu loop
        return 0;
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swapchain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            create_rtv();
        }
        GetWindowPlacement(hWnd, &g_last_placement);
        GetWindowRect(hWnd, &g_last_rect);
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
        return 0;
    }
    case WM_CLOSE:
        // Capture placement while the window is still valid
        GetWindowPlacement(hWnd, &g_last_placement);
        GetWindowRect(hWnd, &g_last_rect);
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

    // Create D3D11 device and swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_swapchain, &g_device, nullptr, &g_context);
    create_rtv();

    ShowWindow(hwnd, dbg.win_maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);
    g_dbg = &dbg;
    DragAcceptFiles(hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

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

    bool done = false;
    while (!done && !dbg.want_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (dbg.title_dirty) {
            dbg.title_dirty = false;
            wchar_t cwd[MAX_PATH];
            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                std::wstring title = L"dcmake - ";
                title += cwd;
                SetWindowTextW(hwnd, title.c_str());
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        dcmake_frame(&dbg);

        ImGui::Render();
        float clear_color[] = {0.1f, 0.1f, 0.1f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0);
    }

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

    if (g_rtv) g_rtv->Release();
    if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();

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
