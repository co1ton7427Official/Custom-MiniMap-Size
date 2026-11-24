#include "thunk.h"

#include "log.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

void* BuildCdeclThunk(void* core, int argCount)
{
    if (!core || argCount < 0 || argCount > 4)
        return nullptr;

    constexpr size_t kMaxSize = 40;

    void* const block = ::VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!block) {
        Log("thunk: VirtualAlloc failed");
        return nullptr;
    }

    // Entry stack (cdecl): [esp] = return address, [esp+4] = arg0, [esp+8] = arg1, ...
    unsigned char buf[kMaxSize] = {};
    unsigned char* p = buf;

    *p++ = 0x51;              // push ecx
    *p++ = 0x52;              // push edx
    *p++ = 0x89; *p++ = 0xE1; // mov  ecx, esp          (frame base; ecx is safe, it is saved)

    // push dword ptr [ecx + 8 + 4 + 4*i]   ->   FF 71 disp8
    // after the two pushes arg0 sits at [ecx + 4(ret) + 8(two pushes)] = [ecx+12].
    for (int i = argCount - 1; i >= 0; --i) {
        *p++ = 0xFF;
        *p++ = 0x71;
        *p++ = (unsigned char)(12 + 4 * i);
    }

    // mov eax, <core>
    *p++ = 0xB8;
    const uint32_t coreAddr = (uint32_t)(uintptr_t)core;
    std::memcpy(p, &coreAddr, 4);
    p += 4;

    *p++ = 0xFF; *p++ = 0xD0;                   // call eax
    *p++ = 0x83; *p++ = 0xC4;                   // add  esp, argCount*4
    *p++ = (unsigned char)(argCount * 4);
    *p++ = 0x5A;                                // pop  edx
    *p++ = 0x59;                                // pop  ecx
    *p++ = 0xC3;                                // ret  (cdecl: caller cleans the args)

    const size_t size = (size_t)(p - buf);
    std::memcpy(block, buf, size);
    ::FlushInstructionCache(::GetCurrentProcess(), block, size);

    Log("thunk: %u-byte stub at %p -> %p (%d args)", (unsigned)size, block, core, argCount);
    return block;
}
