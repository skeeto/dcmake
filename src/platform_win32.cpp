#ifdef _WIN32
#include "dcmake.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <cstdio>
#include <string>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Platform pipe implementation ---

struct Win32Platform {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    HANDLE job = INVALID_HANDLE_VALUE;
    HANDLE cmake_process = INVALID_HANDLE_VALUE;
    OVERLAPPED read_op = {};
    OVERLAPPED write_op = {};
};

// Overlapped I/O: concurrent read and write on a single pipe handle.
// Without FILE_FLAG_OVERLAPPED, synchronous ReadFile in the reader thread
// locks the handle and WriteFile from the main thread deadlocks.

static int win32_pipe_read(void *ctx, char *buf, int len)
{
    auto *p = (Win32Platform *)ctx;
    p->read_op.Offset = p->read_op.OffsetHigh = 0;
    ResetEvent(p->read_op.hEvent);
    BOOL ok = ReadFile(p->pipe, buf, len, nullptr, &p->read_op);
    DWORD err = GetLastError();
    if (ok || err == ERROR_IO_PENDING) {
        DWORD n = 0;
        if (GetOverlappedResult(p->pipe, &p->read_op, &n, TRUE))
            return (int)n;
    }
    return 0;
}

static bool win32_pipe_write(void *ctx, const char *buf, int len)
{
    auto *p = (Win32Platform *)ctx;
    p->write_op.Offset = p->write_op.OffsetHigh = 0;
    ResetEvent(p->write_op.hEvent);
    BOOL ok = WriteFile(p->pipe, buf, len, nullptr, &p->write_op);
    DWORD err = GetLastError();
    if (ok || err == ERROR_IO_PENDING) {
        DWORD n = 0;
        if (GetOverlappedResult(p->pipe, &p->write_op, &n, TRUE))
            return (int)n == len;
    }
    return false;
}

static void win32_pipe_shutdown(void *ctx)
{
    auto *p = (Win32Platform *)ctx;
    if (p->pipe != INVALID_HANDLE_VALUE) {
        CancelIo(p->pipe);
        CloseHandle(p->pipe);
        p->pipe = INVALID_HANDLE_VALUE;
    }
}

static int win32_stdout_read(void *ctx, char *buf, int len)
{
    auto *p = (Win32Platform *)ctx;
    DWORD n = 0;
    if (ReadFile(p->stdout_handle, buf, (DWORD)len, &n, nullptr))
        return (int)n;
    return 0;
}

static void win32_stdout_shutdown(void *ctx)
{
    // Don't CloseHandle here -- it blocks while another thread is in
    // ReadFile on the same handle.  Instead, kill the cmake process tree
    // so the write end of the pipe closes, which unblocks ReadFile.
    // platform_cleanup closes the handle after the thread is joined.
    auto *p = (Win32Platform *)ctx;
    if (p->job != INVALID_HANDLE_VALUE) {
        TerminateJobObject(p->job, 1);
    }
}

// Convert wide string to UTF-8
static std::string to_utf8(const wchar_t *wide)
{
    if (!wide || !*wide) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                        out.data(), len, nullptr, nullptr);
    return out;
}

// Convert UTF-8 string to wide
static std::wstring to_wide(const char *utf8)
{
    if (!utf8 || !*utf8) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), len);
    return out;
}

// Skip past argv[0] in the raw command line to get the arguments portion.
// If the first character is a double quote, skip to the closing double quote.
// Otherwise skip to the first space or tab. Then trim leading whitespace.
std::string platform_quote_argv(int, char **)
{
    const wchar_t *cmd = GetCommandLineW();
    if (*cmd == L'"') {
        cmd++;
        while (*cmd && *cmd != L'"') cmd++;
        if (*cmd) cmd++;
    } else {
        while (*cmd && *cmd != L' ' && *cmd != L'\t') cmd++;
    }
    while (*cmd == L' ' || *cmd == L'\t') cmd++;
    return to_utf8(cmd);
}

