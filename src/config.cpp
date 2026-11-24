#define _CRT_SECURE_NO_WARNINGS
#include "config.h"

#include "log.h"
#include "strutil.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {

char     g_iniPath[MAX_PATH] = {};
FILETIME g_lastWrite{};

// Booleans go through GetPrivateProfileInt, as the brief requires.
int ReadInt(const char* key, int fallback)
{
    return (int)::GetPrivateProfileIntA("minimap", key, fallback, g_iniPath);
}

// The same API only returns integers, so the float keys come back as text.
float ReadFloat(const char* key, float fallback)
{
    char buf[64] = {};
    ::GetPrivateProfileStringA("minimap", key, "", buf, sizeof(buf), g_iniPath);
    if (buf[0] == '\0')
        return fallback;

    char* end = nullptr;
    const double v = std::strtod(buf, &end);   // decimal point required: "94.5"
    if (end == buf)
        return fallback;
    return (float)v;
}

void ReadString(const char* key, const char* fallback, char* out, size_t outSize)
{
    ::GetPrivateProfileStringA("minimap", key, fallback, out, (DWORD)outSize, g_iniPath);
}

bool ParseAnchor(const char* s, RadarAnchor& out)
{
    if (IEquals(s, "topleft")     || IEquals(s, "top_left")     || IEquals(s, "tl")) { out = RadarAnchor::TopLeft;     return true; }
    if (IEquals(s, "topright")    || IEquals(s, "top_right")    || IEquals(s, "tr")) { out = RadarAnchor::TopRight;    return true; }
    if (IEquals(s, "bottomleft")  || IEquals(s, "bottom_left")  || IEquals(s, "bl")) { out = RadarAnchor::BottomLeft;  return true; }
    if (IEquals(s, "bottomright") || IEquals(s, "bottom_right") || IEquals(s, "br")) { out = RadarAnchor::BottomRight; return true; }
    return false;
}

bool ParseUnits(const char* s, RadarUnits& out)
{
    if (IEquals(s, "pixels") || IEquals(s, "pixel") || IEquals(s, "raw"))         { out = RadarUnits::Pixels;  return true; }
    if (IEquals(s, "stretch") || IEquals(s, "reference") || IEquals(s, "640x448")) { out = RadarUnits::Stretch; return true; }
    return false;
}

void RememberTimestamp()
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (::GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &fad))
        g_lastWrite = fad.ftLastWriteTime;
    else
        g_lastWrite = FILETIME{};
}

} // namespace

bool ConfigLoad(const wchar_t* iniPath, RadarConfig& out)
{
    ::WideCharToMultiByte(CP_ACP, 0, iniPath, -1, g_iniPath, MAX_PATH, nullptr, nullptr);

    const bool exists = ::GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES;

    RadarConfig cfg{};

    // ---- booleans (GetPrivateProfileInt) -----------------------------------
    cfg.enabled            = ReadInt("Enable",             cfg.enabled            ? 1 : 0) != 0;
    cfg.forceSquare        = ReadInt("ForceSquare",        cfg.forceSquare        ? 1 : 0) != 0;
    cfg.keepRadarDisc      = ReadInt("KeepRadarDisc",      cfg.keepRadarDisc      ? 1 : 0) != 0;
    cfg.hotReload          = ReadInt("HotReload",          cfg.hotReload          ? 1 : 0) != 0;
    cfg.ignoreVersionCheck = ReadInt("IgnoreVersionCheck", cfg.ignoreVersionCheck ? 1 : 0) != 0;

    // ---- geometry ----------------------------------------------------------
    cfg.x      = ReadFloat("X",      cfg.x);
    cfg.y      = ReadFloat("Y",      cfg.y);
    cfg.width  = ReadFloat("Width",  cfg.width);
    cfg.height = ReadFloat("Height", cfg.height);

    // ---- text enums --------------------------------------------------------
    char buf[64] = {};
    ReadString("Anchor", "BOTTOMLEFT", buf, sizeof(buf));
    if (!ParseAnchor(buf, cfg.anchor))
        Log("config: unknown Anchor \"%s\" -> BottomLeft", buf);

    ReadString("Units", "STRETCH", buf, sizeof(buf));
    if (!ParseUnits(buf, cfg.units))
        Log("config: unknown Units \"%s\" -> Stretch", buf);

    // ---- sanity ------------------------------------------------------------
    if (cfg.width  < 8.0f)    cfg.width  = 8.0f;
    if (cfg.height < 8.0f)    cfg.height = 8.0f;
    if (cfg.width  > 4096.0f) cfg.width  = 4096.0f;
    if (cfg.height > 4096.0f) cfg.height = 4096.0f;

    if (cfg.forceSquare)
        cfg.height = cfg.width;

    out = cfg;
    RememberTimestamp();

    if (!exists)
        Log("config: %ls not found - built-in defaults in use", iniPath);

    return exists;
}

void ConfigLog(const RadarConfig& cfg)
{
    const char* anchor = "BottomLeft";
    switch (cfg.anchor) {
    case RadarAnchor::TopLeft:     anchor = "TopLeft";     break;
    case RadarAnchor::TopRight:    anchor = "TopRight";    break;
    case RadarAnchor::BottomLeft:  anchor = "BottomLeft";  break;
    case RadarAnchor::BottomRight: anchor = "BottomRight"; break;
    }

    Log("config: Enable=%d ForceSquare=%d KeepRadarDisc=%d HotReload=%d IgnoreVersionCheck=%d",
        cfg.enabled, cfg.forceSquare, cfg.keepRadarDisc, cfg.hotReload, cfg.ignoreVersionCheck);
    Log("config: Anchor=%s Units=%s X=%.1f Y=%.1f Width=%.1f Height=%.1f",
        anchor, cfg.units == RadarUnits::Pixels ? "Pixels" : "Stretch",
        cfg.x, cfg.y, cfg.width, cfg.height);
}

bool ConfigFileChanged(const wchar_t* iniPath)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!::GetFileAttributesExW(iniPath, GetFileExInfoStandard, &fad))
        return false;

    return fad.ftLastWriteTime.dwLowDateTime  != g_lastWrite.dwLowDateTime
        || fad.ftLastWriteTime.dwHighDateTime != g_lastWrite.dwHighDateTime;
}
