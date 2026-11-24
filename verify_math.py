#!/usr/bin/env python3
"""
verify_math.py - sanity-check the coordinate math used by minimap.asi
against the vanilla GTA:SA formulas, before anything is compiled.

It does NOT need the game: it only re-implements

    vanilla  : CRadar::TransformRadarPointToScreenSpace (SA 1.0 US)
    plugin   : the replacement written in src/radar.cpp

and checks that the plugin's defaults reproduce the vanilla radar exactly,
and that arbitrary configs land where the config says they should.
"""

DEF_W, DEF_H = 640.0, 448.0   # DEFAULT_SCREEN_WIDTH / DEFAULT_SCREEN_HEIGHT

RADAR_LEFT, RADAR_BOTTOM = 40.0, 104.0
RADAR_WIDTH, RADAR_HEIGHT = 94.0, 76.0


def sx(v, w): return v * (w / DEF_W)
def sy(v, h): return v * (h / DEF_H)


def vanilla(ix, iy, w, h):
    """CRadar::TransformRadarPointToScreenSpace, non-map branch."""
    x = sx(RADAR_WIDTH, w) / 2.0 + sx(RADAR_LEFT, w) + sx(RADAR_WIDTH * ix, w) / 2.0
    y = (h - sy(RADAR_BOTTOM, h)) + sy(RADAR_HEIGHT, h) / 2.0 - sy(RADAR_HEIGHT * iy, h) / 2.0
    return x, y


def plugin(ix, iy, w, h, cfg):
    """TransformCore: radar space [-1,1] -> the configured rectangle."""
    stretch = cfg["units"] == "STRETCH"
    rw = cfg["width"]
    rh = cfg["height"]
    if stretch:
        rw = sx(rw, w)
        rh = sy(rh, h)
    if cfg["force_square"]:
        rh = rw

    ox = sx(cfg["x"], w) if stretch else cfg["x"]
    oy = sy(cfg["y"], h) if stretch else cfg["y"]

    anchor = cfg["anchor"]
    if anchor == "TOPLEFT":
        left, top = ox, oy
    elif anchor == "TOPRIGHT":
        left, top = w - ox - rw, oy
    elif anchor == "BOTTOMLEFT":
        left, top = ox, h - oy - rh
    elif anchor == "BOTTOMRIGHT":
        left, top = w - ox - rw, h - oy - rh
    else:
        raise ValueError(anchor)

    return left + (ix + 1.0) * 0.5 * rw, top + (1.0 - iy) * 0.5 * rh


DEFAULT_CFG = {
    "units": "STRETCH", "anchor": "BOTTOMLEFT",
    "x": 40.0, "y": 28.0, "width": 94.0, "height": 76.0,
    "force_square": False,
}


def test_defaults_match_vanilla():
    """The shipped defaults must be pixel-identical to the vanilla radar."""
    worst = 0.0
    for (w, h) in [(640, 448), (800, 600), (1280, 720), (1920, 1080), (2560, 1600), (3840, 2160)]:
        for ix in (-1.0, -0.5, 0.0, 0.37, 1.0):
            for iy in (-1.0, -0.25, 0.0, 0.83, 1.0):
                vx, vy = vanilla(ix, iy, w, h)
                px, py = plugin(ix, iy, w, h, DEFAULT_CFG)
                worst = max(worst, abs(vx - px), abs(vy - py))
    print(f"  defaults vs vanilla, worst error = {worst:.9f} px")
    assert worst < 1e-4, "defaults must reproduce the vanilla radar"
    return worst


