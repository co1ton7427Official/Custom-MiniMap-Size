// radar.cpp
//
//   hook  CRadar::TransformRadarPointToScreenSpace  (0x583480)  -> move / resize
//   hook  CRadar::LimitRadarPoint                   (0x5832F0)  -> square clamp
//   patch DrawRadarMask's arc-radius operand        (0x58585C)  -> square mask
//   patch CHud::DrawRadar's disc draws              (0x58A789 / 0x58A88F /
//                                                    0x58A8D9 / 0x58A98F)
//
// Why these functions and not one "DrawRadar":
//   GTA:SA has no single DrawRadar() that owns the minimap rectangle. The
//   minimap is nine streamed map tiles plus every blip, the player arrow, the
//   north marker, gang areas and the waypoint - and *all of them* are placed
//   through CRadar::TransformRadarPointToScreenSpace(). Radar space is
//   [-1,+1] on both axes, so re-mapping that one function moves and scales the
//   whole radar. That is the entire trick behind this plugin.
//
//   The round shape is not geometry. CRadar::DrawRadarMask() writes a stencil
//   mask by fanning a quarter circle out of each of the four radar corners; the
//   arc radius is read through a float pointer whose address is baked into a
//   4-byte operand inside that function (0x58585C). Repointing that operand at
//   a ~0 float collapses all four fans, nothing is masked, and the tiles cover
//   the full rectangle - that is the square radar. Since the mask no longer
//   follows a circle, CRadar::LimitRadarPoint() would still drag blips onto the
//   inscribed circle, hence the second hook, which clamps to the square edge.
#define _CRT_SECURE_NO_WARNINGS
#include "radar.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "strutil.h"
#include "thunk.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwchar>

// 32-bit only: every address in game.h is a 32-bit VA of gta_sa.exe 1.0 US.
// MSVC defines _M_IX86, MinGW/GCC define __i386__.
#if !defined(_M_IX86) && !defined(__i386__)
#error This plugin targets 32-bit GTA:San Andreas (x86) only.
#endif

// Reached through the runtime-built ECX/EDX-preserving thunks.
extern "C" void  __cdecl TransformCore(CVector2D* out, const CVector2D* in);
extern "C" float __cdecl LimitCore(CVector2D* point);

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

RadarConfig g_cfg{};          // written by the game thread only
volatile LONG g_reloadFlag = 0;  // set by the watcher thread, consumed by the game thread
wchar_t g_iniPath[MAX_PATH] = {};

bool g_active = false;
bool g_initialised = false;

InlineHook g_hookTransform;
InlineHook g_hookLimit;

void* g_thunkTransform = nullptr;
void* g_thunkLimit     = nullptr;

// DrawRadarMask dereferences this address as the arc radius. 1e-6 is the value
// every working rectangular-radar mod uses: degenerate fans, nothing else
// changes anywhere in the game.
float g_arcRadiusEpsilon = 0.000001f;

bool          g_arcPatched = false;
unsigned char g_arcOriginal[4] = {};

bool          g_discPatched = false;
unsigned char g_discOriginal[4] = {};

// Screen-space rectangle in pixels, rebuilt on the game thread.
struct ScreenRect {
    float left   = 0.0f;
    float top    = 0.0f;
    float width  = 94.0f;
    float height = 76.0f;
};

ScreenRect g_rect{};
int32_t    g_lastW = -1;
int32_t    g_lastH = -1;

// ---------------------------------------------------------------------------
// Vanilla scaling helpers: exactly SCREEN_STRETCH_X / SCREEN_STRETCH_Y with
// DEFAULT_SCREEN_WIDTH = 640 and DEFAULT_SCREEN_HEIGHT = 448.
// ---------------------------------------------------------------------------

inline float StretchX(float v, int32_t w) { return v * ((float)w / 640.0f); }
inline float StretchY(float v, int32_t h) { return v * ((float)h / 448.0f); }

// The vanilla radar, expressed in the same rectangle the config describes:
// 94x76 at 40 from the left and 28 from the bottom, in 640x448 reference units.
constexpr float kVanillaX = 40.0f;
constexpr float kVanillaY = 28.0f;
constexpr float kVanillaW = 94.0f;
constexpr float kVanillaH = 76.0f;

