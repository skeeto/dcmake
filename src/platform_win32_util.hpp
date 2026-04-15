#pragma once

// UTF-8 <-> wide-string conversion helpers shared between the Win32
// GUI half (platform_gui_win32.cpp) and the Win32 OS half
// (platform_os_win32.cpp).  Inline so each translation unit gets its
// own copy and nothing has to be explicitly linked.

#include <windows.h>

#include <string>

static inline std::string to_utf8(const wchar_t *wide)
{
    if (!wide || !*wide) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                        out.data(), len, nullptr, nullptr);
    return out;
}

static inline std::wstring to_wide(const char *utf8)
{
    if (!utf8 || !*utf8) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring out((size_t)(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), len);
    return out;
}
