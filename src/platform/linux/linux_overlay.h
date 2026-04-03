#pragma once

#include "platform/overlay.h"

#include <X11/Xlib.h>
#include <string>

class LinuxOverlay : public platform::IOverlay {
public:
    LinuxOverlay(Display* dpy, Window target);
    ~LinuxOverlay() override;

    bool init() override;
    int getEventFd() const override;
    std::vector<platform::OverlayAction> pollActions() override;

    void activate() override;
    void deactivate() override;
    bool isActive() const override;

    void drawStroke(const std::vector<platform::StrokePoint>& points) override;
    void clear() override;
    void redraw(const std::vector<std::vector<platform::StrokePoint>>& history) override;

    std::string targetName() const override;

private:
    void setOverlayInputTransparent();
    void clearBacking();
    void updateShape();
    void drawStrokeInternal(const std::vector<platform::StrokePoint>& points);

    Display* dpy_;
    Window root_;
    Window target_;
    Window overlay_ = 0;
    GC gc_ = nullptr;
    Pixmap backingPixmap_ = 0;
    Pixmap maskPixmap_ = 0;
    GC maskGC_ = nullptr;
    int overlayWidth_ = 0;
    int overlayHeight_ = 0;

    bool mapped_ = false;
    bool xfixesAvailable_ = false;
    bool shapeDirty_ = false;

    std::string targetName_;
};
