// radar.h - the actual minimap manipulation
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Reads the ini, checks the exe, installs the hooks and applies the memory
// patches. Returns false (and patches nothing) on an unsupported executable.
bool RadarInit(const wchar_t* iniPath);

// Undoes every patch / hook. Called from DllMain / DLL_PROCESS_DETACH.
void RadarShutdown();
