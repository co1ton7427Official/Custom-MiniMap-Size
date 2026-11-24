// main.cpp - DLL entry point. An ASI loader just calls LoadLibrary on this file.
//
// minimap.asi
//   Replaces CRadar::TransformRadarPointToScreenSpace (0x583480) with our own
//   mapping so the radar can be moved / resized / reshaped from
//   minimap_config.ini, and optionally replaces CRadar::LimitRadarPoint
//   (0x5832F0) so blips are clamped to a square instead of a circle.
#define _CRT_SECURE_NO_WARNINGS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "log.h"
#include "radar.h"
#include "strutil.h"

#include <cstdio>
#include <wchar.h>

namespace {

HMODULE g_module = nullptr;

// Builds "<plugin folder>\minimap_config.ini" and "<plugin folder>\minimap.log".
void BuildSiblingPaths(wchar_t* iniOut, wchar_t* logOut, size_t count)
{
    wchar_t dir[MAX_PATH] = {};
    ::GetModuleFileNameW(g_module, dir, MAX_PATH);

    // Strip the file name.
    for (int i = (int)wcslen(dir) - 1; i >= 0; --i) {
        if (dir[i] == L'\\' || dir[i] == L'/') {
            dir[i] = L'\0';
            break;
        }
    }
    WJoinPath(iniOut, count, dir, L"minimap_config.ini");
    WJoinPath(logOut, count, dir, L"minimap.log");
}

DWORD WINAPI InitThread(LPVOID)
{
    // 300 ms after injection so the game's image and window are fully up.
    ::Sleep(300);

    wchar_t iniPath[MAX_PATH] = {};
    wchar_t logPath[MAX_PATH] = {};
    BuildSiblingPaths(iniPath, logPath, MAX_PATH);

    LogOpen(logPath);
    Log("minimap.asi starting, module=%p", g_module);
    Log("plugin folder ini: %ls", iniPath);

    if (!RadarInit(iniPath))
        Log("RadarInit failed - the game is left untouched");

    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_module = hModule;
        ::DisableThreadLibraryCalls(hModule);

        if (HANDLE thread = ::CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr)) {
            ::CloseHandle(thread);
        } else {
            // No thread: fall back to doing it right now.
            InitThread(nullptr);
        }
        return TRUE;

    case DLL_PROCESS_DETACH:
        // tidAttach: game is still alive -> undo everything cleanly.
        // tidDetach inside DllMain: do not touch threads, just restore bytes.
        RadarShutdown();
        LogClose();
        return TRUE;
    }

    return TRUE;
}
