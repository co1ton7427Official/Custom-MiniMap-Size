#include "hook.h"

#include "game.h"
#include "log.h"

#include <cstring>

namespace {

// Can we encode "jmp from -> to" as a 5-byte relative jump?
bool FitsRel32(const void* from, const void* to)
{
    const int64_t delta = (int64_t)(uintptr_t)to - ((int64_t)(uintptr_t)from + 5);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

// Allocates an executable page within +/-2GB of `nearAddr`.
// Used only in the unlikely case that the DLL was mapped too far away.
void* AllocRelayNear(const void* nearAddr)
{
    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    const uintptr_t granularity = si.dwAllocationGranularity;
    const uintptr_t target      = (uintptr_t)nearAddr;

    for (uintptr_t delta = granularity; delta < 0x5F000000u; delta += granularity) {
        for (int dir = 0; dir < 2; ++dir) {
            const uintptr_t candidate = dir == 0 ? target - delta : target + delta;
            if (candidate < 0x10000u)
                continue;

            void* page = ::VirtualAlloc((void*)candidate, granularity,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!page)
                continue;

            if (FitsRel32(nearAddr, page))
                return page;

            ::VirtualFree(page, 0, MEM_RELEASE);
        }
    }
    return nullptr;
}

} // namespace

bool WriteBytes(void* dst, const void* src, size_t size)
{
    if (!dst || !src || size == 0)
        return false;

    DWORD oldProtect = 0;
    if (!::VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    std::memcpy(dst, src, size);

    DWORD ignored = 0;
    ::VirtualProtect(dst, size, oldProtect, &ignored);
    ::FlushInstructionCache(::GetCurrentProcess(), dst, size);
    return true;
}

bool InlineHook::Install(uintptr_t absoluteVa, const void* detour, size_t stolenBytes)
{
    if (m_installed || !detour || stolenBytes < 5 || stolenBytes > sizeof(m_original))
        return false;

    void* const target = sa::At(absoluteVa);
    if (!sa::IsInGameImage(target)) {
        Log("hook: 0x%08X is not inside the game image (unsupported exe?)", (unsigned)absoluteVa);
        return false;
    }

    unsigned char* const code = (unsigned char*)target;
    std::memcpy(m_original, code, stolenBytes);

    // Already an E9 jmp => someone else (or us) patched it first.
    if (code[0] == 0xE9) {
        Log("hook: 0x%08X already starts with E9 (conflicting patch?)", (unsigned)absoluteVa);
    }

    const void* jumpTo = detour;
    if (!FitsRel32(target, detour)) {
        Log("hook: target 0x%08X too far from detour (%p), allocating relay",
            (unsigned)absoluteVa, detour);
        m_relay = AllocRelayNear(target);
        if (!m_relay) {
            Log("hook: relay allocation failed");
            return false;
        }
        // FF 25 <ptr>  ->  jmp dword ptr [ptr];  ptr lives at relay+8.
        unsigned char* r = (unsigned char*)m_relay;
        const uintptr_t slot = (uintptr_t)m_relay + 8;
        r[0] = 0xFF; r[1] = 0x25;
        std::memcpy(r + 2, &slot, 4);
        r[6] = 0x90; r[7] = 0x90;
        const uintptr_t impl = (uintptr_t)detour;
        std::memcpy(r + 8, &impl, 4);
        ::FlushInstructionCache(::GetCurrentProcess(), m_relay, 16);
        jumpTo = m_relay;
    }

    unsigned char patch[16];
    std::memcpy(patch, m_original, stolenBytes);
    patch[0] = 0xE9;
    const int32_t rel = (int32_t)((int64_t)(uintptr_t)jumpTo - ((int64_t)(uintptr_t)target + 5));
    std::memcpy(patch + 1, &rel, 4);

    if (!WriteBytes(target, patch, stolenBytes)) {
        Log("hook: writing the jmp at 0x%08X failed", (unsigned)absoluteVa);
        return false;
    }

    m_target    = target;
    m_length    = stolenBytes;
    m_installed = true;

    Log("hook: installed at 0x%08X -> %p (%u bytes)", (unsigned)absoluteVa, jumpTo, (unsigned)stolenBytes);
    return true;
}

void InlineHook::Remove()
{
    if (!m_installed)
        return;

    WriteBytes(m_target, m_original, m_length);
    m_installed = false;

    if (m_relay) {
        ::VirtualFree(m_relay, 0, MEM_RELEASE);
        m_relay = nullptr;
    }

    Log("hook: removed at 0x%08X", (unsigned)(uintptr_t)m_target);
}
