#ifdef _WIN32
#include "dcmake.h"

#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Platform pipe implementation ---

struct Win32Platform {
    HANDLE pipe = INVALID_HANDLE_VALUE;
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

bool platform_launch(Debugger *dbg, int argc, char **argv)
{
    auto *p = new Win32Platform;
    p->read_op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->write_op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dbg->platform = p;
    dbg->pipe_read = win32_pipe_read;
    dbg->pipe_write = win32_pipe_write;
    dbg->pipe_shutdown = win32_pipe_shutdown;

    // Build named pipe path
    std::string pipe_name = "\\\\.\\pipe\\dcmake-"
                          + std::to_string(GetCurrentProcessId());

    // Build cmake command line
    std::string cmdline = "cmake --debugger --debugger-pipe=" + pipe_name;
    for (int i = 1; i < argc; i++) {
        cmdline += ' ';
        // Quote arguments that contain spaces
        std::string arg = argv[i];
        if (arg.find(' ') != std::string::npos) {
            cmdline += '"';
            cmdline += arg;
            cmdline += '"';
        } else {
            cmdline += arg;
        }
    }

    // Launch cmake subprocess
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, nullptr, &si, &pi)) {
        dbg->status = "Failed to start cmake";
        return false;
    }
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
    if (p->read_op.hEvent) CloseHandle(p->read_op.hEvent);
    if (p->write_op.hEvent) CloseHandle(p->write_op.hEvent);

    if (p->cmake_process != INVALID_HANDLE_VALUE) {
        TerminateProcess(p->cmake_process, 1);
        WaitForSingleObject(p->cmake_process, 3000);
        CloseHandle(p->cmake_process);
        p->cmake_process = INVALID_HANDLE_VALUE;
    }

    delete p;
    dbg->platform = nullptr;
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

static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swapchain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            create_rtv();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Convert wide argv to UTF-8
    int wargc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> arg_strings;
    for (int i = 0; i < wargc; i++) {
        arg_strings.push_back(to_utf8(wargv[i]));
    }
    LocalFree(wargv);
    // Collect pointers after all strings are in place (no more reallocation)
    std::vector<char *> argv_ptrs;
    for (auto &s : arg_strings) {
        argv_ptrs.push_back(s.data());
    }
    argv_ptrs.push_back(nullptr);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"dcmake";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"dcmake", L"dcmake",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
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

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    Debugger dbg = {};
    dcmake_init(&dbg, wargc, argv_ptrs.data());

    bool done = false;
    while (!done && !dbg.want_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

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
