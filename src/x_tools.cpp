#include "x_tools.h"
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <cmath>
#include <cstdlib>

namespace xtools {

void XQuadraticCurveTo(Display* dpy, Drawable win, GC gc,
                       const std::vector<Point>& points) {
    constexpr int segments = 15;
    int l = static_cast<int>(points.size()) - 1;

    double currentX = points[l].x;
    double currentY = points[l].y;
    double xc = points[l - 1].x;
    double yc = points[l - 1].y;

    // Control point
    double cpx = (currentX + xc) / 2.0;
    double cpy = (currentY + yc) / 2.0;

    for (int i = 0; i <= segments; i++) {
        double t = static_cast<double>(i) / segments;
        double u = 1.0 - t;
        double pointX = u * u * currentX + 2.0 * u * t * cpx + t * t * xc;
        double pointY = u * u * currentY + 2.0 * u * t * cpy + t * t * yc;

        XPoint line[2];
        line[0].x = static_cast<short>(currentX);
        line[0].y = static_cast<short>(currentY);
        line[1].x = static_cast<short>(pointX);
        line[1].y = static_cast<short>(pointY);
        XDrawLines(dpy, win, gc, line, 2, CoordModeOrigin);

        currentX = pointX;
        currentY = pointY;
    }
}

std::vector<Window> getParents(Display* dpy, Window root, Window src) {
    std::vector<Window> parents;
    parents.push_back(src);

    Window current = src;
    while (true) {
        Window parent_ret, root_ret;
        Window* children = nullptr;
        unsigned int nchildren = 0;

        if (!XQueryTree(dpy, current, &root_ret, &parent_ret, &children, &nchildren)) {
            if (children) XFree(children);
            break;
        }
        if (children) XFree(children);

        parents.push_back(parent_ret);
        if (parent_ret == root) break;
        current = parent_ret;
    }
    return parents;
}

XWindowAttributes getTopGeom(Display* dpy, const std::vector<Window>& parents) {
    XWindowAttributes attr{};
    // parents: [target, ..., parent_before_root, root]
    // We want the one just before root
    if (parents.size() >= 2) {
        Window wid = parents[parents.size() - 2];
        XGetWindowAttributes(dpy, wid, &attr);
    }
    return attr;
}

std::string getWindowName(Display* dpy, Window wid) {
    char* name = nullptr;
    if (XFetchName(dpy, wid, &name) && name) {
        std::string result(name);
        XFree(name);
        return result;
    }
    return "";
}

unsigned long webColorToXColor(const std::string& color) {
    if (color.empty()) return 0;
    if (color[0] == '#') {
        return std::strtoul(color.c_str() + 1, nullptr, 16);
    }
    return std::strtoul(color.c_str(), nullptr, 0);
}

Window selectWindowByClick(Display* dpy) {
    Window root = DefaultRootWindow(dpy);
    Cursor cursor = XCreateFontCursor(dpy, XC_crosshair);

    if (XGrabPointer(dpy, root, False, ButtonPressMask,
                     GrabModeSync, GrabModeAsync, root, cursor,
                     CurrentTime) != GrabSuccess) {
        XFreeCursor(dpy, cursor);
        return None;
    }

    if (XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
                      CurrentTime) != GrabSuccess) {
        XUngrabPointer(dpy, CurrentTime);
        XFreeCursor(dpy, cursor);
        return None;
    }

    Window result = None;
    bool done = false;
    while (!done) {
        XAllowEvents(dpy, SyncPointer, CurrentTime);
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case ButtonPress: {
                Window picked = ev.xbutton.subwindow;
                if (picked == None) {
                    picked = ev.xbutton.window;
                }
                // Walk up to find the top-level child of root
                Window parent_ret, root_ret;
                Window* children = nullptr;
                unsigned int nchildren = 0;
                Window current = picked;
                while (current != root) {
                    if (!XQueryTree(dpy, current, &root_ret, &parent_ret,
                                    &children, &nchildren)) {
                        break;
                    }
                    if (children) XFree(children);
                    if (parent_ret == root) break;
                    current = parent_ret;
                }
                result = current;
                done = true;
                break;
            }
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape) {
                    done = true;
                }
                break;
            }
            default:
                break;
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cursor);
    XFlush(dpy);
    return result;
}

Window getActiveWindow(Display* dpy) {
    Window root = DefaultRootWindow(dpy);
    Atom prop = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

    Atom actual_type;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, root, prop, 0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &data) != Success || nitems == 0) {
        if (data) XFree(data);
        return None;
    }

    Window result = *reinterpret_cast<Window*>(data);
    XFree(data);
    return result;
}

std::vector<WindowEntry> collectTopLevelWindows(Display* dpy) {
    std::vector<WindowEntry> result;
    Window root = DefaultRootWindow(dpy);

    // Try _NET_CLIENT_LIST first (EWMH)
    Atom prop = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char* data = nullptr;

    bool usedNetList = false;
    if (XGetWindowProperty(dpy, root, prop, 0, 4096, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &data) == Success && nitems > 0) {
        auto* windows = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            std::string name = getWindowName(dpy, windows[i]);
            if (!name.empty()) {
                result.push_back({windows[i], std::move(name)});
            }
        }
        usedNetList = true;
    }
    if (data) XFree(data);

    // Fallback: XQueryTree + filter visible
    if (!usedNetList) {
        Window root_ret, parent_ret;
        Window* children = nullptr;
        unsigned int nchildren = 0;
        if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; ++i) {
                XWindowAttributes attr{};
                XGetWindowAttributes(dpy, children[i], &attr);
                if (attr.map_state != IsViewable) continue;
                std::string name = getWindowName(dpy, children[i]);
                if (!name.empty()) {
                    result.push_back({children[i], std::move(name)});
                }
            }
            if (children) XFree(children);
        }
    }

    return result;
}

} // namespace xtools
