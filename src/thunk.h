// thunk.h - runtime-built call thunks.
//
// Why not just point the jmp straight at the C++ function?
//   CRadar::TransformRadarPointToScreenSpace is hand-written assembly that
//   leaves ECX/EDX untouched, and CRadar::DrawRadarSection keeps a live value
//   in EDX across the call. A plain C++ replacement would clobber both, which
//   shows up as corrupted tile geometry. The thunk saves/restores ECX+EDX
//   around the C++ core, so the replacement is call-compatible with anything
//   the game's own code expects.
//
//   Building the few instruction bytes at runtime also avoids __declspec(naked)
//   / inline asm incompatibilities between MSVC versions and avoids a
//   per-compiler re-verification of the assembly.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <cstddef>

// Builds   push ecx ; push edx ; mov ecx,esp ; push args(rtl) ;
//          mov eax,core ; call eax ; add esp,n*4 ; pop edx ; pop ecx ; ret
// as executable memory and returns it. Never freed while the hook is live.
//
//   core      - __cdecl target
//   argCount  - 0..4 arguments, pushed right to left
void* BuildCdeclThunk(void* core, int argCount);
