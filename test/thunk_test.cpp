// thunk_test.cpp - host-side test for the runtime-built cdecl thunks.
//
// The hand-written machine code in src/thunk.cpp is the riskiest part of the
// plugin, so it gets exercised here in a normal 32-bit process:
//   * arguments really arrive (both slots, several values)
//   * the return value really comes back
//   * ECX and EDX are preserved (GTA's DrawRadarSection keeps a live pointer
//     in EDX across the call, see gta-reversed Radar.cpp)
//   * ESP balance is unchanged, so cdecl callers are not corrupted
//   * the thunk is transparent enough to call thousands of times in a loop
#include "../src/thunk.h"

#include <Windows.h>
#include <cstdio>
#include <cmath>

static int g_failures = 0;

#define CHECK(cond, what)                                     \
    do {                                                      \
        if (cond) {                                           \
            std::printf("  [ ok ] %s\n", what);               \
        } else {                                              \
            std::printf("  [FAIL] %s  (line %d)\n", what, __LINE__); \
            ++g_failures;                                     \
        }                                                     \
    } while (0)

extern "C" {
struct Vec2 { float x, y; };

// Stand-ins with the same C++ signatures the real cores use.
Vec2 g_lastIn{};
Vec2 g_lastOut{};

void __cdecl MockTransform(Vec2* out, const Vec2* in)
{
    g_lastIn = *in;
    g_lastOut = *out;
    out->x = in->x * 2.0f + 1.0f;
    out->y = in->y * 3.0f - 1.0f;
}

float __cdecl MockLimit(Vec2* p)
{
    return p->x + p->y;
}

// Three args, to prove the arg-count loop indexes the stack correctly.
float __cdecl MockSum3(float a, float b, float c) { return a + b + c; }

} // extern "C"

typedef void(__cdecl* TransformFn)(Vec2*, const Vec2*);
typedef float(__cdecl* LimitFn)(Vec2*);
typedef float(__cdecl* Sum3Fn)(float, float, float);

// Calls `fn(a, b)` while checking that ECX/EDX survive the call.
static void CallCheckingRegs(TransformFn fn, Vec2* a, const Vec2* b,
                             unsigned* ecxAfter, unsigned* edxAfter)
{
    const unsigned ecxIn = 0x11223344u;
    const unsigned edxIn = 0x55667788u;
    __asm__ __volatile__(
        "movl %4, %%ecx\n\t"
        "movl %5, %%edx\n\t"
        "pushl %3\n\t"
        "pushl %2\n\t"
        "call  *%6\n\t"
        "addl  $8, %%esp\n\t"
        "movl  %%ecx, %0\n\t"
        "movl  %%edx, %1\n\t"
        : "=m"(*ecxAfter), "=m"(*edxAfter)
        : "m"(a), "m"(b), "m"(ecxIn), "m"(edxIn), "m"(fn)
        : "eax", "ecx", "edx", "memory");
}

int main()
{
    std::printf("thunk self-test\n");

    void* thunkT = BuildCdeclThunk((void*)&MockTransform, 2);
    void* thunkL = BuildCdeclThunk((void*)&MockLimit, 1);
    void* thunkS = BuildCdeclThunk((void*)&MockSum3, 3);
    CHECK(thunkT && thunkL && thunkS, "all three thunks allocated");

    if (!thunkT || !thunkL || !thunkS)
        return 1;

    // ---- 2-argument transform thunk -------------------------------------
    {
        Vec2 in{1.5f, -2.0f};
        Vec2 out{-100.0f, -100.0f};
        ((TransformFn)thunkT)(&out, &in);

        CHECK(g_lastIn.x == 1.5f && g_lastIn.y == -2.0f,
              "arg1 (CVector2D const*) arrives intact");
        CHECK(g_lastOut.x == -100.0f && g_lastOut.y == -100.0f,
              "arg0 (CVector2D*) is the same object the core writes");
        CHECK(std::fabs(out.x - 4.0f) < 1e-6f && std::fabs(out.y - (-7.0f)) < 1e-6f,
              "out-parameter modification is visible to the caller");
    }

    // ---- 1-argument limit thunk -----------------------------------------
    {
        Vec2 p{2.0f, 0.5f};
        const float r = ((LimitFn)thunkL)(&p);
        CHECK(std::fabs(r - 2.5f) < 1e-6f, "float return value survives the thunk");
        CHECK(p.x == 2.0f && p.y == 0.5f, "single pointer argument untouched");
    }

    // ---- 3-argument thunk: stack index arithmetic -----------------------
    {
        const float r = ((Sum3Fn)thunkS)(1.0f, 2.0f, 4.0f);
        CHECK(std::fabs(r - 7.0f) < 1e-6f, "3 arguments are read from the right slots");
    }

    // ---- ECX / EDX preservation -----------------------------------------
    {
        Vec2 in{0.25f, 0.75f};
        Vec2 out{0.0f, 0.0f};
        unsigned ecxAfter = 0, edxAfter = 0;
        CallCheckingRegs((TransformFn)thunkT, &out, &in, &ecxAfter, &edxAfter);

        CHECK(ecxAfter == 0x11223344u, "ECX is preserved across the thunk");
        CHECK(edxAfter == 0x55667788u, "EDX is preserved across the thunk");
        CHECK(std::fabs(out.x - 1.5f) < 1e-6f && std::fabs(out.y - 1.25f) < 1e-6f,
              "the call still did its work while registers were preserved");
    }

    // ---- stack balance over many calls ----------------------------------
    {
        Vec2 in{0.0f, 0.0f};
        Vec2 out{0.0f, 0.0f};
        const uintptr_t spBefore = (uintptr_t)__builtin_frame_address(0);
        for (int i = 0; i < 100000; ++i) {
            in.x = (float)i;
            ((TransformFn)thunkT)(&out, &in);
        }
        const uintptr_t spAfter = (uintptr_t)__builtin_frame_address(0);
        CHECK(spBefore == spAfter, "100000 calls leave the stack frame intact");
        CHECK(std::fabs(out.x - (2.0f * 99999.0f + 1.0f)) < 1.0f,
              "result of the last loop iteration is correct");
    }

    std::printf("\n%s\n", g_failures == 0 ? "ALL OK" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
