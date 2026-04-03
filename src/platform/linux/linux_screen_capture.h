#pragma once

#include "platform/screen_capture.h"

#include <X11/Xlib.h>

class LinuxScreenCapture : public platform::IScreenCapture {
public:
    LinuxScreenCapture(Display* dpy, Window root, Window target);

    platform::CaptureFrame capture() override;
    void takeScreenshot(const std::string& dir, const std::string& name) override;

private:
    Display* dpy_;
    Window root_;
    Window target_;
};
