// hook.h - a tiny 32-bit inline hook used to replace whole GTA functions.
//
// Design note: every detour in this plugin is a COMPLETE replacement of the
// original function, so no trampoline / original-byte replay is needed. We
// only overwrite the first 5 bytes (E9 rel32) and keep the originals so the
// patch can be undone at any time.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>

// Writes `size` bytes at `dst` with PAGE_EXECUTE_READWRITE temporarily applied.
bool WriteBytes(void* dst, const void* src, size_t size);

class InlineHook {
public:
    // `absoluteVa` is the absolute game address (e.g. 0x583480).
    // Returns false and leaves memory untouched on any failure.
    bool Install(uintptr_t absoluteVa, const void* detour, size_t stolenBytes = 5);

    // Restores the original bytes.
    void Remove();

    bool Installed() const { return m_installed; }

private:
    void*         m_target    = nullptr;
    unsigned char m_original[16] = {};
    size_t        m_length    = 0;
    void*         m_relay     = nullptr;   // only if a direct jmp did not fit
    bool          m_installed = false;
};
