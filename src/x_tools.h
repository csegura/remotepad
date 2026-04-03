#pragma once

#include <X11/Xlib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace xtools {

// X11 colors (RGB pixel values)
struct XColor {
    static constexpr unsigned long Green = 0xADFF2F;
    static constexpr unsigned long Blue  = 0x54C2CC;
    static constexpr unsigned long Red   = 0xDE3700;
};

// Key codes (use xev to verify on your system)
struct XKey {
    static constexpr int r     = 27;
    static constexpr int g     = 42;
    static constexpr int b     = 56;
    static constexpr int ESC   = 49;
    static constexpr int Space = 65;
    static constexpr int q     = 24;
    static constexpr int c     = 54;
    static constexpr int s     = 39;
    static constexpr int d     = 40;
    static constexpr int e     = 26;
    static constexpr int u     = 30;
    static constexpr int CTRL  = 37;
    static constexpr int SHIFT = 50;
};

struct Point {
    double x;
    double y;
    double lineWidth;
    unsigned long color;
};

// Draw a quadratic curve between the last two points
void XQuadraticCurveTo(Display* dpy, Drawable win, GC gc,
                       const std::vector<Point>& points);

// Get list of parent windows up to root
std::vector<Window> getParents(Display* dpy, Window root, Window src);

// Get geometry of topmost window before root
XWindowAttributes getTopGeom(Display* dpy, const std::vector<Window>& parents);

// Get WM_NAME of a window
std::string getWindowName(Display* dpy, Window wid);

// Convert web hex color string (#rrggbb) to X11 pixel value
unsigned long webColorToXColor(const std::string& color);

// Interactive window selection: grab pointer, user clicks a window, return its ID.
// Returns None (0) if cancelled (ESC) or on error.
Window selectWindowByClick(Display* dpy);

// Get the currently focused/active window via _NET_ACTIVE_WINDOW.
// Returns None (0) if unavailable.
Window getActiveWindow(Display* dpy);

struct WindowEntry {
    Window wid;
    std::string name;
};

// Collect visible top-level windows with their names.
std::vector<WindowEntry> collectTopLevelWindows(Display* dpy);

} // namespace xtools
