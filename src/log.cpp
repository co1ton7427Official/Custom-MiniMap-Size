#define _CRT_SECURE_NO_WARNINGS
#include "log.h"

#include "strutil.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace {

HANDLE           g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock{};
bool             g_lockReady = false;

} // namespace

void LogOpen(const wchar_t* filePath)
{
    if (!g_lockReady) {
        ::InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }

    g_file = ::CreateFileW(
        filePath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,               // fresh log every launch
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (g_file == INVALID_HANDLE_VALUE)
        return;

    // UTF-8 BOM so Notepad/VS Code pick the right encoding.
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    ::WriteFile(g_file, bom, sizeof(bom), &written, nullptr);

    Log("=== minimap.asi log ===");
}

void LogClose()
{
    if (g_file != INVALID_HANDLE_VALUE) {
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

void Log(const char* fmt, ...)
{
    if (g_file == INVALID_HANDLE_VALUE)
        return;

    char body[2048] = {};
    va_list args;
    va_start(args, fmt);
    VFormat(body, sizeof(body), fmt, args);
    va_end(args);

    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    char line[2304] = {};
    const int n = Format(
        line, sizeof(line),
        "[%02u:%02u:%02u.%03u] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        body);

    if (n <= 0)
        return;

    if (g_lockReady)
        ::EnterCriticalSection(&g_lock);

    DWORD written = 0;
    ::WriteFile(g_file, line, static_cast<DWORD>(n), &written, nullptr);

    if (g_lockReady)
        ::LeaveCriticalSection(&g_lock);
}