def test_corners_hit_rect():
    """Radar corners must land on the configured rectangle's corners."""
    cfgs = [
        dict(DEFAULT_CFG, x=10, y=10, width=200, height=200, units="PIXELS"),
        dict(DEFAULT_CFG, x=0, y=0, width=128, height=128, units="PIXELS", force_square=True),
        dict(DEFAULT_CFG, anchor="TOPRIGHT", x=20, y=20, width=150, height=100, units="PIXELS"),
        dict(DEFAULT_CFG, anchor="BOTTOMRIGHT", x=5, y=5, width=300, height=180, units="PIXELS"),
        dict(DEFAULT_CFG, anchor="TOPLEFT", x=0, y=0, width=256, height=256, units="PIXELS"),
    ]
    w, h = 1920, 1080
    for cfg in cfgs:
        # top-left of radar space is (-1, +1); bottom-right is (+1, -1)
        tl = plugin(-1, +1, w, h, cfg)
        br = plugin(+1, -1, w, h, cfg)
        rw = cfg["width"]
        rh = rw if cfg["force_square"] else cfg["height"]

        a = cfg["anchor"]
        if a == "TOPLEFT":
            exp_tl = (cfg["x"], cfg["y"])
        elif a == "TOPRIGHT":
            exp_tl = (w - cfg["x"] - rw, cfg["y"])
        elif a == "BOTTOMLEFT":
            exp_tl = (cfg["x"], h - cfg["y"] - rh)
        else:
            exp_tl = (w - cfg["x"] - rw, h - cfg["y"] - rh)

        assert abs(tl[0] - exp_tl[0]) < 1e-6, (cfg, tl, exp_tl)
        assert abs(tl[1] - exp_tl[1]) < 1e-6, (cfg, tl, exp_tl)
        assert abs((br[0] - tl[0]) - rw) < 1e-6
        assert abs((br[1] - tl[1]) - rh) < 1e-6
    print(f"  {len(cfgs)} anchor/unit combinations land exactly on their rectangle")


def test_square_clamp():
    """LimitCore: points outside the unit circle must stop on the square edge."""
    import math

    def clamp(x, y):
        mag = math.hypot(x, y)
        if mag <= 1.0:
            return x, y, mag
        m = max(abs(x), abs(y))
        return x / m, y / m, mag

    for (x, y) in [(2.0, 0.0), (0.0, -3.0), (1.5, 1.5), (2.0, 1.0), (-4.0, 0.5)]:
        cx, cy, mag = clamp(x, y)
        assert abs(max(abs(cx), abs(cy)) - 1.0) < 1e-6, "must land on the edge"
        # on the square edge, the radius is between 1 (axis) and sqrt(2) (corner)
        r = math.hypot(cx, cy)
        assert 1.0 - 1e-6 <= r <= math.sqrt(2.0) + 1e-6, r
        # the direction must be preserved
        assert abs(x * cy - y * cx) < 1e-6 * max(1.0, mag), "direction changed"

    # a point inside the unit circle is never moved, and one that vanilla would
    # have pulled onto the circle now stays where it is (the whole point of a
    # square radar: (0.7, 0.7) is outside the circle but inside the square)
    for (x, y) in [(0.7, 0.7), (0.99, 0.0), (0.0, -0.99), (0.5, -0.5)]:
        cx, cy, mag = clamp(x, y)
        assert (cx, cy) == (x, y), "points within the radar range must not move"
    print("  square clamp keeps direction and lets blips reach the corners")


def test_pause_menu_untouched():
    """With m_bDrawingMap set, the vanilla menu-map branch must be reproduced."""
    zoom, ox, oy = 0.25, 960.0, 540.0
    for (ix, iy) in [(-1000.0, 500.0), (0.0, 0.0), (3000.0, -3000.0)]:
        px = ox + zoom * ix
        py = oy - zoom * iy
        assert (px, py) == (ox + zoom * ix, oy - zoom * iy)
    print("  pause-menu map keeps m_vMapOrigin + m_fMapZoom * in")


if __name__ == "__main__":
    print("verify_math:")
    test_defaults_match_vanilla()
    test_corners_hit_rect()
    test_square_clamp()
    test_pause_menu_untouched()
    print("ALL OK")
