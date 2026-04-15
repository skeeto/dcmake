#include "dcmake.hpp"
#include "platform_win32_util.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

// Win32 OS half of the platform layer.  Paired with
// platform_gui_win32.cpp for native Windows (MSVC / MinGW) builds.
// Contains cmake subprocess launch via CreateProcessW + named pipe,
// job-object process-tree cleanup, and wide-char filesystem helpers.
// No windowing / dialog code lives here.

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
    if (p->read_op.hEvent) CloseHandle(p->read_op.hEvent);
    if (p->write_op.hEvent) CloseHandle(p->write_op.hEvent);

    // Kill cmake before closing the stdout handle.  CloseHandle blocks
    // while another thread is in ReadFile on the same handle, so the
    // job must be terminated first to close the write end of the pipe
    // and unblock ReadFile.
    if (p->job != INVALID_HANDLE_VALUE) {
        TerminateJobObject(p->job, 1);
        if (p->cmake_process != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(p->cmake_process, 3000);
        }
        CloseHandle(p->job);
        p->job = INVALID_HANDLE_VALUE;
    }
    if (p->stdout_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(p->stdout_handle);
        p->stdout_handle = INVALID_HANDLE_VALUE;
    }
    if (p->cmake_process != INVALID_HANDLE_VALUE) {
        CloseHandle(p->cmake_process);
        p->cmake_process = INVALID_HANDLE_VALUE;
    }

    delete p;
    dbg->platform = nullptr;
}

std::string platform_now_iso8601()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    TIME_ZONE_INFORMATION tz;
    DWORD mode = GetTimeZoneInformation(&tz);
    int bias = -(int)tz.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT)
        bias -= (int)tz.DaylightBias;
    char sign = bias >= 0 ? '+' : '-';
    if (bias < 0) bias = -bias;
    char buf[64];
    int n = snprintf(buf, sizeof(buf),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        sign, bias / 60, bias % 60);
    return std::string(buf, (size_t)n);
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

std::string platform_config_dir()
{
    std::wstring dir;
    wchar_t buf[MAX_PATH];
    DWORD n;
    n = GetEnvironmentVariableW(L"XDG_CONFIG_HOME", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        dir = buf;
    } else {
        n = GetEnvironmentVariableW(L"HOME", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            dir = buf;
            dir += L"/.config";
        } else {
            wchar_t appdata[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA,
                                           NULL, 0, appdata)))
                dir = appdata;
        }
    }
    if (dir.empty()) return {};
    dir += L"/dcmake";
    // Create directory and all parents (like mkdir -p).
    {
        std::wstring seg;
        for (const wchar_t *p = dir.c_str(); *p; p++) {
            if ((*p == L'/' || *p == L'\\') && !seg.empty())
                CreateDirectoryW(seg.c_str(), nullptr);
            seg += *p;
        }
        CreateDirectoryW(seg.c_str(), nullptr);
    }
    return to_utf8(dir.c_str());
}

std::string platform_realpath(const std::string &path)
{
    std::wstring wpath = to_wide(path.c_str());
    HANDLE h = CreateFileW(wpath.c_str(), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return path;
    wchar_t buf[MAX_PATH];
    DWORD len = GetFinalPathNameByHandleW(h, buf, MAX_PATH,
                                          FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (len == 0 || len >= MAX_PATH) return path;
    std::wstring result(buf, len);
    if (result.starts_with(L"\\\\?\\"))
        result = result.substr(4);
    std::string out = to_utf8(result.c_str());
    for (char &c : out)
        if (c == '\\') c = '/';
    return out;
}
