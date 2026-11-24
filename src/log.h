// log.h - minimal file logger
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Opens "minimap.log" next to the plugin. Silently no-ops if it fails:
// logging must never be able to take the game down.
void LogOpen(const wchar_t* filePath);
void LogClose();

// printf-style, one timestamped line per call, flushed immediately so the log
// survives a crash.
void Log(const char* fmt, ...);