bool platform_launch(Debugger *dbg, const char *args)
{
    auto *p = new Win32Platform;
    p->read_op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->write_op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dbg->platform = p;
    dbg->pipe_read = win32_pipe_read;
    dbg->pipe_write = win32_pipe_write;
    dbg->pipe_shutdown = win32_pipe_shutdown;
    dbg->stdout_read = win32_stdout_read;
    dbg->stdout_shutdown = win32_stdout_shutdown;

    // Build named pipe path
    static int launch_count = 0;
    std::string pipe_name = "\\\\.\\pipe\\dcmake-"
                          + std::to_string(GetCurrentProcessId())
                          + "-" + std::to_string(launch_count++);

    // Build cmake command line via cmd /c for shell variable expansion
    std::wstring cmdline = L"cmd /c cmake --debugger --debugger-pipe=";
    cmdline += to_wide(pipe_name.c_str());
    cmdline += L' ';
    cmdline += to_wide(args);

    // Create job object so the entire process tree dies together.
    // KILL_ON_JOB_CLOSE ensures cleanup even if dcmake crashes.
    p->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(p->job, JobObjectExtendedLimitInformation,
                            &jeli, sizeof(jeli));

    // Create pipe for capturing cmake stdout/stderr
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE stdout_read_h = INVALID_HANDLE_VALUE;
    HANDLE stdout_write_h = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdout_read_h, &stdout_write_h, &sa, 0)) {
        dbg->status = "Failed to create stdout pipe";
        return false;
    }
    SetHandleInformation(stdout_read_h, HANDLE_FLAG_INHERIT, 0);

    // Launch cmake subprocess suspended, assign to job, then resume
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write_h;
    si.hStdError = stdout_write_h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                        TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, nullptr,
                        &si, &pi)) {
        CloseHandle(stdout_read_h);
        CloseHandle(stdout_write_h);
        dbg->status = "Failed to start cmake";
        return false;
    }
    CloseHandle(stdout_write_h);
    p->stdout_handle = stdout_read_h;
    AssignProcessToJobObject(p->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p->cmake_process = pi.hProcess;

    // Connect to cmake's named pipe (retry loop)
    // CMake creates the pipe with CreateNamedPipe then calls ConnectNamedPipe.
    // We open it with CreateFile once it exists.
    bool connected = false;
    for (int i = 0; i < 500; i++) {
        p->pipe = CreateFileA(pipe_name.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (p->pipe != INVALID_HANDLE_VALUE) {
            connected = true;
            break;
        }
        // Check if cmake exited prematurely
        if (WaitForSingleObject(p->cmake_process, 0) == WAIT_OBJECT_0) {
            break;
        }
        Sleep(10);
    }

    if (!connected) {
        dbg->status = "Failed to connect to cmake debugger";
        return false;
    }

    return true;
}

void platform_cleanup(Debugger *dbg)
{
    auto *p = (Win32Platform *)dbg->platform;
    if (!p) return;

    if (p->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(p->pipe);
        p->pipe = INVALID_HANDLE_VALUE;
    }
    if (p->stdout_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(p->stdout_handle);
        p->stdout_handle = INVALID_HANDLE_VALUE;
    }
    if (p->read_op.hEvent) CloseHandle(p->read_op.hEvent);
    if (p->write_op.hEvent) CloseHandle(p->write_op.hEvent);

    if (p->job != INVALID_HANDLE_VALUE) {
        TerminateJobObject(p->job, 1);
        if (p->cmake_process != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(p->cmake_process, 3000);
        }
        CloseHandle(p->job);
        p->job = INVALID_HANDLE_VALUE;
    }
    if (p->cmake_process != INVALID_HANDLE_VALUE) {
        CloseHandle(p->cmake_process);
        p->cmake_process = INVALID_HANDLE_VALUE;
    }

    delete p;
    dbg->platform = nullptr;
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
        return to_utf8(path);
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
        return to_utf8(path);
    }
    return {};
}

bool platform_chdir(const char *path)
{
    return SetCurrentDirectoryW(to_wide(path).c_str());
}

std::string platform_read_file(const char *path)
{
    HANDLE h = CreateFileW(to_wide(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0) {
        CloseHandle(h);
        return {};
    }
    std::string out((size_t)size.QuadPart, '\0');
    DWORD got = 0;
    ReadFile(h, out.data(), (DWORD)size.QuadPart, &got, nullptr);
    CloseHandle(h);
    out.resize(got);
    return out;
}

bool platform_write_file(const char *path, const char *data, size_t len)
{
    HANDLE h = CreateFileW(to_wide(path).c_str(), GENERIC_WRITE,
                           0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    WriteFile(h, data, (DWORD)len, &wrote, nullptr);
    CloseHandle(h);
    return wrote == (DWORD)len;
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
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
    std::string initial_args = platform_quote_argv(0, nullptr);
    if (initial_args.empty()) initial_args = "-B build";
    snprintf(dbg.cmdline, sizeof(dbg.cmdline), "%s", initial_args.c_str());
    {
        wchar_t appdata[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
            std::wstring dir = appdata;
            dir += L"\\dcmake";
            CreateDirectoryW(dir.c_str(), nullptr);
            dir += L"\\imgui.ini";
            dbg.ini_path = to_utf8(dir.c_str());
        }
    }
    dcmake_load_config(&dbg);

    // Center on primary monitor on first start
    int init_x = dbg.win_x, init_y = dbg.win_y;
    if (init_x < 0 || init_y < 0) {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        int mw = mi.rcWork.right - mi.rcWork.left;
        int mh = mi.rcWork.bottom - mi.rcWork.top;
        init_x = mi.rcWork.left + (mw - dbg.win_w) / 2;
        init_y = mi.rcWork.top + (mh - dbg.win_h) / 2;
    }

    HWND hwnd = CreateWindowExW(
        0, L"dcmake", L"dcmake",
        WS_OVERLAPPEDWINDOW,
        init_x, init_y, dbg.win_w, dbg.win_h,
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    dcmake_init(&dbg);

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
        dbg.win_x = r.left;
        dbg.win_y = r.top;
        dbg.win_w = r.right - r.left;
        dbg.win_h = r.bottom - r.top;
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

#endif // _WIN32
