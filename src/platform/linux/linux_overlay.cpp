#include "platform/linux/linux_overlay.h"

#include "draw_color.h"
#include "x_tools.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

LinuxOverlay::LinuxOverlay(Display* dpy, Window target)
    : dpy_(dpy), root_(DefaultRootWindow(dpy_)), target_(target) {}

LinuxOverlay::~LinuxOverlay() {
    if (maskGC_) {
        XFreeGC(dpy_, maskGC_);
    }
    if (maskPixmap_) {
        XFreePixmap(dpy_, maskPixmap_);
    }
    if (gc_) {
        XFreeGC(dpy_, gc_);
    }
    if (backingPixmap_) {
        XFreePixmap(dpy_, backingPixmap_);
    }
    if (overlay_) {
        XDestroyWindow(dpy_, overlay_);
    }
}

bool LinuxOverlay::init() {
    const auto parents = xtools::getParents(dpy_, root_, target_);
    const auto geom = xtools::getTopGeom(dpy_, parents);

    targetName_ = xtools::getWindowName(dpy_, target_);
    std::cout << "Overlay over: " << targetName_
              << " (" << target_ << ")" << std::endl;

    overlayWidth_ = geom.width;
    overlayHeight_ = geom.height;

    int screen = DefaultScreen(dpy_);
    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy_, screen);
    attrs.border_pixel = 0;
    attrs.event_mask = 0;

    overlay_ = XCreateWindow(
        dpy_, root_,
        geom.x, geom.y, overlayWidth_, overlayHeight_,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs);

    if (!overlay_) {
        std::cerr << "Failed to create overlay window" << std::endl;
        return false;
    }

    const char* wmName = "REMOTEPAD";
    XChangeProperty(dpy_, overlay_, XA_WM_NAME, XA_STRING, 8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(wmName),
                    std::strlen(wmName));

    int xfixesEventBase = 0;
    int xfixesErrorBase = 0;
    xfixesAvailable_ = XFixesQueryExtension(dpy_, &xfixesEventBase, &xfixesErrorBase);
    if (!xfixesAvailable_) {
        std::cerr << "XFixes extension unavailable; overlay may block local input" << std::endl;
    }

    setOverlayInputTransparent();

    XShapeCombineRectangles(dpy_, overlay_, ShapeBounding,
                            0, 0, nullptr, 0, ShapeSet, Unsorted);

    int depth = DefaultDepth(dpy_, screen);
    backingPixmap_ = XCreatePixmap(dpy_, overlay_, overlayWidth_, overlayHeight_, depth);
    maskPixmap_ = XCreatePixmap(dpy_, overlay_, overlayWidth_, overlayHeight_, 1);

    XGCValues gcv{};
    gcv.foreground = draw::kGreen & 0x00FFFFFF;
    gcv.line_width = 3;
    gcv.cap_style = CapRound;
    gcv.fill_style = FillSolid;
    gc_ = XCreateGC(dpy_, backingPixmap_,
                    GCForeground | GCLineWidth | GCCapStyle | GCFillStyle,
                    &gcv);

    XGCValues mgcv{};
    mgcv.foreground = 1;
    mgcv.line_width = 3;
    mgcv.cap_style = CapRound;
    mgcv.fill_style = FillSolid;
    maskGC_ = XCreateGC(dpy_, maskPixmap_,
                        GCForeground | GCLineWidth | GCCapStyle | GCFillStyle,
                        &mgcv);

    clearBacking();

    constexpr unsigned int modMask = ShiftMask | ControlMask;
    XGrabKey(dpy_, xtools::XKey::q, modMask, root_, False, GrabModeAsync, GrabModeAsync);

    XFlush(dpy_);
    return true;
}

int LinuxOverlay::getEventFd() const {
    return ConnectionNumber(dpy_);
}

std::vector<platform::OverlayAction> LinuxOverlay::pollActions() {
    std::vector<platform::OverlayAction> actions;

    if (shapeDirty_ && mapped_) {
        updateShape();
    }

    while (XPending(dpy_)) {
        XEvent ev;
        XNextEvent(dpy_, &ev);

        if (ev.type != KeyPress) {
            continue;
        }

        if (ev.xkey.keycode == xtools::XKey::q) {
            actions.push_back(platform::OverlayAction::Terminate);
        }
    }

    return actions;
}

void LinuxOverlay::activate() {
    if (mapped_) {
        return;
    }

    XMapRaised(dpy_, overlay_);
    setOverlayInputTransparent();
    mapped_ = true;
    if (shapeDirty_) {
        updateShape();
    }
    XFlush(dpy_);
}