// Builds the screen-space rectangle for `cfg`. `useVanilla` ignores the geometry
// keys and returns the untouched 94x76 / 40 / 28 radar, which is what the plugin
// falls back to when it is disabled or when the pause-menu map is open.
bool ComputeRect(const RadarConfig& cfg, bool useVanilla, ScreenRect& out)
{
    const int32_t w = sa::ScreenWidth();
    const int32_t h = sa::ScreenHeight();
    if (w <= 0 || h <= 0)
        return false;

    const bool stretch = useVanilla || cfg.units == RadarUnits::Stretch;

    const float rx = useVanilla ? kVanillaX : cfg.x;
    const float ry = useVanilla ? kVanillaY : cfg.y;
    const float rw = useVanilla ? kVanillaW : cfg.width;
    float       rh = useVanilla ? kVanillaH : cfg.height;

    float width  = stretch ? StretchX(rw, w) : rw;
    float height = stretch ? StretchY(rh, h) : rh;

    if (!useVanilla && cfg.forceSquare)
        height = width;

    const float offX = stretch ? StretchX(rx, w) : rx;
    const float offY = stretch ? StretchY(ry, h) : ry;

    ScreenRect r{};
    switch (useVanilla ? RadarAnchor::BottomLeft : cfg.anchor) {
    case RadarAnchor::TopLeft:
        r.left = offX;
        r.top  = offY;
        break;
    case RadarAnchor::TopRight:
        r.left = (float)w - offX - width;
        r.top  = offY;
        break;
    case RadarAnchor::BottomLeft:
        r.left = offX;
        r.top  = (float)h - offY - height;
        break;
    case RadarAnchor::BottomRight:
        r.left = (float)w - offX - width;
        r.top  = (float)h - offY - height;
        break;
    }

    r.width  = width;
    r.height = height;

    out = r;
    return true;
}

// Caches ComputeRect(cfg, false) into g_rect, logging only when it changes.
void BuildRect(const RadarConfig& cfg)
{
    ScreenRect r{};
    if (!ComputeRect(cfg, false, r))
        return;

    g_rect  = r;
    g_lastW = sa::ScreenWidth();
    g_lastH = sa::ScreenHeight();

    Log("rect: screen %dx%d -> left=%.2f top=%.2f width=%.2f height=%.2f",
        g_lastW, g_lastH, r.left, r.top, r.width, r.height);
}

// True when the configured radar rectangle is not exactly the vanilla one.
//
// CHud::DrawRadar (0x58A330) positions the round HUD_RADARDISC texture with its
// own hard-coded 94x76 / 40 / 28 constants - it never goes through
// TransformRadarPointToScreenSpace. So the moment the radar is moved or resized,
// the vanilla ring is left behind at the ORIGINAL place, drawn over whatever is
// there. Hiding those four sprite draws is the only way to get rid of it.
bool RectDiffersFromVanilla(const RadarConfig& cfg)
{
    ScreenRect mine{}, vanilla{};
    if (!ComputeRect(cfg, false, mine) || !ComputeRect(cfg, true, vanilla))
        return false;

    const float eps = 0.5f;
    return std::fabs(mine.left - vanilla.left)     > eps
        || std::fabs(mine.top - vanilla.top)       > eps
        || std::fabs(mine.width - vanilla.width)   > eps
        || std::fabs(mine.height - vanilla.height) > eps;
}

// ---------------------------------------------------------------------------
// Reversible memory patches
// ---------------------------------------------------------------------------

void ApplyArcPatch(bool on)
{
    if (on == g_arcPatched)
        return;

    void* const site = sa::At(sa::addr::DrawRadarMaskArcConstantPtr);
    if (!sa::IsInGameImage(site)) {
        Log("patch: 0x%08X is not inside the game image", (unsigned)sa::addr::DrawRadarMaskArcConstantPtr);
        return;
    }

    if (on) {
        std::memcpy(g_arcOriginal, site, sizeof(g_arcOriginal));

        const uintptr_t p = (uintptr_t)&g_arcRadiusEpsilon;
        if (!WriteBytes(site, &p, sizeof(p)))
            return;

        g_arcPatched = true;
        Log("patch: DrawRadarMask arc constant @0x%08X -> %p (fans collapsed)",
            (unsigned)sa::addr::DrawRadarMaskArcConstantPtr, (void*)p);
    } else {
        WriteBytes(site, g_arcOriginal, sizeof(g_arcOriginal));
        g_arcPatched = false;
        Log("patch: DrawRadarMask arc constant restored");
    }
}

