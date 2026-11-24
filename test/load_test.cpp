// load_test.cpp - smoke test for minimap.asi.
//
// Loads the real .asi into a plain 32-bit console process and checks that:
//   * DllMain and its init thread run without crashing,
//   * the sibling ini/log paths resolve next to the .asi,
//   * minimap_config.ini is really parsed (the log echoes the ini values),
//   * the version guard refuses to patch a non-GTA executable,
//   * FreeLibrary (DLL_PROCESS_DETACH) shuts down and restores cleanly.
//
// It does NOT test the in-game radar: that needs gta_sa.exe itself.
//
// usage: load_test <path-to-minimap.asi> [ini-file-to-install-as-minimap_config.ini]
#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool cond, const char* what)
{
    if (cond) {
        std::printf("  ok   %s\n", what);
    } else {
        std::printf("  FAIL %s (GetLastError=%lu)\n", what, (unsigned long)::GetLastError());
        ++g_failures;
    }
}

// The .asi keeps the log open for writing, so this must allow writers.
bool ReadWholeFile(const wchar_t* path, char* out, size_t cap)
{
    HANDLE f = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;

    DWORD got = 0;
    const BOOL ok = ::ReadFile(f, out, (DWORD)(cap - 1), &got, nullptr);
    ::CloseHandle(f);
    if (!ok)
        return false;

    out[got] = '\0';
    return true;
}

void StripFileName(wchar_t* path)
{
    for (int i = (int)lstrlenW(path) - 1; i >= 0; --i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            path[i] = L'\0';
            return;
        }
    }
}

bool CopyFileW2(const wchar_t* from, const wchar_t* to)
{
    return ::CopyFileW(from, to, FALSE) != FALSE;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: load_test <minimap.asi> [ini-to-install]\n");
        return 2;
    }

    wchar_t dll[MAX_PATH] = {};
    ::MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dll, MAX_PATH);

    wchar_t fullDll[MAX_PATH] = {};
    if (!::GetFullPathNameW(dll, MAX_PATH, fullDll, nullptr)) {
        std::printf("FAIL: GetFullPathNameW\n");
        return 1;
    }

    wchar_t dir[MAX_PATH] = {};
    ::lstrcpynW(dir, fullDll, MAX_PATH);
    StripFileName(dir);

    wchar_t iniPath[MAX_PATH] = {};
    ::lstrcpynW(iniPath, dir, MAX_PATH);
    ::lstrcatW(iniPath, L"\\minimap_config.ini");

    wchar_t logPath[MAX_PATH] = {};
    ::lstrcpynW(logPath, dir, MAX_PATH);
    ::lstrcatW(logPath, L"\\minimap.log");

    // A stale log would hide a crash before the first write.
    ::DeleteFileW(logPath);

    for (int pass = 0; pass < (argc > 2 ? 2 : 1); ++pass) {
        if (argc > 2 && pass == 1) {
            wchar_t srcIni[MAX_PATH] = {};
            ::MultiByteToWideChar(CP_ACP, 0, argv[2], -1, srcIni, MAX_PATH);
            std::printf("\n== pass 2: installing %s ==\n", argv[2]);
            Check(CopyFileW2(srcIni, iniPath), "install minimap_config.ini next to the .asi");
        } else {
            std::printf("\n== pass 1: no ini, built-in defaults ==\n");
            ::DeleteFileW(iniPath);
        }

        HMODULE mod = ::LoadLibraryW(fullDll);
        Check(mod != nullptr, "LoadLibraryW(minimap.asi)");
        if (!mod)
            continue;

        // DllMain spawns the init thread with a 300 ms delay.
        ::Sleep(1500);

        char log[16384] = {};
        const bool haveLog = ReadWholeFile(logPath, log, sizeof(log));

        std::printf("--- minimap.log ---\n%s--- end ---\n", haveLog ? log : "(missing)\n");
        Check(haveLog, "minimap.log written next to the .asi");
        Check(std::strstr(log, "minimap.asi starting") != nullptr, "init thread ran");

        if (pass == 0) {
            Check(std::strstr(log, "not found - built-in defaults in use") != nullptr,
                  "missing ini falls back to defaults");
            Check(std::strstr(log, "Width=94.0 Height=76.0") != nullptr,
                  "default geometry is the vanilla 94x76");
        } else {
            // Values that exist only in the installed ini.
            Check(std::strstr(log, "Width=222.0") != nullptr, "Width read from the ini");
            Check(std::strstr(log, "Height=222.0") != nullptr, "Height read from the ini (ForceSquare)");
            Check(std::strstr(log, "X=11.0 Y=22.0") != nullptr, "X/Y read from the ini");
            Check(std::strstr(log, "Anchor=TopRight") != nullptr, "Anchor read from the ini");
            Check(std::strstr(log, "Units=Pixels") != nullptr, "Units read from the ini");
            Check(std::strstr(log, "ForceSquare=1") != nullptr, "ForceSquare read from the ini");
            Check(std::strstr(log, "IgnoreVersionCheck=1") != nullptr, "IgnoreVersionCheck read from the ini");
            Check(std::strstr(log, "version: check disabled by config") != nullptr,
                  "IgnoreVersionCheck=1 skips the size check");
            Check(std::strstr(log, "not inside the game image") != nullptr,
                  "patching a host without gta_sa.exe addresses is refused");
        }

        ::FreeLibrary(mod);
        ::Sleep(200);
        Check(::GetModuleHandleW(fullDll) == nullptr, "unloaded");
    }

    ::DeleteFileW(iniPath);

    if (g_failures != 0) {
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }

    std::printf("\nALL OK\n");
    return 0;
}