void LinuxOverlay::deactivate() {
    if (!mapped_) {
        return;
    }

    XUnmapWindow(dpy_, overlay_);
    mapped_ = false;
    XFlush(dpy_);
}

bool LinuxOverlay::isActive() const {
    return mapped_;
}

void LinuxOverlay::drawStroke(const std::vector<platform::StrokePoint>& points) {
    drawStrokeInternal(points);
    if (mapped_ && shapeDirty_) {
        updateShape();
    }
}

void LinuxOverlay::clear() {
    clearBacking();

    if (mapped_) {
        XShapeCombineRectangles(dpy_, overlay_, ShapeBounding,
                                0, 0, nullptr, 0, ShapeSet, Unsorted);
        setOverlayInputTransparent();
        XFlush(dpy_);
    }
    shapeDirty_ = false;
}

void LinuxOverlay::redraw(const std::vector<std::vector<platform::StrokePoint>>& history) {
    clearBacking();

    // Keep Linux behavior from the prior implementation: delay redraw slightly
    // after clear to avoid transient compositing artifacts.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    for (const auto& stroke : history) {
        std::vector<platform::StrokePoint> points;
        points.reserve(stroke.size());
        for (const auto& point : stroke) {
            points.push_back(point);
            drawStrokeInternal(points);
        }
    }

    if (mapped_ && shapeDirty_) {
        updateShape();
    }
}

std::string LinuxOverlay::targetName() const {
    return targetName_;
}

void LinuxOverlay::setOverlayInputTransparent() {
    if (xfixesAvailable_) {
        XserverRegion emptyRegion = XFixesCreateRegion(dpy_, nullptr, 0);
        XFixesSetWindowShapeRegion(dpy_, overlay_, ShapeInput, 0, 0, emptyRegion);
        XFixesDestroyRegion(dpy_, emptyRegion);
        return;
    }

    XShapeCombineRectangles(
        dpy_, overlay_, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
}

void LinuxOverlay::clearBacking() {
    XGCValues gcv{};
    gcv.foreground = BlackPixel(dpy_, DefaultScreen(dpy_));
    XChangeGC(dpy_, gc_, GCForeground, &gcv);
    XFillRectangle(dpy_, backingPixmap_, gc_, 0, 0, overlayWidth_, overlayHeight_);

    XGCValues mgcv{};
    mgcv.foreground = 0;
    XChangeGC(dpy_, maskGC_, GCForeground, &mgcv);
    XFillRectangle(dpy_, maskPixmap_, maskGC_, 0, 0, overlayWidth_, overlayHeight_);

    mgcv.foreground = 1;
    XChangeGC(dpy_, maskGC_, GCForeground, &mgcv);
}

void LinuxOverlay::updateShape() {
    XShapeCombineMask(dpy_, overlay_, ShapeBounding, 0, 0, maskPixmap_, ShapeSet);
    setOverlayInputTransparent();
    XCopyArea(dpy_, backingPixmap_, overlay_, gc_,
              0, 0, overlayWidth_, overlayHeight_, 0, 0);
    XFlush(dpy_);
    shapeDirty_ = false;
}

void LinuxOverlay::drawStrokeInternal(const std::vector<platform::StrokePoint>& points) {
    int l = static_cast<int>(points.size()) - 1;
    if (l < 3) {
        return;
    }

    std::vector<xtools::Point> stroke;
    stroke.reserve(points.size());
    for (const auto& point : points) {
        stroke.push_back({
            .x = point.x,
            .y = point.y,
            .lineWidth = point.lineWidth,
            .color = point.color,
        });
    }

    const int lineWidth = std::max(1, static_cast<int>(points[l - 1].lineWidth));
    const unsigned long color = points[l - 1].color & 0x00FFFFFF;

    XGCValues gcv{};
    gcv.line_width = lineWidth;
    gcv.foreground = color;
    XChangeGC(dpy_, gc_, GCLineWidth | GCForeground, &gcv);
    xtools::XQuadraticCurveTo(dpy_, backingPixmap_, gc_, stroke);

    XGCValues mgcv{};
    mgcv.line_width = lineWidth;
    mgcv.foreground = 1;
    XChangeGC(dpy_, maskGC_, GCLineWidth | GCForeground, &mgcv);
    xtools::XQuadraticCurveTo(dpy_, maskPixmap_, maskGC_, stroke);

    shapeDirty_ = true;
}