void ApplyDiscPatch(bool on)
{
    if (on == g_discPatched)
        return;

    for (int i = 0; i < 4; ++i) {
        void* const site = sa::At(sa::addr::HudRadarDiscBytes[i]);
        if (!sa::IsInGameImage(site))
            continue;

        if (on) {
            g_discOriginal[i] = *(const unsigned char*)site;
            const unsigned char zero = 0;
            WriteBytes(site, &zero, 1);
        } else {
            WriteBytes(site, &g_discOriginal[i], 1);
        }
    }

    g_discPatched = on;
    Log("patch: HUD radar disc draws %s", on ? "disabled" : "restored");
}

// Applies every toggle that depends on the current config. Idempotent, so it
// can be called after a hot reload.
void ApplyToggles()
{
    if (!g_initialised)
        return;

    const bool active     = g_active && g_cfg.enabled;
    const bool wantSquare = active && g_cfg.forceSquare;

    if (wantSquare) {
        ApplyArcPatch(true);

        // Blips must reach the corners, so the circle clamp has to go too.
        if (!g_hookLimit.Installed() && g_thunkLimit)
            g_hookLimit.Install(sa::addr::LimitRadarPoint, g_thunkLimit);
    } else {
        ApplyArcPatch(false);

        if (g_hookLimit.Installed())
            g_hookLimit.Remove();
    }

    // The HUD disc is drawn from its own hard-coded rectangle, so it keeps
    // showing up at the vanilla spot unless the radar is left exactly where the
    // game puts it. Either the shape changed (square) or the rectangle moved =>
    // the round art is wrong and has to be hidden. KeepRadarDisc=1 forces it
    // back on for people who want the ring anyway.
    const bool discWrong = active && (wantSquare || RectDiffersFromVanilla(g_cfg));
    ApplyDiscPatch(active && discWrong && !g_cfg.keepRadarDisc);
}

// Re-reads the ini after the watcher thread noticed a change. Called from the
// game thread (at the top of every radar draw) so no shared state is touched
// from two threads at once.
void ConsumeReloadRequest()
{
    // Cheap non-atomic fast path: this runs for every tile and blip.
    if (g_reloadFlag == 0)
        return;

    if (::InterlockedExchange(&g_reloadFlag, 0) == 0)
        return;

    Log("config: change detected, reloading");

    RadarConfig fresh{};
    ConfigLoad(g_iniPath, fresh);
    ConfigLog(fresh);
    g_cfg = fresh;

    g_lastW = -1;
    g_lastH = -1;

    ApplyToggles();
}

// ---------------------------------------------------------------------------
// Hot-reload watcher: only sets a flag, never touches the config itself.
// ---------------------------------------------------------------------------

