// config.h - minimap_config.ini
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Where the (X, Y) offset is measured from.
enum class RadarAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// What X / Y / Width / Height mean.
enum class RadarUnits {
    Pixels,   // literal pixels of the current resolution
    Stretch   // 640x448 reference units, scaled exactly like the vanilla HUD
};

struct RadarConfig {
    bool enabled            = true;   // Enable
    bool forceSquare        = false;  // ForceSquare : square radar (Height := Width + square mask)
    bool keepRadarDisc      = false;  // KeepRadarDisc : keep the round HUD disc art on a square radar
    bool hotReload          = true;   // HotReload
    bool ignoreVersionCheck = false;  // IgnoreVersionCheck

    RadarAnchor anchor = RadarAnchor::BottomLeft; // Anchor
    RadarUnits  units  = RadarUnits::Stretch;     // Units

    // Defaults reproduce the vanilla radar in 640x448 reference space:
    // 94 x 76, 40 px from the left edge, 28 px from the bottom edge.
    float x      = 40.0f;
    float y      = 28.0f;
    float width  = 94.0f;
    float height = 76.0f;
};

// Reads the whole file. A missing file is not an error (defaults are kept).
bool ConfigLoad(const wchar_t* iniPath, RadarConfig& out);

// One line per value, straight into the log.
void ConfigLog(const RadarConfig& cfg);

// True when the .ini has a newer write time than the last ConfigLoad().
bool ConfigFileChanged(const wchar_t* iniPath);
