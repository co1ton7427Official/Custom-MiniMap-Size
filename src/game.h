// game.h - GTA:SA (1.0 US) addresses and tiny inline accessors.
//
// All addresses below are absolute virtual addresses inside the 1.0 US
// gta_sa.exe image (base 0x400000, 14,383,616 bytes). They come from the
// plugin-sdk (DK22Pac/plugin-sdk, plugin_sa/game_sa/CRadar.cpp) and from the
// gta-reversed / re3 source, and are cross-checked against the address list in
// that project. Nothing here is valid for 1.01 / 2.0 / The Definitive Edition.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>

// Minimal stand-in for plugin-sdk's CVector2D (keep in sync: 8 bytes, x then y).
struct CVector2D {
    float x;
    float y;
};

namespace sa {

constexpr uintptr_t kImageBase = 0x400000;

namespace addr {

// CRadar::TransformRadarPointToScreenSpace(CVector2D& out, CVector2D const& in)
//   __cdecl, void. Maps radar space [-1,+1]^2 to screen pixels.
//   0x583480 - 0x58352F  (the whole function, ~0xB0 bytes)
constexpr uintptr_t TransformRadarPointToScreenSpace = 0x583480;

// CRadar::LimitRadarPoint(CVector2D& point) __cdecl, returns the pre-clamp
// magnitude. Clamps radar-space points into the unit circle.
//   0x5832F0 - 0x58334C (0x5D bytes)
constexpr uintptr_t LimitRadarPoint = 0x5832F0;

// CRadar::DrawRadarMask() - draws four corner fans that build the round stencil
// mask. 0x585700.
constexpr uintptr_t DrawRadarMask = 0x585700;

// 4-byte operand *inside* DrawRadarMask: it is the absolute address of a float
// constant used to build the quarter-circle points. Redirecting it to a tiny
// float collapses the fans to zero area => nothing gets masked => square radar.
constexpr uintptr_t DrawRadarMaskArcConstantPtr = 0x58585C;

// Single bytes inside CHud::DrawRadar() (0x58A330) that gate the vanilla
// HUD_RADARDISC texture / border draws. Zeroing them removes the round disc.
constexpr uintptr_t HudRadarDiscBytes[4] = { 0x58A789, 0x58A88F, 0x58A8D9, 0x58A98F };

// CMenuManager FrontEndMenuManager instance (sizeof 0x1B78).
constexpr uintptr_t FrontEndMenuManager = 0xBA6748;

// RsGlobal: { char* AppName; int32 MaximumWidth; int32 MaximumHeight; ... }
constexpr uintptr_t RsGlobalMaximumWidth  = 0xC17044;
constexpr uintptr_t RsGlobalMaximumHeight = 0xC17048;

} // namespace addr

// CMenuManager member offsets (plugin-sdk VALIDATE_OFFSET values).
constexpr uintptr_t kMenuDrawingMap = addr::FrontEndMenuManager + 0x59; // bool m_bDrawingMap
constexpr uintptr_t kMenuMapZoom    = addr::FrontEndMenuManager + 0x64; // float m_fMapZoom
constexpr uintptr_t kMenuMapOriginX = addr::FrontEndMenuManager + 0x68; // float m_vMapOrigin.x
constexpr uintptr_t kMenuMapOriginY = addr::FrontEndMenuManager + 0x6C; // float m_vMapOrigin.y

// The executable is not relocatable in practice, but never hard-code the base.
inline uintptr_t GameBase()
{
    static const uintptr_t base = (uintptr_t)::GetModuleHandleW(nullptr);
    return base;
}

inline void* At(uintptr_t absoluteVa)
{
    return (void*)(GameBase() + (absoluteVa - kImageBase));
}

inline int32_t ScreenWidth()
{
    return *(const int32_t*)At(addr::RsGlobalMaximumWidth);
}

inline int32_t ScreenHeight()
{
    return *(const int32_t*)At(addr::RsGlobalMaximumHeight);
}

inline bool IsFrontEndMapOpen()
{
    return *(const uint8_t*)At(kMenuDrawingMap) != 0;
}

inline float ReadFloat(uintptr_t absoluteVa)
{
    return *(const float*)At(absoluteVa);
}

// True when `p` lives in the committed game executable image.
inline bool IsInGameImage(const void* p)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!::VirtualQuery(p, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    return (uintptr_t)mbi.AllocationBase == GameBase();
}

} // namespace sa