DWORD WINAPI ReloadThread(LPVOID)
{
    for (;;) {
        ::Sleep(750);

        if (!ConfigFileChanged(g_iniPath))
            continue;

        ::InterlockedExchange(&g_reloadFlag, 1);
    }

    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Detour cores (game thread)
// ---------------------------------------------------------------------------

extern "C" void __cdecl TransformCore(CVector2D* out, const CVector2D* in)
{
    if (!out || !in)
        return;

    ConsumeReloadRequest();

    // The pause-menu map (and the intro flythrough) have their own coordinate
    // system; leaving them alone is what keeps the full-screen map usable.
    if (sa::IsFrontEndMapOpen()) {
        const float zoom    = sa::ReadFloat(sa::kMenuMapZoom);
        const float originX = sa::ReadFloat(sa::kMenuMapOriginX);
        const float originY = sa::ReadFloat(sa::kMenuMapOriginY);
        out->x = originX + zoom * in->x;
        out->y = originY - zoom * in->y;
        return;
    }

    if (!g_active || !g_cfg.enabled) {
        // Vanilla mapping (a config reload that disabled the plugin lands here).
        const int32_t w = sa::ScreenWidth();
        const int32_t h = sa::ScreenHeight();
        out->x = StretchX(kVanillaW, w) * 0.5f + StretchX(kVanillaX, w)
               + StretchX(kVanillaW * in->x, w) * 0.5f;
        out->y = ((float)h - StretchY(kVanillaY + kVanillaH, h))
               + StretchY(kVanillaH, h) * 0.5f
               - StretchY(kVanillaH * in->y, h) * 0.5f;
        return;
    }

    const int32_t w = sa::ScreenWidth();
    const int32_t h = sa::ScreenHeight();
    if (w != g_lastW || h != g_lastH) {
        BuildRect(g_cfg);
        // The disc decision depends on the pixel rectangle, which needs a valid
        // screen size - at init time the game may not have one yet, so the
        // toggles are re-evaluated as soon as the rectangle is known.
        ApplyToggles();
    }

    const ScreenRect r = g_rect;

    // Radar space [-1,+1] -> the configured screen rectangle.
    out->x = r.left + (in->x + 1.0f) * 0.5f * r.width;
    out->y = r.top  + (1.0f - in->y) * 0.5f * r.height;
}

extern "C" float __cdecl LimitCore(CVector2D* point)
{
    if (!point)
        return 0.0f;

    const float mag = std::sqrt(point->x * point->x + point->y * point->y);

    if (sa::IsFrontEndMapOpen())
        return mag;

    if (mag <= 1.0f)
        return mag;

    // Square clamp: scale so the larger component lands on +/-1, i.e. project
    // the point onto the nearest edge of the unit square instead of
    // normalising it onto the unit circle.
    const float ax = std::fabs(point->x);
    const float ay = std::fabs(point->y);
    const float m  = ax > ay ? ax : ay;

    if (m > 0.0f) {
        point->x /= m;
        point->y /= m;
    }

    return mag;
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

namespace {

// GTA:SA 1.0 US (Hoodlum). Every address in game.h belongs to this build.
constexpr long long kSupportedExeSize = 14383616LL;

bool CheckExecutable(bool ignoreCheck)
{
    if (ignoreCheck) {
        Log("version: check disabled by config (IgnoreVersionCheck=1)");
        return true;
    }

    wchar_t path[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);

    HANDLE f = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("version: cannot open %ls -> refusing to patch", path);
        return false;
    }

    LARGE_INTEGER size{};
    ::GetFileSizeEx(f, &size);
    ::CloseHandle(f);

    Log("version: %ls is %lld bytes", path, (long long)size.QuadPart);

    if (size.QuadPart != kSupportedExeSize) {
        Log("version: UNSUPPORTED build (1.0 US is %lld bytes). Set "
            "IgnoreVersionCheck=1 only if you know the addresses match.",
            kSupportedExeSize);
        return false;
    }
    return true;
}

} // namespace

bool RadarInit(const wchar_t* iniPath)
{
    WCopy(g_iniPath, MAX_PATH, iniPath);

    ConfigLoad(g_iniPath, g_cfg);
    ConfigLog(g_cfg);

    if (!g_cfg.enabled) {
        Log("minimap disabled by config - nothing patched");
        return true;
    }

    if (!CheckExecutable(g_cfg.ignoreVersionCheck)) {
        Log("minimap: refusing to touch an unsupported executable");
        return false;
    }

    // The cores are plain C++ and would clobber ECX/EDX, which the vanilla
    // code does not expect (DrawRadarSection keeps a live value in EDX). The
    // thunk saves and restores both registers around the call.
    g_thunkTransform = BuildCdeclThunk((void*)&TransformCore, 2);
    g_thunkLimit     = BuildCdeclThunk((void*)&LimitCore, 1);

    if (!g_thunkTransform || !g_thunkLimit) {
        Log("minimap: could not build the detour thunks");
        return false;
    }

    g_active = true;
    g_lastW  = -1;
    g_lastH  = -1;

    // The limit hook is optional (only needed for a square mask), so a failure
    // there is logged but not fatal.
    g_initialised = true;
    ApplyToggles();

    if (!g_hookTransform.Install(sa::addr::TransformRadarPointToScreenSpace, g_thunkTransform)) {
        Log("minimap: hooking TransformRadarPointToScreenSpace failed");
        g_active = false;
        ApplyToggles();
        return false;
    }

    if (g_cfg.hotReload) {
        if (HANDLE t = ::CreateThread(nullptr, 0, &ReloadThread, nullptr, 0, nullptr))
            ::CloseHandle(t);
        else
            Log("config: could not start the hot-reload watcher");
    }

    Log("minimap: active");
    return true;
}

void RadarShutdown()
{
    // Deliberately not guarded by g_active: if init failed half-way, whatever
    // was already patched still has to be put back.
    g_active = false;

    g_hookTransform.Remove();
    g_hookLimit.Remove();

    ApplyArcPatch(false);
    ApplyDiscPatch(false);

    Log("minimap: shut down, everything restored");
}
